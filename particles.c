/*
 * particles.c : manage particles lifetime and movement.
 *
 * written by T.Pierron, dec 2020
 */

#define PARTICLES_IMPL
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
#include "blocks.h"
#include "particles.h"
#include "glad.h"
#include "globals.h"

struct ParticlePrivate_t particles;
struct EmitterPrivate_t  emitters;
static struct VTXBBox_t  particleBBox = {
	.pt1 = {VERTEX(0), VERTEX(0), VERTEX(0)},
	.pt2 = {VERTEX(0.05), VERTEX(0.05), VERTEX(0.05)}
};

//#define NOEMITTERS

Bool particlesInit(void)
{
	ParticleList list = calloc(sizeof *list, 1);

	particles.shader = createGLSLProgram("particles.vsh", "particles.fsh", "particles.gsh");
	if (! particles.shader)
		/* error message already showed */
		return False;

	glGenVertexArrays(1, &particles.vao);
	glGenBuffers(1, &particles.vbo);

	glBindVertexArray(particles.vao);
	glBindBuffer(GL_ARRAY_BUFFER, particles.vbo);
	glBufferData(GL_ARRAY_BUFFER, PARTICLES_VBO_SIZE * PARTICLES_MAX, NULL, GL_STATIC_DRAW);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, PARTICLES_VBO_SIZE, 0);
	glEnableVertexAttribArray(0);
	glVertexAttribIPointer(1, 2, GL_UNSIGNED_INT, PARTICLES_VBO_SIZE, (void *) 12);
	glEnableVertexAttribArray(1);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	ListAddTail(&particles.buffers, &list->node);

	int x, z, y, i;
	for (x = z = y = -1, i = 0; i < 27; i ++)
	{
		emitters.offsets[i] = (x+1) | ((z+1)<<2) | ((y+1) << 4);
		x ++;
		if (x == 2)
		{
			x = -1; z ++;
			if (z == 2)
				z = -1, y ++;
		}
	}
	memset(emitters.startIds, 0xff, sizeof emitters.startIds);
	return True;
}

/* map about to be closed */
void particleDelAll(void)
{
	ParticleList list = (ParticleList) ListRemHead(&particles.buffers);
	list->count = 0;
	memset(list->usage, 0, sizeof list->usage);
	particles.count = 0;

	ListNode * node;
	while ((node = ListRemHead(&particles.buffers)))
		free(node);

	ListAddTail(&particles.buffers, &list->node);

	free(emitters.active);
	free(emitters.buffer);

	memset(&emitters, 0, offsetof(struct EmitterPrivate_t, startIds));
	memset(emitters.startIds, 0xff, sizeof emitters.startIds);
}

static Particle particlesAlloc(void)
{
	ParticleList list;

	if (particles.count == PARTICLES_MAX)
		return NULL;

	for (list = HEAD(particles.buffers); list; NEXT(list))
	{
		int nth = mapFirstFree(list->usage, 4);
		if (nth >= 0)
		{
			/* try at end of array */
			particles.count ++;
			list->count ++;
			memset(list->buffer + nth, 0, sizeof list->buffer[0]);
			return list->buffer + nth;
		}
	}
	list = calloc(sizeof *list, 1);
	ListAddTail(&particles.buffers, &list->node);
	list->usage[0] = 1;
	list->count = 1;
	particles.count ++;
	return list->buffer;
}

#ifndef NOEMITTERS
static Emitter emitterAlloc(void)
{
	int count = emitters.count;
	int max   = emitters.max;
	if (count == max)
	{
		emitters.max = max += 32;
		Emitter emit = realloc(emitters.buffer, max * sizeof *emit + (max >> 5) * 4);
		if (emit)
		{
			emitters.buffer = emit;
			/* move usage flags at end of buffer */
			count >>= 5;
			memmove(emitters.usage = (DATA32) (emit + max), emit + (count<<5), count * 4);
			emitters.usage[count] = 0;
			max = count + 1;
		}
		else return NULL;
	}
	else max >>= 5;
	emitters.count ++;
	return emitters.buffer + mapFirstFree(emitters.usage, max);
}
#endif

