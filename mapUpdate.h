/*
 * mapUpdate.h: function to update NBT tables in a consistent manner.
 *
 * Written by T.Pierron, sep 2020
 */

#ifndef MCMAPUPDATE_H
#define MCMAPUPDATE_H

#include "maps.h"

void mapUpdate(Map, vec4 pos, int blockId, DATA8 tile, int blockUpdate);
void mapUpdateBlock(Map, vec4 pos, int blockId, int oldBlockId, DATA8 tile);
void mapUpdateDeleteRails(Map, BlockIter, int blockId);
int  mapUpdateRails(Map, int blockId, BlockIter);
int  mapUpdatePowerRails(Map, int id, BlockIter);
int  mapUpdateGate(BlockIter, int id, Bool init);
int  mapUpdateDoor(BlockIter, int blockId, Bool init);
int  mapUpdatePiston(Map, BlockIter, int blockId, Bool init);
void mapUpdateTable(BlockIter, int val, int table);
Bool mapActivate(Map, vec4 pos);
int  mapActivateBlock(BlockIter, vec4 pos, int blockId);
void mapUpdateMesh(Map);
void mapUpdateFlush(Map);
void mapUpdatePush(Map, vec4 pos, int blockId);
int  mapUpdateGetCnxGraph(ChunkData, int start, DATA8 visited);

enum /* extra flags for blockUpdate param from mapUpdate() */
{
	UPDATE_SILENT    = 16,
	UPDATE_KEEPLIGHT = 32,
};

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
