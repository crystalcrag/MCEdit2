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

/* from mapUpdate.c */
extern int8_t xoff[], yoff[], zoff[];
static int8_t relx[], rely[], relz[];

/* check if a signal (torch or wire) can go through a repeater from <side> */
static int redstoneConnectToRepeater(int side, int blockId)
{
	/* id encodes the way repeater is facing, check if it compatible with <side> */
	static uint8_t sides[] = {5, 10, 5, 10};
	return sides[blockId & 3] & (1 << side);
}

/* do we need to update signal because of these block */
Bool redstonePropagate(int blockId)
{
	blockId >>= 4;
	return blockId == RSWIRE || blockId == RSTORCH_ON || blockId == RSREPEATER_ON;
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
		cnx->signal = MAXSIGNAL+1;
		return True;
	case RSREPEATER_ON:
	case RSREPEATER_OFF:
		if (redstoneConnectToRepeater(side, blockId))
		{
			cnx->signal = RSUPDATE;
			cnx->pow = POW_VERYWEAK; /* only look for this block */
			return True;
		}
	}
	return False;
}

/* get list of blocks that might receive an update from <iter> pos */
int redstoneConnectTo(struct BlockIter_t iter, RSWire connectTo)
{
	uint16_t blockSide[4];
	uint8_t  flags;
	RSWire   list = connectTo;
	Block    b;
	int      i, id;

	/* this is where the magic of redstone propagation happens */
	switch (iter.blockIds[iter.offset]) {
	case RSTORCH_OFF:
	case RSTORCH_ON: /* which block a rs torch will update */
		for (i = 0; i < 4; i ++)
		{
			struct RSWire_t cnx = {.dx = relx[i], .dz = relz[i], .pow = POW_STRONG, .signal = MAXSIGNAL};
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
				if (redstoneConnectToRepeater(i, cnx.data))
					*list++ = cnx;
			}
		}
		/* check on top */
		mapIter(&iter, 1, 1, 0);
		i = getBlockId(&iter);
		if (blockIsSolidSide(i, SIDE_TOP))
		{
			mapIter(&iter, 0, 1, 0);
			struct RSWire_t cnx = {0, 2, 0, .blockId = i>>4, .data = i & 15, .pow = POW_STRONG};
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
		/* check on bottom */
		mapIter(&iter, 0, -2, 0);
		i = getBlockId(&iter);
		list->dx = 0;
		list->dy = 1;
		list->dz = 0;
		list->signal = MAXSIGNAL;
		list->pow = POW_WEAK;
		list->blockId = i >> 4;
		list->data = i & 15;
		if (list->blockId != RSWIRE)
			list->pow = POW_VERYWEAK;
		list ++;
		break;
	case RSWIRE: /* which block a rs wire will update if its signal change */

		id = getBlockId(&iter);
		/* first: check on the side S, E, N, W */
		for (i = 0; i < 4; i ++)
		{
			struct RSWire_t cnx = {.dx = relx[i], .dz = relz[i]};
			mapIter(&iter, xoff[i], 0, zoff[i]);
			blockSide[i] = id = getBlockId(&iter);
			cnx.blockId = id >> 4;
			flags |= 1 << i;
			if (redstoneConnectToBlock(&cnx, i, id))
				*list++ = cnx;
		}
		/* next: check on bottom */
		mapIter(&iter, 1, -1, 0);
		b = &blockIds[iter.blockIds[iter.offset]];
		if (b->special != BLOCK_HALF && b->special != BLOCK_STAIRS)
		{
			/* block where rswrire is sitting will need a block update */
			if (b->type == SOLID)
			{
				struct RSWire_t cnx = {.dy = -1, .signal = RSUPDATE, .blockId = b->id, .pow = POW_WEAK};
				*list++ = cnx;
			}
			/* rswire sitting on a stair or half slab can't power wire down, only up */
			for (i = 0; i < 4; i ++)
			{
				struct RSWire_t cnx = {.dx = relx[i], .dy = -1, .dz = relz[i]};
				mapIter(&iter, xoff[i], 0, zoff[i]);
				id = getBlockId(&iter);
				cnx.blockId = id >> 4;
				flags |= 1 << i;
				if (redstoneIsBlocking(blockSide[i]) == 0 && cnx.blockId == RSWIRE)
				{
					cnx.signal = cnx.data;
					*list++ = cnx;
				}
			}
		}
		/* finally check on top */
		mapIter(&iter, 1, 2, 0);
		if (! redstoneIsBlocking(getBlockId(&iter)))
		{
			for (i = 0; i < 4; i ++)
			{
				struct RSWire_t cnx = {.dx = relx[i], 1, .dz = relz[i]};
				mapIter(&iter, xoff[i], 0, zoff[i]);
				id = getBlockId(&iter);
				cnx.blockId = id >> 4;
				flags |= 1 << i;
				if (redstoneIsBlocking(blockSide[i]) == 0 && cnx.blockId == RSWIRE)
				{
					cnx.signal = cnx.data;
					*list++ = cnx;
				}
			}
		}
		/* queue block update */
		switch (popcount(flags)) {
		case 1:
			flags |= flags < 3 ? 12 : 3;
			// no break;
		case 0:
			for (i = 0; i < 4; i ++, flags >>= 1)
			{
				if (flags & 1) continue;
				b = &blockIds[blockSide[i]];
				struct RSWire_t cnx = {.dx = relx[i], .dz = relz[i], .signal = RSUPDATE, .blockId = b->id, .pow = POW_WEAK};
				if (b->type != SOLID)
					cnx.pow = POW_VERYWEAK;
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
			struct RSWire_t connect[RSMAXUPDATE];
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
				case RSBLOCK:
				case RSTORCH_ON:
				case RSREPEATER_ON: return MAXSIGNAL;
				}
				if (max < sig) max = sig;
			}
			if (max < MAXSIGNAL)
			{
				/* check for nearby power source */
				for (i = 0; i < 6; i ++)
				{
					mapIter(&iter, xoff[i], yoff[i], zoff[i]);
					Block b = &blockIds[iter.blockIds[iter.offset]];
					if (b->type == SOLID && redstoneIsPowered(iter, i, POW_STRONG))
						return MAXSIGNAL;
				}
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

/* check if block need adjustment according to power level and direction */
int redstonePowerAdjust(int blockId, int side, int power)
{
	Block b = &blockIds[blockId >> 4];

	switch (b->id) {
	case RSWIRE:
		if ((power > POW_WEAK) != ((blockId & 15) == 15))
		{
			if (power > POW_WEAK)
				blockId |= 15;
			else
				blockId &= ~15;
		}
		break;
	case RSTORCH_ON:
		if (power >= POW_WEAK)
		{
			uint8_t data = blockId & 15;
			return ID(RSTORCH_OFF, data) | TICK_DELAY(1);
		}
		break;
	case RSTORCH_OFF:
		if (power < POW_WEAK)
		{
			uint8_t data = blockId & 15;
			return ID(RSTORCH_ON, data) | TICK_DELAY(1);
		}
		break;
	case RSREPEATER_OFF:
		if (power >= POW_WEAK)
		{
			uint8_t data = blockId & 15;
			return ID(RSREPEATER_ON, data) | TICK_DELAY(redstoneRepeaterDelay(data));
		}
		break;
	case RSREPEATER_ON:
		if (power < POW_WEAK)
		{
			uint8_t data = blockId & 15;
			return ID(RSREPEATER_OFF, data) | TICK_DELAY(redstoneRepeaterDelay(data));
		}
	}
	return blockId;
}

static int redstoneIsWirePowering(BlockIter iter, int side)
{
	if (side == SIDE_TOP) return POW_WEAK;
	struct RSWire_t connect[RSMAXUPDATE];

	uint8_t count = redstoneConnectTo(*iter, connect);
	uint8_t flags = 0;
	uint8_t j;
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
	case 1: if (side & 1 ? flags & 10 : flags & 5) return POW_WEAK;
	}
	return POW_NONE;
}

/* is the block pointer by <iter> powered by any redstone signal from <side>: returns enum POW_* */
int redstoneIsPowered(struct BlockIter_t iter, int side, int minPower)
{
	static uint8_t facingRepeater[] = {0, 3, 2, 1};

	Block b;
	int i, pow = POW_NONE;
	if (side != RSSAMEBLOCK)
		mapIter(&iter, relx[side], rely[side], relz[side]);

	/* check if the block itself if powered first */
	i = getBlockId(&iter);
	b = &blockIds[i >> 4];

	switch (b->id) {
	case RSBLOCK:
	case RSTORCH_ON:
		return POW_WEAK;
	case RSWIRE:
		return minPower < POW_STRONG ? (i & 15) > 0 : POW_NONE;
	case RSREPEATER_ON:
		if (side == RSSAMEBLOCK || facingRepeater[i&3] == side) return POW_STRONG;
	}

	if (b->type != SOLID)
		return POW_NONE;

	for (i = 0; i < 6; i ++)
	{
		mapIter(&iter, xoff[i], yoff[i], zoff[i]);
		int id = getBlockId(&iter);
		uint8_t data = id & 15;
		id >>= 4;
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
			if (minPower <= POW_WEAK)
			{
				/* argh, need to check connectivity */
				if (data == 0 || i == SIDE_BOTTOM) return POW_NONE;
				id = redstoneIsWirePowering(&iter, i);
				if (id > pow) pow = id;
			}
		}
		else if (id == RSREPEATER_ON)
		{
			if (facingRepeater[data] == i) return POW_STRONG;
		}
		else if (id == RSTORCH_ON)
		{
			if (i == SIDE_TOP && pow == 0) pow = POW_VERYWEAK;
			else return POW_STRONG;
		}
	}
	return pow;
}
