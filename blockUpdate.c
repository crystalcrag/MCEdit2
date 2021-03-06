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
#include "tileticks.h"
#include "redstone.h"
#include "entities.h"
#include "inventories.h"
#include "globals.h"

int8_t railsNeigbors[] = { /* find the potential 2 neighbors of a rail based on data table */
	0, 0, 1, SIDE_SOUTH,   0, 0,-1, SIDE_NORTH,      /* N/S */
	1, 0, 0, SIDE_EAST,   -1, 0, 0, SIDE_WEST,       /* E/W */
	1, 1, 0, SIDE_EAST,   -1, 0, 0, SIDE_WEST,       /* ASCE */
	1, 0, 0, SIDE_EAST,   -1, 1, 0, SIDE_WEST,       /* ASCW */
	0, 1,-1, SIDE_NORTH,   0, 0, 1, SIDE_SOUTH,      /* ASCN */
	0, 1, 1, SIDE_SOUTH,   0, 0,-1, SIDE_NORTH,      /* ASCS */
	0, 0, 1, SIDE_SOUTH,   1, 0, 0, SIDE_EAST,       /* curved S/E */
	0, 0, 1, SIDE_SOUTH,  -1, 0, 0, SIDE_WEST,       /* curved S/W */
	0, 0,-1, SIDE_NORTH,  -1, 0, 0, SIDE_WEST,       /* curved N/W */
	0, 0,-1, SIDE_NORTH,   1, 0, 0, SIDE_EAST,       /* curved N/E */
};

/* minecraft update order is S, E, W, N; default neighbor enumeration will be ordered S, E, N, W */
static uint8_t mcNextOrder[] = {1, 3, 4, 2};

/* blocks accepted by flower pot */
TEXT flowerPotList[] =
	"yellow_flower,red_flower,sapling,cactus,brown_mushroom,red_mushroom,cactus,tall_grass";

void mapUpdateChangeRedstone(Map map, BlockIter iterator, int side, RSWire dir);

/* update "Data" NBT array */
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
		neighbors[i] = iter.cd ? getBlockId(&iter) : 0;
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

/* convert a S,E,N,W bitfield (flags) into block data for rails (powered or normal) */
static int mapGetRailData(int blockId, int flags)
{
	static uint8_t curvedTo[] = {0, 0, 0, 6, 0, 0, 9, 0, 0, 7, 0, 0, 8};
	uint8_t data = blockId & 15;
	uint8_t curved = (blockId >> 4) == RSRAILS;
	uint8_t powered = curved ? 0 : data & 8;
	switch (flags & 15) {
	case 1:
	case 4:
	case 5:
	case 7:
	case 13:
		/* force a north/south direction */
		return powered | (flags & 0x10 ? 5 : flags & 0x40 ? 4 : 0);
	case 2:
	case 8:
	case 10:
	case 11:
	case 14:
		/* force a east/west direction */
		return powered | (flags & 0x20 ? 2 : flags & 0x80 ? 3 : 1);
	case 3:
	case 9:
	case 12:
	case 6:
		return curved ? curvedTo[flags & 15] : data;
	default:
		return data; /* use original direction */
	}
}

#define RAILORIENT(blockId)    ((blockId >> 4) == RSRAILS ? blockId & 15 : blockId & 7)

/* a rail has been placed, check if block state need to be updated on nearby rails */
static void mapUpdateNearbyRails(Map map, vec4 pos, int blockId, DATA16 nbors)
{
	uint16_t neighbors[4];
	int      flags, i, data;

	memcpy(neighbors, nbors, sizeof neighbors);
	flags = mapGetRailNeighbors(map, neighbors, pos);

	/* check if neighbors can be updated too */
	for (i = data = 0; i < 4; i = mcNextOrder[i])
	{
		uint16_t id;
		int8_t * normal;
		vec4     loc;

		if ((flags & (1 << i)) == 0) continue;
		memcpy(loc, pos, sizeof loc);

		/* check if rail in direction <i> can connect to our position <pos> */
		normal = cubeNormals + i * 4;
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
		cnx = connect[RAILORIENT(id)];
		for (j = 0; j < 4; j = mcNextOrder[j])
		{
			uint16_t n = nbors2[j], flag = 1 << j;
			if ((cnx & flag) == 0 || flag == opposite[i] || blockIds[n>>4].special != BLOCK_RAILS) continue;
			if (connect[RAILORIENT(n)] & opposite[j]) flags2 |= flag;
		}
		if (popcount(flags2 & 15) < 2)
		{
			/* yes, we can */
			flags2 |= opposite[i];
			mapUpdate(map, loc, (id & ~15) | mapGetRailData(id, flags2), NULL, UPDATE_UNDOLINK);
			data |= flags & (0x111 << i);
			if (popcount(data&15) == 2) break;
		}
	}
	mapSetData(map, pos, mapGetRailData(blockId, data));
}

static void mapUpdateRailsChain(Map map, struct BlockIter_t iter, int id, int offset, int powered);

static int8_t bedOffsetX[] = {0, -1,  0, 1};
static int8_t bedOffsetZ[] = {1,  0, -1, 0};

