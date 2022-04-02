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
#include "entities.h"
#include "undoredo.h"
#include "render.h"
#include "globals.h"

static struct UndoPrivate_t journal;

/* map is being deleted */
void undoDelAll(void)
{
	ListNode * node;
	while ((node = ListRemHead(&journal.undoLog))) free(node);
	while ((node = ListRemHead(&journal.redoLog))) free(node);
}

/* store a chunk of memory in the undo log */
static void undoAddMem(ListHead * head, APTR buffer, int size)
{
	UndoLogBuf log = TAIL(*head);

	do {
		if (log == NULL)
		{
			log = malloc(sizeof *log + UNDO_LOG_SIZE - 4);
			log->usage = 0;
			ListAddHead(head, &log->node);
		}
		int remain = UNDO_LOG_SIZE - log->usage;
		if (remain > size)
			remain = size;
		memcpy(log->buffer + log->usage, buffer, remain);
		size -= remain;
		buffer += remain;
		log->usage += remain;
		NEXT(log);
	}
	while (size > 0);
}

static void undoFreeLog(ListHead * head)
{
	UndoLogBuf log, next;
	for (log = HEAD(*head); log; next = (UndoLogBuf) log->node.ln_Next, free(log), log = next);
	memset(head, 0, sizeof *head);
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

static inline void undoFlushRepeat(ListHead * head)
{
	*journal.regionRepeatLoc = UNDO_BLOCK_REPEAT;
	undoAddMem(head, &journal.regionRepeat, sizeof journal.regionRepeat);
	journal.regionBytes += sizeof journal.regionRepeat;
	journal.regionRepeat = 0;
	journal.regionRepeatId = -1;
}

/* register an operation in the log */
void undoLog(int type, ...)
{
	va_list    args;
	ListHead * head;

	if (journal.inUndo == 0)
	{
		UndoLogBuf log = HEAD(journal.redoLog);
		if (log && log->usage > 0)
			/* redo log is now out of sync, so discard everything */
			undoFreeLog(&journal.redoLog);
		head = &journal.undoLog;
	}
	else head = &journal.redoLog;

	va_start(args, type);
	switch (type & 0x7f) {
	case LOG_SELECTION: /* selection was removed: add its last state in the log */
		{
			struct UndoSelection_t mem;
			vec points = va_arg(args, vec);
			mem.start[VX] = points[VX];
			mem.start[VY] = points[VY];
			mem.start[VZ] = points[VZ];
			mem.size[VX] = points[VX+4];
			mem.size[VY] = points[VY+4];
			mem.size[VZ] = points[VZ+4];
			mem.typeSize = (sizeof mem << 8) | type;
			undoAddMem(head, &mem, sizeof mem);
		}
		break;
	case LOG_BLOCK: /* single block changed: add its previous state in */
	{
		uint16_t  blockId = va_arg(args, int);
		DATA8     tile    = va_arg(args, DATA8);
		ChunkData cd      = va_arg(args, ChunkData);
		int       offset  = va_arg(args, int);
		uint32_t  point[3];
		point[VX] = cd->chunk->X + (offset & 15); offset >>= 4;
		point[VZ] = cd->chunk->Z + (offset & 15);
		point[VY] = cd->Y + (offset >> 4);
		if (journal.inSelection == 0)
		{
			struct UndoBlock_t mem;
			mem.itemId = blockId;
			int size = 0;
			if (tile)
			{
				/* no pointers outside this module must be stored in the journal */
				size = NBT_Size(tile) + 4;
				undoAddMem(head, tile, size);
			}
			memcpy(mem.loc, point, sizeof mem.loc);
			mem.typeSize = ((sizeof mem + size) << 8) | type;
			if (tile) mem.itemId |= HAS_TILEENTITY;
			undoAddMem(head, &mem, sizeof mem);
		}
		else /* the difference with previous branch, is that we won't have to store location */
		{
			offset = (point[VX] - journal.regionLoc[VX]) +
			        ((point[VY] - journal.regionLoc[VY]) * journal.regionSize[VZ] +
			          point[VZ] - journal.regionLoc[VZ]) * journal.regionSize[VX];
			if (offset != journal.regionOffset)
			{
				/* non-contiguous modification: need to add a skip count */
				int skip = journal.regionOffset < 0 ? offset : offset - journal.regionOffset;
				if (journal.regionRepeat > 1)
					undoFlushRepeat(head);
				else
					journal.regionRepeatId = -1;
				/* note: selection can be processed top to bottom, moving to another Y layer will generate a negative offset */
				if (-32768 <= skip && skip <= 32767)
				{
					uint16_t store[2] = {UNDO_BLOCK_SKIP, skip};
					undoAddMem(head, store, sizeof store);
					journal.regionBytes += sizeof store;
				}
				else /* hmm, very sparse modifications */
				{
					uint16_t store[3] = {UNDO_BLOCK_SKIP32, skip, skip >> 16};
					undoAddMem(head, store, sizeof store);
					journal.regionBytes += sizeof store;
				}
			}
			journal.regionOffset = offset+1;
			if (tile)
			{
				if (journal.regionRepeat > 1)
					undoFlushRepeat(head);
				/* store tile entity before block id */
				int size = NBT_Size(tile) + 4;
				uint16_t info[] = {UNDO_BLOCK_TILEENT, size};
				journal.regionBytes += 4 + size;
				/* need a 4 bytes header though */
				undoAddMem(head, info, sizeof info);
				undoAddMem(head, tile, size);
			}
			else if (journal.regionRepeatId == blockId)
			{
				/* compress selection using run-length encoding */
				if (journal.regionRepeat == 0xffff)
					undoFlushRepeat(head);
				journal.regionRepeat ++;
				if (journal.regionRepeat == 1)
				{
					/* this method will save bytes only with 3 or more consecutive blocks of the same type */
					undoAddMem(head, &blockId, sizeof blockId);
					journal.regionBytes += 2;
					UndoLogBuf log = TAIL(journal.undoLog);
					journal.regionRepeatLoc = (DATA16) (log->buffer + log->usage - 2);
				}
				break;
			}
			else journal.regionRepeatId = blockId, journal.regionRepeat = 0;
			undoAddMem(head, &blockId, sizeof blockId);
			journal.regionBytes += 2;
		}
	}	break;
	case LOG_ENTITY_DEL:
	case LOG_ENTITY_CHANGED:
		{
			struct UndoEntity_t mem;
			/* can be retrieved from NBT, but it is way too much work */
			memcpy(mem.loc, va_arg(args, vec), sizeof mem.loc);
			DATA8 nbt = va_arg(args, DATA8);
			int   size = NBT_Size(nbt) + 4;
			/* NBT content first */
			mem.entityId = va_arg(args, int);
			undoAddMem(head, nbt, size);
			mem.typeSize = type | ((size + sizeof mem) << 8);
			undoAddMem(head, &mem, sizeof mem);
		}
		break;
	case LOG_ENTITY_ADDED:
		{
			/* simply log the entityId, no need for NBT content */
			uint32_t info[] = {va_arg(args, int), type | (8 << 8)};
			undoAddMem(head, info, sizeof info);
		}
		break;
	case LOG_REGION_START: /* won't store anything in the log (yet) */
		if (journal.inSelection == 0)
		{
			/* need to write blocks changed first */
			journal.regionRepeatId = -1;
			journal.regionRepeat = 0;
			journal.inSelection = 1;
			journal.regionOffset = -1;
			journal.regionBytes = 0;
			memcpy(journal.regionLoc, va_arg(args, int *), 2 * sizeof journal.regionLoc);
		}
		break;
	case LOG_REGION_END:
		if (journal.inSelection == 1)
		{
			if (journal.regionRepeat > 1)
				undoFlushRepeat(head);
			struct UndoSelection_t mem;
			memcpy(mem.start, journal.regionLoc, 2 * sizeof mem.start);
			mem.typeSize = LOG_REGION_START | ((journal.regionBytes + sizeof mem) << 8);
			undoAddMem(head, &mem, sizeof mem);
			journal.inSelection = 0;
		}
	}
}

#ifdef DEBUG
void undoDebugLog(STRPTR name, ListHead * head)
{
	UndoLogBuf log;
	int offset;
	for (offset = 0, log = HEAD(*head); log; offset += log->usage, NEXT(log));
	fprintf(stderr, "%s log, usage: %d bytes\n", name, offset);
	log = TAIL(*head);
	if (log == NULL || log->usage == 0) return;
	for (offset = log->usage; log; )
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
					mem.start[VX], mem.start[VY], mem.start[VZ], mem.size[VX], mem.size[VY], mem.size[VZ]);
			}
			break;
		case LOG_BLOCK:
			{
				struct UndoBlock_t mem;
				undoGetMem(&mem, sizeof mem, log, offset);
				fprintf(stderr, "%c block changed, old: %d:%d at %d, %d, %d, tile: %d\n", chr,
					(mem.itemId >> 4) & 0xffff, mem.itemId & 15, mem.loc[VX], mem.loc[VY], mem.loc[VZ], (mem.typeSize >> 8) - sizeof mem);
			}
			break;
		case LOG_REGION_START:
			{
				struct UndoSelection_t mem;
				undoGetMem(&mem, sizeof mem, log, offset);
				fprintf(stderr, "%c region: start at %d, %d, %d, size = %d, %d, %d, data = %d bytes\n", chr,
					mem.start[VX], mem.start[VY], mem.start[VZ], mem.size[VX], mem.size[VY], mem.size[VZ], (typeSize >> 8) - sizeof mem);
			}
			break;
		case LOG_ENTITY_DEL:
		case LOG_ENTITY_CHANGED:
			{
				struct UndoEntity_t mem;
				undoGetMem(&mem, sizeof mem, log, offset);
				fprintf(stderr, "%c %s entity at %g, %g, %g, NBT = %d bytes\n", chr, (typeSize & 0x7f) == LOG_ENTITY_DEL ? "Deleted" : "Changed",
					(double) mem.loc[VX], (double) mem.loc[VY], (double) mem.loc[VZ], (typeSize >> 8) - sizeof mem);
			}
			break;
		case LOG_ENTITY_ADDED:
			{
				uint32_t info[2];
				undoGetMem(info, sizeof info, log, offset);
				fprintf(stderr, "%c Added entity %d\n", chr, info[0]);
			}
			break;
		default:
			fprintf(stderr, "not good: unknown type %d (size: %d)", typeSize & 0x7f, typeSize >> 8);
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

