/*
 * meshBanks.c : manage banks of chunk mesh on the GPU. This one of the few parts that rely on multi-threading.
 *               The synchronization logic used in this module is NOT trivial, check doc/internals.html for details
 *               on how it is implemented.
 *
 * Written by T.Pierron, apr 2022.
 */

#include <glad.h>
#include <malloc.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "zlib.h" /* crc32 */
#include "chunks.h"
#include "meshBanks.h"
#include "particles.h"
#include "tileticks.h"

struct Staging_t staging;                /* chunk meshing (MT context) */
static ListHead  meshBanks;              /* chunk meshing (ST context, MeshBuffer) */
static Thread_t  threads[NUM_THREADS];   /* thread pool for meshing chunks */
static QUADHASH  quadMerge;              /* single thread greedy meshing */
static int       threadStop;             /* THREAD_EXIT_* */


#define processing         nbt.page
#define QUAD_MERGE                       /* comment to disable greedy meshing (debug) */


/*
 * this function is the main multi-threaded entry point, everything done in here must be reentrant.
 */
#if NUM_THREADS > 0
static void meshGenAsync(void * arg)
{
	struct Thread_t * thread = arg;

	Map map = thread->map;

	while (threadStop != THREAD_EXIT)
	{
		/* waiting for something to do... */
		thread->state = THREAD_WAIT_GENLIST;
		SemWait(map->genCount);

		staging.chunkTotal --;

		/* a long time can have passed waiting on that semaphore... */
		if (threadStop == THREAD_EXIT_LOOP) continue;
		if (threadStop == THREAD_EXIT) break;

		thread->state = THREAD_RUNNING;
		/* that mutex lock will let the main thread know we are busy */
		MutexEnter(thread->wait);
		/* process chunks /!\ need to unlock the mutex (<wait>) before exiting this branch!! */



		/* grab one chunk to process */
		MutexEnter(map->genLock);
		Chunk list = (Chunk) ListRemHead(&map->genList);
		/* needs to be set before exiting mutex */
		if (list) list->cflags |= CFLAG_PROCESSING;
		MutexLeave(map->genLock);



		/* already processed? */
		if (! list || (list->cflags & (CFLAG_HASMESH|CFLAG_STAGING)))
			goto bail;

		static uint8_t directions[] = {12, 4, 6, 8, 0, 2, 9, 1, 3};
		Chunk checkLater[9];
		int i, X, Z, check;

		/* load 8 surrounding chunks too (mesh generation will need this) */
		for (i = check = 0, X = list->X, Z = list->Z; i < DIM(directions); i ++)
		{
			int   dir  = directions[i];
			Chunk load = list + map->chunkOffsets[list->neighbor + dir];

			if (load->cflags & CFLAG_GOTDATA) continue;
			MutexEnter(map->genLock);
			if (load->processing)
			{
				/* being processed by another thread: process another one in the meantime */
				checkLater[check++] = load;
				MutexLeave(map->genLock);
				continue;
			}
			load->processing = 1;
			MutexLeave(map->genLock);

			if (chunkLoad(load, map->path,
					X + (dir & 8 ? -16 : dir & 2 ? 16 : 0),
					Z + (dir & 4 ? -16 : dir & 1 ? 16 : 0)))
			{
				chunkExpandTileEntities(load);
				load->cflags |= CFLAG_GOTDATA;
			}
			load->processing = 0;

			if (threadStop) goto bail;
		}

		/* need to be sure all chunks have been loaded */
		for (i = 0; i < check; i ++)
		{
			Chunk load = checkLater[i];
			while (load->processing)
			{
				/* not done yet: wait a bit */
				double timeMS = FrameGetTime();
				while (FrameGetTime() - timeMS < 0.5 && load->processing && ! threadStop);
			}
			if (threadStop) goto bail;
		}

		/* transform chunk into mesh */
		for (i = 0; i < list->maxy; i ++)
		{
			ChunkData cd = list->layer[i];
			/* World1 has a chunk at -208, -1408 where a section is missing :-/ */
			if (cd == NULL) continue;
			list->cdIndex = thread - threads;
			list->save = map->chunks;
			chunkUpdate(map, list, chunkAir, i, meshInitMT);
			meshQuadMergeReset(&thread->hash);
			list->cdIndex = 0;
			if (cd->cdFlags == CDFLAG_PENDINGDEL)
			{
				/* empty ChunkData: link within chunk has already been removed in chunkUpdate() */
				free(cd);
				continue;
			}
			if (threadStop)
			{
				list->save = NULL;
				goto bail;
			}
			//ThreadPause(50);
		}
		//fprintf(stderr, "chunk %d, %d (%d) done mem: %d/256\n", list->X, list->Z, list->maxy, staging.total);
		/* mark the chunk as ready to be pushed to the GPU */
		list->cflags |= CFLAG_STAGING;
		list->save = NULL;

		bail:
		/* this is to inform the main thread that this thread has finished its work */
		MutexLeave(thread->wait);
	}
	thread->state = THREAD_EXITED;
}

void meshInitThreads(Map map)
{
	/* mesh-based chunks */
	staging.mem = malloc(STAGING_AREA);
	staging.capa = SemInit(STAGING_SLOT);
	staging.alloc = MutexCreate();

	/* already load center chunk */
	Chunk center = map->center;
	if (chunkLoad(center, map->path, center->X, center->Z))
	{
		chunkExpandTileEntities(center);
		center->cflags |= CFLAG_GOTDATA;
	}
//	NBT_Dump(&center->nbt, 0, 0, 0);

	int nb;
	/* threads to process chunks into mesh */
	for (nb = 0; nb < NUM_THREADS; nb ++)
	{
		threads[nb].wait = MutexCreate();
		threads[nb].map  = map;
		#ifdef QUAD_MERGE
		meshQuadMergeInit(&threads[nb].hash);
		#endif
		ThreadCreate(meshGenAsync, threads + nb);
	}
}