static void particlesGetBlockInfo(Map map, vec4 pos, DATA8 plight)
{
	struct BlockIter_t iter;
	mapInitIter(map, &iter, pos, False);
	if (iter.cd)
	{
		/* need to check for half slab or stairs */
		Block b = &blockIds[getBlockId(&iter) >> 4];
		if (b->special == BLOCK_HALF || b->special == BLOCK_STAIRS)
		{
			/* non-full block, but sky/light value is 0, check on top then */
			mapIter(&iter, 0, 1, 0);
		}
		uint8_t light = iter.blockIds[(iter.offset >> 1) + BLOCKLIGHT_OFFSET];
		uint8_t sky   = iter.blockIds[(iter.offset >> 1) + SKYLIGHT_OFFSET];
		if (iter.offset & 1) light = (sky & 0xf0) | (light >> 4);
		else                 light = (sky << 4) | (light & 0x0f);
		*plight = light;
	}
	else *plight = 0xf0;
}


void particlesExplode(Map map, int count, int blockId, vec4 pos)
{
	Particle p = NULL;
	BlockState b = blockGetById(blockId);

	/* invalid state id (none defined in blocksTable.js) */
	if (b->id == 0)
		return;

	int x, y, z;
	float step = 1. / (count+1);
	uint8_t light;

	particlesGetBlockInfo(map, pos, &light);

	for (y = 1; y <= count; y ++)
	{
		float yp = pos[VY] + y * step;

		for (x = 1; x <= count; x ++)
		{
			float xp = pos[VX] + x * step;
			for (z = 1; z <= count; z ++, p ++)
			{
				float zp = pos[VZ] + z * step;
				float pitch = RandRange(M_PI/6, M_PI_2);
				float yaw   = RandRange(0, M_PI_2);
				float cp    = cosf(pitch);
				if (xp < pos[VX] + 0.5f) yaw = M_PIf - yaw;
				if (zp < pos[VZ] + 0.5f) yaw = M_PIf*2 - yaw;

				/*
				 * the speed of particles have been calibrated with 40fps
				 * but can be linearly scaled to match any other fps
				 */
				p = particlesAlloc();
				if (p == NULL) return;
				p->physics.dir[VX] = cosf(yaw) * cp * 0.1f;
				p->physics.dir[VZ] = sinf(yaw) * cp * 0.1f;
				p->physics.dir[VY] = sinf(pitch) * 0.1f;

				p->physics.loc[VX] = xp;
				p->physics.loc[VY] = yp;
				p->physics.loc[VZ] = zp;

				physicsInitEntity(&p->physics, blockId);

				p->physics.light = light;
				p->physics.bbox = &particleBBox;

				int V = b->nzV;
				int U = b->nzU;
				if (V == 62 && U < 17) V = 63; /* biome dependent color */

				U = (U * 16 + (int) (x * step * 16)) | ((V * 16 + (int) (y * step * 16)) << 9);
				p->size = 2 + rand() % 8;
				p->UV = PARTICLE_BITS | (p->size << 6) | (U << 10);
				p->ttl = RandRange(1000, 1500);
				p->time = globals.curTime + p->ttl;
			}
		}
	}
}

/* init a SMOKE particle */
static Particle particlesSmoke(Map map, int blockId, vec4 pos)
{
	Particle p = particlesAlloc();
	if (p == NULL) return NULL;
	Block b = &blockIds[blockId >> 4];
	int range = RandRange(b->particleTTL, b->particleTTL * 3);
	int UV    = 31 * 16 + ((9*16) << 9);
	vec4 offset;

	blockGetEmitterLocation(blockId, offset);

	memset(p, 0, sizeof *p);
	p->physics.loc[0] = pos[0] + offset[0];
	p->physics.loc[1] = pos[1] + offset[1];
	p->physics.loc[2] = pos[2] + offset[2];
	p->time = globals.curTime + range;
	/* will rise in the air */
	p->physics.dir[VY] = 0.01;
	p->physics.bbox = &particleBBox;
	p->ttl = range;

	p->size = 6 + rand() % 6;
	p->UV = PARTICLE_SMOKE | (UV << 10) | (p->size << 6);

	if ((blockId >> 4) == RSWIRE)
	{
		int8_t color = (blockId & 15) - (rand() & 3);
		if (color < 0) color = 0;
		p->color = color + (56 << 4),
		/* way slower */
		p->physics.dir[VY] = 0.005;
	}
	else if (b->category == REDSTONE)
	{
		p->color = 15 - (rand() & 3) + (56 << 4);
	}
	else p->color = (rand() & 15) | (60 << 4); /* torch, fire */
	return p;
}

