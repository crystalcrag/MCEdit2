/*
 * tileticks.h: public function to handle delayed block updates.
 *
 * Written by T.Pierron, mar 2021.
 */

#ifndef MC_TILE_TICKS
#define MC_TILE_TICKS

#include "maps.h"

typedef void (*UpdateCb_t)(Map map, BlockIter);

void updateTick(void);
void updateParseNBT(Chunk);
void updateFinished(DATA8 tile, vec4 dest);
void updateAdd(BlockIter iter, int blockId, int nbTick);
void updateAddTickCallback(BlockIter iter, int nbTick, UpdateCb_t cb);
void updateAddRSUpdate(struct BlockIter_t iter, int side, int nbTick);
void updateRemove(ChunkData cd, int offset, Bool clearSorted);
Bool updateAlloc(int max);
void updateClearAll(void);
int  updateCount(Chunk);
Bool updateGetNBT(Chunk, NBTFile nbt, DATA16 index);

#ifdef TILE_TICK_IMPL
typedef struct TileTick_t *    TileTick;
typedef struct TileTick_t      TileTick_t;
#define BLOCK_UPDATE           0x1000000

struct TileTick_t
{
	uint16_t   prev;
	uint16_t   next;
	ChunkData  cd;
	uint16_t   offset;
	int16_t    priority;
	ItemID_t   blockId;
	unsigned   tick;
	UpdateCb_t cb;
};

struct UpdatePrivate_t
{
	TileTick list;
	DATA16   sorted;
	int      count, max;
};
#endif
#endif