static void meshFreeStaging(struct Staging_t * mem)
{
	SemClose(mem->capa);
	MutexDestroy(mem->alloc);
	free(mem->mem);
	memset(mem, 0, sizeof *mem);
}

void meshAddToProcessMT(Map map, int count)
{
	fprintf(stderr, "adding %d chunks to genList\n", count);
	staging.chunkTotal = count;
	SemAdd(map->genCount, count);
}
#endif

/* ask threads to stop what they are doing and wait for them */
void meshStopThreads(Map map, int exit)
{
	threadStop = exit;

	#if NUM_THREADS > 0
	/* list is about to be redone/freed */
	while (SemWaitTimeout(map->genCount, 0));

	int i;
	/* need to wait, threads might hold pointer to object that are going to be freed */
	for (i = 0; i < NUM_THREADS; i ++)
	{
		switch (threads[i].state) {
		case THREAD_WAIT_GENLIST:
			/* that's where we want the thread to be */
			continue;
		case THREAD_RUNNING:
			/* meshing/reading stuff: not good, need to stop */
			break;
		case THREAD_WAIT_BUFFER:
			/* waiting for mem block, will jump to sleep right after */
			SemAdd(staging.capa, 1);
			staging.total --;
		}
		/* need to wait for thread to stop though */
		double tick = FrameGetTime();
		/* active loop for 1ms */
		while (FrameGetTime() - tick < 1)
		{
			if (threads[i].state == THREAD_WAIT_GENLIST)
			{
				goto continue_loop;
			}
		}

		/* thread still hasn't stopped, need to wait then :-/ */
		MutexEnter(threads[i].wait);
		MutexLeave(threads[i].wait);

		continue_loop: ;
	}

	if (exit == THREAD_EXIT)
	{
		/* map being closed: need to be sure threads have exited */
		SemAdd(map->genCount, NUM_THREADS);
		for (i = 0; i < NUM_THREADS; i ++)
		{
			while (threads[i].state >= 0);
			MutexDestroy(threads[i].wait);
			free(threads[i].hash.entries);
		}

		memset(threads, 0, sizeof threads);

		meshFreeStaging(&staging);
	}
	else SemAdd(staging.capa, staging.total);
	#endif

	/* clear staging area */
	memset(staging.usage, 0, sizeof staging.usage);
	staging.total = 0;
	staging.chunkData = 0;
	staging.chunkTotal = 0;

	threadStop = 0;
}


/*
 * alloc temp buffer for single-threaded meshing
 */
static MeshBuffer meshAllocST(ListHead * head)
{
	MeshBuffer mesh = malloc(sizeof *mesh + MAX_MESH_CHUNK);
	if (! mesh) return NULL;
	memset(mesh, 0, sizeof *mesh);
	ListAddTail(head, &mesh->node);
	return mesh;
}

/* partial mesh data */
static void meshFlushST(MeshWriter buffer)
{
	MeshBuffer list = buffer->mesh;

	list->usage = (DATA8) buffer->cur - (DATA8) buffer->start;

	if (list->usage < MAX_MESH_CHUNK)
	{
		/* still some room left, don't alloc a new block just yet */
		return;
	}
	else if (list->node.ln_Next)
	{
		/* these buffer are not freed and will be reused */
		NEXT(list);
	}
	else list = meshAllocST(&meshBanks);

	buffer->mesh = list;
	buffer->cur = buffer->start = list->buffer;
	buffer->end = list->buffer + (MAX_MESH_CHUNK / 4);
}

Bool meshInitST(ChunkData cd, MeshWriter writer)
{
	MeshBuffer mesh;
	/* typical sub-chunk is usually below 64Kb of mesh data */
	if (meshBanks.lh_Head == NULL)
		mesh = meshAllocST(&meshBanks);
	else
		mesh = HEAD(meshBanks);
	#ifdef QUAD_MERGE
	if (quadMerge.capa == 0)
		meshQuadMergeInit(&quadMerge);
	else
		meshQuadMergeReset(&quadMerge);
	#endif

	mesh->chunk = cd;

	for (mesh = HEAD(meshBanks);  mesh; mesh->usage = 0, NEXT(mesh));

	mesh = HEAD(meshBanks);
	mesh->chunk   = cd;
	writer->start = writer->cur = mesh->buffer;
	writer->end   = mesh->buffer + (MAX_MESH_CHUNK / 4);
	writer->mesh  = mesh;
	#ifdef QUAD_MERGE
	writer->merge = &quadMerge;
	#else
	writer->merge = NULL;
	#endif
	writer->flush = meshFlushST;

	return True;
}

/*
 * multi-threaded context mem allocation
 */
static DATA32 meshAllocMT(struct Thread_t * thread, int first, int start, int * index_ret)
{
	thread->state = THREAD_WAIT_BUFFER;
	SemWait(staging.capa);

	/* it might have passed a long time since */
	if (threadStop)
	{
		SemAdd(staging.capa, 1);
		return NULL;
	}

	MutexEnter(staging.alloc);

//	fprintf(stderr, "alloc mem for thread %d: first = %d\n", thread - threads, first);

	int index = *index_ret = mapFirstFree(staging.usage, DIM(staging.usage));
	DATA32 mem = staging.mem + (index * STAGING_BLOCK);
	staging.total ++;
	if (first)
		staging.start[staging.chunkData++] = index;

	mem[0] = start; /* chunk position in grid and ChunkData (layer) */
	mem[1] = 0;     /* next link / memory used */

	MutexLeave(staging.alloc);

	thread->state = THREAD_RUNNING;

	return mem;
}

