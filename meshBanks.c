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
#include "meshBanks.h"
#include "particles.h"
#include "tileticks.h"

       Staging_t staging;               /* chunk loading/meshing (MT context) */
static ListHead  meshBanks;             /* MeshBuffer (ST context) */
static ListHead  alphaBanks;            /* MeshBuffer (ST context) */
static Thread_t  threads[NUM_THREADS];
static int       threadStop;            /* THREAD_EXIT_* */


#define MAX_MESH_CHUNK     64*1024
#define processing         nbt.page


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

		/* a long time can have passed waiting on that semaphore... */
		if (threadStop == THREAD_EXIT_LOOP) continue;
		if (threadStop == THREAD_EXIT) break;

		thread->state = THREAD_RUNNING;
		/* that mutex lock will let the main thread know we are busy */
		MutexEnter(thread->wait);
		/* process chunks /!\ need to unlock the mutex (<wait>) before exiting this branch!! */



		MutexEnter(map->genLock);
		Chunk list = (Chunk) ListRemHead(&map->genList);
		MutexLeave(map->genLock);
		if (! list || (list->cflags & CFLAG_HASMESH))
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

		/* process chunk */
		for (i = 0; i < list->maxy; i ++)
		{
			list->cdIndex = thread - threads;
			list->save = map->chunks;
			ChunkData cd = list->layer[i];
			chunkUpdate(list, chunkAir, map->chunkOffsets, i, meshInitMT);
			list->cdIndex = 0;
			if (cd->cdFlags == CDFLAG_PENDINGDEL)
			{
				/* empty ChunkData: link within chunk has already been removed in chunkUpdate() */
				free(cd);
				continue;
			}
			if (! list->save && threadStop)
				goto bail;
		}
		//fprintf(stderr, "chunk %d, %d (%d) done mem: %d/256\n", list->X, list->Z, list->maxy, staging.total);
		/* mark the chunk as ready to be pushed to the GPU mem */
		list->cflags |= CFLAG_STAGING;
		list->save = NULL;

		bail:
		/* this is to inform the main thread that this thread has finished its work */
		MutexLeave(thread->wait);
	}
	thread->state = THREAD_EXITED;
}
#endif

#if NUM_THREADS > 0
void meshInitThreads(Map map)
{
	int nb;
	staging.mem = malloc(STAGING_AREA);
	staging.capa = SemInit(STAGING_AREA/4096);
	staging.alloc = MutexCreate();
	for (nb = 0; nb < NUM_THREADS; nb ++)
	{
		threads[nb].wait = MutexCreate();
		threads[nb].map  = map;
		ThreadCreate(meshGenAsync, threads + nb);
	}

	/* already load center chunk */
	Chunk center = map->center;
	chunkLoad(center, map->path, center->X, center->Z);
}
#endif

/* ask thread to stop what they are doing and wait for them */
void meshStopThreads(Map map, int exit)
{
	threadStop = exit;

	#if NUM_THREADS > 0
	/* list is about to be redone/freed */
	while (SemWaitTimeout(map->genCount, 0));

	int i;
	/* need to wait, thread might hold pointer to object that are going to be freed */
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
		/* need to be sure threads have exited */
		SemAdd(map->genCount, NUM_THREADS);
		for (i = 0; i < NUM_THREADS; i ++)
		{
			while (threads[i].state >= 0);
			MutexDestroy(threads[i].wait);
		}
		memset(threads, 0, sizeof threads);
	}
	#endif

	/* clear staging area */
	memset(staging.usage, 0, sizeof staging.usage);
	SemAdd(staging.capa, staging.total);
	staging.total = 0;
	staging.chunkData = 0;

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

static Bool meshCheckCoplanar(MeshWriter buffer)
{
	/* check if all quads are coplanar */
	DATA32 vertex, end;
	for (vertex = buffer->start, end = buffer->cur; vertex < end; vertex += VERTEX_INT_SIZE)
	{
		/* get normal */
		uint16_t coord, norm = (vertex[5] >> 9) & 7;
		DATA16   cop = buffer->coplanar + norm;
		switch (norm) {
		case 0: case 2: coord = vertex[1]; break;
		case 1: case 3: coord = vertex[0]; break;
		default:        coord = vertex[0] >> 16;
		}
		if (cop[0] == 0)     cop[0] = coord; else
		if (cop[0] != coord) return False;
	}
	return True;
}

