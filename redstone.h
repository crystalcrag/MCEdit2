/*
 * redstone.h : public functions to manipulate redstone signal.
 *
 * written by T.Pierron, feb 2021
 */

#ifndef REDSTONE_H
#define REDSTONE_H

typedef struct RSWire_t *     RSWire;

int  redstoneConnectTo(struct BlockIter_t iter, RSWire connectTo);
int  redstoneSignalStrength(struct BlockIter_t iter, Bool dirty);
Bool redstoneNeedUpdate(int blockId);
int  redstoneIsPowered(struct BlockIter_t iter);
void redstoneDoTick(void);

#define RS_TICK_PER_SECOND    20

#if 0 // what's for?
enum /* possible values for redstoneConnectTo() state parameter */
{
	RS_DELETED,    /* block is about to be removed */
	RS_ADDED,      /* block has been added */
	RS_PROPAGATE,  /* propagate changes */
};
#endif

struct RSWire_t /* track where a wire can connect to */
{
	int8_t  dx;
	int8_t  dy;
	int8_t  dz;
	uint8_t blockId;
	uint8_t data;
	uint8_t signal;
};

#define MAXSIGNAL     15

enum /* common redstone devices */
{
	RSWIRE         = 55,
	RSTORCH_OFF    = 75,
	RSTORCH_ON     = 76,
	RSREPEATER_OFF = 93,
	RSREPEATER_ON  = 04
};

enum /* return value from redstoneIsPowered() */
{
	POW_NONE,
	POW_VERYWEAK,     /* torch below block (can't transmit to repeater or wire) */
	POW_WEAK,         /* redstone wire powered */
	POW_STRONG,       /* repeater/torch powered (can transmit signal through block) */
};

#ifdef REDSTONE_IMPL
struct RSUpdate_t
{
	ChunkData chunk;
	uint16_t  offset;
	int       tick;
};

struct RedstonePrivate_t
{
	vector_t updates;
	int      curTick;
};

#endif
#endif
