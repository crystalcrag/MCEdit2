/*
 * blockUpdate.c: whenever a block is updated, check that nearby blocks remain consistent.
 *
 * Written by T.Pierron, jan 2021.
 */

#define BLOCK_UPDATE_IMPL
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
#include "NBT2.h"
#include "mapUpdate.h"
#include "blocks.h"
#include "blockUpdate.h"
#include "redstone.h"

extern int8_t normals[];               /* from render.c */
extern double curTime;                 /* from main.c */
extern int8_t relx[], rely[], relz[];  /* from mapUpdate.c */
extern int8_t xoff[], yoff[], zoff[], opp[];
extern struct BlockSides_t blockSides; /* from blocks.c */
static struct UpdatePrivate_t updates;

static int8_t railsNeigbors[] = { /* find the potential 2 neighbors of a rail based on data table */
	0, 0, 1, SIDE_SOUTH,   0, 0,-1, SIDE_NORTH,      /* N/S */
	1, 0, 0, SIDE_EAST,   -1, 0, 0, SIDE_WEST,       /* E/W */
	1, 1, 0, SIDE_EAST,   -1, 0, 0, SIDE_WEST,       /* ASCE */
	1, 0, 0, SIDE_EAST,   -1, 1, 0, SIDE_WEST,       /* ASCW */
	0, 1,-1, SIDE_NORTH,   0, 0, 1, SIDE_SOUTH,      /* ASCN */
	0, 1, 1, SIDE_SOUTH,   0, 0,-1, SIDE_NORTH,      /* ASCS */
};

static void mapSetData(Map map, vec4 pos, int data)
{
	struct BlockIter_t iter;
	mapInitIter(map, &iter, pos, False);
	DATA8 p = iter.blockIds + DATA_OFFSET + (iter.offset >> 1);
	uint8_t cur = p[0];

	if (iter.offset & 1) p[0] = (cur & 0x0f) | (data << 4);
	else                 p[0] = (cur & 0xf0) | data;
}

static void mapGetNeigbors(Map map, vec4 pos, DATA16 neighbors, int max)
{
	struct BlockIter_t iter;
	int i;
	mapInitIter(map, &iter, pos, False);
	for (i = 0; i < max; i ++)
	{
		mapIter(&iter, xoff[i], yoff[i], zoff[i]);
		if (iter.cd)
		{
			int block = iter.blockIds[iter.offset];
			int data  = iter.blockIds[DATA_OFFSET + (iter.offset >> 1)];
			neighbors[i] = (block << 4) | (iter.offset & 1 ? data >> 4 : data & 15);
		}
		else neighbors[i] = 0;
	}
}

/* return which neighbors (bitfield of SIDE_*) rail can connect to */
static int mapGetRailNeighbors(Map map, DATA16 neighbors, vec4 pos)
{
	int flags, i;
	for (i = flags = 0; i < 4; i ++)
	{
		if (blockIds[neighbors[i] >> 4].special == BLOCK_RAILS)
			flags |= 1 << i;
	}

	if (popcount(flags) < 2)
	{
		/* check 1 block up for other rails to connect to */
		uint16_t nborsTmp[6];
		pos[VY] ++; mapGetNeigbors(map, pos, nborsTmp, 4);
		pos[VY] --;
		for (i = 0; i < 4; i ++)
		{
			if ((flags & (1 << i)) == 0 && blockIds[nborsTmp[i] >> 4].special == BLOCK_RAILS)
				flags |= 0x11 << i, neighbors[i] = nborsTmp[i];
		}
		if (popcount(flags&15) < 2)
		{
			/* check 1 block down */
			pos[VY] --; mapGetNeigbors(map, pos, nborsTmp, 4);
			pos[VY] ++;
			for (i = 0; i < 4; i ++)
			{
				if ((flags & (1 << i)) == 0 && blockIds[nborsTmp[i] >> 4].special == BLOCK_RAILS)
					flags |= 0x101 << i, neighbors[i] = nborsTmp[i];
			}
		}
	}
	return flags;
}

