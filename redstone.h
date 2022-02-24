/*
 * redstone.h : public functions to manipulate redstone signal.
 *
 * written by T.Pierron, feb 2021
 */

#ifndef REDSTONE_H
#define REDSTONE_H

#define TICK_PER_SECOND    10     /* needs to be a divisor of 1000 */

typedef struct RSWire_t *     RSWire;

int  redstoneConnectTo(struct BlockIter_t iter, RSWire connectTo);
int  redstoneSignalStrength(BlockIter iter, Bool dirty);
Bool redstonePropagate(int blockId);
int  redstoneIsPowered(struct BlockIter_t iter, int side, int minPower);
void redstonePowerChange(struct BlockIter_t iter, RSWire connectTo, int count);
#ifdef MCMAPUPDATE_H
int  redstonePushedByPiston(struct BlockIter_t iter, int blockId, RSWire list, BlockUpdate blockedBy);
#endif

#define redstoneRepeaterDelay(blockId)       (((blockId&15) >> 2)+1)

struct RSWire_t /* track where a wire can connect to */
{
	int8_t   dx;
	int8_t   dy;
	int8_t   dz;
	uint8_t  data;
	uint8_t  signal;
	uint8_t  pow;
	uint16_t blockId;
};

#define RSSAMEBLOCK   255 /* possible value for <side> param of redstoneIsPowered() */
#define MAXSIGNAL     15
#define RSMAXUPDATE   12
#define MAXPUSH       12  /* maximum number of blocks that can be pushed/retracted by piston (note: stack allocated) */
#ifndef DEBUG
#define RSMAXDISTRAIL 9   /* maximum distance a power source will power golden rails */
#else
#define RSMAXDISTRAIL 4   /* easier to debug */
#endif
#define RSUPDATE      255

enum /* possible values Block_t.rsupdate */
{
	RSUPDATE_NONE = 0,     /* this block doesn't care about redstone signal update */
	RSUPDATE_RECV = 1,     /* this block will react to rs update */
	RSUPDATE_SEND = 2,     /* this device can change rs power level */
	RSUPDATE_BOTH = 3,     /* receive and send */
};

enum /* return value from redstoneIsPowered() */
{
	POW_NONE,
	POW_WEAK,         /* torch below block (can't transmit to repeater or wire) */
	POW_NORMAL,       /* redstone wire powered */
	POW_STRONG,       /* repeater/torch powered (can transmit signal through block) */
};

#endif
