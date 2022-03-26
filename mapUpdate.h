/*
 * mapUpdate.h: function to update NBT tables in a consistent manner.
 *
 * Written by T.Pierron, sep 2020
 */

#ifndef MCMAPUPDATE_H
#define MCMAPUPDATE_H

#include "maps.h"

typedef struct BlockUpdate_t *     BlockUpdate;

Bool mapUpdate(Map, vec4 pos, int blockId, DATA8 tile, int blockUpdate);
void mapUpdateBlock(Map, struct BlockIter_t, int blockId, int oldBlockId, DATA8 tile, Bool gravity);
void mapUpdateDeleteRails(Map, BlockIter, int blockId);
void mapUpdateCheckGravity(struct BlockIter_t iter, int side);
int  mapUpdateRails(Map, int blockId, BlockIter);
int  mapUpdatePowerRails(Map, int id, BlockIter);
int  mapUpdateGate(BlockIter, int id, Bool init);
int  mapUpdateDoor(BlockIter, int blockId, Bool init);
int  mapUpdatePiston(Map, BlockIter, int blockId, Bool init, DATA8 * tile, BlockUpdate blocked);
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
	UPDATE_UNDOLINK  = 128,        /* more updates will follow, must be cancelled with this one */
	UPDATE_FORCE     = 256         /* don't check if block is same as currently stored */
};

struct BlockUpdate_t
{
	ChunkData cd;
	DATA8     tile;
	uint16_t  offset;
	uint16_t  blockId;
};

struct ChunkUpdate_t
{
	ChunkData cd;
	uint32_t  nearby;
};

/* private stuff below that point */
typedef struct BlockUpdate_t       BLOCKBUF;
typedef struct ChunkUpdate_t *     ChunkUpdate;
typedef struct UpdateBuffer_t *    UpdateBuffer;

struct UpdateBuffer_t              /* group BlockUpdate_t in chunk of 128 */
{
	ListNode node;
	int      count;
	BLOCKBUF buffer[128];
};

struct MapUpdate_t
{
	ChunkUpdate modif;             /* chunk list being modified in the current chain */
	ListHead    updates;           /* async updates (UpdateBuffer) */
	int         updateCount;       /* total updates waiting to be applied */
	int         nbCheck;           /* re-check piston blocked */
	int         modifCount;        /* chunks waiting for mesh update in modif[] */
	int         modifMax;          /* max capacity of arrray modif[] */
	BlockUpdate curUpdate;         /* used by piston update order */
	BlockIter   iter;              /* mapUpdate() will use an external iterator (mostly used by selection) */
	int8_t *    coord;             /* ring buffer */
	int16_t     max;               /* params for ring buffer */
	int16_t     pos, last, usage;
	int16_t     maxUsage;          /* debug */
	uint8_t     unique;            /* values will be unique in <coord> */
};

#endif
