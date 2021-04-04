/*
 * blockUpdate.h : public functions to deal with delayed block update
 *
 * Written by T.Pierron, march 2021.
 */

#ifndef BLOCK_UPDATE
#define BLOCK_UPDATE

#define TICK_PER_SECOND    20     /* needs to be a divisor of 1000 */

void updateTick(Map map);
void updateAdd(BlockIter iter, int blockId, int nbTick);

#ifdef BLOCK_UPDATE_IMPL
typedef struct TileTick_t *    TileTick;

struct TileTick_t
{
	ChunkData cd;
	uint16_t  offset;
	uint16_t  blockId;
	int       tick;
};

struct UpdatePrivate_t
{
	TileTick list;
	DATA32   usage;
	DATA16   sorted;
	int      curTick;
	int      count, max, start;
};

#endif
#endif
