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
extern double curTime;

//#define SLOW

void particlesInit(int vbo)
{
	ParticleList list = calloc(sizeof *list, 1);

	particles.vbo = vbo;

	ListAddTail(&particles.buffers, &list->node);

	/* used to generate a random pattern for spark particles */
	int incx = 1, incy = 0, i, j, k, step, range;
	for (i = k = 0, j = step = 7, range = 1; i < 49; i ++)
	{
		particles.spiral[i] = k;
		j --;
		if (j == 0)
		{
			/* change direction */
			if (incx > 0) incx =  0, incy = 8, step --; else
			if (incy > 0) incx = -1, incy = 0; else
			if (incx < 0) incx =  0, incy = -8, step --;
			else          incx =  1, incy = 0, particles.ranges[range++] = i+1;
			j = step;
		}
		k += incx + incy;
	}
	particles.ranges[range] = i;
}

static Particle particlesAlloc(void)
{
	ParticleList list;

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
	EmitterList list;
	for (list = HEAD(particles.emitters); list; NEXT(list))
	{
		int nth = mapFirstFree(list->usage, 2);
		if (nth >= 0)
		{
			list->count ++;
			return list->buffer + nth;
		}
	}
	list = calloc(sizeof *list, 1);
	ListAddTail(&particles.emitters, &list->node);
	list->usage[0] = 1;
	list->count = 1;
	Emitter emit, eof;
	int i;
	for (emit = list->buffer, eof = emit + 64, i = 0; emit < eof; emit->index = i++, emit ++);
	return list->buffer;
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
	int range = RandRange(500, 1000);
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

void particlesAddEmitter(vec4 pos, int blockId, int type, int interval)
{
	Emitter emit = emitterAlloc();

	if (interval < 16)
		interval = 16;

	memcpy(emit->loc, pos, sizeof emit->loc);
	emit->type = type;
	emit->interval = interval;
	emit->time = curTime + interval;
	emit->blockId = blockId;

	/* keep them in a sorted linked list in increasing spawn time */
	Emitter list, prev;
	for (list = HEAD(particles.sortedEmitter), prev = NULL; list && list->time < emit->time; prev = list, NEXT(list));
	ListInsert(&particles.sortedEmitter, &emit->node, &prev->node);
}

void particlesDelEmitter(vec4 pos)
{
	Emitter emit;
	/* pos is float, but location only uses integer */
	for (emit = HEAD(particles.sortedEmitter); emit && memcmp(pos, emit->loc, 12); NEXT(emit));
	if (emit)
	{
		EmitterList list = (EmitterList) ((DATA8) (emit - emit->index) - sizeof emit->node);
		uint8_t id = emit->index;
		list->count --;
		list->usage[id>>5] ^= 1 << (id & 31);
		ListRemove(&particles.sortedEmitter, &list->node);
	}
}

/* chunk is about to be unloaded */
void particlesDelFromChunk(int X, int Z)
{
	// TODO
}

/* move particles */
int particlesAnimate(Map map, vec4 camera)
{
	ParticleList list;
	Emitter      emit;
	Particle     p;
	float *      buf;
	int          i, count, curTimeMS = curTime;

	emit = HEAD(particles.sortedEmitter);
	if (emit && emit->time <= curTimeMS)
	{
		do {
			if (vecDistSquare(emit->loc, camera) < 10*10)
			{
				particlesSmoke(map, emit->blockId, emit->loc);
				emit->time = curTimeMS + RandRange(emit->interval>>1, emit->interval);
			}
			else emit->time = curTimeMS + (emit->interval << 1);

			/* keep list sorted */
			Emitter list, next, prev;
			for (prev = (Emitter) emit->node.ln_Prev, list = next = (Emitter) emit->node.ln_Next; list && list->time < emit->time; prev = list, NEXT(list));
			ListRemove(&particles.sortedEmitter, &emit->node);
			ListInsert(&particles.sortedEmitter, &emit->node, &prev->node);
			emit = next;
		}
		while (emit && emit->time <= curTimeMS);
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
		uint8_t nth;
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
			switch (p->UV&63) {
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
				p->brake[VX] += 0.0005 * speed;
				p->brake[VZ] += 0.0005 * speed;
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
					if (p->onGround)
						p->brake[VY] = 0.02;
					continue;
				}
				/* inside a solid block: light will be 0 */

				if (pos[VX] != old[VX]) p->dir[VX] = p->brake[VX] = 0, p->loc[VX] = buf[-5];
				if (pos[VZ] != old[VZ]) p->dir[VZ] = p->brake[VZ] = 0, p->loc[VZ] = buf[-3];
				if (pos[VY] >  old[VY]) p->dir[VZ] = 0;
				if (pos[VY] <  old[VY])
				{
					p->loc[VY] = buf[-4] = pos[VY]+0.95;
					p->dir[VY] = 0;
					p->brake[VY] = 0;
					p->onGround = 1;
				}
				else p->brake[VY] = 0.02;
			}
			if (count == 1000) goto break_all;
		}
	}
	break_all:
	glUnmapBuffer(GL_ARRAY_BUFFER);

	particles.lastTime = curTime;

	return count;
}
