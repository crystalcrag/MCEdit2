/*
 * chunks.h: public function and datatypes to manage map chunks
 *
 * written by T.Pierron, jan 2020.
 */

#ifndef MCCHUNKS_H
#define	MCCHUNKS_H

#include "utils.h"
#include "NBT2.h"

#define BUILD_HEIGHT               256
#define CHUNK_LIMIT                (BUILD_HEIGHT/16)
#define CHUNK_BLOCK_POS(x,z,y)     ((x) + ((z) << 4) + ((y) << 8))

typedef struct Chunk_t *           Chunk;
typedef struct ChunkData_t         ChunkData_t;
typedef struct ChunkData_t *       ChunkData;
typedef struct WriteBuffer_t *     WriteBuffer;

typedef void (*ChunkFlushCb_t)(WriteBuffer);

void      chunkInitStatic(void);
Bool      chunkLoad(Chunk, const char * path, int x, int z);
Bool      chunkSave(Chunk, const char * path);
void      chunkUpdate(Chunk update, ChunkData air, int layer, ChunkFlushCb_t cb);
int       chunkFree(Chunk);
ChunkData chunkCreateEmpty(Chunk, int layer);
DATA8     chunkGetTileEntity(Chunk, int * XYZ);
DATA8     chunkDeleteTileEntity(Chunk, int * XYZ);
Bool      chunkAddTileEntity(Chunk, int * XYZ, DATA8 mem);
Bool      chunkCopyTileEntity(Chunk, DATA8 nbtToCopy, vec4 pos);
Bool      chunkUpdateNBT(Chunk, int blockOffset, NBTFile nbt);
void      chunkUpdateTilePosition(Chunk, int * XYZ, DATA8 tile);
void      chunkMarkForUpdate(Chunk);

struct ChunkData_t                 /* one sub-chunk of 16x16x16 blocks */
{
	ChunkData visible;             /* frustum culling list */
	ChunkData update;              /* need updating */
	Chunk     chunk;               /* bidirectional link */
	uint16_t  Y;                   /* vertical pos in blocks */

	uint8_t   pendingDel;
	uint8_t   slot;                /* used by ChunkFake */

	DATA8     blockIds;            /* 16*16*16 = XZY ordered, note: point directly to NBT struct (4096 bytes) */
//	DATA8     addId;               /* 4bits (2048 bytes) */
	DATA16    emitters;            /* pos (10bits) + type (6bits) for particles emitters */

	/* VERTEX_ARRAY_BUFFER location */
	void *    glBank;              /* GPUBank (filled by renderStoreArray()) */
	int       glSlot;
	int       glSize;              /* size in bytes */
	int       glAlpha;             /* alpha triangles, need separate pass */
};

struct Chunk_t                     /* an entire column of 16x16 blocks */
{
	ListNode  next;                /* chunk loading */
	Chunk     save;                /* next chunk that needs saving */
	ChunkData layer[CHUNK_LIMIT];  /* sub-chunk array */
	uint8_t   outflags[CHUNK_LIMIT+1];
	uint8_t   cflags;              /* CLFAG_* */
	uint8_t   neighbor;            /* offset for chunkNeighbor[] table */
	uint8_t   maxy;                /* number of sub-chunks in layer[], starting at 0 */
	uint8_t   lightPopulated;      /* information from NBT */
	uint8_t   terrainDeco;         /* trees and ores have been generated */
	uint8_t   cdIndex;             /* iterate over ChunkData when saving */
	int       X, Z;                /* coord in blocks unit (not chunk, ie: map coord) */
	DATA8     biomeMap;            /* XZ map of biome id */
	DATA32    heightMap;           /* XZ map of lowest Y coordinate where skylight value == 15 */
	APTR      tileEntities;        /* hashmap of tile entities (direct NBT records) */
	int       secOffset;           /* offset within <nbt> where "Sections" TAG_List_Compound starts */
	int       teOffset;            /* same with "TileEntities" */
	int       signList;            /* linked list of all the signs in this chunk */
	NBTFile_t nbt;                 /* keep entire NBT structure, we'll need it to save chunk back to region */
};

#define chunkFrame     nbt.alloc   /* this is field is not used after NBT has been read */

extern int16_t chunkNeighbor[];    /* where neighbors of a chunk based on Chunk->neighbor+direction value */

enum /* flags for Chunk_t.flags */
{
	CFLAG_GOTDATA    = 0x01,       /* data has been retrieved */
	CFLAG_HASMESH    = 0x02,       /* mesh generated and pushed to GPU */
	CFLAG_NEEDSAVE   = 0x04,       /* modifications need to be saved on disk */
	CFLAG_MARKMODIF  = 0x10,       /* mark for modif at the NBT level */
};

enum /* NBT update tag */
{
	CHUNK_NBT_SECTION = 1,
	CHUNK_NBT_TILEENTITES,
	CHUNK_NBT_ENTITES,
};

#ifdef CHUNK_IMPL                  /* private stuff below */

#define STATIC_HASH(hash, min, max)  (min <= (DATA8) hash && (DATA8) hash < max)

typedef struct EntityHash_t *      EntityHash;
typedef struct EntityEntry_t *     EntityEntry;

struct EntityEntry_t
{
	uint16_t xz;
	uint16_t y;
	uint16_t prev;
	uint16_t next;
	DATA8    data;
};

struct EntityHash_t
{
	uint32_t count;
	uint32_t max;
	uint32_t save;
};

#endif
#endif
