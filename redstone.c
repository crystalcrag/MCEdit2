/*
 * redstone.c : handle logic to propagate redstone signals and generates block updates
 *
 * written by T.Pierron, feb 2021.
 */

#define REDSTONE_IMPL
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "maps.h"
#include "blocks.h"
#include "redstone.h"

/* order is S, E, N, W, T, B */
static int8_t xoff[] = {0,  1, -1, -1, 1,  0};
static int8_t zoff[] = {1, -1, -1,  1, 0,  0};
static int8_t yoff[] = {0,  0,  0,  0, 1, -2};
static int8_t dx[] = {0,  1,  0, -1, 0, 0};
static int8_t dz[] = {1,  0, -1,  0, 0, 0};

static struct RedstonePrivate_t redstone;

/* check if a signal (torch or wire) can go through a repeater from <side> */
static int redstoneConnectToRepeater(int side, int blockId)
{
	/* id encodes the way repeater is facing, check if it compatible with <side> */
	return 1;
}

/* do we need to update signal because of these block */
Bool redstoneNeedUpdate(int blockId)
{
	blockId >>= 4;
	return blockId == RSWIRE || blockId == RSTORCH_ON;
}

/* check if <blockId> can block redstone wire */
static int redstoneIsBlocking(int blockId)
{
	Block b = &blockIds[blockId];

	if (b->type == SOLID)
	{
		return b->special != BLOCK_HALF;
	}
	return False;
}

static int redstoneConnectToBlock(RSWire cnx, int side, int blockId)
{
	cnx->data = blockId & 15;
	cnx->signal = 0;
	switch (cnx->blockId) {
	case RSWIRE:
		cnx->signal = cnx->data;
		return True;
	case RSTORCH_ON:
	case RSREPEATER_ON:
		cnx->signal = MAXSIGNAL+1;
	case RSREPEATER_OFF:
		return redstoneConnectToRepeater(side, blockId);
	}
	return False;
}

/* get list of connected block that might receive an update from <iter> pos */
int redstoneConnectTo(struct BlockIter_t iter, RSWire connectTo)
{
	uint16_t blockSide[4];
	RSWire   list = connectTo;
	int      i, id;

	switch (iter.blockIds[iter.offset]) {
	case RSTORCH_ON: /* which block a rs torch will update */
		for (i = 0; i < 4; i ++)
		{
			struct RSWire_t cnx = {.dx = dx[i], .dz = dz[i]};
			mapIter(&iter, xoff[i], 0, zoff[i]);
			id = getBlockId(&iter);
			cnx.blockId = id >> 4;
			cnx.data = id & 15;
			switch (cnx.blockId) {
			case RSWIRE:
				cnx.signal = cnx.data;
				*list++ = cnx;
				break;
			case RSREPEATER_OFF:
				cnx.signal = MAXSIGNAL+1;
				if (redstoneConnectToRepeater(i, id))
					*list++ = cnx;
			}
		}
		/* check on top */
		mapIter(&iter, 1, 1, 0);
		i = getBlockId(&iter);
		if (blockIsSolidSide(i, SIDE_TOP))
		{
			mapIter(&iter, 0, 1, 0);
			struct RSWire_t cnx = {0, 2, 0, i>>8, i & 15};
			switch (cnx.blockId) {
			case RSWIRE:
				cnx.signal = cnx.data;
				*list++ = cnx;
				break;
			case RSTORCH_ON:
				cnx.signal = 0;
				*list++ = cnx;
			}
		}
		break;
	case RSWIRE: /* which block a rs wire will update if its signal change */

		id = getBlockId(&iter);
		/* first: check on the side S, E, N, W */
		for (i = 0; i < 4; i ++)
		{
			struct RSWire_t cnx = {.dx = dx[i], .dz = dz[i]};
			mapIter(&iter, xoff[i], 0, zoff[i]);
			blockSide[i] = id = getBlockId(&iter);
			cnx.blockId = id >> 4;
			if (redstoneConnectToBlock(&cnx, i, id))
				*list++ = cnx;
		}
		/* next: check on bottom */
		mapIter(&iter, 1, -1, 0);
		for (i = 0; i < 4; i ++)
		{
			struct RSWire_t cnx = {.dx = dx[i], -1, .dz = dz[i]};
			mapIter(&iter, xoff[i], 0, zoff[i]);
			id = getBlockId(&iter);
			cnx.blockId = id >> 4;
			if (redstoneIsBlocking(blockSide[i]) == 0 &&
			    redstoneConnectToBlock(&cnx, i, id))
				*list++ = cnx;
		}
		/* finally check on top */
		mapIter(&iter, 1, 2, 0);
		if (! redstoneIsBlocking(getBlockId(&iter)))
		{
			for (i = 0; i < 4; i ++)
			{
				struct RSWire_t cnx = {.dx = dx[i], 1, .dz = dz[i]};
				mapIter(&iter, xoff[i], 0, zoff[i]);
				id = getBlockId(&iter);
				cnx.blockId = id >> 4;
				if (redstoneIsBlocking(blockSide[i]) == 0 &&
					redstoneConnectToBlock(&cnx, i, id))
					*list++ = cnx;
			}
		}
	}
	return list - connectTo;
}

