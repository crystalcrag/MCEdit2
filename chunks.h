/*
 * chunks.h: public function and datatypes to manage map chunks
 *
 * written by T.Pierron, jan 2020.
 */

#ifndef MCCHUNKS_H
#define	MCCHUNKS_H

#include "utils.h"
#include "NBT2.h"

#define BUILD_HEIGHT                   256
#define CHUNK_LIMIT                    (BUILD_HEIGHT/16)
#define CHUNK_BLOCK_POS(x,z,y)         ((x) + ((z) << 4) + ((y) << 8))
#define CHUNK_POS2OFFSET(chunk,pos)    (((int) floorf(pos[VX]) - chunk->X) + (((int) floorf(pos[VZ]) - chunk->Z) << 4) + (((int) floorf(pos[VY]) & 15) << 8))
#define CHUNK_EMIT_SIZE                4

typedef struct Chunk_t *               Chunk;
typedef struct ChunkData_t             ChunkData_t;
typedef struct ChunkData_t *           ChunkData;

typedef Bool (*MeshInitializer)(ChunkData, APTR, APTR);

void      chunkInitStatic(void);
Bool      chunkLoad(Chunk, const char * path, int x, int z);
Bool      chunkSave(Chunk, const char * path);
void      chunkUpdate(Chunk update, ChunkData air, DATAS16 chunkOffsets, int layer, MeshInitializer);
int       chunkFree(Chunk, Bool clear);
ChunkData chunkCreateEmpty(Chunk, int layer);
DATA8     chunkGetTileEntity(ChunkData cd, int offset);
DATA8     chunkUpdateTileEntity(ChunkData, int offset);
DATA8     chunkDeleteTileEntity(ChunkData, int offset, Bool extract, DATA8 observed);
Bool      chunkAddTileEntity(ChunkData, int offset, DATA8 mem);
Bool      chunkUpdateNBT(ChunkData, int offset, NBTFile nbt);
void      chunkUpdateTilePosition(ChunkData, int offset, DATA8 tile);
DATA8     chunkIterTileEntity(Chunk, int XYZ[3], int * offset);
void      chunkUnobserve(ChunkData, int offset, int side);
void      chunkMarkForUpdate(Chunk, int type);
void      chunkExpandEntities(Chunk);
void      chunkDeleteTile(Chunk, DATA8 tile);

struct ChunkData_t                     /* one sub-chunk of 16x16x16 blocks */
{
	ChunkData visible;                 /* frustum culling list */
	Chunk     chunk;                   /* bidirectional link */
	uint16_t  Y;                       /* vertical pos (in blocks) */
	uint16_t  cnxGraph;                /* face graph connection (cave culling) */

	uint16_t  cdFlags;                 /* CDFLAG_* */
	uint8_t   slot;                    /* used by ChunkFake (0 ~ 31) */
	uint8_t   comingFrom;              /* cave culling (face id 0 ~ 5) */
	int       frame;                   /* is this ChunkData is the frustum (map->frame == cd->frame) */

	DATA8     blockIds;                /* 16*16*16 = XZY ordered, note: point directly to NBT struct (4096 bytes) */
	DATA16    emitters;                /* pos (12bits) + type (4bits) for particles emitters */

	/* VERTEX_ARRAY_BUFFER location */
	void *    glBank;                  /* GPUBank (filled by meshAllocGPU()) */
	int       glSlot;
	int       glSize;                  /* size in quads */
	int       glAlpha;                 /* alpha quads, need separate pass */
	int       glDiscard;               /* discardable quads if too far away */
	int       glMerge;                 // DEBUG
	float     yaw, pitch;              /* heuristic to limit amount of sorting for alpha transparency */
};

struct Chunk_t                         /* an entire column of 16x16 blocks */
{
	ListNode  next;                    /* chunk loading */
	Chunk     save;                    /* next chunk that needs saving */
	ChunkData layer[CHUNK_LIMIT];      /* sub-chunk array */
	uint8_t   outflags[CHUNK_LIMIT+1];
	uint8_t   neighbor;                /* offset for chunkNeighbor[] table */
	uint8_t   maxy;                    /* number of sub-chunks in layer[], starting at 0 */
	uint8_t   noChunks;                /* S,E,N,W bitfield: no chunks in this direction */

	uint16_t  cflags;                  /* CLFAG_* */
	uint16_t  entityList;              /* linked list of all entities in this chunk */

	uint16_t  cdIndex;                 /* iterate over ChunkData/Entities/TileEnt when saving */
	int16_t   signList;                /* linked list of all the signs in this chunk */

	int       X, Z;                    /* coord in blocks unit (not chunk, ie: map coord) */
	DATA32    heightMap;               /* XZ map of lowest Y coordinate where skylight value == 15 */
	APTR      tileEntities;            /* hashmap of tile entities (direct NBT records) *(TileEntityHash)c->tileEntities */
	NBTFile_t nbt;                     /* keep entire NBT structure, we'll need it to save chunk back to region */
};

#define chunkFrame     nbt.alloc       /* this is field is not used after NBT has been read */

extern int16_t chunkNeighbor[];        /* where neighbors of a chunk based on Chunk->neighbor+direction value */
extern ChunkData chunkAir;             /* chunk entirely made of air, skylight = 15, blocklight = 0 */