static int mapGetRailData(int curData, int flags)
{
	switch (flags & 15) {
	case 1:
	case 4:
	case 5:
	case 7:
	case 13:
		/* force a north/south direction */
		return flags & 0x10 ? 5 : flags & 0x40 ? 4 : 0;
	case 2:
	case 8:
	case 10:
	case 11:
	case 14:
		/* force a east/west direction */
		return flags & 0x20 ? 2 : flags & 0x80 ? 3 : 1;
	case 3:  return 6; /* curved to SE */
	case 9:  return 7; /* curved to SW */
	case 12: return 8; /* curved to NW */
	case 6:  return 9; /* curved to NE */
	default: return curData; /* use original direction */
	}
}

static void mapUpdateRails(Map map, vec4 pos, int blockId, DATA16 nbors)
{
	uint16_t neighbors[4];
	int      flags, i, data;

	memcpy(neighbors, nbors, sizeof neighbors);
	flags = mapGetRailNeighbors(map, neighbors, pos);

	/* check if neighbors can be updated too */
	for (i = data = 0; i < 4; i ++)
	{
		uint16_t id;
		int8_t * normal;
		vec4     loc;

		if ((flags & (1 << i)) == 0) continue;
		memcpy(loc, pos, sizeof loc);

		/* check if rail in direction <i> can connect to our position <pos> */
		normal = normals + i * 4;
		loc[VX] += normal[VX];
		loc[VY] += normal[VY];
		loc[VZ] += normal[VZ];

		id = flags & (0x111 << i);
		if (id >= 0x100) loc[VY] --; else
		if (id >= 0x10)  loc[VY] ++;
		id = neighbors[i];

		static uint8_t opposite[] = {4, 8, 1, 2};
		static uint8_t connect[]  = {5, 10, 10, 10, 5, 5, 3, 9, 12, 6, 0, 0, 0, 0, 0, 0};
		uint16_t nbors2[4];
		uint16_t cnx, flags2, j;

		mapGetNeigbors(map, loc, nbors2, 4);
		flags2 = mapGetRailNeighbors(map, nbors2, loc) & ~15;
		cnx = connect[id & 15];
		for (j = 0; j < 4; j ++)
		{
			uint16_t n = nbors2[j], flag = 1 << j;
			if ((cnx & flag) == 0 || flag == opposite[i] || blockIds[n>>4].special != BLOCK_RAILS) continue;
			if (connect[n & 15] & opposite[j]) flags2 |= flag;
		}
		if (popcount(flags2 & 15) < 2)
		{
			/* yes, we can */
			flags2 |= opposite[i];
			mapUpdate(map, loc, (id & ~15) | mapGetRailData(id & 15, flags2), NULL, False);
			data |= flags & (0x111 << i);
			if (popcount(data&15) == 2) break;
		}
	}
	mapSetData(map, pos, mapGetRailData(blockId & 15, data));
}

static int8_t bedOffsetX[] = {0, -1,  0, 1};
static int8_t bedOffsetZ[] = {1,  0, -1, 0};