/* partial mesh data */
static void meshFlushST(MeshWriter buffer)
{
	MeshBuffer list = buffer->mesh;

	list->usage = (DATA8) buffer->cur - (DATA8) buffer->start;

	if (buffer->alpha && buffer->isCOP)
		buffer->isCOP = meshCheckCoplanar(buffer);

	if (list->usage < MAX_MESH_CHUNK - VERTEX_DATA_SIZE)
	{
		/* still some room left, don't alloc a new block just yet */
		return;
	}
	else if (list->node.ln_Next)
	{
		NEXT(list);
	}
	else list = meshAllocST(buffer->alpha ? &alphaBanks : &meshBanks);

	buffer->mesh = list;
	buffer->cur = buffer->start = list->buffer;
	buffer->end = list->buffer + (MAX_MESH_CHUNK / 4);
}

Bool meshInitST(ChunkData cd, APTR opaque, APTR alpha)
{
	MeshBuffer mesh;
	/* typical sub-chunk is usually below 64Kb of mesh data */
	if (meshBanks.lh_Head == NULL)
	{
		mesh = meshAllocST(&alphaBanks);
		mesh = meshAllocST(&meshBanks);
	}
	else mesh = HEAD(meshBanks);
	mesh->chunk = cd;

	for (mesh = HEAD(meshBanks);  mesh; mesh->usage = 0, NEXT(mesh));
	for (mesh = HEAD(alphaBanks); mesh; mesh->usage = 0, NEXT(mesh));

	MeshWriter writer = opaque;
	mesh = HEAD(meshBanks);
	mesh->chunk   = cd;
	writer->start = writer->cur = mesh->buffer;
	writer->end   = mesh->buffer + (MAX_MESH_CHUNK / 4);
	writer->alpha = 0;
	writer->isCOP = 1;
	writer->mesh  = mesh;
	writer->flush = meshFlushST;

	writer = alpha;
	mesh = HEAD(alphaBanks);
	mesh->chunk  = cd;
	writer->start = writer->cur = mesh->buffer;
	writer->end   = mesh->buffer + (MAX_MESH_CHUNK / 4);
	writer->alpha = 1;
	writer->isCOP = 1;
	writer->mesh  = mesh;
	writer->flush = meshFlushST;
	memset(writer->coplanar, 0, sizeof writer->coplanar);
	return True;
}

/*
 * multi-threaded context mem allocation
 */
static DATA32 meshAllocMT(struct Thread_t * thread, int first, int start)
{
	thread->state = THREAD_WAIT_BUFFER;
	SemWait(staging.capa);

	/* it might have passed a long time since */
	if (threadStop) return NULL;

	MutexEnter(staging.alloc);

//	fprintf(stderr, "alloc mem for thread %d: first = %d\n", thread - threads, first);

	/* alloc in chunk of 4Kb */
	int index = mapFirstFree(staging.usage, DIM(staging.usage));
	DATA32 mem = staging.mem + (index << 10);
	staging.total ++;
	if (first)
		staging.start[staging.chunkData++] = index;

	mem[0] = start; /* chunk position */
	mem[1] = 0;     /* next link / memory used */

	MutexLeave(staging.alloc);

	thread->state = THREAD_RUNNING;

	return mem;
}

