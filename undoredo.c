/*
 * undoredo.c : manage a journal of all the modifications done in the world.
 *
 * written by T.Pierron, jan 2022
 */

#define UNDOREDO_IMPL
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <malloc.h>
#include <stdarg.h>
#include "selection.h"
#include "mapUpdate.h"
#include "undoredo.h"
#include "render.h"
#include "globals.h"

static struct UndoPrivate_t journal;

/* store a chunk of memory in the undo log */
static void undoAddMem(APTR mem, int size)
{
	UndoLogBuf log = TAIL(journal.undoLog);

	do {
		if (log == NULL)
		{
			log = malloc(sizeof *log + UNDO_LOG_SIZE - 4);
			log->usage = 0;
			ListAddHead(&journal.undoLog, &log->node);
		}
		int remain = UNDO_LOG_SIZE - log->usage;
		if (remain > size)
			remain = size;
		memcpy(log->buffer + log->usage, mem, remain);
		size -= remain;
		log->usage += remain;
		NEXT(log);
	}
	while (size > 0);
}

/* retrieve some memory from the log */
static void undoGetMem(APTR mem, int max, UndoLogBuf log, int offset)
{
	DATA8 eom = mem + max;

	while (offset < 0)
	{
		PREV(log);
		offset += log->usage;
	}

	while (max > 0)
	{
		int avail = MIN(max, offset);
		eom -= avail;
		offset -= avail;
		max -= avail;
		memcpy(eom, log->buffer + offset, avail);
		if (offset == 0)
		{
			PREV(log);
			if (log == NULL) return; /* hmm, should not happen */
			offset = log->usage;
		}
	}
}

/* register an operation in the log */
void undoLog(int type, ...)
{
	va_list args;

	if (journal.inUndo)
		return;

	va_start(args, type);
	switch (type & 0x7f) {
	case LOG_SELECTION: /* selection was removed: add its last state in the log */
		{
			struct UndoSelection_t mem;
			vec points = va_arg(args, vec);
			mem.start[VX] = points[VX];
			mem.start[VY] = points[VY];
			mem.start[VZ] = points[VZ];
			mem.end[VX] = points[VX+4];
			mem.end[VY] = points[VY+4];
			mem.end[VZ] = points[VZ+4];
			mem.typeSize = (sizeof mem << 8) | type;
			undoAddMem(&mem, sizeof mem);
		}
		break;
	case LOG_BLOCK: /* single block changed: add its previous state in */
		{
			struct UndoBlock_t mem;
			mem.itemId = va_arg(args, int);
			DATA8 tile = va_arg(args, DATA8);
			DATA32 point = va_arg(args, DATA32);
			int size = 0;
			if (tile)
			{
				/* no pointers outside this module must be stored in the journal */
				size = NBT_Size(tile) + 4;
				undoAddMem(tile, size);
			}
			memcpy(mem.loc, point, sizeof mem.loc);
			mem.typeSize = ((sizeof mem + size) << 8) | type;
			if (tile) mem.itemId |= HAS_TILEENTITY;
			undoAddMem(&mem, sizeof mem);
		}
		break;
	}
}

#ifdef DEBUG
void undoDebug(void)
{
	UndoLogBuf log;
	int offset;
	for (log = TAIL(journal.undoLog), offset = log ? log->usage : 0; log; )
	{
		uint32_t typeSize;
		undoGetMem(&typeSize, sizeof typeSize, log, offset);

		uint8_t chr = typeSize & UNDO_LINK ? '+' : '-';
		switch (typeSize & 0x7f) {
		case LOG_SELECTION:
			{
				struct UndoSelection_t mem;
				undoGetMem(&mem, sizeof mem, log, offset);
				fprintf(stderr, "%c selection: from %d, %d, %d to %d, %d, %d\n", chr,
					mem.start[VX], mem.start[VY], mem.start[VZ], mem.end[VX], mem.end[VY], mem.end[VZ]);
			}
			break;
		case LOG_BLOCK:
			{
				struct UndoBlock_t mem;
				undoGetMem(&mem, sizeof mem, log, offset);
				fprintf(stderr, "%c block changed, old: %d:%d at %d, %d, %d, tile: %d\n", chr,
					(mem.itemId >> 4) & 0xffff, mem.itemId & 15, mem.loc[VX], mem.loc[VY], mem.loc[VZ], (mem.typeSize >> 8) - sizeof mem);
			}
		}
		typeSize >>= 8;
		offset -= typeSize;
		if (offset <= 0)
		{
			for (PREV(log), offset = -offset; log && log->usage < offset; offset -= log->usage, PREV(log));
			if (log == NULL) break;
			offset = log->usage - offset;
		}
	}
}
#endif

/* cancel last operation stored in the log */
void undoOperation(void)
{
	UndoLogBuf log = TAIL(journal.undoLog);
	if (log == NULL) return;
	int offset = log->usage;
	uint32_t typeSize;
	undoGetMem(&typeSize, sizeof typeSize, log, offset);

	journal.inUndo = 1;
	for (;;)
	{
		uint8_t link = typeSize & UNDO_LINK;
		switch (typeSize & 0x7f) {
		case LOG_SELECTION:
			{
				struct UndoSelection_t mem;
				undoGetMem(&mem, sizeof mem, log, offset);
				renderSetSelection(mem.start);
			}
			break;
		case LOG_BLOCK:
			{
				struct UndoBlock_t mem;
				DATA8 tile = NULL;
				undoGetMem(&mem, sizeof mem, log, offset);
				mem.typeSize = (typeSize >> 8) - sizeof mem;
				if (mem.typeSize > 0)
				{
					/* tile entity */
					tile = malloc(mem.typeSize);
					undoGetMem(tile, mem.typeSize, log, offset - sizeof mem);
				}
				if (journal.inUndo == 1)
					mapUpdateInit(NULL);
				mapUpdate(globals.level, (vec4) {mem.loc[0], mem.loc[1], mem.loc[2]}, mem.itemId & 0xfffff, tile, UPDATE_SILENT);
				journal.inUndo = 2;
				/* if this operation is part of a group: only one modif has been registered for the whole group */
				if ((typeSize & UNDO_LINK) == 0)
					renderCancelModif();
			}
			break;
		default: return;
		}

		typeSize >>= 8;
		offset -= typeSize;
		if (offset < 0)
		{
			offset = -offset;
			typeSize = 0;
			do {
				offset -= typeSize;
				ListRemTail(&journal.undoLog);
				free(log);
				log = TAIL(journal.undoLog);
				if (log == NULL) return;
				typeSize = log->usage;
			}
			while (log->usage < offset);
			log->usage -= offset;
		}
		else log->usage = offset;

		/* check if the next operation needs to cancelled as well */
		if (link && log && log->usage > 0)
		{
			undoGetMem(&typeSize, 4, log, offset = log->usage);
		}
		else break;
	}
	if (journal.inUndo == 2)
		mapUpdateEnd(globals.level);
	journal.inUndo = 0;
}
