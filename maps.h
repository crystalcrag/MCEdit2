/*
 * maps.h: public functions/datatypes to handle maps using anvil file format.
 *
 * written by T.Pierron, jan 2020.
 */

#ifndef MCMAPS_H
#define MCMAPS_H

#include <stdint.h>
#include "chunks.h"
#include "items.h"

typedef struct Map_t *             Map;
typedef struct MapExtraData_t *    MapExtraData;
typedef struct BlockIter_t *       BlockIter;
typedef struct ChunkFake_t *       ChunkFake;

#define MAP_MIN       7            /* nb chunks active around player (need odd numer) */
#define MAP_HALF      (MAP_MIN/2)
#define MAP_AREA      (MAP_MIN+4)  /* 1 for lazy chunks, 1 for purgatory */
#define MAX_PATHLEN   256
#define MAX_PICKUP    24           /* max reach in blocks */

#define MAP_SIZE      (MAP_AREA * MAP_AREA)
#define CPOS(pos)     ((int) floorf(pos) >> 4)
#define CREM(pos)     ((int) floorf(pos) & 15)

struct Map_t
{
	float     cx, cy, cz;          /* player pos (init) */
	int       mapX, mapZ;          /* map center */
	int       maxDist;             /* max render distance in chunks */
	int       mapArea;             /* size of entire area of map (including lazy chunks) */
	int       mapSize;             /* mapArea * mapArea */
	int       frame;               /* needed by frustum culling */
	int       GPUchunk;            /* statistic used by debug screen (chunks with mesh on GPU) */
	Chunk     center;              /* chunks + mapX + mapZ * mapArea */
	ListHead  genList;             /* chunks to process */
	Chunk     genLast;
	char      path[MAX_PATHLEN];   /* path to level.dat */
	ChunkData air;                 /* missing ChunkData will use this */
	ChunkData dirty;               /* sub-chunks that needs mesh regeneration */
	ChunkData firstVisible;        /* list of visible chunks according to the MVP matrix */
	Chunk     needSave;            /* linked list of chunk that have been modified */
	Chunk     chunks;              /* 2d array of chunk containing the entire area around player */
	NBTFile_t levelDat;            /* more or less a direct dump of NBT records from level.dat */
	ChunkFake cdPool;              /* pool of partial ChunkData for frustum culling */
};

struct Frustum_t                   /* frustum culling static tables (see doc/internals.html for detail) */
{
	uint32_t  neighbors[8];        /* 8 corners having 8 neighbors: bitfield encode 27 neighbors */
	uint8_t   chunkOffsets[27];    /* bitfield of where each chunks are (S, E, N, W, T, B) */
	int8_t *  spiral;
	uint8_t   firstFree[256];
	float *   mvp;                 /* model-view-projection matrix (4x4) */
};

struct MapExtraData_t              /* extra info returned from mapPointToBlock() and mapGetBlockId() */
{
	ChunkData cd;                  /* sub-chunk where block is */
	Chunk     chunk;
	int       blockId;             /* (blockid << 4) | metadata */
	uint16_t  offset;              /* offset in ChunkData.blocksIds[] */
	uint16_t  entity;              /* entity selected instead of block if > 0 */
	uint8_t   side;                /* 0:S, 1:E, 2:N, 3:W, 4:T, 5:B */
	uint8_t   cnxFlags;            /* connected flags */
	uint8_t   topHalf;             /* slab/stairs placement */
	uint8_t   special;             /* value from Block_t */
	float     inter[3];
};

struct BlockIter_t                 /* iterate over nearby blocks */
{
	Chunk     ref;                 /* current chunk the block is */
	ChunkData cd;                  /* current sub-chunk */
	int8_t    x, z, y;             /* relative pos within sub-chunk */
	uint8_t   alloc;               /* alloc sub-chunk on the fly */
	uint16_t  yabs, offset;        /* abs Y value and offset within blocks table (blockIds, skyLight, ...) */
	DATA8     blockIds;            /* blockIds table within ChunkData or NULL if sub-chunk is missing */
};

void   mapInitStatic(void);
Map    mapInitFromPath(STRPTR path, int renderDist);
void   mapGenerateMesh(Map);
int    mapGetBlockId(Map, vec4 pos, MapExtraData canBeNULL);
Bool   mapPointToBlock(Map, vec4 camera, float * yawPitch, vec4 dir, vec4 ret, MapExtraData exxtra);
Bool   mapMoveCenter(Map, vec4 old, vec4 pos);
Bool   mapSetRenderDist(Map, int maxDist);
Bool   mapSaveAll(Map);
Bool   mapSaveLevelDat(Map);
int    mapConnectChest(Map, MapExtraData sel, MapExtraData ret);
NBTHdr mapLocateItems(MapExtraData);
void   mapDecodeItems(Item item, int count, NBTHdr nbtItems);
Bool   mapSerializeItems(MapExtraData sel, STRPTR listName, Item items, int count, NBTFile ret);
Bool   mapUpdateNBT(MapExtraData sel, NBTFile nbt);
void   mapViewFrustum(Map, mat4 mvp, vec4 camera);
int    mapFirstFree(uint32_t * usage, int count);
Chunk  mapGetChunk(Map, vec4 pos);
int    getBlockId(BlockIter iter);
void   printCoord(STRPTR hdr, BlockIter);

/*
 * block iterator over map
 */
void mapInitIter(Map, BlockIter, vec4 pos, Bool autoAlloc);
void mapInitIterOffset(BlockIter, ChunkData, int offset);
void mapIter(BlockIter iter, int dx, int dy, int dz);

/* private stuff below that point */
struct ChunkFake_t
{
	ChunkFake next;
	uint32_t  usage;
	uint8_t   total;
	uint8_t   buffer[0]; /* more bytes will follow */
};

#endif
