/*
 * redstone.c : handle logic to propagate redstone signals (mapUpdate.c will generate
 *              block updates, blockUpdate.c will process them).
 *
 * written by T.Pierron, feb 2021.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "maps.h"
#include "blocks.h"
#include "mapUpdate.h"
#include "redstone.h"


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
static Bool redstoneIsConnected(RSWire cnx, int fromId, int side)
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
		if (fromId == RSWIRE)
		{
			cnx->signal = cnx->blockId == RSTORCH_ON ? MAXSIGNAL+1 : 0;
			return True;
		}
		break;
	case RSCOMPARATOR: /* wire can connect to all sides */
		return True;
	case RSREPEATER_ON:
	case RSREPEATER_OFF:
		if (blockSides.repeater[data&3] == side)
		{
			cnx->signal = cnx->blockId == RSREPEATER_ON ? MAXSIGNAL+1 : 0;
			cnx->pow = POW_WEAK; /* only look for this block */
			return True;
		}
		break;
	case RSOBSERVER:
		return blockSides.piston[data&7] == opp[side];
	default:
		b = &blockIds[cnx->blockId];
		if (fromId != RSWIRE) break;
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
		else if (b->rsupdate & RSUPDATE_RECV)
		{
			cnx->signal = RSUPDATE;
			return True;
		}
	}
	return False;
}