enum /* flags for Chunk.cflags */
{
	CFLAG_GOTDATA    = 0x0001,         /* data has been retrieved */
	CFLAG_STAGING    = 0x0002,         /* in staging area, waiting to be pushed to GPU */
	CFLAG_HASMESH    = 0x0004,         /* mesh generated and pushed to GPU */
	CFLAG_NEEDSAVE   = 0x0008,         /* modifications need to be saved on disk */
	CFLAG_HASENTITY  = 0x0010,         /* entity transfered in active list */
	CFLAG_ETTLIGHT   = 0x0020,         /* update entity light for this chunk */
	CFLAG_REBUILDSEC = 0x0040,         /* Sections list in NBT needs to be rebuilt */
	CFLAG_REBUILDTE  = 0x0080,         /* mark TileEntity list as needing to be rebuilt (the NBT part) */
	CFLAG_REBUILDENT = 0x0100,         /* mark Entity list for rebuilt when saved */
	CFLAG_REBUILDTT  = 0x0200,         /* TileTicks */

	CFLAG_HAS_SEC    = 0x0400,         /* flag set if corresponding NBT record is present */
	CFLAG_HAS_TE     = 0x0800,
	CFLAG_HAS_ENT    = 0x1000,         /* note: not exactly the same than CFLAG_HASENTITY (see below) */
	CFLAG_HAS_TT     = 0x2000,
};

/*
 * note: difference between HASENTITY and HAS_ENT:
 * CFLAG_HAS_ENT: has an "Entities" TAG_List_Compound in the NBT stream.
 * CFLAG_HAS_ENTITY: entites are loaded and rendered (lazy chunks must not load any though).
 */

enum /* flags for ChunkData.cdFlags */
{
	CDFLAG_CHUNKAIR     = 0x0001,      /* chunk is a full of air (sky = 15, light = 0, data = 0) */
	CDFLAG_PENDINGDEL   = 0x0002,      /* chunk is empty: can be deleted */
	CDFLAG_PENDINGMESH  = 0x0004,      /* chunk will be processed by chunkUpdate() at some point */
	CDFLAG_NOALPHASORT  = 0x0008,      /* sorting of alpha quads not necessary */
	CDFLAG_NOLIGHT      = 0x0010,      /* cd->blockIds only contains block and data table (brush) */

	CDFLAG_EDGESOUTH    = 0x0020,      /* south face of ChunkData is at edge of map => apply cave fog quad */
	CDFLAG_EDGEEAST     = 0x0040,
	CDFLAG_EDGENORTH    = 0x0080,
	CDFLAG_EDGEWEST     = 0x0100,
	CDFLAG_EDGESENW     = 0x01e0,

	CDFLAG_SOUTHHOLE    = 0x0200,      /* needed by the cave culling for the initial ChunkData */
	CDFLAG_EASTHOLE     = 0x0400,
	CDFLAG_NORTHHOLE    = 0x0800,
	CDFLAG_WESTHOLE     = 0x1000,
	CDFLAG_TOPHOLE      = 0x2000,
	CDFLAG_BOTTOMHOLE   = 0x4000,
	CDFLAG_HOLE         = 0x7e00,

	CDFLAG_DISCARDABLE  = 0x8000       /* discard "discardable" quads (set by frustum culling) */
};

/* alias */
#define CDFLAG_ISINUPDATE    0x10

enum /* NBT update tag (type parameter of chunkMarkForUpdate) */
{
	CHUNK_NBT_SECTION      = 0x01,
	CHUNK_NBT_TILEENTITIES = 0x02,
	CHUNK_NBT_ENTITIES     = 0x04,
	CHUNK_NBT_TILETICKS    = 0x08
};

/* chunk vertex data (see doc/internals.html#vtxdata) */
#define FLAG_TEX_KEEPX                 (1 << 12)
#define FLAG_DUAL_SIDE                 (1 << 13)
#define FLAG_TRIANGLE                  (1 << 14)
#define FLAG_UNDERWATER                (1 << 15)

#ifdef CHUNK_IMPL                      /* private stuff below */

#define STATIC_HASH(hash, min, max)    (min <= (DATA8) hash && (DATA8) hash < max)
#define TILE_ENTITY_ID(XYZ)            ((XYZ[1] << 8) | (XYZ[2] << 4) | XYZ[0])
#define TILE_OBSERVED_OFFSET           26
#define TILE_OBSERVED_DATA             ((DATA8)1)
#define TILE_COORD                     ((1 << TILE_OBSERVED_OFFSET)-1)

typedef struct TileEntityHash_t *      TileEntityHash;
typedef struct TileEntityEntry_t *     TileEntityEntry;

struct TileEntityEntry_t               /* one entry in the hash table of tile entities */
{
	uint32_t xzy;                      /* TILE_ENTITY_ID() */
	uint16_t prev;                     /* needed if we have to delete an entry */
	uint16_t next;                     /* needed to scan collision */
	DATA8    data;                     /* NBT stream */
};

struct TileEntityHash_t                /* will be followed by <max> TileEntityEntry_t */
{
	uint32_t count;
	uint32_t max;
};

#endif
#endif