/* init a DUST particle */
static Particle particlesDust(Map map, int blockId, vec4 pos)
{
	if ((rand() & 255) < 127) return NULL;
	Particle p = particlesAlloc();
	BlockState state = blockGetById(blockId);
	Block b = &blockIds[blockId >> 4];
	if (p == NULL) return NULL;
	int range = RandRange(b->particleTTL, b->particleTTL * 2);
	int UV    = state->nzU * 16 + 8 + ((state->nzV * 16 + 8) << 9);

	memset(p, 0, sizeof *p);
	p->physics.loc[0] = pos[0] + RandRange(0.1, 0.9);
	p->physics.loc[1] = pos[1] - 0.01f;
	p->physics.loc[2] = pos[2] + RandRange(0.1, 0.9);
	p->physics.friction[VY] = 0.00125;
	p->time = globals.curTime + range;
	p->physics.dir[VY] = -RandRange(0.01, 0.04);
	p->physics.bbox = &particleBBox;
	p->ttl = range;
	p->color = RandRange(64, 255); /* speed-up or slow down rotation */

	particlesGetBlockInfo(map, p->physics.loc, &p->physics.light);

	p->size = 6 + rand() % 3;
	p->UV = PARTICLE_DUST | (UV << 10) | (p->size << 6);
	return p;
}

/* init a DRIP particle */
static Particle particlesDrip(Map map, int blockId, vec4 pos)
{
	if ((rand() & 255) < 127) return NULL;
	Particle p = particlesAlloc();
	BlockState state = blockGetById(blockId);
	Block b = &blockIds[blockId >> 4];
	if (p == NULL) return NULL;
	int UV = state->nzU * 16 + 8 + ((state->nzV * 16 + 8) << 9);

	memset(p, 0, sizeof *p);
	p->physics.loc[0] = pos[0] + RandRange(0.1, 0.9);
	p->physics.loc[1] = pos[1] - 1.05f;
	p->physics.loc[2] = pos[2] + RandRange(0.1, 0.9);
	p->time = globals.curTime + 5000;
	p->physics.dir[VY] = -0.01;
	p->physics.friction[VY] = 0.005;
	p->physics.bbox = &particleBBox;
	p->physics.rebound = b->density;
	p->ttl = 5000;

	particlesGetBlockInfo(map, p->physics.loc, &p->physics.light);
	p->physics.loc[1] -= 0.1f;
	if (b->emitLight > 0) p->physics.light |= b->emitLight;

	p->size = 2 + rand() % 3;
	p->UV = PARTICLE_DRIP | (UV << 10) | (p->size << 6);
	return p;
}


#ifndef NOEMITTERS
static Emitter particlesAddEmitter(ChunkData cd, DATA16 data, int16_t ** next)
{
	Emitter old  = emitters.buffer;
	Emitter emit = emitterAlloc();

	/* relocate <*next> if it was pointing within emitters.buffer :-/ */
	if (old != emitters.buffer && (DATAS16) old <= *next && *next < (DATAS16) (old + emitters.count-1))
		*next = (int16_t *) ((DATA8) emitters.buffer + ((DATA8) *next - (DATA8) old));

	if (emit)
	{
		emit->cd = cd;
		emit->Y = (data[0] & 7) * 2;
		emit->type = ((data[0] >> 3) & 31) + 1;
		emit->interval = data[1];
		emit->time = globals.curTime + 100;
		emit->next = -1;
		emit->area = data[2] | (data[3] << 16);
		emit->count = data[0] >> 8;

		// fprintf(stderr, "adding emitter at %g,%g,%g for %d:%d\n", pos[0], pos[1], pos[2], blockId>>4, blockId & 15);
	}
	return emit;
}