/* called from chunkUpdate(): vertex buffer is full */
static void meshFlushMT(MeshWriter buffer)
{
	ChunkData cd = buffer->mesh;
	/* mesh generation cancelled */
	if (! cd) return;

	int size = ((DATA8) buffer->cur - (DATA8) buffer->start) / VERTEX_DATA_SIZE;

	// fprintf(stderr, "flush mem for thread %d: size = %d\n", cd->chunk->cdIndex, size);

	if (size < TEX_MESH_INT_SIZE / VERTEX_INT_SIZE)
	{
		/* still some room left, don't alloc a new block just yet */
		buffer->start[-1] = size << 16;
		return;
	}

	int index;
	DATA32 mem = meshAllocMT(&threads[cd->chunk->cdIndex], False, 0, &index);
	if (mem == NULL)
	{
		/* cancel mesh generation */
		buffer->cur = buffer->start;
		buffer->mesh = NULL;
		return;
	}

	buffer->start[-1] = (size << 16) | (index + 1);
	buffer->cur = buffer->start = mem + MESH_HDR;
	buffer->end = mem + STAGING_BLOCK;
}

/* this function is called in a MT context */
Bool meshInitMT(ChunkData cd, MeshWriter writer)
{
	Chunk  chunk = cd->chunk;
	int    start = (chunk - chunk->save) | (cd->Y << 12);
	DATA32 mem   = meshAllocMT(threads + chunk->cdIndex, True, start, &start);

	if (mem)
	{
		writer->start = writer->cur = mem + MESH_HDR;
		writer->end   = mem + STAGING_BLOCK;
		writer->mesh  = cd;
		writer->flush = meshFlushMT;
		#ifdef QUAD_MERGE
		writer->merge = &threads[chunk->cdIndex].hash;
		#else
		writer->merge = NULL;
		#endif
		cd->glSize    = mem - staging.mem;

		return True;
	}
	return False;
}




/*
 * store a compressed mesh into the GPU mem and keep track of where it is, in ChunkData
 * this is basically a custom allocator /!\ must be called from main thread only.
 */
static int meshAllocGPU(Map map, ChunkData cd, int size)
{
	GPUBank bank;

	if (size == 0)
	{
		if (cd->glBank)
		{
			meshFreeGPU(cd);
			cd->glBank = NULL;
		}
		return -1;
	}

	for (bank = HEAD(map->gpuBanks); bank && bank->memAvail <= bank->memUsed + size /* bank is full */; NEXT(bank));

	if (bank == NULL)
	{
		if (map->GPUMaxChunk < size)
			map->GPUMaxChunk = (size * 2 + 16384) & ~16383;
		bank = calloc(sizeof *bank, 1);
		bank->memAvail = map->GPUMaxChunk;
		bank->maxItems = MEMITEM;
		bank->usedList = calloc(sizeof *bank->usedList, MEMITEM);

		glGenVertexArrays(1, &bank->vaoTerrain);
		/* will also init vboLocation and vboMDAI */
		glGenBuffers(3, &bank->vboTerrain);

		/* pre-configure terrain VAO */
		glBindVertexArray(bank->vaoTerrain);
		glBindBuffer(GL_ARRAY_BUFFER, bank->vboTerrain);
		/* this will allocate memory on the GPU: mem chunks of 20Mb */
		glBufferData(GL_ARRAY_BUFFER, map->GPUMaxChunk, NULL, GL_STATIC_DRAW);
		glVertexAttribIPointer(0, 4, GL_UNSIGNED_INT, VERTEX_DATA_SIZE, 0);
		glEnableVertexAttribArray(0);
		glVertexAttribIPointer(1, (VERTEX_DATA_SIZE-16)/4, GL_UNSIGNED_INT, VERTEX_DATA_SIZE, (void *) 16);
		glEnableVertexAttribArray(1);
		/* 16 bytes of per-instance data (3 float for loc and 1 uint for flags) */
		glBindBuffer(GL_ARRAY_BUFFER, bank->vboLocation);
		glVertexAttribPointer(2, VERTEX_INSTANCE/4, GL_FLOAT, 0, 0, 0);
		glEnableVertexAttribArray(2);
		glVertexAttribDivisor(2, 1);
		glBindBuffer(GL_ARRAY_BUFFER, 0);

		ListAddTail(&map->gpuBanks, &bank->node);
	}

	/* check for free space in the bank */
	GPUMem store = bank->usedList + bank->nbItem;
	GPUMem free  = bank->usedList + bank->maxItems - 1;
	GPUMem eof   = free - bank->freeItem + 1;
	int    off   = bank->memUsed;
	while (free >= eof)
	{
		/* first place available */
		if (size <= free->size)
		{
			/* no need to keep track of such a small quantity (typical chunk mesh is around 10Kb) */
			off = free->offset;
			if (free->size == size)
			{
				/* freed slot entirely reused */
				bank->freeItem --;
				/* free list must be contiguous */
				memmove(eof + 1, eof, (DATA8) free - (DATA8) eof);
			}
			else /* still some capacity left */
			{
				free->size -= size;
				free->offset += size;
			}
			goto found;
		}
		free --;
	}
	bank->memUsed += size;
	found:
	/* no free block big enough: alloc at the end */
	if (bank->nbItem + bank->freeItem + 1 > bank->maxItems)
	{
		/* not enough items */
		store = realloc(bank->usedList, (bank->maxItems + MEMITEM) * sizeof *store);
		if (store)
		{
			/* keep free list at the end */
			memmove(store + bank->maxItems + MEMITEM - bank->freeItem, store + bank->maxItems - bank->freeItem, bank->freeItem * sizeof *store);
			memset(store + bank->maxItems - bank->freeItem, 0, MEMITEM * sizeof *store);
			bank->maxItems += MEMITEM;
			bank->usedList = store;
		}
		else { fprintf(stderr, "alloc failed: aborting\n"); return -1; }
	}
	store = bank->usedList + bank->nbItem;
	store->size = size;
	store->offset = off;

	bank->nbItem ++;
	store->cd = cd;
	if (cd->glBank && cd->glBank != bank)
		fprintf(stderr, "bank relocated: %d\n", map->frame);
	cd->glSlot = bank->nbItem - 1;
	cd->glSize = size;
	cd->glBank = bank;

	return store->offset;
}

