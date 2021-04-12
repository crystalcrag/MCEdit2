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
extern int8_t relx[], rely[], relz[], opp[];

extern struct BlockSides_t blockSides;

/* do we need to update signal because of these block */
Bool redstonePropagate(int blockId)
{
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

/* used to build of connected redstone devices to update */
static Bool redstoneIsConnected(RSWire cnx, int side)
{
	uint8_t data = cnx->data;
	Block b;
	side = opp[side];
	switch (cnx->blockId) {
	case RSWIRE:
		cnx->signal = cnx->data;
		return True;
	case RSBLOCK:
		cnx->signal = MAXSIGNAL+1;
		return True;
	case RSTORCH_ON:
	case RSTORCH_OFF:
		if (blockSides.torch[data&7] == side)
		{
			cnx->signal = cnx->blockId == RSTORCH_ON ? MAXSIGNAL+1 : 0;
			return True;
		}
		break;
	case RSREPEATER_ON:
	case RSREPEATER_OFF:
		if (blockSides.repeater[data&3] == opp[side])
		{
			cnx->signal = cnx->blockId == RSREPEATER_ON ? MAXSIGNAL+1 : 0;
			cnx->pow = POW_WEAK; /* only look for this block */
			return True;
		}
		break;
	default:
		b = &blockIds[cnx->blockId];
		if (b->orientHint == ORIENT_LEVER)
		{
			cnx->signal = data >= 8 ? MAXSIGNAL+1 : 0;
			return True;
		}
		else if (b->special == BLOCK_PLATE)
		{
			cnx->signal = data > 0 ? MAXSIGNAL+1 : 0;
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
	int      id = iter.blockIds[iter.offset], i;

	switch (id) {
	case RSTORCH_ON:
	case RSTORCH_OFF: /* which block a rs torch will update */
		for (i = 0; i < 4; i ++)
		{
			struct RSWire_t cnx = {.dx = relx[i], .dz = relz[i], .pow = POW_STRONG, .signal = MAXSIGNAL};
			mapIter(&iter, xoff[i], 0, zoff[i]);
			id = getBlockId(&iter);
			cnx.blockId = id >> 4;
			cnx.data = id & 15;
			if (redstoneIsConnected(&cnx, i))
				*list ++ = cnx;
		}
		/* check on top */
		mapIter(&iter, 1, 1, 0);
		i = getBlockId(&iter);
		if (blockIsSolidSide(i, SIDE_TOP))
		{
			struct RSWire_t cnx = {0, 1, 0, .blockId = i>>4, .data = i & 15, .pow = POW_STRONG};
			b = &blockIds[cnx.blockId];
			if (b->rsupdate & RSUPDATE_RECV)
				*list ++ = cnx;
			if (b->type == SOLID)
			{
				for (i = 0; i < 4; i ++)
				{
					cnx.dx = relx[i];
					cnx.dz = relz[i];
					mapIter(&iter, xoff[i], 0, zoff[i]);
					id = getBlockId(&iter);
					cnx.blockId = id >> 4;
					cnx.data = id & 15;
					if (redstoneIsConnected(&cnx, i))
						*list ++ = cnx;
				}
				mapIter(&iter, xoff[i], 0, zoff[i]); /* bakc to start */
			}

			cnx.dy = 2;
			mapIter(&iter, 0, 1, 0);
			i = getBlockId(&iter);
			cnx.blockId = i >> 4;
			cnx.data = i & 15;
			if (redstoneIsConnected(&cnx, SIDE_TOP))
				*list ++ = cnx;
		}
		/* check on bottom */
		mapIter(&iter, 0, -2, 0);
		i = getBlockId(&iter);
		list->dx = 0;
		list->dy = 1;
		list->dz = 0;
		list->signal = MAXSIGNAL;
		list->pow = POW_NORMAL;
		list->blockId = i >> 4;
		list->data = i & 15;
		if (list->blockId != RSWIRE)
			list->pow = POW_WEAK;
		list ++;
		break;

	case RSWIRE: /* which block a rs wire will update if its signal change */
		id = getBlockId(&iter);
		/* first: check on the side S, E, N, W */
		for (i = 0, flags = 0; i < 4; i ++)
		{
			struct RSWire_t cnx = {.dx = relx[i], .dz = relz[i]};
			mapIter(&iter, xoff[i], 0, zoff[i]);
			blockSide[i] = id = getBlockId(&iter);
			cnx.blockId = id >> 4;
			cnx.data = id & 15;
			if (redstoneIsConnected(&cnx, i))
			{
				*list++ = cnx;
				flags |= 1 << i;
			}
		}
		/* next: check on bottom */
		mapIter(&iter, 1, -1, 0);
		b = &blockIds[iter.blockIds[iter.offset]];
		if (b->special != BLOCK_HALF && b->special != BLOCK_STAIRS)
		{
			/* block where rswrire is sitting will need a block update */
			if (b->type == SOLID)
			{
				struct RSWire_t cnx = {.dy = -1, .signal = RSUPDATE, .blockId = b->id, .pow = POW_NORMAL};
				*list++ = cnx;
			}
			/* rswire sitting on a stair or half slab can't power wire down, only up */
			for (i = 0; i < 4; i ++)
			{
				struct RSWire_t cnx = {.dx = relx[i], .dy = -1, .dz = relz[i]};
				mapIter(&iter, xoff[i], 0, zoff[i]);
				id = getBlockId(&iter);
				cnx.blockId = id >> 4;
				cnx.data = cnx.signal = id & 15;
				if (cnx.blockId == RSWIRE && redstoneIsBlocking(blockSide[i]) == 0)
				{
					*list++ = cnx;
					flags |= 1 << i;
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
				cnx.data = cnx.signal = id & 15;
				if (cnx.blockId == RSWIRE && redstoneIsBlocking(blockSide[i]) == 0)
				{
					*list++ = cnx;
					flags |= 1 << i;
				}
			}
		}
		/* queue block update */
		switch (popcount(flags)) {
		case 1:
			flags |= flags & 5 ? 10 : 5;
			// no break;
		case 0:
			for (i = 0; i < 4; i ++, flags >>= 1)
			{
				if (flags & 1) continue;
				b = &blockIds[blockSide[i] >> 4];
				struct RSWire_t cnx = {.dx = relx[i], .dz = relz[i], .signal = RSUPDATE, .blockId = b->id, .pow = POW_NORMAL};
				if (b->type != SOLID)
					cnx.pow = POW_WEAK;
				*list++ = cnx;
			}
		}
		break;

	case RSBLOCK:
		for (i = 0; i < 6; i ++)
		{
			struct RSWire_t cnx = {.dx = relx[i], .dy = rely[i], .dz = relz[i], .pow = POW_NORMAL, .signal = MAXSIGNAL};
			mapIter(&iter, xoff[i], yoff[i], zoff[i]);
			id = getBlockId(&iter);
			cnx.blockId = id >> 4;
			cnx.data = id & 15;
			if (redstoneIsConnected(&cnx, i))
				*list ++ = cnx;
		}
		break;

	case RSREPEATER_OFF:
	case RSREPEATER_ON:
		break;

	default:
		/* pressure plates, lever, buttons */
		if (blockIds[id].rsupdate & RSUPDATE_SEND)
		{
			/* generic update: only check on 6 sides */
			for (i = 0; i < 6; i ++)
			{
				struct RSWire_t cnx = {.dx = relx[i], .dy = rely[i], .dz = relz[i], .pow = POW_NORMAL, .signal = MAXSIGNAL};
				mapIter(&iter, xoff[i], yoff[i], zoff[i]);
				id = getBlockId(&iter);
				cnx.blockId = id >> 4;
				cnx.data = id & 15;
				if (redstoneIsConnected(&cnx, i))
					*list ++ = cnx;
			}
		}
	}
	#ifdef DEBUG
	if (list - connectTo > RSMAXUPDATE)
		fprintf(stderr, "buffer overflow in redstoneConnectTo(): %d\n", list - connectTo);
	#endif
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
			Block b;
			for (i = 0; i < count; i ++)
			{
				RSWire cnx = connect + i;
				int    sig = 0;
				if (cnx->signal == RSUPDATE)
					continue;
				switch (cnx->blockId) {
				case RSWIRE:
					sig = cnx->data - 1;
					if (sig < 0) sig = 0;
					break;
				case RSBLOCK:
				case RSTORCH_ON:
				case RSREPEATER_ON: return MAXSIGNAL;
				default:
					b = &blockIds[cnx->blockId];
					if (b->orientHint == ORIENT_LEVER)
					{
						if (cnx->data >= 8)
							return MAXSIGNAL;
					}
					else if (b->special == BLOCK_PLATE)
					{
						if (cnx->data > 0)
							return MAXSIGNAL;
					}
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
					if (b->type == SOLID && redstoneIsPowered(iter, RSSAMEBLOCK, POW_STRONG))
						return MAXSIGNAL;
				}
			}
			return max;
		}
		else return getBlockId(&iter) & 15;
		break;
	case RSBLOCK:
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
		if ((power > POW_NORMAL) != ((blockId & 15) == 15))
		{
			if (power > POW_NORMAL)
				blockId |= 15;
			else
				blockId &= ~15;
		}
		break;
	case RSTORCH_ON:
		if (power >= POW_NORMAL && blockIsAttached(blockId, opp[side]))
		{
			uint8_t data = blockId & 15;
			return ID(RSTORCH_OFF, data) | TICK_DELAY(1);
		}
		break;
	case RSTORCH_OFF:
		if (power < POW_NORMAL && blockIsAttached(blockId, opp[side]))
		{
			uint8_t data = blockId & 15;
			return ID(RSTORCH_ON, data) | TICK_DELAY(1);
		}
		break;
	case RSREPEATER_OFF:
		if (power >= POW_NORMAL && blockSides.repeater[blockId&3] == side)
		{
			uint8_t data = blockId & 15;
			return ID(RSREPEATER_ON, data) | TICK_DELAY(redstoneRepeaterDelay(data));
		}
		break;
	case RSREPEATER_ON:
		if (power < POW_NORMAL && blockSides.repeater[blockId&3] == side)
		{
			uint8_t data = blockId & 15;
			return ID(RSREPEATER_OFF, data) | TICK_DELAY(redstoneRepeaterDelay(data));
		}
	}
	return blockId;
}

static int redstoneIsWirePowering(BlockIter iter, int side)
{
	if (side == SIDE_TOP) return POW_NORMAL;
	struct RSWire_t connect[RSMAXUPDATE];

	uint8_t count = redstoneConnectTo(*iter, connect);
	uint8_t flags = 0;
	uint8_t j;
	for (j = 0; j < count; j ++)
	{
		RSWire cnx = connect + j;
		if (cnx->signal == RSUPDATE) continue;
		if (cnx->dx < 0) flags |= 1 << SIDE_WEST; else
		if (cnx->dx > 0) flags |= 1 << SIDE_EAST;
		if (cnx->dz < 0) flags |= 1 << SIDE_NORTH; else
		if (cnx->dz > 0) flags |= 1 << SIDE_SOUTH;
	}
	switch (popcount(flags)) {
	case 0: return POW_NORMAL;
	case 1: if (side & 1 ? flags & 10 : flags & 5) return POW_NORMAL;
	}
	return POW_NONE;
}

/* is the block pointer by <iter> powered by any redstone signal from <side>: returns enum POW_* */
int redstoneIsPowered(struct BlockIter_t iter, int side, int minPower)
{
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
		return POW_NORMAL;
	case RSWIRE:
		return minPower < POW_STRONG ? (i & 15) > 0 : POW_NONE;
	case RSREPEATER_ON:
		if (side == RSSAMEBLOCK || blockSides.repeater[i&3] == side) return POW_STRONG;
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
			/* buttons or lever */
			if (data >= 8 && opp[blockSides.lever[data&7]] == i)
				return POW_STRONG;
		}
		else switch (id) {
		case RSBLOCK:
			if (pow < POW_NORMAL)
				pow = POW_NORMAL;
			break;
		case RSWIRE:
			if (minPower <= POW_NORMAL)
			{
				/* argh, need to check connectivity */
				if (data == 0 || i == SIDE_BOTTOM) continue;
				id = redstoneIsWirePowering(&iter, i);
				if (id > pow) pow = id;
			}
			break;
		case RSREPEATER_ON:
			if (blockSides.repeater[data] == i) return POW_STRONG;
			break;
		case RSTORCH_ON:
			if (i == SIDE_TOP && pow == 0) pow = POW_WEAK;
			else if (i == SIDE_BOTTOM) return POW_STRONG;
		}
	}
	return pow;
}