static void meshFlushMT(MeshWriter buffer)
{
	ChunkData cd = buffer->mesh;
	/* mesh generation cancelled */
	if (! cd) return;

	int size = (DATA8) buffer->cur - (DATA8) buffer->start;

	if (buffer->alpha && buffer->isCOP)
		buffer->isCOP = meshCheckCoplanar(buffer);

//	fprintf(stderr, "flush mem for thread %d: size = %d\n", cd->chunk->cdIndex, size);

	if (size < 4088 - VERTEX_DATA_SIZE)
	{
		/* still some room left, don't alloc a new block just yet */
		buffer->start[-1] = size << 10;
		return;
	}

	DATA32 mem = meshAllocMT(&threads[cd->chunk->cdIndex], False, 0);
	if (mem == NULL)
	{
		/* cancel mesh generation */
		buffer->mesh = NULL;
		return;
	}

	buffer->start[-1] = (size << 10) | (((mem - staging.mem) >> 10) + 1);
	buffer->cur = buffer->start = mem + 2;
	buffer->end = mem + 1024;
}

/* this function is called in a MT context */
Bool meshInitMT(ChunkData cd, APTR opaque, APTR alpha)
{
	/* typical sub-chunk is usually below 64Kb of mesh data */
	Chunk  chunk    = cd->chunk;
	int    start    = (chunk - chunk->save) | (cd->Y << 12);
	DATA32 memSOLID = meshAllocMT(threads + chunk->cdIndex, True, start);

	if (memSOLID)
	{
		DATA32 memALPHA = meshAllocMT(threads + chunk->cdIndex, False, start);

		if (memALPHA)
		{
			MeshWriter writer = opaque;
			writer->start = writer->cur = memSOLID + 2;
			writer->end   = memSOLID + 1024;
			writer->alpha = 0;
			writer->isCOP = 1;
			writer->mesh  = cd;
			writer->flush = meshFlushMT;

			writer = alpha;
			writer->start = writer->cur = memALPHA + 2;
			writer->end   = memALPHA + 1024;
			writer->alpha = 1;
			writer->isCOP = 1;
			writer->mesh  = cd;
			writer->flush = meshFlushMT;
			cd->glAlpha   = ((memALPHA - staging.mem) >> 10) + 1;
			memset(writer->coplanar, 0, sizeof writer->coplanar);
			return True;
		}
		/* no need to free memSOLID block, it will be in meshStopThreads() */
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

		/* pre-configure terrain VAO: 5 bytes per vertex */
		glBindVertexArray(bank->vaoTerrain);
		glBindBuffer(GL_ARRAY_BUFFER, bank->vboTerrain);
		/* this will allocate memory on the GPU: mem chunks of 20Mb */
		glBufferData(GL_ARRAY_BUFFER, map->GPUMaxChunk, NULL, GL_STATIC_DRAW);
		glVertexAttribIPointer(0, 4, GL_UNSIGNED_INT, VERTEX_DATA_SIZE, 0);
		glEnableVertexAttribArray(0);
		glVertexAttribIPointer(1, 3, GL_UNSIGNED_INT, VERTEX_DATA_SIZE, (void *) 16);
		glEnableVertexAttribArray(1);
		/* 16 bytes of per-instance data (3 float for loc and 1 uint for flags) */
		glBindBuffer(GL_ARRAY_BUFFER, bank->vboLocation);
		glVertexAttribPointer(2, 4, GL_FLOAT, 0, 0, 0);
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
			int used = size;
			if (used + 2*4096 >= free->size)
				used = free->size;
			off = free->offset;
			if (free->size == used)
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
			glBufferData(GL_ARRAY_BUFFER, count * VERTEX_INSTANCE, NULL, GL_STATIC_DRAW);
			glBindBuffer(GL_DRAW_INDIRECT_BUFFER, bank->vboMDAI);
			glBufferData(GL_DRAW_INDIRECT_BUFFER, count * sizeof (MDAICmd_t), NULL, GL_STATIC_DRAW);
		}
	}
}