/* mark memory occupied by the vertex array as free */
void meshFreeGPU(ChunkData cd)
{
	GPUMem  free;
	GPUBank bank  = cd->glBank;
	GPUMem  mem   = bank->usedList + cd->glSlot;
	GPUMem  eof   = bank->usedList + bank->nbItem - 1;
	int     start = mem->offset;
	int     size  = mem->size;
	int     end   = start + size;

	cd->glBank = NULL;
	cd->glAlpha = 0;
	cd->glSize = 0;
	cd->glDiscard = 0;
//	fprintf(stderr, "freeing chunk %d at %d\n", cd->chunk->color, cd->glSlot);

	if (mem < eof)
	{
		/* keep block list contiguous, but not necessarily ordered */
		mem[0] = eof[0];
		eof->cd->glSlot = cd->glSlot;
	}
	bank->nbItem --;

	/* add block <off> - <size> to free list */
	mem = free = bank->usedList + bank->maxItems - 1;
	eof = mem - bank->freeItem + 1;

	/* keep free list ordered in increasing offset (from end of array toward beginning) */
	while (mem >= eof)
	{
		if (end < mem->offset)
		{
			/* insert before mem */
			memmove(eof - 1, eof, (DATA8) (mem + 1) - (DATA8) eof);
			mem->offset = start;
			mem->size   = size;
			bank->freeItem ++;
			return;
		}
		else if (end == mem->offset)
		{
			/* can be merged at beginning of <mem> */
			mem->offset = start;
			mem->size += size;
			/* can we merge with previous item? */
			if (mem < free && mem[1].offset + mem[1].size == start)
			{
				mem[1].size += mem->size;
				memmove(eof + 1, eof, (DATA8) mem - (DATA8) free);
				bank->freeItem --;
				eof ++;
				mem ++;
			}
			check_free:
			if (mem->size + mem->offset == bank->memUsed)
			{
				/* discard last free block */
				bank->memUsed -= mem->size;
				bank->freeItem --;
			}
			return;
		}
		else if (start == mem->offset + mem->size)
		{
			/* can be merged at end of <mem> */
			mem->size += size;
			/* can we merge with next item? */
			if (mem > eof && mem[-1].offset == end)
			{
				mem->size += mem[-1].size;
				memmove(eof + 1, eof, (DATA8) (mem - 1) - (DATA8) eof);
				bank->freeItem --;
			}
			goto check_free;
		}
		else mem --;
	}

	/* cannot merge with existing free list: add it at the beginning */
	if (end < bank->memUsed)
	{
		/* we just removed an item, therefore it is safe to add one back */
		eof[-1].offset = start;
		eof[-1].size = size;
		bank->freeItem ++;
	}
	else bank->memUsed -= size;
	/* else last item being removed: simply discard everything */
}

/* about to build command list for glMultiDrawArraysIndirect() */
void meshClearBank(Map map)
{
	GPUBank bank;
	for (bank = HEAD(map->gpuBanks); bank; bank->vtxSize = 0, bank->cmdTotal = 0, NEXT(bank));
}

/* number of sub-chunk we will have to render: will define the size of the command list */
void meshWillBeRendered(ChunkData cd)
{
	GPUBank bank = cd->glBank;
	if (cd->glSize - cd->glAlpha > 0) bank->vtxSize ++;
	if (cd->glAlpha > 0) bank->vtxSize ++;
}

/* alloc command list buffer on the GPU */
void meshAllocCmdBuffer(Map map)
{
	GPUBank bank;
	for (bank = HEAD(map->gpuBanks); bank; NEXT(bank))
	{
		/* avoid reallocating this buffer: it is used quite a lot (changed every frame) */
		int count = map->GPUMaxChunk > 1024*1024 ? (bank->vtxSize + 1023) & ~1023 :
			/* else brush: no need to alloc more than what's in the brush */
			bank->vtxSize;

		if (bank->vboLocSize < count)
		{
			/* be sure we have enough mem on GPU for command buffer */
			bank->vboLocSize = count;
			glBindBuffer(GL_ARRAY_BUFFER, bank->vboLocation);
			glBufferData(GL_ARRAY_BUFFER, count * VERTEX_INSTANCE, NULL, GL_DYNAMIC_DRAW);
			glBindBuffer(GL_DRAW_INDIRECT_BUFFER, bank->vboMDAI);
			glBufferData(GL_DRAW_INDIRECT_BUFFER, count * sizeof (MDAICmd_t), NULL, GL_DYNAMIC_DRAW);
		}
	}
}

