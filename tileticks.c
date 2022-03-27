/*
 * tileticks.c: delayed block update. kept in a hash table along with a sorted list (by tick) to scan all items.
 *
 * Written by T.Pierron, mar 2021.
 */

#define TILE_TICK_IMPL
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
#include "NBT2.h"
#include "mapUpdate.h"
#include "blocks.h"
#include "tileticks.h"
#include "redstone.h"
#include "globals.h"


static struct UpdatePrivate_t updates;


static TileTick updateInsert(ChunkData cd, int offset, int tick);
void mapUpdateChangeRedstone(Map map, BlockIter iterator, int side, RSWire dir);

#define TOHASH(cd, offset)      (((uint64_t) (int)cd) | ((uint64_t)offset << 32))
#define EOL                     0xffff

Bool updateAlloc(int max)
{
	max = roundToUpperPrime(max < 32 ? 32 : max);

	updates.list   = calloc(max, sizeof (struct TileTick_t) + sizeof *updates.sorted);
	updates.max    = max;
	updates.count  = 0;
	updates.sorted = (DATA16) (updates.list + max);

	return updates.list != NULL;
}

/* map will be closed shortly */
void updateClearAll(void)
{
	free(updates.list);
	memset(&updates, 0, sizeof updates);
}

static void updateExpand(void)
{
	TileTick old = updates.list;
	int      max = updates.max;

	/* redo from scratch */
	if (updateAlloc(max+1))
	{
		/* shouldn't happen very often: performance is not really a concern */
		TileTick entry, eof;
		for (entry = old, eof = entry + max; entry < eof; entry ++)
		{
			if (entry->tick > 0)
				updateInsert(entry->cd, entry->offset, entry->tick);
		}
		free(old);
	}
}

static TileTick updateInsert(ChunkData cd, int offset, int tick)
{
	if ((updates.count * 36 >> 5) >= updates.max)
	{
		/* 90% full: expand */
		updateExpand();
	}

	TileTick entry, last;
	int      index = TOHASH(cd, offset) % updates.max;

	for (entry = updates.list + index, last = entry->tick == 0 || entry->prev == EOL ? NULL : updates.list + entry->prev; entry->tick;
	     last = entry, entry = updates.list + entry->next)
	{
		/* check if already inserted */
		if (entry->cd == cd && entry->offset == offset)
			return entry;
		if (entry->next == EOL)
		{
			TileTick eof = updates.list + updates.max;
			last = entry;
			do {
				entry ++;
				if (entry == eof) entry = updates.list;
			} while (entry->tick);
			break;
		}
	}

	if (last) last->next = entry - updates.list;
	entry->prev   = last ? last - updates.list : EOL;
	entry->next   = EOL;
	entry->cd     = cd;
	entry->offset = offset;
	entry->tick   = tick;

	/* sort insert into sorted[] array */
	int start, end;
	for (start = 0, end = updates.count; start < end; )
	{
		/* dichotomic search */
		index = (start + end) >> 1;
		TileTick middle = updates.list + updates.sorted[index];
		if (middle->tick == tick) { start = index; break; }
		if (middle->tick <  tick) start = index + 1;
		else end = index;
	}

	if (start < updates.count)
	{
		DATA16 move = updates.sorted + start;
		memmove(move + 1, move, (updates.count - start) * sizeof *move);
	}
	updates.sorted[start] = entry - updates.list;
	updates.count ++;

	return entry;
}

void updateRemove(ChunkData cd, int offset, Bool clearSorted)
{
	if (! updates.list) return;
	TileTick entry = updates.list + TOHASH(cd, offset) % updates.max;
	TileTick last;

	if (entry->tick == 0) return;
	for (last = entry->prev == EOL ? NULL : updates.list + entry->prev; entry->cd != cd || entry->offset != offset;
	     last = entry, entry = updates.list + entry->next)
		if (entry->next == EOL) return;

	/* entry is in the table */
	if (last)
	{
		last->next = entry->next;
		if (entry->next != EOL)
		{
			TileTick next = updates.list + entry->next;
			next->prev = last - updates.list;
		}
		entry->tick = 0;
	}
	else if (entry->next != EOL)
	{
		/* first link of list and more chain link follows */
		int      index = entry->next, i;
		TileTick next = updates.list + index;
		DATA16   p;
		memcpy(entry, next, sizeof *entry);
		entry->prev = EOL;
		next->tick = 0;
		for (i = updates.max, p = updates.sorted; i > 0; i --, p ++)
			if (*p == index) { *p = entry - updates.list; break; }
	}
	/* clear slot */
	else entry->tick = 0;

	updates.count --;
	if (clearSorted)
	{
		DATA16 p;
		int    i, index;
		for (i = updates.max, p = updates.sorted, index = entry - updates.list; i > 0; i --, p ++)
		{
			if (*p != index) continue;
			if (i > 1) memmove(p, p + 1, i * 2 - 2);
			break;
		}
	}
}