/* a block has been placed/deleted, check if we need to update nearby blocks */
void mapUpdateBlock(Map map, struct BlockIter_t iter, int blockId, int oldBlockId, DATA8 tile, Bool gravity)
{
	uint16_t neighbors[6];
	uint8_t  i;
	vec4     pos = {iter.ref->X + iter.x, iter.yabs, iter.ref->Z + iter.z};

	for (i = 0; i < 6; i ++)
	{
		mapIter(&iter, xoff[i], yoff[i], zoff[i]);
		neighbors[i] = getBlockId(&iter);
	}
	mapIter(&iter, 0, 1, 0);

	if (blockId > 0)
	{
		/* block placed */
		switch (blockIds[blockId >> 4].special) {
		case BLOCK_TALLFLOWER:
			/* weird state values from minecraft :-/ */
			mapSetData(map, pos, (blockId & 15) - 10); pos[VY] ++;
			mapUpdate(map, pos, (blockId & ~15) | 10, NULL, UPDATE_UNDOLINK);
			break;
		case BLOCK_DOOR:
			if ((oldBlockId >> 4) != (blockId >> 4))
			{
				/* new door being placed */
				uint8_t data = (blockId & 8) >> 1;
				/* need to update bottom data part */
				mapSetData(map, pos, (blockId & 3) | data); pos[VY] ++;
				/* and create top part */
				mapUpdate(map, pos, ((blockId & 15) < 4 ? 8 : 9) | (data >> 1) | (blockId & ~15), NULL, UPDATE_UNDOLINK);
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
				mapUpdate(map, pos, blockId + 8, NBT_Copy(tile), UPDATE_UNDOLINK);
			}
			break;
		case BLOCK_RAILS:
			mapUpdateNearbyRails(map, pos, blockId, neighbors);
			if ((blockId >> 4) == RSPOWERRAILS)
			{
				blockId = getBlockId(&iter);
				mapUpdateRailsChain(map, iter, blockId, 0, blockId & 8);
				mapUpdateRailsChain(map, iter, blockId, 4, blockId & 8);
			}
			break;
		case BLOCK_CHEST:
			/* check if neighbor is a chest and has same orientation */
			i = blockId >> 4;
			if ((blockId & 15) < 4)
			{
				/* oriented N/S */
				if ((neighbors[3] >> 4) == i) i = 3; else
				if ((neighbors[1] >> 4) == i) i = 1; else break;
			}
			else /* oriented E/W */
			{
				if ((neighbors[2] >> 4) == i) i = 2; else
				if ((neighbors[0] >> 4) == i) i = 0; else break;
			}
			if (neighbors[i] != blockId)
			{
				/* not the same orient: adjust then */
				mapIter(&iter, relx[i], 0, relz[i]);
				mapUpdateTable(&iter, blockId & 15, DATA_OFFSET);
			}
		}
		for (i = 0; i < 6; i ++)
		{
			Block b = &blockIds[neighbors[i] >> 4];
			/* extremely likely there are nothing around */
			if (b->rsupdate & RSUPDATE_SEND)
			{
				/* maybe */
				mapUpdateChangeRedstone(map, &iter, RSSAMEBLOCK, NULL);
				break;
			}
			/* trigger falling entity on nearby block, causing cascading avalanche of blocks */
			if (b->gravity && gravity)
				mapUpdateCheckGravity(iter, i);
		}
	}
	else /* block deleted */
	{
		static uint8_t quadCheckSides[] = {
			/* QUAD_CROSS */   SIDE_TOP,
			/* QUAD_CROSS2 */  SIDE_TOP,
			/* QUAD_SQUARE */  SIDE_TOP,
			/* QUAD_SQUARE2 */ SIDE_TOP,
			/* QUAD_SQUARE3 */ SIDE_TOP,
			/* QUAD_SQUARE4 */ SIDE_TOP,
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

		if (((oldBlockId >> 4) == RSPISTON || (oldBlockId >> 4) == RSSTICKYPISTON) && (oldBlockId & 8))
		{
			/* extended piston: delete extension */
			i = blockSides.piston[oldBlockId & 7];
			vec4 loc = {pos[VX] + relx[i], pos[VY] + rely[i], pos[VZ] + relz[i]};
			mapUpdate(map, loc, 0, NULL, UPDATE_UNDOLINK);
			return;
		}

		/* check around the 6 sides of the cube what was deleted */
		for (i = 0; i < 6; i ++)
		{
			if (neighbors[i] == 0) continue;
			BlockState state = blockGetById(neighbors[i]);
			if (state->type == QUAD)
			{
				/* quads need solid ground opposite of their normal */
				uint8_t check = quadCheckSides[state->pxU];
				do {
					uint8_t norm = check&7;
					if (norm == i)
						/* no solid block attached: delete current block */
						goto break_block;
					check >>= 3;
				} while(check);
			}
			else
			{
				Block b = &blockIds[neighbors[i] >> 4];
				DATA8 p = b->name + b->placement;
				int   j;
				if (gravity && b->gravity)
					mapUpdateCheckGravity(iter, i);
				if (b->placement > 0)
				/* check that placement constraints are still satisfied */
				for (j = p[0], p ++; j > 0; j --, p += 2)
				{
					int id = (p[0] << 8) | p[1];
					switch (id) {
					case PLACEMENT_GROUND:
						if (i != SIDE_TOP || ! blockIsAttached(neighbors[i], opp[i], True)) continue;
						/* no ground below that block: remove it */
						break;
					case PLACEMENT_WALL:
						if (i >= SIDE_TOP || ! blockIsAttached(neighbors[i], opp[i], False))
							continue;
						break;
					default:
						if (! blockIsAttached(neighbors[i], opp[i], i == 4))
							continue;
					}
					break_block:
					{
						/* constraint not satisfied: delete neighbor block */
						int8_t * normal = cubeNormals + i * 4;
						vec4     loc;
						loc[VX] = pos[VX] + normal[VX];
						loc[VY] = pos[VY] + normal[VY];
						loc[VZ] = pos[VZ] + normal[VZ];
						mapUpdate(map, loc, 0, NULL, UPDATE_UNDOLINK);
						mapIter(&iter, normal[VX], normal[VY], normal[VZ]);
						mapUpdateBlock(map, iter, 0, neighbors[i], NULL, gravity);
						mapIter(&iter, -normal[VX], -normal[VY], -normal[VZ]);
					}
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
			if ((mapGetBlockId(map, pos, NULL) & ~15) == (blockId & ~15))
				mapUpdate(map, pos, 0, NULL, UPDATE_UNDOLINK);
			break;
		case BLOCK_DOOR:
			/* remove all parts of door */
			if (blockId & 8)
				/* we just removed the top part, remove bottom */
				pos[VY] --;
			else
				pos[VY] ++;
			if ((mapGetBlockId(map, pos, NULL) & ~15) == (blockId & ~15))
				mapUpdate(map, pos, 0, NULL, UPDATE_UNDOLINK);
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
			if ((mapGetBlockId(map, pos, NULL) & ~15) == (blockId & ~15))
				mapUpdate(map, pos, 0, NULL, UPDATE_UNDOLINK);
		}
	}
}

/* follows a chain of powered rails in direction <offset> */
static void mapUpdateRailsChain(Map map, struct BlockIter_t iter, int id, int offset, int powered)
{
	uint8_t power = powered;
	uint8_t max;
	for (max = 0; max < RSMAXDISTRAIL; max ++)
	{
		int8_t * next = railsNeigbors + (id & 7) * 8 + offset;
		mapUpdateTable(&iter, power ? (id&15) | 8 : id & ~0xffff8, DATA_OFFSET);
		mapIter(&iter, next[0], next[1], next[2]);
		id = getBlockId(&iter);
		if ((id >> 4) != RSPOWERRAILS)
		{
			/* check one block below: might be an ascending rail */
			static uint8_t sideTop[] = {0xff, 0xff, SIDE_WEST, SIDE_EAST, SIDE_SOUTH, SIDE_NORTH, 0xff, 0xff};
			mapIter(&iter, 0, -1, 0);
			id = getBlockId(&iter);
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
				if (redstoneIsPowered(iter, i, POW_NORMAL))
				{
					/* need to propagate the power from there now */
					max = -1; power = 8; powered = 1;
					offset = 4 - offset;
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
			mapIter(&iter, next[0], next[1], next[2]);
			id = getBlockId(&iter);
			if ((id >> 4) == RSPOWERRAILS && (id&15) >= 8)
			{
				uint8_t i;
				for (i = 0; i < 6 && ! redstoneIsPowered(iter, i, POW_NORMAL); i ++);
				if (i < 6) { mapUpdateRailsChain(map, iter, id, 4-offset, 8); break; }
			}
			else break;
		}
	}
}

/* power near a power rails has changed, update everything connected */
int mapUpdatePowerRails(Map map, int blockId, BlockIter iterator)
{
	struct BlockIter_t iter = *iterator;

	int id = getBlockId(&iter), i;
	for (i = 0; i < 6 && ! redstoneIsPowered(iter, i, POW_NORMAL); i ++);

	if ((id & 15) < 8)
	{
		if (i == 6) return blockId & ~8;
		/* rails not powered, but has a power source nearby: update neighbor */
		mapUpdateRailsChain(map, *iterator, id, 0, 8);
		mapUpdateRailsChain(map, *iterator, id, 4, 8);
		return blockId | 8;
	}
	else if (i == 6)
	{
		/* rails powered with no power source nearby */
		mapUpdateRailsChain(map, *iterator, id, 0, 0);
		mapUpdateRailsChain(map, *iterator, id, 4, 0);
		return blockId & ~8;
	}
	return blockId;
}

/* powered rail deleted: adjust nearby rails */
void mapUpdateDeleteRails(Map map, BlockIter iterator, int blockId)
{
	/* will be cleared properly later */
	iterator->blockIds[iterator->offset] = 0;
	mapUpdateRailsChain(map, *iterator, blockId, 0, 0);
	mapUpdateRailsChain(map, *iterator, blockId, 4, 0);
}

/* power level near rail has changed: check if we need to update the rail direction */
int mapUpdateRails(Map map, int blockId, BlockIter iterator)
{
	int id = getBlockId(iterator), i;

	/* rail needs to be curved */
	if ((id& 15) < 4) return blockId;

	for (i = 0; i < 6 && ! redstoneIsPowered(*iterator, i, POW_NORMAL); i ++);

	struct BlockIter_t iter = *iterator;
	uint8_t  powered = i < 6;
	uint8_t  flags;
	for (i = 0, flags = 0; i < 4; i ++)
	{
		mapIter(&iter, xoff[i], yoff[i], zoff[i]);
		if (iter.cd && blockIds[iter.blockIds[iter.offset]].special == BLOCK_RAILS)
			flags |= 1 << i;
	}
	if (popcount(flags) == 3)
	{
		/* needs to be exactly 3 possible ways */
		switch (flags) {
		case 14: flags = powered ? 8 : 9; break; /* E,N,W */
		case 13: flags = powered ? 8 : 7; break; /* S,N,W */
		case 11: flags = powered ? 7 : 6; break; /* S,E,W */
		case  7: flags = powered ? 9 : 6;        /* S,E,N */
		}
		blockId = (blockId & ~0xf) | flags;
	}

	return blockId;
}

/* power near fence gate/trapdoor/dropper/dispenser has changed */
int mapUpdateGate(BlockIter iterator, int id, Bool init)
{
	/* trapdoor and fence gate have slightly different data value */
	uint8_t flag, powered, i;
	for (i = 0; i < 6 && ! redstoneIsPowered(*iterator, i, POW_NORMAL); i ++);
	switch (blockIds[id>>4].special) {
	case BLOCK_TRAPDOOR:  flag = 4;  powered = id & 4; break;
	case BLOCK_FENCEGATE: flag = 12; powered = id & 8; break;
	default:              flag = 8;  powered = id & 8; break;
	}
	if (powered == 0)
	{
		if (i == 6 || init) return id;
		/* gate closed, but has a power source nearby */
		mapUpdateTable(iterator, (id | flag) & 15, DATA_OFFSET);
	}
	else if (i == 6)
	{
		/* gate powered/opened, without power source */
		if (! init)
		{
			/* cut power and close gate */
			mapUpdateTable(iterator, (id & ~flag) & 15, DATA_OFFSET);
			if ((id >> 4) == RSHOPPER)
				mapUpdateHopper(iterator->cd, iterator->offset);
		}
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

/* cannot update a piston while its head is moving */
static Bool mapUpdateIsPistonActivated(struct BlockIter_t iter, int blockId)
{
	if (chunkGetTileEntity(iter.cd, iter.offset))
		return False;

	/* <iter> points to piston body */
	if (blockId & 8) /* extended */
	{
		/* tile entity could be one block ahead of facing direction */
		uint8_t ext = blockSides.piston[blockId & 7];
		mapIter(&iter, relx[ext], rely[ext], relz[ext]);
		if (chunkGetTileEntity(iter.cd, iter.offset))
			return False;
	}
	return True;
}

/* add tile entity for piston extension: <iter> points at piston block */
static void mapUpdateAddPistonExt(Map map, struct BlockIter_t iter, int blockId, Bool extend)
{
	uint8_t ext = blockSides.piston[blockId & 7];
	vec4    src = {iter.x + iter.ref->X, iter.yabs, iter.z + iter.ref->Z};

	/* place tile entity into final position (that way it will mark the block as occupied) */
	struct BlockIter_t head = iter;
	mapIter(&head, relx[ext], rely[ext], relz[ext]);

	vec4 dest = {head.x + head.ref->X, head.yabs, head.z + head.ref->Z};

	NBTFile_t ret = {.page = 127};
	TEXT itemId[64];
	int  id = ID(RSPISTONHEAD, blockId & 7);

	if ((blockId >> 4) == RSSTICKYPISTON)
		/* bit 4 is sticky flag for piston head */
		id |= 8;

	NBT_Add(&ret,
		TAG_String, "id",        itemGetTechName(id, itemId, sizeof itemId, True),
		TAG_Int,    "x",         (int) dest[VX],
		TAG_Int,    "y",         (int) dest[VY],
		TAG_Int,    "z",         (int) dest[VZ],
		TAG_Int,    "extending", extend,
		TAG_Int,    "facing",    blockId & 7,
		TAG_Double, "progress",  0.0,
		TAG_Int,    "source",    1,
		TAG_Compound_End
	);

	//fprintf(stderr, "%s piston from %d, %d, %d\n", extend ? "extending" : "retracting",
	//	(int) src[0], (int) src[1], (int) src[2]);

	/* already add/remove head */
	if (! extend)
	{
		/* if retracting, <dest> points to piston, but head is extended right now */
		float tmp[3];
		memcpy(tmp,  src,  12);
		memcpy(src,  dest, 12);
		memcpy(dest, tmp,  12);
		/* already delete piston head */
		mapUpdate(map, src, 0, NULL, UPDATE_KEEPLIGHT | UPDATE_DONTLOG | UPDATE_SILENT);
		chunkAddTileEntity(iter.cd, iter.offset, ret.mem);
	}
	else mapUpdate(map, dest, ID(RSPISTONEXT, 0), ret.mem, UPDATE_KEEPLIGHT | UPDATE_DONTLOG);

	blockId = itemGetByName(NBT_PayloadFromStream(ret.mem, 0, "id"), False);
	/* create or update the moving piston head */
	if (blockId > 0)
		/* XXX need progress */
		entityCreateOrUpdate(iter.ref, src, blockId, dest, 1, ret.mem);
}

/* convert blocks push/rectracted by a piston into block 36 */
void mapUpdateToBlock36(Map map, RSWire list, int count, int dir, BlockIter iterator)
{
	vec4 pos = {iterator->x + iterator->ref->X, iterator->yabs, iterator->z + iterator->ref->Z};
	vec4 off = {relx[dir], rely[dir], relz[dir]};
	int  i;

	for (i = 0; i < count; i ++, list ++)
	{
		vec4 src = {pos[0] + list->dx, pos[1] + list->dy, pos[2] + list->dz};
		vec4 dst = {src[0] + off[0], src[1] + off[1], src[2] + off[2]};

		struct BlockIter_t iter = *iterator;
		mapIter(&iter, list->dx, list->dy, list->dz);

		if (list->pow > 0)
		{
			/* block needs to be converted to world item */
			worldItemCreateFromBlock(&iter, list->signal);
			/* delete block */
			mapUpdate(map, src, 0, NULL, UPDATE_KEEPLIGHT | UPDATE_DONTLOG | UPDATE_SILENT);
			continue;
		}

		/* place tile entity of block 36 into destination block (like piston head) */
		NBTFile_t tile = {.page = 127};
		int       cnx  = 0;
		TEXT      itemId[128];
		STRPTR    blockName;
		itemGetTechName(ID(RSPISTONEXT, 0), itemId, sizeof itemId, False);
		blockName = strchr(itemId, 0) + 1;
		itemGetTechName(list->blockId << 4, blockName, sizeof itemId - (blockName - itemId), False);

		switch (blockIds[list->blockId].special) {
		case BLOCK_FENCE:
		case BLOCK_FENCE2:
		case BLOCK_GLASS:
		case BLOCK_WALL:
			/* try to keep the model somewhat close to what it was in block form */
			cnx = mapGetConnect(iter.cd, iter.offset, blockGetById(ID(list->blockId, list->data)));
			break;
		case BLOCK_CHEST:
			/* only want single block chest */
			cnx = 1;
		}

		DATA8 compound = chunkDeleteTileEntity(iter.cd, iter.offset, True, NULL);

		NBT_Add(&tile,
			TAG_String,  "id",        itemId,
			TAG_String,  "blockId",   blockName,
			TAG_Int,     "blockData", list->data,
			TAG_Int,     "blockCnx",  cnx,
			TAG_Int,     "x",         (int) dst[VX],
			TAG_Int,     "y",         (int) dst[VY],
			TAG_Int,     "z",         (int) dst[VZ],
			TAG_Raw_Ptr, "blockTE",   compound,
			TAG_Int,     "facing",    opp[dir], /* where the block was originating from */
			TAG_Compound_End
		);


		/* delete source block (updates are ordered so that they don't overwrite each other) */
		mapUpdate(map, src, 0, NULL, UPDATE_KEEPLIGHT | UPDATE_DONTLOG | UPDATE_SILENT);

		mapUpdate(map, dst, ID(RSPISTONEXT, 0), tile.mem, UPDATE_KEEPLIGHT | UPDATE_DONTLOG);
		//fprintf(stderr, "adding block 36 (from %s) at %g,%g,%g\n", blockName, src[0], src[1], src[2]);

		entityCreateOrUpdate(iter.ref, src, (list->blockId << 4) | list->data, dst, 1, tile.mem);
	}
}

/* power level near piston has changed */
int mapUpdatePiston(Map map, BlockIter iterator, int blockId, Bool init, BlockUpdate blockedBy)
{
	/* it is MAXPUSH * 2, because some items can be destroyed in the process (and do not count toward MAXPUSH limit) */
	struct RSWire_t connect[MAXPUSH*2];

	int avoid = blockSides.piston[blockId & 7];
	int count, i;
	for (i = count = 0; i < 6 && (i == avoid || ! redstoneIsPowered(*iterator, i, POW_WEAK)); i ++);

	if (blockId & 8)
	{
		/* check if piston is already moving */
		if (! mapUpdateIsPistonActivated(*iterator, blockId) || i < 6)
			/* need to wait for movement to be finished */
			return blockId;

		/* piston extended, but no power source */
		count = redstonePushedByPiston(*iterator, blockId, connect, NULL);
		/* <0 == movement is blocked */
		if (count < 0) return blockId;

		mapUpdateAddPistonExt(map, *iterator, blockId, False);
		mapUpdateToBlock36(map, connect, count, opp[avoid], iterator);

		if (blockedBy)
			blockedBy->tile = (APTR) 1;
		if (init)
			blockId &= ~8;
		/* else keep extended state until head has fully retracted */
	}
	else if (i < 6)
	{
		if (! mapUpdateIsPistonActivated(*iterator, blockId))
			return blockId;

		/* not extended, but has a power source nearby */
		count = redstonePushedByPiston(*iterator, blockId, connect, blockedBy);
		if (count < 0) return blockId;

		/* convert all blocks pushed to block 36 (in reverse order) */
		mapUpdateToBlock36(map, EOT(connect) - count, count, avoid, iterator);
		mapUpdateAddPistonExt(map, *iterator, blockId, True);

		if (init)
			blockId |= 8;
		else /* already convert to extended state */
			mapUpdateTable(iterator, (blockId | 8) & 15, DATA_OFFSET);
	}
	return blockId;
}


/*
 * check if comparator state need to change based on nearby power levels; comparator data values:
 * - bit0-1: orientation (SWNE)
 * - bit2:   subtraction mode
 * - bit3:   powered
 */
int mapUpdateComparator(Map map, BlockIter iterator, int blockId, Bool init, DATA8 * tile)
{
	static uint8_t rot90side[] = {SIDE_EAST, SIDE_NORTH, SIDE_WEST, SIDE_SOUTH};
	struct BlockIter_t input = *iterator;
	NBTFile_t nbt = {.page = 127};
	if (tile == NULL)
		nbt.mem = chunkGetTileEntity(input.cd, input.offset);
	else
		nbt.mem = *tile;

	/* get signal power for rear side */
	uint8_t side = blockSides.repeater[blockId & 3];
	uint8_t rear, left, right, signal;
	mapIter(&input, relx[side], 0, relz[side]);
	rear = redstoneSignalStrength(&input, False);

	/* get signal from both sides */
	input = *iterator;
	side  = rot90side[side];
	mapIter(&input, relx[side], 0, relz[side]);
	left  = redstoneSignalStrength(&input, False);

	input = *iterator;
	side  = opp[side];
	mapIter(&input, relx[side], 0, relz[side]);
	right = redstoneSignalStrength(&input, False);

	if (blockId & 4)
	{
		/* subtraction mode */
		if (left < right) left = right; /* max(left, right) */
		signal = rear > left ? rear - left : 0;
	}
	else /* comparator mode */
		signal = rear * (left <= rear && right <= rear);

	if (init)
	{
		if (nbt.mem == NULL)
		{
			TEXT id[32];
			itemGetTechName(ID(RSCOMPARATOR, 0), id, sizeof id, False);
			/* coord will be filled later in mapUpdate() */
			NBT_Add(&nbt,
				TAG_String, "id", id,
				TAG_Int,    "x",  0,
				TAG_Int,    "y",  0,
				TAG_Int,    "z",  0,
				TAG_Int,    "OutputSignal", 0,
				TAG_Compound_End
			);
			*tile = nbt.mem;
		}
		/* prevent uselss updates later */
		iterator->blockIds[iterator->offset] = RSCOMPARATOR;
	}

	/* only function supported function so far: maintain signal */
	if (nbt.mem)
	{
		int offset = NBT_FindNode(&nbt, 0, "OutputSignal");
		NBT_SetInt(&nbt, offset, signal);
	}

	if (signal > 0 && (blockId & 8) == 0)
		return blockId | 8;
	if (signal == 0 && (blockId & 8))
		return blockId & ~8;

	return blockId;
}

/* player entered or exited a pressure plate */
void mapUpdatePressurePlate(BlockIter iter, float entityBBox[6])
{
	int blockId = getBlockId(iter);
	VTXBBox bbox = blockGetBBox(blockGetById(blockId & ~15)); /* use bbox of non-activated plate */
	float plateBBox[6] = {iter->ref->X + iter->x, iter->yabs, iter->ref->Z + iter->z};
	int i;

	memcpy(plateBBox + 3, plateBBox, 12);
	for (i = 0; i < 3; i ++)
	{
		plateBBox[i]   += FROMVERTEX(bbox->pt1[i]);
		plateBBox[3+i] += FROMVERTEX(bbox->pt2[i]);
	}

	if (plateBBox[VX] >= entityBBox[VX+3] || plateBBox[VX+3] <= entityBBox[VX] ||
		plateBBox[VY] >= entityBBox[VY+3] || plateBBox[VY+3] <= entityBBox[VY] ||
		plateBBox[VZ] >= entityBBox[VZ+3] || plateBBox[VZ+3] <= entityBBox[VZ])
	{
		/* not intersecting */
		if ((blockId & 15) > 0)
			/* is activate: start an update tick to release it after a delay */
			updateAdd(iter, blockId & ~15, (blockId >> 4) == 72 ? TICK_PER_SECOND * 1.5 : TICK_PER_SECOND);
	}
	else /* within bbox of plate */
	{
		if ((blockId & 15) == 0)
			/* not activated yet: do not update in this function, do it outside this context */
			updateAdd(iter, blockId | 1, 0);
	}
}

void mapUpdateObserver(BlockIter iterator, int from)
{
	struct BlockIter_t iter;
	int i;
	for (iter = *iterator, i = 0; from > 0; from >>= 1, i ++)
	{
		mapIter(&iter, xoff[i], yoff[i], zoff[i]);
		int blockId = getBlockId(&iter);
		if ((blockId >> 4) == RSOBSERVER && (blockId & 8) == 0 && blockSides.piston[blockId&7] == opp[i])
			updateAdd(&iter, blockId | 8, 1);
	}
}

int mapUpdatePot(int blockId, ItemID_t itemId, DATA8 * tile)
{
	NBTFile_t nbt = {.mem = *tile};
	int curItemId = itemGetByName(NBT_Payload(&nbt, NBT_FindNode(&nbt, 0, "Item")), False);
	if (itemId == 0)
	{
		/* delete item in the pot */
		if (curItemId == 0)
			/* already empty: delete the pot then */
			return 2;
	}
	else /* check if the item can be placed in the pot */
	{
		uint8_t data = itemId & 15;
		/* yellow_flower,red_flower,sapling,cactus,brown_mushroom,red_mushroom,cactus,tall_grass */
		switch (FindInList(flowerPotList, blockIds[itemId>>4].tech, 0)) {
		case 0: if (data > 0) return 0; break;
		case 1: if (data > 8) return 0; break;
		case 2: if (data > 5) return 0; break;
		case -1: return 0;
		}
	}
	if (itemId > 0)
		curItemId |= NBT_GetInt(&nbt, NBT_FindNode(&nbt, 0, "Data"), 0);
	if (curItemId == itemId)
		return 0;

	/* recreate tile entity */
	static uint8_t tileBuffer[256];
	TEXT itemInPot[64];
	itemGetTechName(itemId, itemInPot, sizeof itemInPot, False);
	memset(&nbt, 0, sizeof nbt);
	/* tile will be dup'ed right after, no need to alloc anything here */
	nbt.mem = tileBuffer;
	nbt.max = sizeof tileBuffer;
	/* note: coordinates will be reset in mapUpdate(), no need to bother right now */
	NBT_Add(&nbt,
		TAG_String, "Item", itemInPot,
		TAG_Byte,   "Data", itemId & 15,
		TAG_Int,    "x",    0,
		TAG_Int,    "y",    0,
		TAG_Int,    "z",    0,
		TAG_String, "id",   blockIds[blockId>>4].tech,
		TAG_Compound_End
	);
	*tile = nbt.mem;
	return 1;
}

/* XXX should be done in tileticks.c directy */
static void mapUpdateHopperTick(Map map, BlockIter iter)
{
	mapUpdateHopper(iter->cd, iter->offset);
}

/* called from meshing/chunk loading: schedule an update */
void mapUpdateHopper(ChunkData cd, int pos)
{
	struct BlockIter_t iter;
	mapInitIterOffset(&iter, cd, pos);
	int blockId = getBlockId(&iter);
	if ((blockId & 8) == 0 && ! updateScheduled(cd, pos))
	{
		/* unlocked hopper: check first if we can transmit block */
		struct BlockIter_t neighbor = iter;
		int dir = blockSides.piston[blockId & 7];
		mapIter(&neighbor, relx[dir], rely[dir], relz[dir]);

		if (! inventoryPushItem(&iter, &neighbor))
		{
			/* nothing was grabbed, check if we can pull */
			neighbor = iter;
			mapIter(&neighbor, 0, 1, 0);
			if (! inventoryPushItem(&neighbor, &iter))
				return;

			/* an item was pulled from the container <neighbor>: more are inside */
			updateAddTickCallback(&iter, HOPPER_COOLDOWN, mapUpdateHopperTick);
			mapUpdateContainerChanged(neighbor.cd, neighbor.offset);
		}
		else updateAddTickCallback(&iter, HOPPER_COOLDOWN, mapUpdateHopperTick);
	}
}

Bool mapUpdateHopperGrabItem(BlockIter iter, Item item)
{
	if ((getBlockId(iter) & 8) == 0 && ! updateScheduled(iter->cd, iter->offset))
	{
		return inventoryPushWorldItem(iter, item);
	}
	return False;
}

/* container changed at given location, check if nearby hopper might need to be updated */
void mapUpdateContainerChanged(ChunkData cd, int pos)
{
	struct BlockIter_t iter;
	int i;
	mapInitIterOffset(&iter, cd, pos);
	for (i = 0; i < 7; i ++)
	{
		int blockId = getBlockId(&iter);
		if ((blockId >> 4) == RSHOPPER && (blockId & 8) == 0 && (i == 0 || i == 6 || blockSides.piston[blockId&7] == opp[i-1]) &&
		    ! updateScheduled(iter.cd, iter.offset))
		{
			/* this hopper might be interested by the container update */
			updateAddTickCallback(&iter, HOPPER_COOLDOWN, mapUpdateHopperTick);
		}
		mapIter(&iter, xoff[i], yoff[i], zoff[i]);
	}
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
		switch (FindInList(
			"unpowered_repeater,powered_repeater,cake,lever,stone_button,"
			"wooden_button,cocoa_beans,cauldron,comparator,anvil", b->tech, 0)) {
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
		case 6: /* cocoa beans: cycle through different growth stage */
		case 9: /* anvil: toggle between damaged state */
			if ((blockId & 15) < 8) blockId += 4;
			else                    blockId &= 0xfff0;
			break;
		case 7: /* cauldron - fill level */
			if ((blockId & 3) < 3) blockId ++;
			else                   blockId &= 0xfff0;
			break;
		case 8: /* comparator: toggle between compare and subtract mode */
			blockId ^= 4;
			break;
		default:
			return 0;
		}
		return blockId;
	}
	return 0;
}

/*
 * rotate/mirror block state
 */

/* adjust data part to reorient the block 90deg CW on Y axis */
int blockRotateY90(int blockId)
{
	/* rotate data value directly */
	static uint8_t rotateFull[] = {0, 1, 5, 4, 2, 3, 6, 7};
	static uint8_t rotateSWNE[] = {1, 2, 3, 0};
	static uint8_t rotateNSWE[] = {5, 1, 5, 4, 2, 3, 6, 7};
	static uint8_t rotateRail[] = {1, 1, 5, 4, 2, 3, 7, 8, 9, 6, 10, 11, 12, 13, 14, 15};
	static uint8_t rotateDoor[] = {1, 2, 3, 0};
	static uint8_t rotateStair[] = {2, 3, 1, 0};
	static uint8_t rotateTorch[] = {0, 3, 4, 2, 1, 5, 6, 7};
	static uint8_t rotateLever[] = {7, 3, 4, 2, 1, 6, 5, 0};
	static uint8_t rotateTrapD[] = {3, 2, 0, 1};

	Block b = &blockIds[blockId>>4];
	switch (b->orientHint) {
	case ORIENT_FULL:   return (blockId & ~7) | rotateFull[blockId & 7];
	case ORIENT_SWNE:
	case ORIENT_BED:    return (blockId & ~3) | rotateSWNE[blockId&3];
	case ORIENT_NSWE:   return (blockId & ~7) | rotateNSWE[blockId & 7];
	case ORIENT_SE:     return blockId ^ 1;
	case ORIENT_LOG:    return 4 <= (blockId & 15) && (blockId & 15) < 12 ? blockId ^ 12 : blockId;
	case ORIENT_STAIRS: return (blockId & ~3) | rotateStair[blockId & 3];
	case ORIENT_TORCH:  return (blockId & ~7) | rotateTorch[blockId & 7];
	case ORIENT_LEVER:  return (blockId & ~7) | rotateLever[blockId & 7];
	case ORIENT_VINES:  return (blockId & ~15) | ROT4(blockId);
	case ORIENT_DOOR:
		/* top part of door: orient is in bottom part */
		if (blockId & 8) return blockId;
		return (blockId & ~3) | rotateDoor[blockId & 3];
	case ORIENT_RAILS:
		if (blockStateIndex[(blockId & ~15) | 7])
			/* this rail type can curve */
			return (blockId & ~15) | rotateRail[blockId & 15];
		else /* only straight */
			return (blockId & ~7)  | rotateRail[blockId & 7];
	default:
		switch (b->special) {
		case BLOCK_SIGN:
			if (blockStateIndex[(blockId & ~15) | 8])
				/* standing post */
				return (blockId & ~15) | ((blockId + 4) & 15);
			else /* wall sign */
				return (blockId & ~7) | rotateNSWE[blockId & 7];
		case BLOCK_TRAPDOOR:
			return (blockId & ~3) | rotateTrapD[blockId & 3];
		default:
			return blockId;
		}
	}
}

/* used by "roll" function */
int blockRotateX90(int blockId)
{
	/* careful: these tables are not intuitive */
	static uint8_t rotateXLog[]   = {8, 4, 0, 12};
	static uint8_t rotateXFull[]  = {3, 2, 0, 1, 4, 5, 6, 7};
	static uint8_t rotateXStair[] = {0, 1, 6, 2, 4, 5, 7, 3};
	static uint8_t rotateXLever[] = {3, 1, 2, 6, 0, 4, 4, 3};
	static uint8_t rotateXTrapD[] = {12, 4, 2, 3, 8, 0, 6, 7, 13, 5, 10, 11, 9, 1, 14, 15};

	Block b = &blockIds[blockId>>4];
	switch (b->orientHint) {
	case ORIENT_LOG:    return (blockId & ~12) | rotateXLog[(blockId & 12) >> 2];
	case ORIENT_FULL:   return (blockId & ~7)  | rotateXFull[blockId & 7];
	case ORIENT_STAIRS: return (blockId & ~7)  | rotateXStair[blockId & 7];
	case ORIENT_LEVER:  return (blockId & ~7)  | rotateXLever[blockId & 7];
	case ORIENT_DOOR:
	case ORIENT_BED:    return 0; /* can't be rolled: delete block */
	default:
		if (b->special == BLOCK_TRAPDOOR)
			return (blockId & ~15) | rotateXTrapD[blockId & 15];
	}
	return blockId;
}

/* used by "roll" function */
int blockRotateZ90(int blockId)
{
	/* careful: these tables are not intuitive */
	static uint8_t rotateZLog[]   = {4, 0, 8, 12};
	static uint8_t rotateZFull[]  = {5, 4, 2, 3, 0, 1, 6, 7};
	static uint8_t rotateZStair[] = {4, 0, 3, 4, 5, 1, 6, 7};
	static uint8_t rotateZLever[] = {1, 6, 0, 3, 4, 2, 2, 1};
	static uint8_t rotateZTrapD[] = {0, 1, 14, 6, 4, 5, 10, 2, 8, 9, 15, 7, 12, 13, 11, 3};

	Block b = &blockIds[blockId>>4];
	switch (b->orientHint) {
	case ORIENT_LOG:    return (blockId & ~12) | rotateZLog[(blockId & 12) >> 2];
	case ORIENT_FULL:   return (blockId & ~7)  | rotateZFull[blockId & 7];
	case ORIENT_STAIRS: return (blockId & ~7)  | rotateZStair[blockId & 7];
	case ORIENT_LEVER:  return (blockId & ~7)  | rotateZLever[blockId & 7];
	case ORIENT_DOOR:
	case ORIENT_BED:    return 0; /* can't be rolled: delete block */
	default:
		if (b->special == BLOCK_TRAPDOOR)
			return (blockId & ~15) | rotateZTrapD[blockId & 15];
	}
	return blockId;
}

/* set lower 4bits to reflect a flip/mirror along Y axis */
int blockMirrorY(BlockIter iter)
{
	static uint8_t mirrorYFull[] = {1, 0, 2, 3, 4, 5, 6, 7};
	static uint8_t mirrorYLever[] = {5, 1, 2, 3, 4, 0, 6, 7, 13, 9, 10, 11, 12, 8, 14, 15};
	int blockId = getBlockId(iter);
	Block b = &blockIds[blockId>>4];
	switch (b->orientHint) {
	case ORIENT_FULL:   return (blockId & ~7) | mirrorYFull[blockId & 7];
	case ORIENT_SLAB:   return blockId ^ 8;
	case ORIENT_STAIRS: return blockId ^ 4;
	case ORIENT_LEVER:  return (blockId & !15) | mirrorYLever[blockId & 15];
	default:
		if (b->special == BLOCK_TRAPDOOR)
			return blockId ^ 8;
	}
	return blockId;
}

static int mirrorDoor(BlockIter iter, int offset, int blockId)
{
	/* top part of door: orient is in bottom part */
	if ((blockId & 8) == 0)
	{
		/* would have been much simpler if hinge and orient were in the same data value :-/ */
		static uint8_t mirrorDoorData[]  = {6, 5, 4, 7, 2, 1, 0, 3,    4, 7, 6, 5, 0, 3, 2, 1};
		uint8_t top, data;
		mapIter(iter, 0,  1, 0); top  = getBlockId(iter) & 15;
		mapIter(iter, 0, -1, 0); data = mirrorDoorData[offset + ((blockId & 3) | ((top & 1) << 2))];
		DATA8 p = iter->blockIds + DATA_OFFSET + 128 + (iter->offset >> 1);
		if (iter->offset & 1) p[0] = (p[0] & 0xef) | ((data & 4) << 2);
		else                  p[0] = (p[0] & 0xfe) | ((data & 4) >> 2);
		/* all that crap would not be necessary up to this point; sigh :-/ */
		return (blockId & ~3) | (data & 3);
	}
	else return blockId;
}

int blockMirrorX(BlockIter iter)
{
	static uint8_t mirrorXFull[]  = {0, 1, 2, 3, 5, 4, 6, 7};
	static uint8_t mirrorXRail[]  = {0, 1, 3, 2, 4, 5, 7, 6, 9, 8, 10, 11, 12, 13, 14, 15};
	static uint8_t mirrorXSWNE[]  = {0, 3, 2, 1};
	static uint8_t mirrorXTorch[] = {0, 2, 1, 3, 4, 5, 6, 7};
	static uint8_t mirrorXNSWE[]  = {0, 1, 2, 3, 5, 4, 6, 7};
	static uint8_t mirrorXStair[] = {1, 0, 2, 3};
	static uint8_t mirrorXTrapD[] = {0, 1, 3, 2};
	static uint8_t mirrorXSign[]  = {0, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1};
	static uint8_t mirrorXLever[] = {0, 2, 1, 3, 4, 5, 6, 7};

	int blockId = getBlockId(iter);
	Block b = &blockIds[blockId>>4];
	switch (b->orientHint) {
	case ORIENT_FULL:   return (blockId & ~7) | mirrorXFull[blockId & 7];
	case ORIENT_TORCH:  return (blockId & ~7) | mirrorXTorch[blockId & 7];
	case ORIENT_STAIRS: return (blockId & ~3) | mirrorXStair[blockId & 3];
	case ORIENT_NSWE:   return (blockId & ~7) | mirrorXNSWE[blockId & 7];
	case ORIENT_BED:
	case ORIENT_SE:
	case ORIENT_SWNE:   return (blockId & ~3) | mirrorXSWNE[blockId & 3];
	case ORIENT_LEVER:  return (blockId & ~7) | mirrorXLever[blockId & 7];
	case ORIENT_VINES:  return blockId ^ 10;
	case ORIENT_DOOR:   return mirrorDoor(iter, 0, blockId);
	case ORIENT_RAILS:
		if (blockStateIndex[(blockId & ~15) | 7])
			/* this type of rail type can curve */
			return (blockId & ~15) | mirrorXRail[blockId & 15];
		else
			return (blockId & ~7) | mirrorXRail[blockId & 7];
	default:
		switch (b->special) {
		case BLOCK_SIGN:
			if (blockStateIndex[(blockId & ~15) | 8])
				/* standing post */
				return (blockId & ~15) | mirrorXSign[blockId & 15];
			else /* wall sign */
				return (blockId & ~7) | mirrorXNSWE[blockId & 7];
		case BLOCK_TRAPDOOR:
			return (blockId & ~3) | mirrorXTrapD[blockId & 3];
		}
	// TODO mushroom block
	}
	return blockId;
}

int blockMirrorZ(BlockIter iter)
{
	static uint8_t mirrorZFull[]  = {0, 1, 3, 2, 4, 5, 6, 7};
	static uint8_t mirrorZRail[]  = {0, 1, 2, 3, 5, 4, 9, 8, 7, 6, 10, 11, 12, 13, 14, 15};
	static uint8_t mirrorZSWNE[]  = {2, 1, 0, 3};
	static uint8_t mirrorZTorch[] = {0, 1, 2, 4, 3, 5, 6, 7};
	static uint8_t mirrorZNSWE[]  = {3, 1, 3, 2, 4, 5, 6, 7};
	static uint8_t mirrorZStair[] = {0, 1, 3, 2};
	static uint8_t mirrorZSign[]  = {8, 7, 6, 5, 4, 3, 2, 1, 0, 15, 14, 13, 12, 11, 10, 9};
	static uint8_t mirrorZLever[] = {0, 1, 2, 4, 3, 5, 6, 7};
	static uint8_t mirrorZTrapD[] = {1, 0, 2, 3};

	int blockId = getBlockId(iter);
	Block b = &blockIds[blockId>>4];
	switch (b->orientHint) {
	case ORIENT_FULL:   return (blockId & ~7) | mirrorZFull[blockId & 7];
	case ORIENT_TORCH:  return (blockId & ~7) | mirrorZTorch[blockId & 7];
	case ORIENT_STAIRS: return (blockId & ~3) | mirrorZStair[blockId & 3];
	case ORIENT_NSWE:   return (blockId & ~7) | mirrorZNSWE[blockId & 7];
	case ORIENT_BED:
	case ORIENT_SE:
	case ORIENT_SWNE:   return (blockId & ~3) | mirrorZSWNE[blockId & 3];
	case ORIENT_LEVER:  return (blockId & ~7) | mirrorZLever[blockId & 7];
	case ORIENT_VINES:  return blockId ^ 5;
	case ORIENT_DOOR:   return mirrorDoor(iter, 8, blockId);
	case ORIENT_RAILS:
		if (blockStateIndex[(blockId & ~15) | 7])
			/* this type of rail type can curve */
			return (blockId & ~15) | mirrorZRail[blockId & 15];
		else
			return (blockId & ~7) | mirrorZRail[blockId & 7];
	default:
		switch (b->special) {
		case BLOCK_SIGN:
			if (blockStateIndex[(blockId & ~15) | 8])
				/* standing post */
				return (blockId & ~15) | mirrorZSign[blockId & 15];
			else /* wall sign */
				return (blockId & ~7) | mirrorZNSWE[blockId & 7];
		case BLOCK_TRAPDOOR:
			return (blockId & ~3) | mirrorZTrapD[blockId & 3];
		}
	// TODO mushroom block
	}
	return blockId;
}