typedef struct MeshSize_t *      MeshSize;
typedef struct MeshSize_t        MeshSize_t;
struct MeshSize_t
{
	int      opaque;
	int      discard;
	int      alpha;
	uint16_t coplanar[6];        /* check if quads are all coplanar for a given axis (S, E, N, W, T, B: used by alpha) */
	uint8_t  isCOP;              /* 1 if coplanar, 0 if no */
};

#define CATQUADS                 quad[6]
#define IS3DLIGHTTEX             (quad[0] & QUAD_LIGHT_ID) == QUAD_LIGHT_ID

/* all type of quads are mixed in the buffer, and we need to ignore merged quads too */
static void meshBufferSize(APTR buffer, int bytes, MeshSize sizes)
{
	DATA32 quad, eof;
	for (quad = buffer, eof = buffer + bytes; quad < eof; quad += VERTEX_INT_SIZE)
	{
		if (quad[0] == 0) continue; /* merged */
		if (IS3DLIGHTTEX)             quad += TEX_MESH_INT_SIZE - VERTEX_INT_SIZE; else
		if (CATQUADS & FLAG_DISCARD)  sizes->discard += VERTEX_DATA_SIZE; else
		if (CATQUADS & FLAG_ALPHATEX) sizes->alpha   += VERTEX_DATA_SIZE;
		else                          sizes->opaque  += VERTEX_DATA_SIZE;
	}
}

/* copy all data related to a ChunkDate into the GPU */
static void meshCopyBuffer(Map map, DATA8 dest, APTR buffer, int bytes, MeshSize sizes)
{
	DATA32 quad, eof;
	for (quad = buffer, eof = buffer + bytes; quad < eof; quad += VERTEX_INT_SIZE)
	{
		if (quad[0] == 0) continue;
		if (IS3DLIGHTTEX)
		{
			/* 3D lighting texture */
			LightingTex tex;
			int lightId = quad[0] & 0xffff;

			for (tex = HEAD(map->lightingTex); tex && (lightId & 127) > 0; NEXT(tex), lightId --);

			if (tex)
			{
				/* this structure must have been allocated earlier, otherwise there is a bug somewhere */
				if (tex->glTexId == 0)
				{
					glGenTextures(1, &tex->glTexId);
					glActiveTexture(GL_TEXTURE8 + (quad[0] & 127));
					glBindTexture(GL_TEXTURE_3D, tex->glTexId);
					glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
					glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

					glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
					glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
					glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

					glTexParameteri(GL_TEXTURE_3D, GL_GENERATE_MIPMAP, GL_FALSE);
					glTexImage3D(GL_TEXTURE_3D, 0, GL_RG8, 18*8, 18*8, 18*8, 0, GL_RG, GL_UNSIGNED_BYTE, NULL);
					glActiveTexture(GL_TEXTURE0);
				}

				/*
				 * would have been easier if we could lay the texture on a single axis, but that would
				 * require one of the axis to be 9216px wide, most GL vendors won't support this though :-/
				 */
				lightId >>= 7;
				glActiveTexture(GL_TEXTURE8 + (quad[0] & 127));
				glBindTexture(GL_TEXTURE_3D, tex->glTexId);
				glPixelStorei(GL_PACK_ALIGNMENT, 1);
				glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
				/* 11664 bytes to transfer */
				glTexSubImage3D(GL_TEXTURE_3D, 0, (lightId&7) * 18, ((lightId>>3)&7) * 18, (lightId>>6) * 18,
					18, 18, 18, GL_RG, GL_UNSIGNED_BYTE, quad + 1);
				glActiveTexture(GL_TEXTURE0);
			}
			else fprintf(stderr, "no texture slot pre-allocated: not good\n");

			quad += TEX_MESH_INT_SIZE - VERTEX_INT_SIZE;
		}
		else if (CATQUADS & FLAG_DISCARD)
		{
			memcpy(dest + sizes->discard, quad, VERTEX_DATA_SIZE);
			sizes->discard += VERTEX_DATA_SIZE;
		}
		else if (CATQUADS & FLAG_ALPHATEX)
		{
			memcpy(dest + sizes->alpha, quad, VERTEX_DATA_SIZE);
			sizes->alpha += VERTEX_DATA_SIZE;

			/* check coplanar to speed-up alpha rendering */
			int    normal = (quad[5] >> 19) & 7, coord;
			DATA16 cop = sizes->coplanar + normal;
			switch (normal) {
			case SIDE_SOUTH:
			case SIDE_NORTH: coord = quad[1] & 0xffff; /* Z1 */ break;
			case SIDE_EAST:
			case SIDE_WEST:  coord = quad[0] & 0xffff; /* X1 */ break;
			default:         coord = quad[0] >> 16;    /* Y1 */
			}
			if (cop[0] == 0) cop[0] = coord; else
			if (cop[0] != coord) sizes->isCOP = 0;
		}
		else /* normal quad */
		{
			memcpy(dest + sizes->opaque, quad, VERTEX_DATA_SIZE);
			sizes->opaque += VERTEX_DATA_SIZE;
		}
	}
}

#undef CATQUADS

void renderResetFrustum(void);