void undoDebug(void)
{
	undoDebugLog("undo", &journal.undoLog);
	undoDebugLog("redo", &journal.redoLog);
}
#endif

static void inline undoNextBlock(int XYZ[3], int * start)
{
	XYZ[VX] ++;
	if (XYZ[VX] == start[VX+3])
	{
		XYZ[VX] = 0;
		XYZ[VZ] ++;
		if (XYZ[VZ] == start[VZ+3])
		{
			XYZ[VZ] = 0;
			XYZ[VY] ++;
		}
	}
}

/* cancel modifications done from selection */
static void undoSelection(int * start, UndoLogBuf log, int offset, int bytes)
{
	uint16_t regionRepeat = 0;
	DATA8    tile = NULL;
	int      XYZ[3] = {0, 0, 0};

	for (offset -= bytes; offset < 0; PREV(log), offset += log->usage);

	mapUpdateInit(NULL);
	while (bytes > 0)
	{
		void undoRead(APTR mem, int size)
		{
			while (size > 0)
			{
				int remain = log->usage - offset;
				if (remain > size) remain = size;
				memcpy(mem, log->buffer + offset, remain);
				size -= remain;
				bytes -= remain;
				mem += remain;
				offset += remain;
				if (offset == log->usage) NEXT(log), offset = 0;
			}
		}

		uint16_t blockId;
		int count;
		undoRead(&blockId, 2);
		switch (blockId) {
		case UNDO_BLOCK_REPEAT:
			undoRead(&blockId, 2);
			for (count = blockId, blockId = regionRepeat; count > 0; count --)
			{
				mapUpdate(globals.level, (vec4) {start[VX] + XYZ[VX], start[VY] + XYZ[VY], start[VZ] + XYZ[VZ]}, blockId, NULL, UPDATE_SILENT | UPDATE_DONTLOG);
				undoNextBlock(XYZ, start);
			}
			break;
		case UNDO_BLOCK_SKIP:
			undoRead(&blockId, 2);
			count = (int16_t) blockId;
			goto case_BLOCK_SKIP;
		case UNDO_BLOCK_SKIP32:
			undoRead(&blockId, 2); count = blockId;
			undoRead(&blockId, 2); count |= blockId << 16;
		case_BLOCK_SKIP:
			{
				int   pos = XYZ[VX] + (XYZ[VZ] + XYZ[VY] * start[VZ+3]) * start[VX+3] + count;
				div_t res = div(pos, start[VX+3]);
				XYZ[VX] = res.rem;
				res = div(res.quot, start[VZ+3]);
				XYZ[VZ] = res.rem;
				XYZ[VY] = res.quot;
			}
			break;
		case UNDO_BLOCK_TILEENT:
			{
				if (tile) free(tile); /* shouldn't happen */
				uint16_t size;
				undoRead(&size, sizeof size);
				tile = malloc(size);
				undoRead(tile, size);
			}
			break;
		default:
			mapUpdate(globals.level, (vec4) {start[VX] + XYZ[VX], start[VY] + XYZ[VY], start[VZ] + XYZ[VZ]}, blockId, tile, UPDATE_SILENT | UPDATE_DONTLOG);
			undoNextBlock(XYZ, start);
			regionRepeat = blockId;
			tile = NULL;
		}
	}
	if (tile) free(tile); /* shouldn't happen */
	mapUpdateEnd(globals.level);
}

