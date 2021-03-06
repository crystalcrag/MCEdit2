/*
 * maps.h: public functions/datatypes to handle maps using anvil file format.
 *
 * written by T.Pierron, jan 2020.
 */

#ifndef MCMAPS_H
#define MCMAPS_H

typedef struct Map_t *             Map;
typedef struct MapExtraData_t *    MapExtraData;
typedef struct BlockIter_t *       BlockIter;
typedef struct ChunkFake_t *       ChunkFake;
typedef struct LightingTex_t *     LightingTex;

#include <stdint.h>
#include "chunks.h"
#include "blocks.h"
#include "items.h"

#define MAX_PATHLEN   256
#define MAX_PICKUP    24           /* max reach in blocks */

#define CPOS(pos)     ((int) floorf(pos) >> 4)
#define CREM(pos)     ((int) floorf(pos) & 15)

#define PACKPNG_SIZE  128          /* minecraft uses 64, but that's a bit too small */

struct Map_t
{
	float     cx, cy, cz;          /* player pos (init) */
	int       mapX, mapZ;          /* map center (coords in Map_t.chunks) */
	int       maxDist;             /* max render distance in chunks */
	int       mapArea;             /* size of entire area of map (including lazy chunks) */
	int       frame;               /* needed by frustum culling */
	int       GPUchunk;            /* stat for debug: chunks with mesh on GPU */
	int       GPUMaxChunk;         /* bytes to allocate for a single VBO */
	int       fakeMax;             // DEBUG
	uint16_t  chunkCulled;         /* stat for debug: chunk culled from cave culling (not from frustum) */
	uint16_t  curOffset;           /* reduce sorting for alpha transparency of current chunk */
	uint16_t  size[3];             /* brush only: size in blocks of brush (incl. 1 block margin around) */
	Chunk     center;              /* chunks + mapX + mapZ * mapArea */
	ListHead  gpuBanks;            /* VBO for chunk mesh (GPUBank) */
	ListHead  genList;             /* chunks to process (Chunk) */
	ListHead  lightingTex;         /* tex banks for lighting information of chunks (LightingTex) */
	ListHead  players;             /* list of player on this map (Player) */
	Chunk     genLast;
	Semaphore genCount;            /* for rasterization */
	Mutex     genLock;
	DATAS16   chunkOffsets;        /* array 16*9: similar to chunkNeighbor[] */
	char      path[MAX_PATHLEN];   /* path to level.dat */
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
	uint16_t  lazyCount;
	int8_t *  spiral;
	int8_t *  lazy;
};

struct MapExtraData_t              /* extra info returned from mapPointToObject() and mapGetBlockId() */
{
	ChunkData cd;                  /* sub-chunk where block is */
	Chunk     chunk;
	ItemID_t  blockId;             /* (blockid << 4) | metadata: blockId being pointed to */
	uint16_t  offset;              /* offset in ChunkData.blocksIds[] */
	uint16_t  entity;              /* entity selected instead of block if > 0 (priority over blockId) */
	uint8_t   side;                /* 0:S, 1:E, 2:N, 3:W, 4:T, 5:B */
	uint8_t   cnxFlags;            /* connected flags */
	uint8_t   topHalf;             /* slab/stairs placement */
	uint8_t   special;             /* value from Block_t */
	float     inter[3];
};

enum                               /* extra values for MapExtraData_t.side */
{
	SIDE_ENTITY = 10,              /* <entity> field is an entityId */
	SIDE_WAYPOINT,                 /* <entity> is a waypoint id */
};

struct BlockIter_t                 /* iterate over nearby blocks */
{
	Chunk     ref;                 /* current chunk the block is */
	ChunkData cd;                  /* current sub-chunk */
	int8_t    x, z, y;             /* relative pos within sub-chunk */
	uint8_t   alloc;               /* alloc sub-chunk on the fly */
	uint16_t  yabs, offset;        /* abs Y value and offset within blocks table (blockIds, skyLight, ...) */
	DATA8     blockIds;            /* blockIds table within ChunkData or NULL if sub-chunk is missing */
	DATAS16   nbor;                /* offsets to get to neighbor chunk (c.f. chunkNeighbor[]) */
};

struct LightingTex_t               /* 3D texture for lighting information for fragment shader */
{
	ListNode  node;
	int       glTexId;             /* glGenTextures value (3D texture of 18x18x18 RG8 values) */
	uint32_t  slots[512/32];       /* 512 slots per texture */
	int       usage;               /* number of slots used [0-512] */
};

Map     mapInitFromPath(STRPTR path, int renderDist);
void    mapFreeAll(Map);
void    mapGenerateMesh(Map);
int     mapGetBlockId(Map, vec4 pos, MapExtraData canBeNULL);
Bool    mapPointToObject(Map, vec4 camera, vec4 dir, vec4 ret, MapExtraData extra);
Bool    mapMoveCenter(Map, vec4 old, vec4 pos);
Bool    mapSetRenderDist(Map, int maxDist);
Bool    mapSaveAll(Map);
Bool    mapSaveLevelDat(Map);
int     mapGetConnect(ChunkData cd, int offset, BlockState b);
int     mapConnectChest(Map, MapExtraData sel, MapExtraData ret);
Bool    mapUpdateNBT(MapExtraData sel, NBTFile nbt);
void    mapViewFrustum(Map, vec4 camera);
int     mapIsPositionInLiquid(Map, vec4 pos);
int     mapFirstFree(uint32_t * usage, int count);
Chunk   mapGetChunk(Map, vec4 pos);
VTXBBox mapGetBBox(BlockIter iterator, int * count, int * cnxFlags);
int     getBlockId(BlockIter iter);
uint8_t mapGetSkyBlockLight(BlockIter iter);
void    mapAddToSaveList(Map, Chunk chunk);
int     mapAllocLightingTex(Map);
void    mapFreeLightingSlot(Map, int lightId);
void    printCoord(BlockIter);
int     intersectRayPlane(vec4 P0, vec4 u, vec4 V0, vec norm, vec4 I);


/*
 * block iterator over map
 */
void mapInitIter(Map, BlockIter, vec4 pos, Bool autoAlloc);
void mapInitIterOffset(BlockIter, ChunkData, int offset);
void mapIter(BlockIter iter, int dx, int dy, int dz);

/* enumerate sequentially blocks S, E, N, W, T, B using mapIter() */
extern int8_t relx[], rely[], relz[];
extern int8_t xoff[], yoff[], zoff[], opp[];

/* private stuff below that point */
struct ChunkFake_t
{
	ChunkFake next;
	uint32_t  usage;
//	uint8_t   total;
	uint8_t   buffer[0]; /* more bytes will follow */
};

#endif
