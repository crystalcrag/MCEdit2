/*
 * redstone.c : handle logic to propagate redstone signals and generates block updates
 *
 * written by T.Pierron, feb 2021.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "maps.h"
#include "blocks.h"
#include "redstone.h"

/* order is S, E, N, W */
static int8_t xoff[] = {0,  1, -1, -1};
static int8_t zoff[] = {1, -1, -1,  1};
static int8_t dx[] = {0,  1,  0, -1};
static int8_t dz[] = {1,  0, -1,  0};

static int getBlockId(BlockIter iter)
{
	uint8_t data = iter->blockIds[DATA_OFFSET + (iter->offset >> 1)];
	return (iter->blockIds[iter->offset] << 4) | (iter->offset & 1 ? data >> 4 : data & 15);
}

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
		for (i = 0; i < DIM(xoff); i ++)
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
		for (i = 0; i < DIM(xoff); i ++)
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
		for (i = 0; i < DIM(xoff); i ++)
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
			for (i = 0; i < DIM(xoff); i ++)
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

/* get signal strength emitted by block pointer by <iterator> */
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

