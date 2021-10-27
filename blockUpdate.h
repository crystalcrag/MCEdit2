/*
 * blockUpdate.h : public functions to deal with delayed block update
 *
 * Written by T.Pierron, march 2021.
 */

#ifndef BLOCK_UPDATE
#define BLOCK_UPDATE

#include "maps.h"

void updateTick(void);
void updateFinished(DATA8 tile, vec4 dest);
void updateAdd(BlockIter iter, int blockId, int nbTick);
void updateRemove(ChunkData cd, int offset, Bool clearSorted);
Bool updateAlloc(int max);

int  blockRotateX90(BlockIter);
int  blockRotateY90(int blockId);
int  blockRotateZ90(BlockIter);
int  blockMirrorX(BlockIter);
int  blockMirrorY(BlockIter);
int  blockMirrorZ(BlockIter);

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
	int      count, max;
};

#endif
#endif