/* transfer single ChunkData mesh to GPU (meshing init with meshInitST) */
void meshFinishST(Map map)
{
	MeshBuffer list;
	MeshSize_t sizes;
	int        total, offset;
	int        oldSize, oldAlpha;
	ChunkData  cd;
	GPUBank    bank, oldBank;

	memset(&sizes, 0, sizeof sizes);
	for (list = HEAD(meshBanks), cd = list->chunk; list; NEXT(list))
		meshBufferSize(list->buffer, list->usage, &sizes);

	oldSize = cd->glSize;
	oldAlpha = cd->glAlpha;
	oldBank = cd->glBank;
	total = sizes.opaque + sizes.alpha + sizes.discard;
	bank = NULL;

	if (oldBank)
	{
		GPUMem mem = oldBank->usedList + cd->glSlot;
		if (total > mem->size)
		{
			/* not enough space: need to "free" previous mesh before */
			meshFreeGPU(cd);
			/*
			 * this time reserve some space in case there are further modifications
			 * the vast majority of chunks will never be modified, no need to do this everytime.
			 */
			offset = meshAllocGPU(map, cd, total + MESH_ROUNDTO - total % MESH_ROUNDTO);
			cd->glSize = total;
		}
		else offset = mem->offset, cd->glSize = total; /* reuse mem segment */
	}
	else offset = meshAllocGPU(map, cd, total);

	//fprintf(stderr, "allocating %d bytes at %d (%d/%d/%d) for chunk %d, %d / %d\n", cd->glSize, offset, size, discard, alpha, cd->chunk->X, cd->chunk->Z, cd->Y);
	//fprintf(stderr, "discard %d for chunk %d, %d / %d\n", discard, cd->chunk->X, cd->chunk->Z, cd->Y);

	if (offset >= 0)
	{
		bank = cd->glBank;
		cd->glAlpha = sizes.alpha;
		cd->glDiscard = sizes.discard;
		/* and finally copy the data to the GPU */
		glBindBuffer(GL_ARRAY_BUFFER, bank->vboTerrain);
		DATA8 mem = glMapBufferRange(GL_ARRAY_BUFFER, offset, total, GL_MAP_WRITE_BIT);

		sizes.alpha   = sizes.discard + sizes.opaque;
		sizes.discard = sizes.opaque;
		sizes.opaque  = 0;
		sizes.isCOP   = 1;

		for (list = HEAD(meshBanks); list; NEXT(list))
			meshCopyBuffer(map, mem, list->buffer, list->usage, &sizes);

		glUnmapBuffer(GL_ARRAY_BUFFER);
		glBindBuffer(GL_ARRAY_BUFFER, 0);

		/* setup by meshCopyBuffer() */
		if (sizes.isCOP) cd->cdFlags |=  CDFLAG_NOALPHASORT;
		else             cd->cdFlags &= ~CDFLAG_NOALPHASORT;
	}
	/* check if this chunk is visible: vtxSize must be the total number of MDAICmd_t sent to the GPU */
	if (map->frame == cd->frame)
	{
		if ((oldSize > 0) != (cd->glSize > 0) || (oldAlpha > 0) != (cd->glAlpha > 0) || oldBank != bank)
			renderResetFrustum();
	}
}

/* free all VBO allocated for given map */
void meshFreeAll(Map map, Bool clear)
{
	GPUBank bank, next;
	for (bank = next = HEAD(map->gpuBanks); bank; bank = next)
	{
		NEXT(next);
		glDeleteVertexArrays(1, &bank->vaoTerrain);
		glDeleteBuffers(3, &bank->vboTerrain);
		free(bank->usedList);
		free(bank);
	}
	if (clear)
	{
		ChunkData cd;
		for (cd = map->firstVisible; cd; cd->glBank = NULL, cd->glSize = 0, cd->glDiscard = 0, cd->glAlpha = 0, cd = cd->visible);
		ListNew(&map->gpuBanks);
	}
}

/* map is being closed */
void meshCloseAll(Map map)
{
	meshFreeAll(map, False);
	ListNode * node;
	while ((node = ListRemHead(&meshBanks)))  free(node);

	meshStopThreads(map, THREAD_EXIT);

	int i;
	for (i = 0; i < NUM_THREADS; i ++)
		MutexDestroy(threads[i].wait);

	free(staging.mem);
	MutexDestroy(staging.alloc);
	SemClose(staging.capa);

	free(quadMerge.entries);
	memset(&quadMerge, 0, sizeof quadMerge);
}

//#define SLOW_CHUNK_LOAD

/*
 * load and convert chunk to mesh: this function only works in single thread context.
 */