/* check if blockId is attached to (SOLID) block from side */
Bool redstoneIsAttachedTo(int blockId, int side)
{
	Block b;
	switch (blockId >> 4) {
	case RSWIRE:
	case RSCOMPARATOR: return True;
	case RSTORCH_OFF:
	case RSTORCH_ON:
		if (side == SIDE_BOTTOM) return False;
		return blockSides.torch[blockId&7] == side;
	case RSREPEATER_ON:
	case RSREPEATER_OFF:
		return blockSides.repeater[blockId&7] == side;
	case RSOBSERVER:
		return blockSides.piston[blockId&7] == side;
	default:
		b = &blockIds[blockId >> 4];
		if (b->orientHint == ORIENT_LEVER)
			return blockSides.lever[blockId&8] == side;
		else if (b->special == BLOCK_PLATE)
			return side == SIDE_TOP;
		else if (b->rsupdate & RSUPDATE_RECV)
			return True;
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
			if (redstoneIsConnected(&cnx, RSTORCH_OFF, i))
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
				/* check S, E, N, W, T (not B) */
				for (i = 0; i < 5; i ++)
				{
					cnx.dx = relx[i];
					cnx.dy = rely[i] + 1;
					cnx.dz = relz[i];
					mapIter(&iter, xoff[i], yoff[i], zoff[i]);
					id = getBlockId(&iter);
					cnx.blockId = id >> 4;
					cnx.data = id & 15;
					if (redstoneIsConnected(&cnx, RSWIRE, i))
						*list ++ = cnx;
				}
				/* back to start */
				mapIter(&iter, 0, -1, 0);
			}

			cnx.dy = 2;
			mapIter(&iter, 0, 1, 0);
			i = getBlockId(&iter);
			cnx.blockId = i >> 4;
			cnx.data = i & 15;
			if (redstoneIsConnected(&cnx, RSTORCH_OFF, SIDE_TOP))
				*list ++ = cnx;
		}
		/* check on bottom */
		mapIter(&iter, 0, -2, 0);
		i = getBlockId(&iter);
		list->dx = 0;
		list->dy = -1;
		list->dz = 0;
		list->pow = POW_NORMAL;
		list->blockId = i >> 4;
		list->data = list->signal = i & 15;
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
			if (redstoneIsConnected(&cnx, RSWIRE, i))
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
			mapIter(&iter, 1, 2, 0);
		}
		/* finally check on top */
		else mapIter(&iter, 0, 2, 0);
		if (! redstoneIsBlocking(getBlockId(&iter)))
		{
			for (i = 0; i < 4; i ++)
			{
				struct RSWire_t cnx = {.dx = relx[i], 1, .dz = relz[i]};
				mapIter(&iter, xoff[i], 0, zoff[i]);
				id = getBlockId(&iter);
				cnx.blockId = id >> 4;
				cnx.data = cnx.signal = id & 15;
				if (cnx.blockId == RSWIRE)
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
			if (redstoneIsConnected(&cnx, RSBLOCK, i))
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
				if ((blockIds[cnx.blockId].rsupdate & RSUPDATE_RECV) || cnx.blockId == RSWIRE)
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

/* list blocks pushed/retracted by piston (<iter> must point to piston block or piston head if extended) */
int redstonePushedByPiston(struct BlockIter_t iter, int blockId, RSWire list, BlockUpdate blockedBy)
{
	int retract = blockId & 8;
	if ((blockId >> 4) == RSPISTON && retract)
		/* extended: non-sticky piston can't retract anything (including slime block) */
		return 0;

	uint8_t dir = blockSides.piston[blockId & 7];
	int8_t  dx  = relx[dir], dy = rely[dir], dz = relz[dir];
	int8_t  x   = dx, y = dy, z = dz;
	int8_t  expand = 1;

	struct BlockIter_t orig = iter;
	struct RSWire_t check[MAXPUSH];
	uint8_t maxCheck = 0;
	uint8_t flags    = 0;
	uint8_t inCheck  = 0;
	uint8_t count    = 0;
	uint8_t maxPush  = 0;

	/* only check blocks from these directions when checking for slime blocks */
	switch (blockId & 7) {
	case 0: case 1: flags = 31; break;
	case 2: case 3: flags = 63 - 5; break;
	default:        flags = 63 - 10; break;
	}

	if (blockId & 8)
		/* extended: skip piston head */
		x += dx, y += dy, z += dz;
	else /* piston is about to expand */
		expand = -1, list += MAXPUSH*2-1;

	mapIter(&iter, x, y, z);

	for (;;)
	{
		while (maxPush <= MAXPUSH)
		{
			Block b = blockIds + iter.blockIds[iter.offset];

			if (b->id == 0)
				break;
			switch (b->pushable) {
			case NOPUSH:
				if (blockedBy && b->id == RSPISTONHEAD)
				{
					/* we want the piston body though */
					x = iter.blockIds[DATA_OFFSET + (iter.offset >> 1)];
					if (iter.offset & 1) x = (x >> 4) & 7; else x &= 7;
					x = opp[blockSides.piston[x]];
					mapIter(&iter, relx[x], rely[x], relz[x]);
					blockedBy->tile = (DATA8) iter.cd;
					blockedBy->blockId = iter.offset;
				}
				if (retract && inCheck == 0) goto break_all;
				else return -1;
			case PUSH_ONLY:
				if (retract) goto break_all;
				// no break;
			case PUSH_AND_RETRACT:
				maxPush ++;
				if (maxPush > MAXPUSH) return -1;
				break;
			case PUSH_DESTROY:
				goto break_all;
			case PUSH_DROPITEM:
				/* drop item will be included in the list, but do not count toward push limit */
				if (count == MAXPUSH*2) goto break_all;
			}

			list->dx = x;
			list->dy = y;
			list->dz = z;
			list->blockId = b->id;
			list->data = iter.blockIds[DATA_OFFSET + (iter.offset >> 1)];
			list->pow = b->pushable == PUSH_DROPITEM;
			list->signal = dir;
			if (iter.offset & 1) list->data >>= 4;
			else list->data &= 15;
			if (b->id == SLIMEBLOCK)
			{
				/* check for connected blocks */
				struct BlockIter_t slime = iter;
				uint8_t i, dirBits;
				for (i = 0, dirBits = flags; i < 6 && dirBits; i ++, dirBits >>= 1)
				{
					mapIter(&slime, xoff[i], yoff[i], zoff[i]);
					if ((dirBits & 1) == 0) continue;
					b = blockIds + slime.blockIds[slime.offset];
					if (b->pushable == PUSH_AND_RETRACT)
					{
						if (count == MAXPUSH) return -1;
						RSWire cnx = check + maxCheck;
						cnx->dx = x + relx[i];
						cnx->dy = y + rely[i];
						cnx->dz = z + relz[i];
						cnx->blockId = b->id;
						cnx->pow = 0;
						cnx->signal = dir;
						cnx->data = slime.blockIds[DATA_OFFSET + (slime.offset >> 1)];
						if (slime.offset & 1) cnx->data >>= 4;
						else cnx->data &= 15;
						list += expand;
						count ++;
						*list = *cnx;
						maxCheck ++;
					}
				}
			}
			count ++;
			list += expand;
			x += dx; y += dy; z += dz;
			if (retract) break;
			mapIter(&iter, dx, dy, dz);
		}
		break_all:
		if (maxCheck == 0) break;
		if (! inCheck)
		{
			if (retract) dx = -dx, dy = -dy, dz = -dz;
			inCheck = 1;
		}
		iter = orig;
		mapIter(&iter, x = check->dx + dx, y = check->dy + dy, z = check->dz + dz);
		maxCheck --;
		memmove(check, check + 1, maxCheck * sizeof *check);
	}

	/* push limit exceeded */
	return count <= MAXPUSH ? count : -1;
}

static int redstoneGetComparatorSignal(struct BlockIter_t iter, RSWire cnx)
{
	if (cnx)
		mapIter(&iter, cnx->dx, cnx->dy, cnx->dz);

	DATA8 tile = chunkGetTileEntity(iter.ref, (int[3]) {iter.x, iter.yabs, iter.z});

	if (tile)
	{
		NBTFile_t nbt = {.mem = tile};
		return NBT_GetInt(&nbt, NBT_FindNode(&nbt, 0, "OutputSignal"), 0);
	}
	return 0;
}

/* get signal strength emitted by block pointed by <iter> */
int redstoneSignalStrength(BlockIter iter, Bool dirty)
{
	int blockId = getBlockId(iter);
	switch (blockId >> 4) {
	case RSWIRE:
		if (dirty)
		{
			struct RSWire_t connect[RSMAXUPDATE];
			int count = redstoneConnectTo(*iter, connect);
			int i, max = 0, min = blockId & 15;
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
					if (sig < min) continue;
					if (sig < 0) sig = 0;
					break;
				case RSBLOCK:
				case RSTORCH_ON:
				case RSREPEATER_ON: return MAXSIGNAL;
				case RSCOMPARATOR: return redstoneGetComparatorSignal(*iter, cnx);
				case RSOBSERVER: return cnx->data & 8 ? MAXSIGNAL : 0;
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
					uint8_t power = redstoneIsPowered(*iter, i, POW_STRONG);
					if (power > 0)
					{
						power = power > 15 ? power >> 4 : MAXSIGNAL;
						if (power > max) max = power;
					}
				}
			}
			return max;
		}
		else return blockId & 15;
		break;
	case RSBLOCK:
	case RSTORCH_ON:
	case RSREPEATER_ON: return MAXSIGNAL;
	case RSOBSERVER: return blockId & 8 ? MAXSIGNAL : 0;
	default: return 0;
	}
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

#define POW_MAXSIGNL    (MAXSIGNAL << 4)

/* is the block pointed by <iter> powered by any redstone signal from <side>: returns enum POW_* */
int redstoneIsPowered(struct BlockIter_t iter, int side, int minPower)
{
	Block b;
	int i, pow = POW_NONE, ignore = 0;
	if (side != RSSAMEBLOCK)
		mapIter(&iter, relx[side], rely[side], relz[side]), ignore = 1 << opp[side];

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
		if (side == RSSAMEBLOCK || blockSides.repeater[i&3] == side) return POW_STRONG + POW_MAXSIGNL;
		break;
	default:
		/* buttons or lever */
		if (b->orientHint == ORIENT_LEVER)
			return (i & 15) >= 8 ? POW_STRONG + POW_MAXSIGNL : POW_NONE;
		if (b->special == BLOCK_PLATE)
			return (i & 15) > 0 ? POW_STRONG + POW_MAXSIGNL : POW_NONE;
	}

	if (b->type != SOLID)
		return POW_NONE;

	for (i = 0; i < 6; i ++, ignore >>= 1)
	{
		mapIter(&iter, xoff[i], yoff[i], zoff[i]);
		if (ignore & 1) continue;
		int id = getBlockId(&iter);
		uint8_t data = id & 15;
		id >>= 4;
		b = blockIds + id;
		if (b->orientHint == ORIENT_LEVER)
		{
			/* buttons or lever */
			if (data >= 8 && opp[blockSides.lever[data&7]] == i)
				return POW_STRONG + POW_MAXSIGNL;
		}
		else if (b->special == BLOCK_PLATE)
		{
			if (data > 0 && i == SIDE_TOP)
				return POW_STRONG + POW_MAXSIGNL;
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
			if (blockSides.repeater[data&3] == i) return POW_STRONG + POW_MAXSIGNL;
			break;
		case RSCOMPARATOR:
			if (blockSides.repeater[data&3] == i) return POW_STRONG + (redstoneGetComparatorSignal(iter, NULL) << 4);
			break;
		case RSOBSERVER:
			if ((data & 8) && blockSides.piston[data&7] == opp[i]) return POW_NORMAL;
			break;
		case RSTORCH_ON:
			if (i == SIDE_TOP && pow < POW_WEAK && minPower <= POW_WEAK) pow = POW_WEAK;
			else if (i == SIDE_BOTTOM) return POW_STRONG + POW_MAXSIGNL;
		}
	}
	return pow;
}