#if 0
static void updateDebugSorted(int start)
{
	int i;
	fprintf(stderr, " [%d: ", updates.count);
	for (i = 0; i < updates.count; i ++)
	{
		if (i > 0) fprintf(stderr, ", ");
		int id = updates.sorted[start+i];
		fprintf(stderr, "%d:%d", id, updates.list[id].tick);
	}
	fprintf(stderr, "]\n");
}
#endif

void updateAdd(BlockIter iter, int blockId, int nbTick)
{
	TileTick update = updateInsert(iter->cd, iter->offset, globals.curTime + nbTick * globals.redstoneTick);
	update->blockId = blockId;
}

void updateAddTickCallback(BlockIter iter, int nbTick, UpdateCb_t cb)
{
	TileTick update = updateInsert(iter->cd, iter->offset, globals.curTime + nbTick * globals.redstoneTick);
	update->cb = cb;
}

void updateAddRSUpdate(struct BlockIter_t iter, int side, int nbTick)
{
	if (side != RSSAMEBLOCK)
		mapIter(&iter, relx[side], rely[side], relz[side]);

	TileTick update = updateInsert(iter.cd, iter.offset, globals.curTime + nbTick * globals.redstoneTick);
	update->blockId = BLOCK_UPDATE;
}

/* usually redstone devices (repeater, torch) update surrounding blocks after a delay */
void updateTick(void)
{
	int i = 0, time = globals.curTime;
	mapUpdateInit(NULL);
	/* more tile ticks can be added while scanning this list */
	while (updates.count > 0)
	{
		int        id   = updates.sorted[0];
		TileTick   list = updates.list + id;
		int        off  = list->offset;
		ChunkData  cd   = list->cd;
		UpdateCb_t cb   = list->cb;
		vec4       pos;
		if (list->tick > time) break;
		pos[0] = cd->chunk->X + (off & 15);
		pos[2] = cd->chunk->Z + ((off>>4) & 15);
		pos[1] = cd->Y + (off >> 8);

		//fprintf(stderr, "applying block update %d at %d, %d, %d for %d:%d", id,
		//	(int) pos[0], (int) pos[1], (int) pos[2], list->blockId >> 4, list->blockId & 15);
		i ++;
		id = list->blockId;
		updateRemove(cd, list->offset, False);
		memmove(updates.sorted, updates.sorted + 1, updates.count * sizeof *updates.sorted);
		//updateDebugSorted(0);

		/* this can modify updates.sorted */
		if (cb)
		{
			cb(globals.level, cd, off);
		}
		else if (id == BLOCK_UPDATE)
		{
			struct BlockIter_t iter;
			mapInitIterOffset(&iter, cd, off);
			mapUpdateChangeRedstone(globals.level, &iter, RSSAMEBLOCK, NULL);
		}
		else mapUpdate(globals.level, pos, id, NULL, UPDATE_DONTLOG | UPDATE_SILENT);
	}
	if (i > 0)
	{
		/* remove processed updates in sorted array */
		mapUpdateEnd(globals.level);
	}
}

/* entity animation done (typical: piston and blocks moved in the process) */
void updateFinished(DATA8 tile, vec4 dest)
{
	NBTFile_t nbt = {.mem = tile};
	NBTIter_t iter;
	Map       map = globals.level;
	int       blockId, facing, i;

	if (tile == NULL)
	{
		mapUpdateFlush(map);
		mapUpdateMesh(map);
		return;
	}

	NBT_IterCompound(&iter, tile);
	blockId = 0;
	facing = 0;
	while ((i = NBT_Iter(&iter)) >= 0)
	{
		switch (FindInList("facing,id", iter.name, 0)) {
		case 0: facing = NBT_GetInt(&nbt, i, 0); break;
		case 1: blockId = itemGetByName(NBT_Payload(&nbt, i), False);
		}
	}

	switch (blockId >> 4) {
	case RSPISTONHEAD:
		if (NBT_GetInt(&nbt, NBT_FindNode(&nbt, 0, "extending"), 0) == 0)
		{
			/* remove extended state on piston */
			if (blockId & 8)
				blockId = ID(RSSTICKYPISTON, blockId & 7);
			else
				blockId = ID(RSPISTON, blockId & 7);
		}
		/* else piston extended: add real piston head (instead of entity and delete tile) */
		mapUpdatePush(map, dest, blockId, NULL);
		break;
	case RSPISTONEXT:
		/* get original tile entity of block if any */
		tile = NBT_Payload(&nbt, NBT_FindNode(&nbt, 0, "blockTE"));
		if (tile) memcpy(&tile, tile, sizeof tile);

		/* convert block 36 into actual blocks */
		blockId = itemGetByName(NBT_Payload(&nbt, NBT_FindNode(&nbt, 0, "blockId")), False) |
		          NBT_GetInt(&nbt, NBT_FindNode(&nbt, 0, "blockData"), 0);
		if (blockId > 0)
		{
			vec4 src = {dest[VX] + relx[facing], dest[VY] + rely[facing], dest[VZ] + relz[facing]};
			mapUpdatePush(map, src, 0, NULL);
			/* add block pushed in its final position */
			mapUpdatePush(map, dest, blockId, tile);
		}
	}
}