/* get signal strength emitted by block pointed by <iter> */
int redstoneSignalStrength(struct BlockIter_t iter, Bool dirty)
{
	switch (iter.blockIds[iter.offset]) {
	case RSWIRE:
		if (dirty)
		{
			struct RSWire_t connect[8];
			int count = redstoneConnectTo(iter, connect);
			int i, max = 0;
			for (i = 0; i < count; i ++)
			{
				RSWire cnx = connect + i;
				int    sig = 0;
				switch (cnx->blockId) {
				case RSWIRE:
					sig = cnx->data - 1;
					if (sig < 0) sig = 0;
					break;
				case RSTORCH_ON:
				case RSREPEATER_ON: sig = MAXSIGNAL;
				}
				if (max < sig) max = sig;
			}
			return max;
		}
		else return getBlockId(&iter) & 15;
		break;
	case RSTORCH_ON:
	case RSREPEATER_ON: return MAXSIGNAL+1;
	default: return 0;
	}
}

/* is the block powered by any redstone signal: 0 = none, 1 = weak, 2 = strong */
int redstoneIsPowered(struct BlockIter_t iter)
{
	int id = iter.blockIds[iter.offset];
	Block b = blockIds + id;
	if (b->type == SOLID && b->special != BLOCK_HALF)
	{
		uint8_t i, j, data;
		for (i = 0; i < 6; i ++)
		{
			mapIter(&iter, xoff[i], yoff[i], zoff[i]);
			id = getBlockId(&iter);
			data = id & 15; id >>= 4;
			b = blockIds + id;
			if (b->orientHint == ORIENT_LEVER)
			{
				static uint8_t sideAttached[] = {5, 1, 3, 0, 2, 4, 4, 5};
				/* buttons or lever */
				if (data >= 8 && sideAttached[data] == i)
					return POW_STRONG;
			}
			else if (id == RSWIRE)
			{
				/* argh, need to check connectivity */
				if (data == 0 || i == SIDE_BOTTOM) continue;
				if (i == SIDE_TOP) return POW_WEAK;
				struct RSWire_t connect[8];
				int count = redstoneConnectTo(iter, connect);
				uint8_t flags = 0;
				for (j = 0; j < count; j ++)
				{
					RSWire cnx = connect + j;
					if (cnx->dx < 0) flags |= 1 << SIDE_WEST; else
					if (cnx->dx > 0) flags |= 1 << SIDE_EAST;
					if (cnx->dz < 0) flags |= 1 << SIDE_NORTH; else
					if (cnx->dz > 0) flags |= 1 << SIDE_SOUTH;
				}
				switch (popcount(flags)) {
				case 0: return POW_WEAK;
				case 1: if (i & 1 ? flags & 10 : flags & 5) return POW_WEAK; continue;
				default: continue;
				}
			}
			else if (id == RSREPEATER_ON)
			{
				static uint8_t facing[] = {0, 3, 1, 2};
				if (facing[data] == i) return POW_STRONG;
			}
			else if (id == RSTORCH_ON)
			{
				if (i == 4) return POW_VERYWEAK;
				return POW_STRONG;
			}
		}
		return POW_NONE;
	}
	else return id == RSTORCH_ON ? POW_STRONG : POW_NONE;
}

/*
 * propagate signal after some delay
 */
void redstoneDoTick(void)
{
}
