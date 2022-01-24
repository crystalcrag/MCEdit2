/*
 * mapUpdate.h: function to update NBT tables in a consistent manner.
 *
 * Written by T.Pierron, sep 2020
 */

#ifndef MCMAPUPDATE_H
#define MCMAPUPDATE_H

#include "maps.h"

Bool mapUpdate(Map, vec4 pos, int blockId, DATA8 tile, int blockUpdate);
void mapUpdateBlock(Map, vec4 pos, int blockId, int oldBlockId, DATA8 tile);
void mapUpdateDeleteRails(Map, BlockIter, int blockId);
int  mapUpdateRails(Map, int blockId, BlockIter);
int  mapUpdatePowerRails(Map, int id, BlockIter);
int  mapUpdateGate(BlockIter, int id, Bool init);
int  mapUpdateDoor(BlockIter, int blockId, Bool init);
int  mapUpdatePiston(Map, BlockIter, int blockId, Bool init, DATA8 * tile);
int  mapUpdateComparator(Map, BlockIter, int blockId, Bool init, DATA8 * tile);
void mapUpdatePressurePlate(BlockIter iter, float entityBBox[6]);
void mapUpdateObserver(BlockIter iter, int from);
void mapUpdateTable(BlockIter, int val, int table);
Bool mapActivate(Map, vec4 pos);
int  mapActivateBlock(BlockIter, vec4 pos, int blockId);
void mapUpdateMesh(Map);
void mapUpdateFlush(Map);
void mapUpdatePush(Map, vec4 pos, int blockId, DATA8 tile);
int  mapUpdateGetCnxGraph(ChunkData, int start, DATA8 visited);
void mapUpdateFloodFill(Map map, vec4 pos, uint8_t visited[4096], int8_t minMax[8]);
void mapUpdateInit(BlockIter);
void mapUpdateEnd(Map);

enum /* extra flags for blockUpdate param from mapUpdate() */
{
	UPDATE_NEARBY    = 3,          /* trigger update to nearby block */
	UPDATE_GRAVITY   = 2,          /* only update gravity affected block (also included by UPDATE_NEARBY) */
	UPDATE_SILENT    = 16,         /* don't generate particles */
	UPDATE_KEEPLIGHT = 32,         /* don't change block and sky light (blocks will need an update later though) */
	UPDATE_DONTLOG   = 64,         /* don't store modification in undo log */
	UPDATE_UNDOLINK  = 128         /* more updates will follow, must be cancelled with this one */
};

/* private stuff below that point */
typedef struct BlockUpdate_t *     BlockUpdate;

struct BlockUpdate_t
{
	ChunkData cd;
	DATA8     tile;
	uint16_t  offset;
	uint16_t  blockId;
};

struct MapUpdate_t
{
	ChunkData   modif;
	ChunkData * list;
	BlockUpdate updates;
	BlockIter   iter;
	DATA32      updateUsage;
	int         updateCount;
	int8_t *    coord;
	int16_t     max;
	int16_t     pos, last, usage, maxUsage;
	uint8_t     unique;
};

#endif