/* chunk is about to be unloaded */
static void particlesDelChain(int16_t last)
{
	if (last >= 0)
	{
		int16_t next;
		for (next = last; next >= 0; next = emitters.buffer[next].next)
			emitters.usage[next >> 5] ^= 1 << (next & 31), emitters.count --;
	}
}

#define XPOS(flags)     ((flags&3)-1)
#define ZPOS(flags)     (((flags>>2)&3)-1)
#define YPOS(flags)     ((flags>>4)-1)

#if 0
void emitterDebug(void)
{
	int z, y, i;
	fprintf(stderr, "emitter grid:\n");
	for (y = -1, i = 0; y < 2; y ++)
	{
		fprintf(stderr, "%2d:", y);
		for (z = -1; z < 2; z ++, i += 3)
		{
			int16_t * p = emitters.startIds + i;
			if (z >= 0) fprintf(stderr, "   ");
			fprintf(stderr, p[0] < 0 ? "   " : "%2d ", p[0]);
			fprintf(stderr, p[1] < 0 ? "|    " : "| %2d ", p[1]);
			fprintf(stderr, p[2] < 0 ? "|    \n" : "| %2d \n", p[2]);
		}
		fprintf(stderr, "   ---+----+---\n");
	}

	int count[PARTICLE_MAX] = {0};
	for (i = 0; i < emitters.count; i ++)
	{
		uint16_t cur  = emitters.active[i];
		Emitter  emit = emitters.buffer + cur;
		count[emit->type] ++;
	}

	for (i = 1; i < PARTICLE_MAX; i ++)
	{
		static STRPTR names[] = {NULL, "BITS", "SMOKE", "DUST", "DRIP"};
		fprintf(stderr, "- %s: %d\n", names[i], count[i]);
	}
}
#endif

/* activate particle emitters from the 27 ChunkData surrounding the player */
static void particleMakeActive(Map map)
{
	static uint8_t neighbors[] = { /* 27 neighbor ChunkData */
		3, 1, 9, 2, 0, 8, 6, 4, 12,
		3, 1, 9, 2, 0, 8, 6, 4, 12,
		3, 1, 9, 2, 0, 8, 6, 4, 12,
	};
	int16_t oldIds[27];
	int     i, j;
	int     pos[] = {CPOS(map->cx), CPOS(map->cy), CPOS(map->cz)};
	Chunk   chunk = map->center;

	if (memcmp(pos, emitters.cacheLoc, sizeof pos) == 0)
		return;

	memcpy(oldIds, emitters.startIds, sizeof oldIds);
	memset(emitters.startIds, 0xff, sizeof emitters.startIds);

	/* check which emitters to delete */
	for (i = 0; i < 27; i ++)
	{
		if (oldIds[i] < 0) continue;
		/* damn, coords are not intuitive at all :-/ */
		int dx = pos[0] - emitters.cacheLoc[0] + XPOS(emitters.offsets[i]);
		int dy = emitters.cacheLoc[1] - pos[1] + YPOS(emitters.offsets[i]);
		int dz = pos[2] - emitters.cacheLoc[2] + ZPOS(emitters.offsets[i]);
		if (abs(dx) <= 1 && abs(dy) <= 1 && abs(dz) <= 1)
		{
			/* keep that chain */
			emitters.startIds[dx+dz*3+dy*9+13] = oldIds[i];
		}
		else particlesDelChain(oldIds[i]);
	}

	memcpy(emitters.cacheLoc, pos, sizeof pos);

	for (i = 0; i < 27; i ++)
	{
		if (emitters.startIds[i] >= 0) continue; /* kept from previous center */

		DATA16 emit;
		Chunk  c = chunk + chunkNeighbor[chunk->neighbor + neighbors[i]];
		int    y = pos[1] + YPOS(emitters.offsets[i]);

		if ((c->cflags & CFLAG_HASMESH) == 0)
		{
			/* chunk not loaded: we will get notified later through particleChunkUpdate() */
			continue;
		}

		if (y < 0 || y >= c->maxy) continue;

		ChunkData cd = c->layer[y];
		int16_t * cur = emitters.startIds + i;
		if (cd && cd->emitters)
		for (emit = cd->emitters + 2, j = emit[-2]; j > 0; j --, emit += CHUNK_EMIT_SIZE)
		{
			Emitter e = particlesAddEmitter(cd, emit, &cur);
			*cur = e - emitters.buffer;
			cur = &e->next;
		}
	}
//	debugEmitters();
	emitters.dirtyList = 1;
}