void meshGenerateST(Map map)
{
	#ifndef SLOW_CHUNK_LOAD
	ULONG start = TimeMS();
	#else
	/* artifially slow down chunk loading */
	static int delay;
	delay ++;
	if (delay > 1)
	{
		/* 50 frames == 1 second */
		if (delay > 50) delay = 0;
		return;
	}
	#endif

	while (map->genList.lh_Head)
	{
		static uint8_t directions[] = {12, 4, 6, 8, 0, 2, 9, 1, 3};
		int i, j, X, Z;

		Chunk list = (Chunk) ListRemHead(&map->genList);
		memset(&list->next, 0, sizeof list->next);

		if (list->cflags & CFLAG_HASMESH)
			continue;

		/* load 8 surrounding chunks too (mesh generation will need this) */
		for (i = 0, X = list->X, Z = list->Z; i < DIM(directions); i ++)
		{
			int   dir  = directions[i];
			Chunk load = list + map->chunkOffsets[list->neighbor + dir];

			/* already loaded ? */
			if ((load->cflags & CFLAG_GOTDATA) == 0)
			{
				if (chunkLoad(load, map->path, X + (dir & 8 ? -16 : dir & 2 ? 16 : 0),
						Z + (dir & 4 ? -16 : dir & 1 ? 16 : 0)))
				{
					chunkExpandTileEntities(load);
					load->cflags |= CFLAG_GOTDATA;
				}
			}
		}
		if ((list->cflags & CFLAG_GOTDATA) == 0)
		{
			#ifndef SLOW_CHUNK_LOAD
			if (TimeMS() - start > 15)
				break;
			#endif
			/* no chunk at this location */
			continue;
		}
		//if (list == map->center)
		//	NBT_Dump(&list->nbt, 0, 0, 0);
		//fprintf(stderr, "meshing chunk %d, %d\n", list->X, list->Z);

		/* convert to mesh */
		for (i = 0, j = list->maxy; j > 0; j --, i ++)
		{
			ChunkData cd = list->layer[i];
			if (cd)
			{
				/* this is the function that will convert chunk into triangles */
				chunkUpdate(map, list, chunkAir, i, meshInitST);
				meshFinishST(map);
				particlesChunkUpdate(map, cd);
				if (cd->cdFlags == CDFLAG_PENDINGDEL)
				{
					/* link within chunk has already been removed in chunkUpdate() */
					free(cd);
				}
				else if (cd->glBank)
				{
					map->GPUchunk ++;
				}
			}
		}
		list->cflags |= CFLAG_HASMESH;
		if ((list->cflags & CFLAG_HASENTITY) == 0)
		{
			chunkExpandEntities(list);
			updateParseNBT(list);
		}

		/* we are in the main rendering loop: don't hog the CPU for too long */
		#ifndef SLOW_CHUNK_LOAD
		if (TimeMS() - start > 15)
		#endif
			break;
	}
}

#if 0
void meshDebugStaging(Map map)
{
	fprintf(stderr, "parsing staging area: %d\n", staging.chunkData);
	DATA8 index, eof;
	for (index = staging.start, eof = index + staging.chunkData; index < eof; index ++)
	{
		DATA32 src = staging.mem + index[0] * STAGING_BLOCK;
		Chunk chunk = map->chunks + (src[0] & 0xffff);
		ChunkData cd = chunk->layer[src[0] >> 16];

		fprintf(stderr, "%d: %d, %d, %d: %s (%p)\n", index[0], chunk->X, chunk->Z, src[0] >> 16,
			chunk->cflags & CFLAG_STAGING ? "ready" : "not ready", cd);
	}
}
#endif

void meshDeleteTex(LightingTex light)
{
	glDeleteTextures(1, &light->glTexId);
	light->glTexId = 0;
}


/* flush what the threads have been filling (called from main thread) */
void meshGenerateMT(Map map)
{
	MutexEnter(staging.alloc);

	DATA8 index, eof;
	int   freed, slot;

	/* check if some mesh for blocks.vsh are ready */
	for (index = staging.start, freed = 0, eof = index + staging.chunkData; index < eof; )
	{
		/* is the chunk ready ? */
		DATA32 src = staging.mem + index[0] * STAGING_BLOCK;
		Chunk chunk = map->chunks + (src[0] & 0xffff);
		ChunkData cd = chunk->layer[src[0] >> 16];

		if (cd == NULL)
		{
			/* empty mesh: just free staging mem */
			for (slot = index[0]; ; )
			{
				staging.usage[slot >> 5] ^= 1 << (slot & 31);
				freed ++;
				slot = (src[1] & 0xffff) - 1;
				if (slot < 0) break;
				src = staging.mem + slot * STAGING_BLOCK;
			}
		}
		else if (chunk->cflags & CFLAG_STAGING)
		{
			/* yes, move all ChunkData parts into GPU and free staging area */
			struct MeshSize_t sizes = {0};

			/* count bytes needed (per category) to store this chunk on GPU */
			for (;;)
			{
				meshBufferSize(src + MESH_HDR, (src[1] >> 16) * VERTEX_DATA_SIZE, &sizes);
				slot = src[1] & 0xffff;
				if (slot == 0) break;
				src = staging.mem + (slot - 1) * STAGING_BLOCK;
			}

			int total = sizes.opaque + sizes.discard + sizes.alpha;
			if (total == 0)
			{
				/* only air blocks (usually needed for block light propagation) */
				slot = index[0];
				/* discard everything */
				staging.usage[slot >> 5] ^= 1 << (slot & 31);
				freed ++;
			}
			else
			{
				cd->glSize    = total;
				cd->glAlpha   = sizes.alpha;
				cd->glDiscard = sizes.discard;

				//fprintf(stderr, "mesh ready: chunk %d, %d [%d]: %d + (D:%d) + %d bytes\n", chunk->X, chunk->Z, cd->Y,
				//	sizes.opaque, sizes.discard, sizes.alpha);
				int offset = meshAllocGPU(map, cd, total);

				GPUBank bank = cd->glBank;
				glBindBuffer(GL_ARRAY_BUFFER, bank->vboTerrain);
				DATA8 dst = glMapBufferRange(GL_ARRAY_BUFFER, offset, total, GL_MAP_WRITE_BIT);

				sizes.alpha   = sizes.discard + sizes.opaque;
				sizes.discard = sizes.opaque;
				sizes.opaque  = 0;
				sizes.isCOP   = 1;
				for (slot = index[0], src = staging.mem + index[0] * STAGING_BLOCK; ; )
				{
					staging.usage[slot >> 5] ^= 1 << (slot & 31);
					freed ++;

					/* copy mesh data to GPU */
					meshCopyBuffer(map, dst, src + MESH_HDR, (src[1] >> 16) * VERTEX_DATA_SIZE, &sizes);

					/* get next slot */
					slot = (src[1] & 0xffff) - 1;
					if (slot < 0) break;
					src = staging.mem + slot * STAGING_BLOCK;
				}
				glUnmapBuffer(GL_ARRAY_BUFFER);
				glBindBuffer(GL_ARRAY_BUFFER, 0);
				map->GPUchunk ++;

				/* setup by meshCopyBuffer() */
				if (sizes.isCOP) cd->cdFlags |=  CDFLAG_NOALPHASORT;
				else             cd->cdFlags &= ~CDFLAG_NOALPHASORT;

				if ((chunk->cflags & CFLAG_HASENTITY) == 0)
				{
					chunkExpandEntities(chunk);
					updateParseNBT(chunk);
				}
			}

			/* note: no need to modify "bank->vtxSize" like meshFinishST(), this function is only used for initial chunk loading */
			chunk->cflags = (chunk->cflags | CFLAG_HASMESH) & ~CFLAG_PROCESSING;
		}
		/* wait for next frame */
		else { index ++; continue; }
		eof --;
		memmove(index, index + 1, eof - index);
		staging.chunkData --;
	}
	staging.total -= freed;
	MutexLeave(staging.alloc);
	SemAdd(staging.capa, freed);
}