/* cancel last operation stored in the log */
void undoOperation(int redo)
{
	ListHead * head = redo ? &journal.redoLog : &journal.undoLog;
	UndoLogBuf log = TAIL(*head);
	if (log == NULL || log->usage == 0) return;
	int offset = log->usage;
	uint32_t typeSize;
	uint8_t  meshUpdated = 0;
	undoGetMem(&typeSize, sizeof typeSize, log, offset);

	journal.inUndo = 1 + redo;
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
				if (meshUpdated == 0)
				{
					mapUpdateInit(NULL);
					meshUpdated = 1;
				}
				mapUpdate(globals.level, (vec4) {mem.loc[0], mem.loc[1], mem.loc[2]}, mem.itemId & 0xfffff, tile, UPDATE_SILENT);
				/* if this operation is part of a group: only one modif has been registered for the whole group */
				if ((typeSize & UNDO_LINK) == 0)
					renderCancelModif();
			}
			break;
		case LOG_REGION_START:
			{
				struct UndoSelection_t mem;
				undoGetMem(&mem, sizeof mem, log, offset);
				undoSelection(mem.start, log, offset - sizeof mem, (typeSize >> 8) - sizeof mem);
				renderCancelModif();
			}
			break;
		case LOG_ENTITY_DEL:
		case LOG_ENTITY_CHANGED:
			{
				struct UndoEntity_t mem;
				undoGetMem(&mem, sizeof mem, log, offset);
				Chunk chunk = mapGetChunk(globals.level, mem.loc);
				if (chunk)
				{
					/* not the most optimized way, but at least it works */
					if ((typeSize & 0x7f) == LOG_ENTITY_CHANGED)
						entityDeleteById(globals.level, mem.entityId+1);

					int   size = (typeSize >> 8) - sizeof mem;
					DATA8 stream = malloc(size);
					NBTFile_t nbt = {.mem = stream};
					undoGetMem(stream, size, log, offset - sizeof mem);
					entityParse(chunk, &nbt, 0, NULL);
					renderCancelModif();
				}
			}
			break;
		case LOG_ENTITY_ADDED:
			{
				uint32_t info[2];
				undoGetMem(info, sizeof info, log, offset);
				entityDeleteById(globals.level, info[0]+1);
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
				ListRemTail(head);
				free(log);
				log = TAIL(*head);
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
	if (meshUpdated)
		mapUpdateEnd(globals.level);
	journal.inUndo = 0;
}
