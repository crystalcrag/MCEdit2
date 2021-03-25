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
#include "redstone.h"
#include "glad.h"

struct ParticlePrivate_t particles;
struct EmitterPrivate_t  emitters;
extern double curTime;

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

static Emitter emitterAlloc(void)
{
	int count = emitters.count;
	int max   = (count+31) & ~31;
	if (count == max)
	{
		count = max + 32;
		Emitter emit = realloc(emitters.buffer, count * sizeof *emit + (count >> 5) * 4);
		if (emit)
		{
			emitters.buffer = emit;
			/* move usage flags at end of buffer */
			max >>= 5;
			memmove(emitters.usage = (DATA32) (emit + count), emit + max, max * 4);
			emitters.usage[max] = 0;
			max ++;
		}
		else return NULL;
	}
	else max >>= 5;
	emitters.count ++;
	return emitters.buffer + mapFirstFree(emitters.usage, max);
}

static int particlesGetBlockInfo(Map map, vec4 pos, DATA8 plight)
{
	uint8_t light, sky;

	struct BlockIter_t iter;
	mapInitIter(map, &iter, pos, False);
	if (iter.cd)
	{
		light = iter.blockIds[(iter.offset >> 1) + BLOCKLIGHT_OFFSET];
		sky   = iter.blockIds[(iter.offset >> 1) + SKYLIGHT_OFFSET];
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
				if (xp < pos[VX] + 0.5) yaw = M_PI - yaw;
				if (zp < pos[VZ] + 0.5) yaw = M_PI*2 - yaw;

				/*
				 * the speed of particles have been calibrated with 40fps
				 * but can be linearly scaled to match any other fps
				 */
				p = particlesAlloc();
				if (p == NULL) return;
				p->dir[VX] = cosf(yaw) * cp * 0.1;
				p->dir[VZ] = sinf(yaw) * cp * 0.1;
				p->dir[VY] = sinf(pitch) * 0.1;

				p->loc[VX] = xp;
				p->loc[VY] = yp;
				p->loc[VZ] = zp;

				p->brake[VX] = 0.0001;
				p->brake[VZ] = 0.0001;
				p->brake[VY] = 0.02;
				p->light = light;
				p->onGround = 0;

				int V = b->nzV;
				int U = b->nzU;
				if (V == 62 && U < 17) V = 63; /* biome dependent color */

				U = (U * 16 + (int) (x * step * 16)) | ((V * 16 + (int) (y * step * 16)) << 9);
				p->size = 2 + rand() % 8;
				p->UV = PARTICLE_BITS | (p->size << 6) | (U << 10);
				#ifndef SLOW
				p->ttl = RandRange(1000, 1500);
				#else
				p->ttl = RandRange(4000, 8000);
				#endif
				p->time = curTime + p->ttl;

				if (p->dir[VX] < 0) p->size |= 0x80, p->dir[VX] = - p->dir[VX];
				if (p->dir[VZ] < 0) p->size |= 0x40, p->dir[VZ] = - p->dir[VZ];
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
	p->loc[0] = pos[0] + offset[0];
	p->loc[1] = pos[1] + offset[1];
	p->loc[2] = pos[2] + offset[2];
	p->time = curTime + range;
	p->ttl  = range;
	p->dir[VY] = 0.02;
	p->size = 8 + rand() % 6;
	p->UV = PARTICLE_SMOKE | (UV << 10) | (p->size << 6);
	p->onGround = 0;
	p->light = 0;

	Block b = blockIds + (blockId >> 4);

	if ((blockId >> 4) == RSWIRE)
		p->color = (blockId & 15) + (56 << 4),
		p->dir[VY] = 0.005;
	else if (b->category == REDSTONE)
		p->color = 15 + (56 << 4);
	else
		p->color = (rand() & 15) | (60 << 4);
}

static Emitter particlesAddEmitter(vec4 pos, int blockId, int type, int interval)
{
	Emitter emit = emitterAlloc();

	if (interval < 16)
		interval = 16;

	memcpy(emit->loc, pos, sizeof emit->loc);
	emit->type = type;
	emit->interval = interval;
	emit->time = curTime + interval;
	emit->blockId = blockId;
	emit->next = -1;

	fprintf(stderr, "adding emitter at %g,%g,%g for %d:%d\n", pos[0], pos[1], pos[2], blockId>>4, blockId & 15);

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

static void particleMakeActive(Map map)
{
	static uint8_t neighbors[] = { /* 27 neighbor ChunkData */
		12, 4, 6, 8, 0, 2, 9, 1, 3,
		12, 4, 6, 8, 0, 2, 9, 1, 3,
		12, 4, 6, 8, 0, 2, 9, 1, 3,
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
		int dx = emitters.cacheLoc[0] + XPOS(emitters.offsets[i]) - pos[0];
		int dy = emitters.cacheLoc[1] + YPOS(emitters.offsets[i]) - pos[1];
		int dz = emitters.cacheLoc[2] + ZPOS(emitters.offsets[i]) - pos[2];
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
	emitters.dirtyList = 1;
}

/* emitters list changed, update particle emitters object */
void particlesChunkUpdate(Map map, ChunkData cd)
{
	Chunk chunk = cd->chunk;
	int   pos[] = {emitters.cacheLoc[0] - (chunk->X >> 4), emitters.cacheLoc[1] - (cd->Y >> 4), emitters.cacheLoc[2] - (chunk->Z >> 4)};
	int   i;

	if (abs(pos[0]) <= 1 && abs(pos[1]) <= 1 && abs(pos[2]) <= 1)
	{
		int16_t * newIds = cd->emitters;
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
}

static void particleChangeDir(Particle p)
{
	float angle = RandRange(0, 2 * M_PI);
	p->dir[VY] = 0;
	p->dir[VX] = cosf(angle) * 0.01;
	p->dir[VZ] = sinf(angle) * 0.01;
	p->brake[VZ] = -0.001;
	p->brake[VX] = -0.001;
	if (p->dir[VX] < 0) p->size |= 0x80, p->dir[VX] = - p->dir[VX];
	if (p->dir[VZ] < 0) p->size |= 0x40, p->dir[VZ] = - p->dir[VZ];
}

/* move particles */
int particlesAnimate(Map map, vec4 camera)
{
	ParticleList list;
	Particle     p;
	float *      buf;
	int          i, count, curTimeMS = curTime;

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
		particles.lastTime = curTime;
		return 0;
	}

	glBindBuffer(GL_ARRAY_BUFFER, particles.vbo);
	buf = glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);

	/* this scale factor will make particles move at a constant no matter what fps the screen is refreshed */
	#ifndef SLOW
	float speed = (curTime - particles.lastTime) / 25.0f;
	#else
	float speed = (curTime - particles.lastTime) / 250.0f;
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
			vec4   old = {floorf(p->loc[VX]), floorf(p->loc[VY]), floorf(p->loc[VZ])};
			buf[0]  = p->loc[VX];
			buf[1]  = p->loc[VY];
			buf[2]  = p->loc[VZ];
			info[0] = p->UV;
			type = p->UV&63;
			switch (type) {
			case PARTICLE_BITS:
				info[1] = p->light;
				break;
			case PARTICLE_SMOKE:
				info[1] = p->color;
				p->UV &= 0x7ffff;
				p->UV |= ((int) ((curTime - (p->time - p->ttl)) / p->ttl * 7) * 8 + 9 * 16) << 19;
			}
			buf += PARTICLES_VBO_SIZE/4;
			count ++;

			float inc;
			inc = p->dir[VX] * speed; p->loc[VX] += p->size & 0x80 ? -inc : inc;
			inc = p->dir[VZ] * speed; p->loc[VZ] += p->size & 0x40 ? -inc : inc;
			p->loc[VY] += p->dir[VY] * speed;

			p->dir[VX] -= p->brake[VX] * speed; if (p->dir[VX] < 0) p->dir[VX] = 0;
			p->dir[VZ] -= p->brake[VZ] * speed; if (p->dir[VZ] < 0) p->dir[VZ] = 0;
			p->dir[VY] -= p->brake[VY] * speed;

			if (p->brake[VY] == 0)
			{
				if (p->onGround)
				{
					/* increase friction if sliding on ground */
					p->brake[VX] += 0.0005 * speed;
					p->brake[VZ] += 0.0005 * speed;
				}
			}
			else p->brake[VY] += 0.0015 * speed;

			/* check collision */
			vec4 pos = {floorf(p->loc[VX]), floorf(p->loc[VY]), floorf(p->loc[VZ])};
			if (pos[VX] != old[VX] ||
			    pos[VY] != old[VY] ||
			    pos[VZ] != old[VZ])
			{
				uint8_t light;
				Block b = blockIds + particlesGetBlockInfo(map, pos, &light);
				if (! (b->type == SOLID || b->type == TRANS || b->type == CUST) || b->bboxPlayer == BBOX_NONE)
				{
					p->light = light;
					if (! p->onGround)
					{
						/* check if block above has changed */
						pos[VY] += p->onGround ? -1 : 1;
						Block b = blockIds + particlesGetBlockInfo(map, pos, &light);
						if (! (b->type == SOLID || b->type == TRANS || b->type == CUST) || b->bboxPlayer == BBOX_NONE)
							p->brake[VX] = p->brake[VZ] = 0.001, p->dir[VY] = 0.02;
					}
					else p->brake[VY] = 0.02;
					continue;
				}
				/* inside a solid block: light will be 0 */

				if (pos[VX] != old[VX]) p->dir[VX] = p->brake[VX] = 0, p->loc[VX] = buf[-5];
				if (pos[VZ] != old[VZ]) p->dir[VZ] = p->brake[VZ] = 0, p->loc[VZ] = buf[-3];
				if (pos[VY] >  old[VY])
				{
					/* hit a ceiling */
					p->dir[VY] = 0;
					p->loc[VY] = buf[-4];
					if (type != PARTICLE_SMOKE)
						p->brake[VY] = 0.02;
					else
						particleChangeDir(p);
				}
				else if (pos[VY] < old[VY])
				{
					/* hit the ground */
					p->loc[VY] = buf[-4] = pos[VY]+0.95;
					p->dir[VY] = 0;
					p->brake[VY] = 0;
					p->onGround = 1;
				}
			}
			if (count == 1000) goto break_all;
		}
	}
	break_all:
	glUnmapBuffer(GL_ARRAY_BUFFER);

	particles.lastTime = curTime;

	return count;
}