/* a block has been placed/deleted, check if we need to update nearby blocks */
void mapUpdateBlock(Map map, vec4 pos, int blockId, int oldBlockId, DATA8 tile)
{
	uint16_t neighbors[6];

	if (blockId > 0)
	{
		/* block placed */
		switch (blockIds[blockId >> 4].special) {
		case BLOCK_TALLFLOWER:
			/* weird state values from minecraft :-/ */
			if ((blockId & 15) == 10) return;
			mapSetData(map, pos, (blockId & 15) - 10);              pos[VY] ++;
			mapUpdate(map, pos, (blockId & ~15) | 10, NULL, False); pos[VY] --;
			break;
		case BLOCK_DOOR:
			if ((oldBlockId >> 4) != (blockId >> 4))
			{
				/* new door being placed */
				neighbors[0] = (blockId & 8) >> 1;
				/* need to update bottom data part */
				mapSetData(map, pos, (blockId & 3) | neighbors[0]); pos[VY] ++;
				/* and create top part */
				mapUpdate(map, pos, ((blockId & 15) < 4 ? 8 : 9) | (neighbors[0] >> 1) | (blockId & ~15), NULL, False);
				pos[VY] --;
			}
			else /* existing door: only update bottom part */
			{
				mapSetData(map, pos, blockId & 15);
			}
			break;
		case BLOCK_BED:
			if ((blockId & 15) < 8)
			{
				/* just created foot part, now need to create head part */
				pos[VX] += bedOffsetX[blockId & 3];
				pos[VZ] += bedOffsetZ[blockId & 3];
				/* mapUpdate() will update coord of tile entity */
				mapUpdate(map, pos, blockId + 8, NBT_Copy(tile), False);
			}
			break;
		case BLOCK_RAILS:
			mapGetNeigbors(map, pos, neighbors, 6);
			mapUpdateRails(map, pos, blockId, neighbors);
			break;
		}
	}
	else /* block deleted */
	{
		static uint8_t quadCheckSides[] = {
			/* QUAD_CROSS */   SIDE_TOP,
			/* QUAD_CROSS2 */  SIDE_TOP,
			/* QUAD_NORTH */   SIDE_SOUTH,
			/* QUAD_SOUTH */   SIDE_NORTH,
			/* QUAD_EAST */    SIDE_WEST,
			/* QUAD_WEST */    SIDE_EAST,
			/* QUAD_BOTTOM */  SIDE_BOTTOM,
			/* QUAD_ASCE */    SIDE_BOTTOM | (SIDE_WEST  << 3),
			/* QUAD_ASCw */    SIDE_BOTTOM | (SIDE_EAST  << 3),
			/* QUAD_ASCN */    SIDE_BOTTOM | (SIDE_SOUTH << 3),
			/* QUAD_ASCS */    SIDE_BOTTOM | (SIDE_NORTH << 3)
		};
		int i = oldBlockId >> 4;

		if ((i == RSPISTON || i == RSSTICKYPISTON) && oldBlockId >= 8)
		{
			/* extended piston: delete extension */
			i = blockSides.piston[oldBlockId & 7];
			vec4 loc = {pos[VX] + relx[i], pos[VY] + rely[i], pos[VZ] + relz[i]};
			mapUpdate(map, loc, 0, NULL, False);
			return;
		}

		mapGetNeigbors(map, pos, neighbors, 6);

		/* check around the 6 sides of the cube what was deleted */
		for (i = 0; i < 6; i ++)
		{
			BlockState state = blockGetById(neighbors[i]);
			if (state->type == QUAD)
			{
				/* quads need solid ground opposite of their normal */
				uint8_t check = quadCheckSides[state->pxU];
				do {
					uint8_t norm = check&7;
					if (norm == i)
					{
						/* no solid block attached: delete current block */
						int8_t * normal = normals + norm * 4;
						vec4     loc;
						loc[VX] = pos[VX] + normal[VX];
						loc[VY] = pos[VY] + normal[VY];
						loc[VZ] = pos[VZ] + normal[VZ];
						mapUpdate(map, loc, 0, NULL, False);
						break;
					}
					check >>= 3;
				} while(check);
			}
			else
			{
				Block b = &blockIds[neighbors[i] >> 4];
				DATA8 p = b->name + b->placement;
				int   j;
				if (b->placement == 0) continue;
				/* check that placement constraints are still satisfied */
				for (j = p[0], p ++; j > 0; j --, p += 2)
				{
					int id = (p[0] << 8) | p[1];
					switch (id) {
					case PLACEMENT_GROUND:
						if (i != SIDE_TOP || ! blockIsAttached(neighbors[i], opp[i])) continue;
						/* no ground below that block: remove it */
						break;
					case PLACEMENT_WALL:
						if (i >= SIDE_TOP || ! blockIsAttached(neighbors[i], opp[i]))
							continue;
						break;
					default:
						if (! blockIsAttached(neighbors[i], opp[i]))
							continue;
						break;
					}
					/* constraint not satisfied: delete neighbor block */
					int8_t * normal = normals + i * 4;
					vec4     loc;
					loc[VX] = pos[VX] + normal[VX];
					loc[VY] = pos[VY] + normal[VY];
					loc[VZ] = pos[VZ] + normal[VZ];
					mapUpdate(map, loc, 0, NULL, False);
					break;
				}
			}
		}

		blockId = oldBlockId;
		switch (blockIds[blockId >> 4].special) {
		case BLOCK_TALLFLOWER:
			if ((blockId&15) < 10)
				/* remove bottom part: delete top */
				pos[VY] ++;
			else
				pos[VY] --;
			mapUpdate(map, pos, 0, NULL, False);
			break;
		case BLOCK_DOOR:
			/* remove all parts of door */
			if (blockId & 8)
				/* we just removed the top part, remove bottom */
				pos[VY] --;
			else
				pos[VY] ++;
			mapUpdate(map, pos, 0, NULL, False);
			break;
		case BLOCK_BED:
			/* remove both parts of bed */
			if ((blockId & 15) < 8)
			{
				/* delete foot part, also delete head */
				pos[VX] += bedOffsetX[blockId & 3];
				pos[VZ] += bedOffsetZ[blockId & 3];
			}
			else
			{
				pos[VX] -= bedOffsetX[blockId & 3];
				pos[VZ] -= bedOffsetZ[blockId & 3];
			}
			mapUpdate(map, pos, 0, NULL, False);
			break;
		}
	}
}