/* emitters list changed, update particle emitters object */
void particlesChunkUpdate(Map map, ChunkData cd)
{
	Chunk chunk = cd->chunk;
	int   pos[] = {emitters.cacheLoc[0] - (chunk->X >> 4), (cd->Y >> 4) - emitters.cacheLoc[1], emitters.cacheLoc[2] - (chunk->Z >> 4)};
	int   i;

	/* only care about updated emitter list within the 27 surrounding ChunkData of the player (ie: 3x3x3 ChunkData grid) */
	if (abs(pos[0]) <= 1 && abs(pos[1]) <= 1 && abs(pos[2]) <= 1)
	{
		DATA16  newIds = cd->emitters;
		int16_t index = pos[0]+pos[2]*3+pos[1]*9+13;
		DATAS16 start = &emitters.startIds[index];
		Emitter old;
		int     oldEmit;

		/* current emitters in <start>, new emitter in <newIds>: need to update emitter list accordingly */

		oldEmit = *start;
		/* current emitters must not be reset (because timer will be reset) */
		if (newIds)
		for (i = newIds[0], newIds += 2; i > 0; i --, newIds += CHUNK_EMIT_SIZE)
		{
			uint8_t newOffset = newIds[0] & 7;
			uint8_t oldOffset;
			old = emitters.buffer + oldEmit;
			oldOffset = oldEmit >= 0 ? old->Y : 16;

			if (oldOffset == newOffset)
			{
				/* there can be multiple emitters at the same Y: use type and interval */
				uint8_t type = (newIds[0] >> 3) & 31;
				DATAS16 prev;
				int     chain;
				Emitter e;
				for (chain = oldEmit, prev = start; chain >= 0; prev = &e->next, chain = *prev)
				{
					e = emitters.buffer + chain;
					if (e->Y > newOffset) break;
					if (e->type == type && e->interval == newIds[1])
					{
						/* already in the list */
						e->area = newIds[2] | (newIds[3] << 16);
						e->count = newIds[0] >> 8;
						/* move to front of list to mark it as processed */
						if (prev != start)
						{
							*prev = e->next;
							e->next = *start;
							*start = chain;
							start = &e->next;
						}
						goto nextloop;
					}
				}
			}

			if (newOffset < oldOffset)
			{
				/* new emitter */
				Emitter e = particlesAddEmitter(cd, newIds, &start);
				e->next = oldEmit;
				*start = e - emitters.buffer;
				start = &e->next;
				/* don't do it now, it is highly likely that more chunk updates are coming */
				emitters.dirtyList = 1;
				continue;
			}
			else if (newOffset > oldOffset)
			{
				/* deleted emitter */
				old = emitters.buffer + oldEmit;
				emitters.usage[oldEmit>>5] ^= 1 << (oldEmit & 31);
				*start = old->next;
				newIds -= CHUNK_EMIT_SIZE;
				i ++;
				emitters.count --;
				emitters.dirtyList = 1;
			}
			nextloop:
			oldEmit = *start;
		}
		if (*start >= 0)
		{
			particlesDelChain(*start);
			emitters.dirtyList = 1;
			*start = -1;
		}
	}
}
#else
void particlesChunkUpdate(Map map, ChunkData cd) { }
void particleMakeActive(Map map) { }
#endif