/* transfer single ChunkData mesh to GPU (meshing init with meshInitST) */
void meshFinishST(Map map)
{
	MeshBuffer list;
	int        size, alpha;
	int        total, offset;
	int        oldSize, oldAlpha;
	ChunkData  cd;
	GPUBank    bank;

	for (list = HEAD(meshBanks), cd = list->chunk, size = 0; list; size += list->usage, NEXT(list));
	for (list = HEAD(alphaBanks), alpha = 0; list; alpha += list->usage, NEXT(list));

	oldSize = cd->glSize;
	oldAlpha = cd->glAlpha;
	total = size + alpha;
	bank = cd->glBank;

	if (bank)
	{
		GPUMem mem = bank->usedList + cd->glSlot;
		if (total > mem->size)
		{
			/* not enough space: need to "free" previous mesh before */
			meshFreeGPU(cd);
			/*
			 * this time reserve some space in case there are further modifications
			 * the vast majority of chunks will never be modified, no need to do this everytime.
			 */
			total = (total + 4095) & ~4095;
			total -= total % VERTEX_DATA_SIZE;
			offset = meshAllocGPU(map, cd, total);
			cd->glSize = size + alpha;
		}
		else offset = mem->offset, cd->glSize = total; /* reuse mem segment */
	}
	else offset = meshAllocGPU(map, cd, total);

//	fprintf(stderr, "allocating %d bytes at %d for chunk %d, %d / %d\n", cd->glSize, offset, cd->chunk->X, cd->chunk->Z, cd->Y);

	if (offset >= 0)
	{
		bank = cd->glBank;
		cd->glAlpha = alpha;
		/* and finally copy the data to the GPU */
		glBindBuffer(GL_ARRAY_BUFFER, bank->vboTerrain);
		DATA8 mem = glMapBufferRange(GL_ARRAY_BUFFER, offset, total, GL_MAP_WRITE_BIT);

		/* first: opaque */
		for (list = HEAD(meshBanks); list; mem += list->usage, NEXT(list))
			memcpy(mem, list->buffer, list->usage);

		/* then alpha: will be rendered in a separate pass */
		for (list = HEAD(alphaBanks); list; mem += list->usage, NEXT(list))
			memcpy(mem, list->buffer, list->usage);

		glUnmapBuffer(GL_ARRAY_BUFFER);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
	}
	/* check if this chunk is visible: vtxSize must be the total number of MDAICmd_t sent to the GPU */
	if (map->frame == cd->chunk->chunkFrame)
	{
		if ((oldSize > 0) != (cd->glSize > 0))
			bank->vtxSize += oldSize ? -1 : 1;
		if ((oldAlpha > 0) != (cd->glAlpha > 0))
			bank->vtxSize += oldAlpha ? -1 : 1;
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
		for (cd = map->firstVisible; cd; cd->glBank = NULL, cd->glSize = 0, cd->glAlpha = 0, cd = cd->visible);
		ListNew(&map->gpuBanks);
	}
}

