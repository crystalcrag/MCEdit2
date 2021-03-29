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
Bool redstonePropagate(int blockId);
int  redstoneIsPowered(struct BlockIter_t iter, int side);
int  redstoneIsPoweredFromAnySide(struct BlockIter_t iter);
void redstonePowerChange(struct BlockIter_t iter, RSWire connectTo, int count);

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
	int8_t   dx;
	int8_t   dy;
	int8_t   dz;
	uint8_t  data;
	uint8_t  signal;
	uint8_t  pow;
	uint16_t blockId;
};

#define MAXSIGNAL     15
#define MAXUPDATE     12
#define RSUPDATE      255

enum /* common redstone devices */
{
	RSDISPENSER    = 23,
	RSNOTEBLOCK    = 25,
	RSPOWRAILS     = 27,
	RSSTICKYPISTON = 29,
	RSPISTON       = 33,
	RSWIRE         = 55,
	RSTORCH_OFF    = 75,
	RSTORCH_ON     = 76,
	RSREPEATER_OFF = 93,
	RSREPEATER_ON  = 94,
	RSLAMP         = 123,
	RSHOPPER       = 154,
	RSDROPPER      = 158,
	RSOBSERVER     = 218
};

enum /* return value from redstoneIsPowered() */
{
	POW_NONE,
	POW_VERYWEAK,     /* torch below block (can't transmit to repeater or wire) */
	POW_WEAK,         /* redstone wire powered */
	POW_STRONG,       /* repeater/torch powered (can transmit signal through block) */
};

#endif
