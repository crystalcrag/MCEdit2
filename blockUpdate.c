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
#include "maps.h"
#include "NBT2.h"
#include "blocks.h"
#include "blockUpdate.h"
#include "redstone.h"

extern int8_t normals[]; /* from render.c */
extern double curTime;
static struct UpdatePrivate_t updates;

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
	static int8_t xoff[] = {0,  1, -1, -1, 1,  0};
	static int8_t yoff[] = {0,  0,  0,  0, 1, -2};
	static int8_t zoff[] = {1, -1, -1,  1, 0,  0};

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
Bool mapUpdateBlock(Map map, vec4 pos, int blockId, int oldBlockId, DATA8 tile)
{
	uint16_t neighbors[6];
	int      ret = False;

	mapGetNeigbors(map, pos, neighbors, 6);

	if (blockId > 0)
	{
		/* block placed */
		switch (blockIds[blockId >> 4].special) {
		case BLOCK_TALLFLOWER:
			/* weird state values from minecraft :-/ */
			if ((blockId & 15) == 10) return False;
			mapSetData(map, pos, (blockId & 15) - 10);              pos[VY] ++;
			mapUpdate(map, pos, (blockId & ~15) | 10, NULL, False); pos[VY] --;
			break;
		case BLOCK_DOOR:
			/* top part was just created */
			if (blockId & 8) return False;
			/* need to update bottom data part */
			mapSetData(map, pos, blockId & 3); pos[VY] ++;
			/* and create top part */
			mapUpdate(map, pos, ((blockId & 15) < 4 ? 8 : 9) | (blockId & ~15), NULL, False);
			pos[VY] --;
			ret = True;
			break;
		case BLOCK_BED:
			if ((blockId & 15) < 8)
			{
				/* just created foot part, now need to create head part */
				pos[VX] += bedOffsetX[blockId & 3];
				pos[VZ] += bedOffsetZ[blockId & 3];
				/* mapUpdate() will update coord of tile entity */
				mapUpdate(map, pos, blockId + 8, NBT_Copy(tile), False);
				ret = True;
			}
			break;
		case BLOCK_RAILS:
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
		int i;

		/* check around the 6 sides of the cube that was deleted */
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
						ret = True;
						break;
					}
					check >>= 3;
				} while(check);
			}
			else
			{
				static uint8_t oppositeSENW[] = {SIDE_NORTH, SIDE_WEST, SIDE_SOUTH, SIDE_EAST, SIDE_TOP, SIDE_BOTTOM};
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
						if (i != SIDE_TOP) continue;
						/* no ground below that block: remove it */
						break;
					case PLACEMENT_WALL:
						if (i >= SIDE_TOP || ! blockIsAttached(neighbors[i], oppositeSENW[i]))
							continue;
						break;
					default:
						if (! blockIsAttached(neighbors[i], oppositeSENW[i]))
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
	return ret;
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
	case BLOCK_FENCE|BLOCK_NOCONNECT: /* fence gate */
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
 * delayed block update (tile tick)
 */
static TileTick updateAlloc(void)
{
	if (updates.count == updates.max)
	{
		int usage  = (DATA8) updates.usage  - (DATA8) updates.list;
		int sorted = (DATA8) updates.sorted - (DATA8) updates.list;
		updates.max += 128;
		TileTick list = realloc(updates.list, updates.max * (sizeof *list + 2) + (updates.max >> 5) * 4);
		if (list == NULL) return NULL;
		updates.list = list;
		updates.sorted = (DATA16) (list + updates.max);
		updates.usage = (DATA32) (updates.sorted + updates.max);
		if (updates.count > 0)
		{
			memmove(updates.sorted, (DATA8) list + usage,  updates.count * 2);
			memmove(updates.usage,  (DATA8) list + sorted, (updates.count >> 5) * 4);
		}
		memset(updates.usage + (updates.count >> 5), 0, 16);
	}
	updates.count ++;
	return updates.list + mapFirstFree(updates.usage, updates.max >> 5);
}

void updateAdd(BlockIter iter, int blockId, int nbTick)
{
	TileTick update = updateAlloc();
	int      tick   = (int) curTime + nbTick * (1000 / TICK_PER_SECOND);
	update->cd      = iter->cd;
	update->offset  = iter->offset;
	update->blockId = blockId;
	update->tick    = tick;

	/* keep them sorted for easier scanning later */
	int i, max;
	for (i = updates.start, max = updates.count-1; i < max; i ++)
	{
		TileTick list = updates.list + updates.sorted[i];
		if (tick < list->tick) break;
	}

	if (i < updates.count-1)
		memmove(updates.sorted + i + 1, updates.sorted + i, (updates.count - i - 1) * sizeof *updates.sorted);

//	fprintf(stderr, "updating %d:%d at ", blockId >> 4, blockId & 15); printCoord(NULL, iter);

	updates.sorted[i] = update - updates.list;
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

		updates.usage[id >> 5] ^= 1 << (id & 31);
		updates.start ++;

		i ++;
		/* after this point <list> can be overwritten, do not use past this point */
		mapUpdate(map, pos, list->blockId, NULL, i == count || updates.list[updates.sorted[i]].tick > time);
	}
	if (i > 0)
	{
		/* remove processed updates in sorted array */
		memmove(updates.sorted, updates.sorted + i, updates.count * sizeof *updates.sorted);
		updates.count -= i;
		updates.start = 0;
	}
}