/* map is being closed */
void meshCloseAll(Map map)
{
	meshFreeAll(map, False);
	ListNode * node;
	while ((node = ListRemHead(&meshBanks)))  free(node);
	while ((node = ListRemHead(&alphaBanks))) free(node);

	meshStopThreads(map, THREAD_EXIT);

	int i;
	for (i = 0; i < NUM_THREADS; i ++)
		MutexDestroy(threads[i].wait);

	free(staging.mem);
	MutexDestroy(staging.alloc);
	SemClose(staging.capa);
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
					load->cflags |= CFLAG_GOTDATA;
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

		/* second: push data to the GPU (only the first chunk) */
		for (i = 0, j = list->maxy; j > 0; j --, i ++)
		{
			ChunkData cd = list->layer[i];
			if (cd)
			{
				/* this is the function that will convert chunk into triangles */
				chunkUpdate(list, chunkAir, map->chunkOffsets, i, meshInitST);
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

static int meshStagingSize(int offset)
{
	DATA32 ptr = staging.mem + offset;
	int size = 0, slot;
	for (;;)
	{
		size += ptr[1] >> 10;
		slot = ptr[1] & 0x3ff;
		if (slot == 0) break;
		ptr = staging.mem + (slot - 1) * 1024;
	}
	return size;
}

/* flush what the threads have been filling (called from main thread) */
void meshGenerateMT(Map map)
{
	MutexEnter(staging.alloc);

	DATA8 index, eof;
	int   freed;

	#if 0
	fprintf(stderr, "parsing staging area: %d\n", staging.chunkData);
	for (index = staging.start, eof = index + staging.chunkData; index < eof; index ++)
	{
		DATA32 src = staging.mem + index[0] * 1024;
		Chunk chunk = map->chunks + (src[0] & 0xffff);
		ChunkData cd = chunk->layer[src[0] >> 16];

		fprintf(stderr, "%d: %d, %d, %d: %s (%p)\n", index[0], chunk->X, chunk->Z, cd->Y,
			chunk->cflags & CFLAG_STAGING ? "ready" : "not ready", cd->glBank);
	}
	#endif

	for (index = staging.start, freed = 0, eof = index + staging.chunkData; index < eof; )
	{
		/* is the chunk ready ? */
		DATA32 src = staging.mem + index[0] * 1024;
		Chunk chunk = map->chunks + (src[0] & 0xffff);
		ChunkData cd = chunk->layer[src[0] >> 16];

		if (cd && (chunk->cflags & CFLAG_STAGING))
		{
			/* yes, move all chunks into GPU and free staging area */
			GPUBank bank;
			DATA8   dst;
			int     count, slot, offset, alpha;

			/* count bytes needed to store this chunk on GPU */
			alpha = cd->glAlpha;
			if (cd->glBank)
				puts("here");
			cd->glAlpha = meshStagingSize((alpha - 1) * 1024);
			cd->glSize  = meshStagingSize(index[0] * 1024) + cd->glAlpha;
			count = cd->glSize;
			bank = cd->glBank;

			if (count == 0)
			{
				/* only air blocks (usually needed for block light propagation) */
				slot = index[0]; staging.usage[slot >> 5] ^= 1 << (slot & 31);
				slot = alpha-1;  staging.usage[slot >> 5] ^= 1 << (slot & 31);
				freed += 2;
			}
			else
			{
				//fprintf(stderr, "mesh ready: chunk %d, %d [%d]: %d + %d\n", chunk->X, chunk->Z, cd->Y, cd->glSize, cd->glAlpha);

				if (bank)
				{
					GPUMem mem = bank->usedList + cd->glSlot;
					if (count > mem->size)
					{
						/* not enough space: need to "free" previous mesh before */
						meshFreeGPU(cd);
						/*
						 * this time reserve some space in case there are further modifications
						 * the vast majority of chunks will never be modified, no need to do this everytime.
						 */
						offset = meshAllocGPU(map, cd, (count + 4095) & ~4095);
					}
					else offset = mem->offset; /* reuse mem segment */
				}
				else offset = meshAllocGPU(map, cd, count);

				bank = cd->glBank;
				glBindBuffer(GL_ARRAY_BUFFER, bank->vboTerrain);
				dst = glMapBufferRange(GL_ARRAY_BUFFER, offset, count, GL_MAP_WRITE_BIT);

				for (slot = index[0]; ; )
				{
					staging.usage[slot >> 5] ^= 1 << (slot & 31);
					freed ++;
					count = src[1] >> 10;
					slot  = src[1] & 0x3ff;

					/* copy mesh data to GPU */
					memcpy(dst, src + 2, count);
					dst += count;
					if (slot == 0)
					{
						if (alpha > 0)
							slot = alpha, alpha = 0;
						else
							break;
					}
					slot --;
					src = staging.mem + slot * 1024;
				}
				glUnmapBuffer(GL_ARRAY_BUFFER);
				glBindBuffer(GL_ARRAY_BUFFER, 0);

				if ((chunk->cflags & CFLAG_HASENTITY) == 0)
				{
					chunkExpandEntities(chunk);
					updateParseNBT(chunk);
				}
			}
			eof --;
			memmove(index, index + 1, eof - index);
			staging.chunkData --;
			chunk->cflags |= CFLAG_HASMESH;

			/* note: no need to modify "bank->vtxSize" like meshFinishST(), this function is only used for initial chunk loading */
		}
		/* wait for next frame */
		else index ++;
	}
	staging.total -= freed;
	MutexLeave(staging.alloc);
	SemAdd(staging.capa, freed);
}

void meshDebugBank(Map map)
{
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
}