static int emitterSort(const void * item1, const void * item2)
{
	Emitter emit1 = emitters.buffer + * (DATA16) item1;
	Emitter emit2 = emitters.buffer + * (DATA16) item2;
	return emit1->time - emit2->time;
}

Bool particleCanSpawn(ChunkData cd, int pos, int metadata, int particleType)
{
	if (cd->blockIds[pos] == RSWIRE && metadata == 0)
		return False;

	if (particleType < PARTICLE_DUST)
		return True;

	struct BlockIter_t iter;
	mapInitIterOffset(&iter, cd, pos);
	mapIter(&iter, 0, -1, 0);
	if (iter.blockIds == NULL)
		return False;

	/* DUST and DRIP: needs an air block below */
	uint8_t block = iter.blockIds[iter.offset];
	switch (particleType) {
	case PARTICLE_DUST:
		return block == 0;
	case PARTICLE_DRIP:
		/* immediately below must be solid */
		if (blockIsFullySolid(blockGetByIdData(block, 0)))
		{
			/* then must be an air block */
			mapIter(&iter, 0, -1, 0);
			return iter.blockIds && iter.blockIds[iter.offset] == 0;
		}
		else return False;
	}
	return True;
}

/* will make it easier to update the list later */
static void particleSortEmitters(void)
{
	if (emitters.count > emitters.activeMax)
	{
		emitters.activeMax = (emitters.count+31) & ~31;
		DATA16 buffer = realloc(emitters.active, emitters.activeMax * 2);
		if (buffer == NULL) return;
		emitters.active = buffer;
	}
	int i, j;
	for (i = j = 0; i < 27; i ++)
	{
		int start = emitters.startIds[i];
		while (start >= 0)
		{
			Emitter list = emitters.buffer + start;
			emitters.active[j++] = start;
			start = list->next;
		}
	}
	qsort(emitters.active, j, 2, emitterSort);
	emitters.dirtyList = 0;
	//debugEmitters();
}

/* emitters cover an area: narrow that area for particle constructors */
static void emitterSpawnParticles(Map map, Emitter emit)
{
	ChunkData cd = emit->cd;
	DATA8 blocks = cd->blockIds;
	uint32_t area = emit->area;
	uint8_t  data, i;
	int count = emit->count+1;
	int X = cd->chunk->X;
	int Z = cd->chunk->Z;
	int Y = cd->Y + emit->Y;
	int zcoord = 0;
	int offset = emit->Y << 8;

	while (area > 0 && count > 0)
	{
		/* find first bit set to 1, by counting leading zero */
		int slotZ = multiplyDeBruijnBitPosition[((uint32_t)((area & -(signed)area) * 0x077CB531U)) >> 27];
		area  >>= slotZ;
		area   ^= 1;
		offset += slotZ << 4;
		zcoord += slotZ;
		if (zcoord >= 16) zcoord -= 16, Y ++;
		/* 16 blocks to check */
		for (i = 0; i < 16; i ++, offset ++)
		{
			Block b = &blockIds[blocks[offset]];
			if (b->particle == emit->type && b->emitInterval == emit->interval)
			{
				data = blocks[DATA_OFFSET + (offset >> 1)];
				if (offset & 1) data >>= 4;
				else data &= 15;
				if (particleCanSpawn(cd, offset, data, emit->type))
				{
					Particle p;
					vec4 pos = {X + i, Y, Z + zcoord};
					switch (emit->type) {
					case PARTICLE_SMOKE: p = particlesSmoke(map, ID(b->id, data), pos); break;
					case PARTICLE_DUST:  p = particlesDust(map, ID(b->id, data), pos); break;
					case PARTICLE_DRIP:  p = particlesDrip(map, ID(b->id, data), pos); break;
					default: continue;
					}
					if (p) p->delay = RandRange(0, 255);
					count --;
				}
			}
		}
		offset -= 16;
	}
}


