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
#define EOL_MARKER              0xffff

static Bool updateAlloc(int max)
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
			{
				TileTick tick = updateInsert(entry->cd, entry->offset, entry->tick);
				tick->cb = entry->cb;
			}
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

	/* similar to TileEntities hash table */
	for (entry = updates.list + index, last = NULL; entry->tick; entry = updates.list + entry->next)
	{
		last = entry;
		/* check if already inserted */
		if (entry->cd == cd && entry->offset == offset)
			return entry;
		if (entry->next == EOL_MARKER)
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

	if (last)
	{
		entry->prev = last - updates.list;
		last->next = entry - updates.list;
	}
	else entry->prev = EOL_MARKER;
	entry->next   = EOL_MARKER;
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

Bool updateRemove(ChunkData cd, int offset, Bool clearSorted)
{
	if (! updates.list)
		return False;
	TileTick base = updates.list;
	TileTick entry = updates.list + TOHASH(cd, offset) % updates.max;

	if (entry->tick == 0) return False;
	while (entry->cd != cd || entry->offset != offset)
	{
		if (entry->next == EOL_MARKER) return False;
		entry = base + entry->next;
	}

	/* entry is in the table */
	updates.count --;
	entry->tick = 0;
	if (entry->prev != EOL_MARKER)
		base[entry->prev].next = entry->next;
	if (entry->next != EOL_MARKER)
		base[entry->next].prev = entry->prev;

	int index = entry - base;
	while (entry->next != EOL_MARKER)
	{
		TileTick next = base + entry->next;

		/* check if this entry depended on the slot that was freed */
		if (index == TOHASH(next->cd, next->offset) % updates.max)
		{
			/* yes, need relocation */
			index = entry->next;
			*entry = *next;
			next->tick = 0;
			if (entry->prev != EOL_MARKER)
				base[entry->prev].next = entry - base;
			if (entry->next != EOL_MARKER)
				base[entry->next].prev = entry - base;

			/* index of <next> has changed, need to update <sorted> array too */
			DATA16 p;
			int    i;
			for (i = updates.max, p = updates.sorted; i > 0; i --, p ++)
				if (*p == index) { *p = entry - base; break; }
		}
		entry = next;
	}

	if (clearSorted)
	{
		DATA16 p;
		int    i;
		for (i = updates.max, p = updates.sorted, index = entry - updates.list; i > 0; i --, p ++)
		{
			if (*p != index) continue;
			if (i > 1) memmove(p, p + 1, i * 2 - 2);
			break;
		}
	}
	return True;
}

/* check if a tile tick is scheduled for this location */
Bool updateScheduled(ChunkData cd, int offset)
{
	if (updates.max == 0)
		return False;

	TileTick entry;
	int      index = TOHASH(cd, offset) % updates.max;

	for (entry = updates.list + index; entry->tick; entry = updates.list + entry->next)
	{
		/* check if already inserted */
		if (entry->cd == cd && entry->offset == offset)
			return True;
		if (entry->next == EOL_MARKER)
			break;
	}
	return False;
}

#if 0
#include <malloc.h>
static void updateDebugSorted(void)
{
	int i, ok;
	DATA8 exists = alloca(updates.max);
	memset(exists, 0, updates.max);
//	fprintf(stderr, " [%d: ", updates.count);
	for (i = 0, ok = 1; i < updates.count; i ++)
	{
		//if (i > 0) fprintf(stderr, ", ");
		int id = updates.sorted[i];
		if (exists[id]) ok = 0;
		exists[id] = 1;
		TileTick tick = updates.list + id;
		if (! updateScheduled(tick->cd, tick->offset))
			puts("no good: not in hash");
		// fprintf(stderr, "%d:%p", id, updates.list[id].cb);
	}
	if (! ok)
		puts("no good: same entry twice");
	for (i = ok = 0; i < updates.max; i ++)
		if (updates.list[i].tick > 0) ok ++;
	if (ok != updates.count)
		puts("no good: entry not cleared");
//	fprintf(stderr, "]: %s\n", ok ? "OK" : "DUP");
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

/* process tile tick coming from NBT: they do not have enough information to be processed as is */
static void updateTileTick(Map map, BlockIter iter)
{
	DATA8 tile = NULL;
	int blockId = getBlockId(iter);
	int newId = mapUpdateIfPowered(map, iter, -1, blockId, True, &tile);
	if (newId != blockId)
	{
		/* note: mapUpdate() has already been configured to use iterator <iter> */
		mapUpdate(map, NULL, newId, tile, UPDATE_DONTLOG | UPDATE_SILENT);
	}
}

/* read tile ticks from NBT records */
void updateParseNBT(Chunk c)
{
	int offset = NBT_FindNode(&c->nbt, 0, "/Level.TileTicks");

	if (NBT_Tag(&c->nbt, offset) != TAG_List_Compound)
		return;

	NBTIter_t iter;
	NBT_InitIter(&c->nbt, offset, &iter);

	/*
	 * TAG_List_Compound: each compound has the following fields (from wiki):
	 * - i: block id (TAG_String)
	 * - p: priority (TAG_Int): lower number means higher priority
	 * - t: ticks (TAG_Int): number of ticks the update must be done, can be negative
	 * - x, y, z: world space coord where the update should happen
	 */
	int count = 0;
	while ((offset = NBT_Iter(&iter)) >= 0)
	{
		TileTick_t tick;
		NBTIter_t compound;
		NBT_InitIter(&c->nbt, offset, &compound);

		memset(&tick, 0, sizeof tick);

		int off, flags = 0, x = 0, y = 0, z = 0, layer, pos;
		while ((off = NBT_Iter(&compound)) >= 0 && flags != 63)
		{
			switch (compound.name[0]) {
			case 'i': flags |= 1;  tick.blockId = itemGetByName(NBT_Payload(&c->nbt, off), False); break;
			case 'p': flags |= 2;  tick.priority = NBT_GetInt(&c->nbt, off, 0); break;
			case 't': flags |= 4;  tick.tick = NBT_GetInt(&c->nbt, off, 0); break;
			case 'x': flags |= 8;  x = NBT_GetInt(&c->nbt, off, 0); break;
			case 'y': flags |= 16; y = NBT_GetInt(&c->nbt, off, 0); break;
			case 'z': flags |= 32; z = NBT_GetInt(&c->nbt, off, 0);
			}
		}
		pos = (x & 15) | ((z & 15) << 4) | ((y & 15) << 8);
		layer = y >> 4;
		if (flags == 63 && (x & ~15) == c->X && (z & ~15) == c->Z && layer < c->maxy && (tick.cd = c->layer[layer]) &&
		    tick.cd->blockIds[pos] == (tick.blockId >> 4))
		{
			TileTick update = updateInsert(tick.cd, pos, globals.curTime + tick.tick * globals.redstoneTick);
			/* blockId stored in tile tick is not going to help */
			update->cb = updateTileTick;
			count ++;
		}
	}
	c->cflags |= CFLAG_HAS_TT;
	if (count > 0)
		//fprintf(stderr, "adding %d tile ticks at %d, %d\n", count, c->X, c->Z),
		/* already mark the NBT record as modified, this list is short lived anyway */
		chunkMarkForUpdate(c, CHUNK_NBT_TILETICKS);
}

/* called before saving a chunk to disk */
int updateCount(Chunk chunk)
{
	int count, i, max;
	for (count = 0, i = 0, max = updates.count; i < max; i ++)
	{
		TileTick tile = &updates.list[updates.sorted[i]];
		if (tile->cd->chunk == chunk) count ++;
	}
	return count;
}

/* serialize a TileTick into an NBT record to be saved on disk */
Bool updateGetNBT(Chunk chunk, NBTFile nbt, DATA16 index)
{
	static uint8_t buffer[256];

	int i, max;

	for (i = index[0], max = updates.count; i < max; i ++)
	{
		TileTick tile = &updates.list[updates.sorted[i]];
		if (tile->cd->chunk != chunk) continue;
		TEXT techName[64];
		int  off = tile->offset;
		int  ticks = (tile->tick - (unsigned) globals.curTime) / globals.redstoneTick;
		/* we can use a static buffer because saving chunks is not multi-threaded */
		nbt->mem = buffer;
		nbt->max = sizeof buffer;
		nbt->usage = 0;
		itemGetTechName(ID(tile->cd->blockIds[off], 0), techName, sizeof techName, False);
		*index = i + 1;
		NBT_Add(nbt,
			TAG_String, "i", techName,
			TAG_Int,    "p", i,
			TAG_Int,    "t", ticks,
			TAG_Int,    "x", chunk->X + (off & 15),
			TAG_Int,    "z", chunk->Z + ((off >> 4) & 15),
			TAG_Int,    "y", tile->cd->Y + (off >> 8),
			TAG_End
		);
		return True;
	}
	return False;
}

/* usually redstone devices (repeater, torch) update surrounding blocks after a delay */
void updateTick(void)
{
	struct BlockIter_t iter;
	int i = 0, time = globals.curTime;
	mapUpdateInit(&iter);
	/* more tile ticks can be added while scanning this list */
	while (updates.count > 0)
	{
		int        id    = updates.sorted[0];
		TileTick   list  = updates.list + id;
		Chunk      chunk = list->cd->chunk;
		UpdateCb_t cb    = list->cb;
		if (list->tick > time) break;
		//if (list->tick == 0)
		//	puts("no good: cleared entry");
		mapInitIterOffset(&iter, list->cd, list->offset);

		//fprintf(stderr, "applying block update %d at %d, %d, %d for %d:%d", id,
		//	(int) pos[0], (int) pos[1], (int) pos[2], list->blockId >> 4, list->blockId & 15);
		i ++;
		id = list->blockId;
		updateRemove(list->cd, list->offset, False);
		//	puts("no good: not cleared");
		memmove(updates.sorted, updates.sorted + 1, updates.count * sizeof *updates.sorted);
		if ((chunk->cflags & CFLAG_REBUILDTT) == 0)
			chunkMarkForUpdate(chunk, CHUNK_NBT_TILETICKS);
		//updateDebugSorted();

		/* this can modify updates.sorted */
		if (cb)
		{
			cb(globals.level, &iter);
		}
		else if (id == BLOCK_UPDATE)
		{
			mapUpdateChangeRedstone(globals.level, &iter, RSSAMEBLOCK, NULL);
		}
		else mapUpdate(globals.level, NULL, id, NULL, UPDATE_DONTLOG | UPDATE_SILENT);
	}
	if (i > 0)
	{
		/* update meshes */
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