static void mapUpdateRailsChain(Map map, BlockIter iter, int id, int offset, int powered)
{
	uint8_t power = powered;
	uint8_t max;
	for (max = 0; max < RSMAXDISTRAIL; max ++)
	{
		int8_t * next = railsNeigbors + (id & 7) * 8 + offset;
		mapUpdateTable(iter, power ? (id&15) | 8 : id & ~0xffff8, DATA_OFFSET);
		mapIter(iter, next[0], next[1], next[2]);
		id = getBlockId(iter);
		if ((id >> 4) != RSPOWERRAILS)
		{
			/* check one block below: might be an ascending rail */
			static uint8_t sideTop[] = {0xff, 0xff, SIDE_WEST, SIDE_EAST, SIDE_SOUTH, SIDE_NORTH, 0xff, 0xff};
			mapIter(iter, 0, -1, 0);
			id = getBlockId(iter);
			if ((id >> 4) != RSPOWERRAILS)
				break;
			if (sideTop[id&7] != next[3])
				/* not connected to previous rail */
				break;
		}
		if ((id&8) == power)
			break;
		if (power == 0)
		{
			/* stop if there is a power source nearby */
			uint8_t i;
			for (i = 0; i < 6; i ++)
			{
				if (redstoneIsPowered(*iter, i, POW_NORMAL))
				{
					/* need to propagate the power from there now */
					max = -1; power = 8; powered = 1;
					break;
				}
			}
		}
	}
	if (power == 0 && (id >> 4) == RSPOWERRAILS && (id&15) >= 8)
	{
		/* we ended up at a rail being powered when unpowering the chain: we might have unpowered too many rails */
		for (max = 0; max < RSMAXDISTRAIL; max ++)
		{
			/* continue following the chain until we find the power source */
			int8_t * next = railsNeigbors + (id & 7) * 8 + offset;
			mapIter(iter, next[0], next[1], next[2]);
			id = getBlockId(iter);
			if ((id >> 4) == RSPOWERRAILS && (id&15) >= 8)
			{
				uint8_t i;
				for (i = 0; i < 6 && ! redstoneIsPowered(*iter, i, POW_NORMAL); i ++);
				if (i < 6) { mapUpdateRailsChain(map, iter, id, 4-offset, 4); break; }
			}
			else break;
		}
	}
}

/* power near a power rails has changed, update everything connected */
void mapUpdatePowerRails(Map map, BlockIter iterator)
{
	struct BlockIter_t iter = *iterator;

	int id = getBlockId(&iter), i;
	for (i = 0; i < 6 && ! redstoneIsPowered(iter, i, POW_NORMAL); i ++);

	if ((id & 15) < 8)
	{
		if (i == 6) return;
		/* rails not powered, but has a power source nearby: update neighbor */
		iter = *iterator; mapUpdateRailsChain(map, &iter, id, 0, 8);
		iter = *iterator; mapUpdateRailsChain(map, &iter, id, 4, 8);
	}
	else if (i == 6)
	{
		/* rails powered with no power source nearby */
		iter = *iterator; mapUpdateRailsChain(map, &iter, id, 0, 0);
		iter = *iterator; mapUpdateRailsChain(map, &iter, id, 4, 0);
	}
}