/* move particles */
int particlesAnimate(Map map)
{
	ParticleList list;
	Particle     p;
	float *      buf;
	int          i, count;
	uint32_t     curTimeMS = globals.curTime;

	particleMakeActive(map);
	if (emitters.dirtyList)
		particleSortEmitters();

	if (emitters.count)
	for (count = emitters.count; ; )
	{
		uint16_t cur  = emitters.active[0];
		Emitter  emit = emitters.buffer + cur;

		if (emit->time <= curTimeMS)
		{
			emitterSpawnParticles(map, emit);
			int next = emit->interval;
			if (next == 0) next = 500;
			emit->time = curTimeMS + RandRange(next>>1, next);

			/* keep the list sorted (start from the end: MUCH cheaper) */
			for (i = count-1; i > 0 && emitters.buffer[emitters.active[i]].time > emit->time; i --);
			if (i > 0)
			{
				memmove(emitters.active, emitters.active + 1, i * 2);
				emitters.active[i] = cur;
			}
		}
		else break;
	}

	if (particles.count == 0)
	{
		particles.lastTime = globals.curTime;
		return 0;
	}

	glBindBuffer(GL_ARRAY_BUFFER, particles.vbo);
	buf = glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);

	/* this scale factor will make particles move at a constant speed no matter at what fps the screen is refreshed */
	float speed = (float) ((globals.curTime - particles.lastTime) / 25);
	uint32_t diff = globals.curTime - particles.lastTime;

//	fprintf(stderr, "speed = %f, diff = %d\n", speed, time - particles.lastTime);

	for (list = HEAD(particles.buffers), count = 0; list; NEXT(list))
	{
		uint8_t nth, type;
		for (i = list->count, nth = 0, p = list->buffer; i > 0; p ++, nth ++)
		{
			if (p->time == 0) continue; i --;
			if (p->delay > 0)
			{
				if (p->delay > (diff >> 2))
				{
					p->delay -= (diff >> 2);
					continue;
				}
				else p->delay = 0, p->time = (uint32_t) globals.curTime + p->ttl;
			}
			if (p->time < curTimeMS)
			{
				/* expired particle */
				p->time = 0;
				list->usage[nth>>5] ^= 1 << (nth&31);
				particles.count --;
				list->count --;
				continue;
			}
			DATA32 info = (DATA32) buf + 3;
			buf[0]  = p->physics.loc[VX];
			buf[1]  = p->physics.loc[VY];
			buf[2]  = p->physics.loc[VZ];
			info[0] = p->UV;
			type = p->UV&63;
			switch (type) {
			case PARTICLE_BITS:
			case PARTICLE_DRIP:
				info[1] = p->physics.light;
				break;
			case PARTICLE_SMOKE:
				info[1] = p->color;
				/* color will get darker over time */
				p->UV &= 0x7ffff;
				p->UV |= ((int) ((globals.curTime - (p->time - p->ttl)) / p->ttl * 8) * 8 + 9 * 16) << 19;
				break;
			case PARTICLE_DUST:
				{
					float ttl = (globals.curTime - (p->time - p->ttl)) / p->ttl;
					int   rotation = ((int) (ttl * (1<<19)) * p->color >> 7) & ((1<<20)-1);
					int   frame = ttl * 8;
					if (frame > 7) frame = 7;
					info[1] = p->physics.light | (rotation << 12) | (frame << 8);
				}
			}
			buf += PARTICLES_VBO_SIZE/4;
			count ++;

			if (physicsMoveEntity(map, &p->physics, speed) && type != PARTICLE_SMOKE)
			{
				/* update light values */
				particlesGetBlockInfo(map, p->physics.loc, &p->physics.light);
				info[1] = (info[1] & ~0xff) | p->physics.light;
			}

			if (count == 1000) goto break_all;
		}
	}
	break_all:
	glUnmapBuffer(GL_ARRAY_BUFFER);

	particles.lastTime = globals.curTime;

	return count;
}

void particlesRender(void)
{
	int count = particlesAnimate(globals.level);
	if (count == 0) return;

	glDisable(GL_CULL_FACE);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glDepthMask(GL_FALSE);

	glUseProgram(particles.shader);
	glBindVertexArray(particles.vao);
	glDrawArrays(GL_POINTS, 0, count);
	glDepthMask(GL_TRUE);
}
