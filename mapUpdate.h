/*
 * mapUpdate.h: function to update NBT tables in a consistent manner.
 *
 * Written by T.Pierron, sep 2020
 */

#ifndef MCMAPUPDATE_H
#define MCMAPUPDATE_H

#include "maps.h"

void mapUpdate(Map, vec4 pos, int blockId, DATA8 tile, Bool blockUpdate);
Bool mapUpdateBlock(Map, vec4 pos, int blockId, int oldBlockId, DATA8 tile);
void mapUpdatePowerRails(Map map, BlockIter iter);
int  mapUpdateGate(BlockIter iterator, int id, Bool init);
void mapUpdateTable(BlockIter iter, int val, int table);
void mapActivate(Map, vec4 pos);
int  mapActivateBlock(BlockIter, vec4 pos, int blockId);

/* private stuff below that point */
typedef struct BlockUpdate_t *     BlockUpdate;

struct BlockUpdate_t
{
	ChunkData cd;
	uint16_t  offset;
	uint16_t  blockId;
};

struct MapUpdate_t
{
	ChunkData   modif;
	ChunkData * list;
	BlockUpdate updates;
	DATA32      updateUsage;
	int         updateCount;
	int8_t *    coord;
	int16_t     max;
	int16_t     pos, last, usage, maxUsage;
	uint8_t     unique;
};

#endif
