/*
 * particles.c : manage particles lifetime and movement.
 *
 * written by T.Pierron, dec 2020
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
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
//#define SLOW

void particlesInit(int vbo)
{
	ParticleList list = calloc(sizeof *list, 1);

	particles.vbo = vbo;

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

static int particlesGetBlockInfo(Map map, vec4 pos, DATA8 plight)
{
	struct BlockIter_t iter;
	mapInitIter(map, &iter, pos, False);
	if (iter.cd)
	{
		uint8_t light = iter.blockIds[(iter.offset >> 1) + BLOCKLIGHT_OFFSET];
		uint8_t sky   = iter.blockIds[(iter.offset >> 1) + SKYLIGHT_OFFSET];
		if (iter.offset & 1) light = (sky & 0xf0) | (light >> 4);
		else                 light = (sky << 4) | (light & 0x0f);
		*plight = light;
		return iter.blockIds[iter.offset];
	}
	*plight = 0xf0;
	return 0;
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
				#ifndef SLOW
				p->ttl = RandRange(1000, 1500);
				#else
				p->ttl = 8000;
				#endif
				p->time = globals.curTime + p->ttl;
			}
		}
	}
}

void particlesSmoke(Map map, int blockId, vec4 pos)
{
	Particle p = particlesAlloc();
	if (p == NULL) return;
	int range = RandRange(500, 1500);
	int UV    = (31 * 16 + ((9*16) << 9));
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

	p->size = 8 + rand() % 6;
	p->UV = PARTICLE_SMOKE | (UV << 10) | (p->size << 6);

	Block b = blockIds + (blockId >> 4);

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
	else p->color = (rand() & 15) | (60 << 4);
}

#ifndef NOEMITTERS
static Emitter particlesAddEmitter(vec4 pos, int blockId, int type, int interval)
{
	Emitter emit = emitterAlloc();

	if (emit)
	{
		if (interval < 16)
			interval = 16;

		memcpy(emit->loc, pos, sizeof emit->loc);
		emit->type = type;
		emit->interval = interval;
		emit->time = globals.curTime + 100;
		emit->blockId = blockId;
		emit->next = -1;

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

static int particleGetBlockId(ChunkData cd, int offset)
{
	uint8_t data = cd->blockIds[DATA_OFFSET + (offset >> 1)];
	return (cd->blockIds[offset] << 4) | (offset & 1 ? data >> 4 : data & 15);
}

#if 0
static void debugEmitters(void)
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
}
#endif

static void particleMakeActive(Map map)
{
	static uint8_t neighbors[] = { /* 27 neighbor ChunkData */
		3, 1, 9, 2, 0, 8, 6, 4, 12,
		3, 1, 9, 2, 0, 8, 6, 4, 12,
		3, 1, 9, 2, 0, 8, 6, 4, 12,
	};
	int16_t oldIds[27];
	int   i, j;
	int   pos[] = {CPOS(map->cx), CPOS(map->cy), CPOS(map->cz)};
	Chunk chunk = map->center;

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
		if (cd->emitters)
		for (emit = cd->emitters + 2, j = emit[-2]; j > 0; j --, emit ++)
		{
			int  pos = *emit & 0xfff;
			vec4 loc = {c->X + (pos & 15), cd->Y + (pos >> 8), c->Z + ((pos >> 4) & 15)};
			Emitter e = particlesAddEmitter(loc, particleGetBlockId(cd, pos), (*emit >> 12) + 1, 750);
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

	if (abs(pos[0]) <= 1 && abs(pos[1]) <= 1 && abs(pos[2]) <= 1)
	{
		DATA16    newIds = cd->emitters;
		int16_t   index = pos[0]+pos[2]*3+pos[1]*9+13;
		int16_t * start = &emitters.startIds[index];
		Emitter   oldEmit;

		oldEmit = *start < 0 ? NULL : emitters.buffer + *start;
		/* current emitters must not be reset (because timer will be reset) */
		if (newIds)
		for (i = newIds[0], newIds += 2; i > 0; i --, newIds ++)
		{
			int oldOffset = oldEmit == NULL ? 4096 :
				((int) oldEmit->loc[0] - chunk->X) +
				((int) oldEmit->loc[2] - chunk->Z) * 16 +
				((int) oldEmit->loc[1] - cd->Y)    * 256;
			int newOffset = *newIds & 0xfff;

			if (newOffset < oldOffset)
			{
				/* new emitter */
				vec4 loc = {chunk->X + (newOffset & 15), cd->Y + (newOffset >> 8), chunk->Z + ((newOffset >> 4) & 15)};
				Emitter e = particlesAddEmitter(loc, particleGetBlockId(cd, newOffset), (*newIds >> 12) + 1, 750);
				e->next = oldEmit == NULL ? -1 : oldEmit - emitters.buffer;
				*start = e - emitters.buffer;
				start = &e->next;
				/* don't do it now, it is highly likely that more chunk updates are coming */
				emitters.dirtyList = 1;
				continue;
			}
			else if (newOffset > oldOffset)
			{
				/* deleted emitter */
				int id = oldEmit - emitters.buffer;
				emitters.usage[id>>5] ^= 1 << (id & 31);
				*start = oldEmit->next;
				newIds --;
				i ++;
				emitters.count --;
				emitters.dirtyList = 1;
			}
			else /* update blockId */
			{
				oldEmit->blockId = particleGetBlockId(cd, newOffset);
				start = &oldEmit->next;
			}
			oldEmit = *start >= 0 ? emitters.buffer + *start : NULL;
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

/* move particles */
int particlesAnimate(Map map, vec4 camera)
{
	ParticleList list;
	Particle     p;
	float *      buf;
	int          i, count, curTimeMS = globals.curTime;

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
			particlesSmoke(map, emit->blockId, emit->loc);
			int next = emit->interval;
			if (next == 0) next = 500;
			emit->time = curTimeMS + RandRange(next>>1, next);

			/* keep the list sorted */
			for (i = 1; i < count && emitters.buffer[emitters.active[i]].time < emit->time; i ++);
			if (i > 1)
			{
				memmove(emitters.active, emitters.active + 1, (i - 1) * 2);
				emitters.active[i-1] = cur;
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

	/* this scale factor will make particles move at a constant no matter at what fps the screen is refreshed */
	#ifndef SLOW
	float speed = (float) ((globals.curTime - particles.lastTime) / 25);
	#else
	float speed = (float) ((globals.curTime - particles.lastTime) / 250);
	#endif

//	fprintf(stderr, "speed = %f, diff = %d\n", speed, time - particles.lastTime);

	for (list = HEAD(particles.buffers), count = 0; list; NEXT(list))
	{
		uint8_t nth, type;
		for (i = list->count, nth = 0, p = list->buffer; i > 0; p ++, nth ++)
		{
			if (p->time == 0) continue; i --;
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
				info[1] = p->physics.light;
				break;
			case PARTICLE_SMOKE:
				info[1] = p->color;
				/* color will get darker over time */
				p->UV &= 0x7ffff;
				p->UV |= ((int) ((globals.curTime - (p->time - p->ttl)) / p->ttl * 7) * 8 + 9 * 16) << 19;
			}
			buf += PARTICLES_VBO_SIZE/4;
			count ++;

			physicsMoveEntity(map, &p->physics, speed);

			if (count == 1000) goto break_all;
		}
	}
	break_all:
	glUnmapBuffer(GL_ARRAY_BUFFER);

	particles.lastTime = globals.curTime;

	return count;
}