/* power near fence gate/trapdoor/dropper/dispenser has changed */
int mapUpdateGate(BlockIter iterator, int id, Bool init)
{
	/* trapdoor and fence gate have sligtly different data value */
	uint8_t flag, powered, i;
	for (i = 0; i < 6 && ! redstoneIsPowered(*iterator, i, POW_NORMAL); i ++);
	switch (blockIds[id>>4].special) {
	case BLOCK_TRAPDOOR:  flag = 4;  powered = id & 4; break;
	case BLOCK_FENCEGATE: flag = 12; powered = id & 8; break;
	default:              flag = 8;  powered = id & 8; break;
	}
	if (powered == 0)
	{
		if (i == 6) return id;
		/* gate closed, but has a power source nearby */
		if (init) return id | flag;
		else mapUpdateTable(iterator, (id | flag) & 15, DATA_OFFSET);
	}
	else if (i == 6)
	{
		/* gate powered/opened, without power source */
		if (init) return id & ~flag; /* cut power and close gate */
		else mapUpdateTable(iterator, (id & ~flag) & 15, DATA_OFFSET);
	}
	return id;
}

/* power near door has changed */
int mapUpdateDoor(BlockIter iterator, int blockId, Bool init)
{
	struct BlockIter_t iter = *iterator;
	int bottom = blockId;
	if (bottom & 8)
		/* top part of door: open state is in bottom part */
		mapIter(&iter, 0, -1, 0), bottom = getBlockId(&iter);

	/* 10 blocks to check :-/ */
	uint8_t i, powered = 2;
	for (i = 0; i < 6 && (i == SIDE_TOP || ! redstoneIsPowered(iter, i, POW_NORMAL)); i ++);
	if (i == 6)
	{
		for (i = 0, mapIter(&iter, 0, 1, 0); i < 5 && ! redstoneIsPowered(iter, i, POW_NORMAL); i ++);
		if (i == 5) powered = 0;
		mapIter(&iter, 0, -1, 0);
	}
	if (! init)
	{
		/* powered state is in top part */
		mapIter(&iter, 0, 1, 0);
		int top = getBlockId(&iter);
		if ((top & 2) != powered)
		{
			mapUpdateTable(&iter, (top&13) | powered, DATA_OFFSET);
			mapIter(&iter, 0, -1, 0);
			/* if powered, force the door in opened state */
			powered <<= 1;
			if ((bottom & 4) != powered)
				mapUpdateTable(&iter, (bottom&11) | powered, DATA_OFFSET);
		}
		return blockId;
	}
	/* this is not a real block state, it will be processed by mapUpdateBlock() */
	else return blockId | (powered << 2);
}

/* power level near piston has changed */
int mapUpdatePiston(BlockIter iterator, int blockId, Bool init)
{
	int avoid = blockSides.piston[blockId & 7];
	int i;
	for (i = 0; i < 6 && (i == avoid || ! redstoneIsPowered(*iterator, i, POW_WEAK)); i ++);
	if (blockId & 8)
	{
		if (i < 6) return blockId;
		/* piston powered, but no power source */
		if (init) return blockId & ~8;
		else mapUpdateTable(iterator, (blockId & ~8) & 15, DATA_OFFSET);
	}
	else if (i < 6)
	{
		/* not powered, but has a power source nearby */
		if (init) return blockId | 8;
		else mapUpdateTable(iterator, (blockId | 8) & 15, DATA_OFFSET);
	}
	return blockId;
}


