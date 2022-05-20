/*
 * meshBanks.h : public functions to handle banks of mesh on the GPU using single or multiple threaded context.
 *
 * Written by T.Pierron, apr 2022.
 */


#ifndef MC_MESH_BANKS_H
#define MC_MESH_BANKS_H

#include "maps.h"

/*
 * avoid more than 2 threads, because the staging area might not have enough space to hold temporary
 * mesh buffer and cause a thread inter-lock, performance gain above 2 is negligible anyway.
 * you can disable multi-thread by setting this value to 0.
 */
#define NUM_THREADS                1
#define MEMITEM                    512

typedef struct MeshWriter_t        MeshWriter_t;
typedef struct MeshWriter_t *      MeshWriter;
typedef struct MeshBuffer_t *      MeshBuffer;
typedef struct Staging_t           Staging_t;
typedef struct Thread_t            Thread_t;

void meshInitThreads(Map);
void meshStopThreads(Map, int exit);
void meshFlushStaging(Map);

enum /* possible values to <exit> */
{
	THREAD_EXIT_LOOP = 1,
	THREAD_EXIT      = 2
};

/* init meshing phase: multi-thread (MT) or single thread (ST) */
Bool meshInitST(ChunkData, APTR opaque, APTR alpha);
Bool meshInitMT(ChunkData, APTR opaque, APTR alpha);
/* note: use of void pointer to prevent making this file dependant on lots of other stuff, argument must be MeshWriter */

/* called from render loop for MT or after chunkUpdate() for ST */
void meshFinishST(Map);
void meshFinishMT(Map);

/* mesh generation (from render loop) */
void meshGenerateST(Map);
void meshGenerateMT(Map);

#if NUM_THREADS > 0
#define meshGenerate                  meshGenerateMT
#define meshReady(map)                staging.chunkData > 0
#define meshAddToProcess(map, count)  SemAdd((map)->genCount, count)
#else
#define meshGenerate                  meshGenerateST
#define meshReady(map)                (map)->genList.lh_Head
#define meshAddToProcess(map, count)  (void) count
#endif

/* free everything */
void meshFreeAll(Map, Bool clear);
void meshFreeGPU(ChunkData);
void meshCloseAll(Map);

/* house keeping */
void meshClearBank(Map);
void meshWillBeRendered(ChunkData);
void meshAllocCmdBuffer(Map map);

/* done in halfBlock.c, but mostly needed for mesh banks */
void meshHalfBlock(MeshWriter write, DATA8 model, int size, DATA8 xyz, BlockState b, DATA16 neighborBlockIds, DATA8 skyBlock, int genSides);

typedef struct GPUBank_t *         GPUBank;
typedef struct GPUMem_t *          GPUMem;
typedef struct HashQuadEntry_t *   HashQuadEntry;
typedef struct HashQuadMerge_t *   HashQuadMerge;


struct GPUMem_t                    /* one allocation with GPUBank */
{
	ChunkData cd;                  /* chunk at this location (if size>0) */
	int       size;                /* in bytes (<0 = free) */
	int       offset;              /* avoid scanning the whole list */
};

struct GPUBank_t                   /* one chunk of memory */
{
	ListNode  node;
	int       memAvail;            /* in bytes */
	int       memUsed;             /* in bytes */
	GPUMem    usedList;            /* array of memory range in use */
	int       maxItems;            /* max items available in usedList */
	int       nbItem;              /* number of items in usedList */
	int       freeItem;            /* number of items in the free list */
	int       vaoTerrain;          /* VERTEX_ARRAY_OBJECT */
	int       vboTerrain;          /* VERTEX_BUFFER_ARRAY */
	int       vboLocation;         /* VERTEX_BUFFER_ARRAY (divisor 1) */
	int       vboMDAI;             /* glMultiDrawArrayIndirect buffer for solid/quad voxels */
	int       vtxSize;             /* chunk to render in this bank according to frustum */
	int       vboLocSize;          /* current size allocated for vboLocation */
	MDAICmd   cmdBuffer;           /* mapped GL buffer */
	float *   locBuffer;
	int       cmdTotal;
	int       cmdAlpha;
};

struct MeshBuffer_t                /* temporary buffer used to collect data from chunkUpdate() */
{
	ListNode   node;
	ChunkData  chunk;
	int        usage;
	int        discard;
	uint32_t   buffer[0];          /* 64Kb: not declared here because gdb doesn't like big table */
};

struct HashQuadEntry_t
{
	uint16_t nextChain;
	uint16_t nextAdded;
	uint32_t crc;
	DATA32   quad;
};

struct HashQuadMerge_t
{
	int capa, usage;
	uint16_t lastAdded;
	uint16_t firstAdded;
	HashQuadEntry entries;
};

#define QUADHASH     struct HashQuadMerge_t
struct MeshWriter_t
{
	DATA32     start, end;           /* do not write past these points */
	DATA32     cur, discard;         /* running pointer */
	APTR       mesh;                 /* private datatype */
	QUADHASH * merge;                /* hash table to merge quad */
	uint16_t   coplanar[6];          /* check if quads are all coplanar for a given axis (S, E, N, W, T, B: used by alpha) */
	uint8_t    isCOP;                /* 1 if coplanar, 0 if no */
	uint8_t    alpha;                /* 1 if buffer is for alpha quads */
	void     (*flush)(MeshWriter);
};

struct Thread_t
{
	Mutex    wait;
	Map      map;
	int      state;
	QUADHASH hash;
};

#define STAGING_SLOT       256
#define MESH_MAX_QUADS     255
#define MESH_HDR           2
#define STAGING_BLOCK      (MESH_MAX_QUADS * VERTEX_DATA_SIZE/4 + MESH_HDR)
#define STAGING_AREA       (STAGING_BLOCK * STAGING_SLOT * 4)
#define MAX_MESH_CHUNK     ((64*1024/VERTEX_DATA_SIZE)*VERTEX_DATA_SIZE)

struct Staging_t
{
	Semaphore capa;
	Mutex     alloc;
	DATA32    mem;
	int       total;
	int       chunkData;
	uint32_t  usage[STAGING_SLOT/32];
	uint8_t   start[STAGING_SLOT];
};

enum /* possible values for Thread_t.state */
{
	THREAD_EXITED = -1,
	THREAD_WAIT_GENLIST,
	THREAD_WAIT_BUFFER,
	THREAD_RUNNING
};

/*
 * quad merge API
 */
void meshQuadMergeReset(HashQuadMerge hash);
void meshQuadMergeInit(HashQuadMerge hash);
void meshQuadMergeAdd(HashQuadMerge hash, DATA32 quad);
int  meshQuadMergeGet(HashQuadMerge hash, DATA32 quad);


extern struct Staging_t staging;

#endif
