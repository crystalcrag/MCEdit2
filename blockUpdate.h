/*
 * blockUpdate.h : public functions to deal with delayed block update
 *
 * Written by T.Pierron, march 2021.
 */

#ifndef BLOCK_UPDATE
#define BLOCK_UPDATE

#define TICK_PER_SECOND    10     /* needs to be a divisor of 1000 */

void updateTick(Map map);
void updateAdd(BlockIter iter, int blockId, int nbTick);
void updateRemove(ChunkData cd, int offset, int clearSorted);
Bool updateAlloc(int max);

#ifdef BLOCK_UPDATE_IMPL
typedef struct TileTick_t *    TileTick;

struct TileTick_t
{
	uint16_t  prev;
	uint16_t  next;
	ChunkData cd;
	uint16_t  offset;
	uint16_t  blockId;
	int       tick;
};

struct UpdatePrivate_t
{
	TileTick list;
	DATA16   sorted;
	int      count, max, start;
};

#endif
#endif