/* return the activated state of <blockId>, but does not modify any tables */
int mapActivateBlock(BlockIter iter, vec4 pos, int blockId)
{
	Block b = &blockIds[blockId >> 4];

	switch (b->special) {
	case BLOCK_DOOR:
		/*
		 * bottom door data:
		 * - bit1: orient
		 * - bit2: orient
		 * - bit3: 1 if open
		 * - bit4: 0 (bottom part)
		 * top door data:
		 * - bit1: 1 if hinge on the right
		 * - bit2: 1 if powered
		 * - bit3: 0
		 * - bit4: 1 (top part)
		 */
		if (blockId & 8)
		{
			mapIter(iter, 0, -1, 0);
			pos[VY] --;
			blockId = getBlockId(iter);
		}

		// no break;
	case BLOCK_TRAPDOOR:
		/*
		 * bit1~2: orient
		 * bit3: open state
		 * bit4: bottom
		 */
		// no break;
	case BLOCK_FENCEGATE:
		return blockId ^ 4;
	default:
		switch (FindInList("unpowered_repeater,powered_repeater,cake,lever,stone_button,wooden_button,cocoa_beans", b->tech, 0)) {
		case 0:
		case 1: /* repeater: bit3~4: delay */
			if ((blockId & 12) == 12) blockId &= ~12;
			else blockId += 4;
			break;
		case 2: /* cake: 0~6: bites */
			if ((blockId & 15) < 6) blockId ++;
			else                    blockId &= 0xfff0;
			break;
		case 3: /* lever: bit4: powered */
			return blockId ^ 8;
		case 4: /* button */
			if ((blockId & 8) == 0)
			{
				updateAdd(iter, blockId, TICK_PER_SECOND);
				return blockId | 8;
			}
			else return 0;
			break;
		case 5: /* wooden */
			if ((blockId & 8) == 0)
			{
				updateAdd(iter, blockId, TICK_PER_SECOND * 1.5);
				return blockId | 8;
			}
			else return 0;
			break;
		case 6: /* cocoa beans - cycle through different growth stage */
			if ((blockId & 15) < 8) blockId += 4;
			else                    blockId &= 0xfff0;
			break;
		default:
			return 0;
		}
		return blockId;
	}
	return 0;
}

/*
 * delayed block update (tile tick).
 * kept in a hash table along with a sorted list (by tick) to scan all items.
 */

static TileTick updateInsert(ChunkData cd, int offset, int tick);

#define TOHASH(cd, offset)      (((uint64_t) (int)cd) | ((uint64_t)offset << 32))
#define EOL                     0xffff

Bool updateAlloc(int max)
{
	max = roundToUpperPrime(max);

	updates.list   = calloc(max, sizeof (struct TileTick_t) + sizeof *updates.sorted);
	updates.max    = max;
	updates.count  = 0;
	updates.sorted = (DATA16) (updates.list + max);

	return updates.list != NULL;
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

void updateRemove(ChunkData cd, int offset, int clearSorted)
{
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

void updateAdd(BlockIter iter, int blockId, int nbTick)
{
	TileTick update = updateInsert(iter->cd, iter->offset, curTime + nbTick * (1000 / TICK_PER_SECOND));
	update->blockId = blockId;

	fprintf(stderr, "adding block update in %d tick at %d, %d, %d to %d:%d [%d]\n", nbTick,
		iter->ref->X + (iter->offset & 15), iter->yabs, iter->ref->Z + ((iter->offset >> 4) & 15), blockId >> 4, blockId & 15, updates.count);
}

void updateTick(Map map)
{
	int i, time = curTime, count;
	/* more tile ticks can be added while scanning this list */
	for (i = 0, count = updates.count; i < count; )
	{
		int       id   = updates.sorted[i];
		TileTick  list = updates.list + id;
		int       off  = list->offset;
		ChunkData cd   = list->cd;
		vec4      pos;
		if (list->tick > time) break;
		pos[0] = cd->chunk->X + (off & 15); off >>= 4;
		pos[2] = cd->chunk->Z + (off & 15);
		pos[1] = cd->Y + (off >> 4);

		updates.start ++;

		i ++;
		mapUpdate(map, pos, list->blockId, NULL, i == count || updates.list[updates.sorted[i]].tick > time);
		updateRemove(cd, list->offset, 0);
	}
	if (i > 0)
	{
		/* remove processed updates in sorted array */
		memmove(updates.sorted, updates.sorted + i, updates.count * sizeof *updates.sorted);
		updates.start = 0;
	}
}