/*
 * quad merging hash table: this hash table is used to merge SOLID quad from meshing phase.
 */
#define ENTRY_EOF     0xffff

void meshQuadMergeReset(HashQuadMerge hash)
{
	hash->usage = 0;
	hash->lastAdded = hash->firstAdded = ENTRY_EOF;
	HashQuadEntry entry, eof;
	for (entry = hash->entries, eof = entry + hash->capa; entry < eof; entry ++)
	{
		entry->nextChain = entry->nextAdded = ENTRY_EOF;
		entry->crc = 0;
		entry->quad = NULL;
	}
}

void meshQuadMergeInit(HashQuadMerge hash)
{
	hash->capa = roundToUpperPrime(6400);
	hash->entries = malloc(hash->capa * sizeof *hash->entries);

	meshQuadMergeReset(hash);
}

/* XXX above a certain point it is pointless to enlarge: too many rejects, simply use single quads */
static void meshQuadEnlarge(HashQuadMerge hash)
{
	uint16_t first = hash->firstAdded;
	HashQuadEntry old = hash->entries;

	fprintf(stderr, "enlarging table from %d to ", hash->capa);

	hash->capa = roundToUpperPrime(hash->capa+1);
	hash->entries = malloc(hash->capa * sizeof *old);

	fprintf(stderr, "%d\n", hash->capa);

	meshQuadMergeReset(hash);

	/* re-add entries in the same order they were first inserted */
	while (first != ENTRY_EOF)
	{
		meshQuadMergeAdd(hash, old[first].quad);
		first = old[first].nextAdded;
	}
	free(old);
}

void meshQuadMergeAdd(HashQuadMerge hash, DATA32 quad)
{
	if (hash->usage == hash->capa)
		meshQuadEnlarge(hash);

	/* need to take into account: V1, norm, UV (don't care about V2 and V3) */
	uint32_t ref[] = {quad[0], quad[1] & 0x0000ffff, quad[5], quad[6]};
	uint32_t crc   = crc32(0, (DATA8) ref, sizeof ref);

	HashQuadEntry entry = hash->entries + crc % hash->capa;

	if (entry->quad)
	{
		HashQuadEntry eof = hash->entries + hash->capa;
		HashQuadEntry slot;
		/* already something here: find a new spot */
		for (slot = entry; slot < eof && slot->quad; slot ++);
		if (slot == eof)
			for (slot = hash->entries; slot < entry && slot->quad; slot ++);

		if (slot == entry) return;

		slot->nextChain = entry->nextChain;
		entry->nextChain = slot - hash->entries;
		entry = slot;
	}
	int index = entry - hash->entries;
	if (hash->firstAdded == ENTRY_EOF)
		hash->firstAdded = index;

	if (hash->lastAdded != ENTRY_EOF)
		hash->entries[hash->lastAdded].nextAdded = index;

	entry->crc = crc;
	entry->quad = quad;
	hash->lastAdded = index;
	hash->usage ++;
}

int meshQuadMergeGet(HashQuadMerge hash, DATA32 quad)
{
	uint32_t ref[] = {quad[0], quad[1] & 0x0000ffff, quad[5], quad[6]};
	uint32_t crc   = crc32(0, (DATA8) ref, sizeof ref);

	HashQuadEntry entry = hash->entries + crc % hash->capa;
	while (entry->crc != crc)
	{
		if (entry->nextChain == ENTRY_EOF)
			return -1;
		entry = hash->entries + entry->nextChain;
	}
	if (entry->quad)
		return entry - hash->entries;
	else
		return -1;
}



void meshDebugBank(Map map)
{
	#ifdef DEBUG
	GPUBank bank;

	for (bank = HEAD(map->gpuBanks); bank; NEXT(bank))
	{
		GPUMem mem;
		int    i, total, max;

		for (mem = bank->usedList, max = 0, i = bank->nbItem, total = 0; i > 0; i --, mem ++)
		{
			if (mem->size > 0) total += mem->size;
			if (max < mem->size) max = mem->size;
		}

		fprintf(stderr, "bank: mem = %d/%dK, items: %d/%d, vtxSize: %d\nmem: %d bytes, avg = %d bytes, max = %d\n", bank->memUsed>>10, bank->memAvail>>10,
			bank->nbItem, bank->maxItems, bank->vtxSize, total, total / bank->nbItem, max);
	}
	#endif
}
