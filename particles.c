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

struct ParticlePrivate_t particles;
extern double curTime;

//#define SLOW

void particlesInit(int vbo)
{
	ParticleList list = calloc(sizeof *list, 1);

	particles.vbo = vbo;

	ListAddTail(&particles.buffers, &list->node);

	/* used to generate a random pattern for spark particles */
	int incx = 1, incy = 0, i, j, k, step;
	for (i = k = 0, j = step = 8; i < 64; i ++)
	{
		particles.spiral[i] = k;
		j --;
		if (j == 0)
		{
			/* change direction */
			if (incx > 0) incx =  0, incy = 8, step --; else
			if (incy > 0) incx = -1, incy = 0; else
			if (incx < 0) incx =  0, incy = -8, step --;
			else          incx =  1, incy = 0;
			j = step;
		}
		k += incx + incy;
	}
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
	particles.count ++;
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

				int V = b->nzV;
				int U = b->nzU;
				if (V == 62 && U < 17) V = 63; /* biome dependent color */

				U = (U * 16 + (int) (x * step * 16)) | ((V * 16 + (int) (y * step * 16)) << 9);
				p->size = 2 + rand() % 8;
				p->UV = (p->size << 6) | (U << 10);
				#ifndef SLOW
				p->time = curTime + RandRange(1000, 1500);
				#else
				p->time = curTime + RandRange(4000, 8000);
				#endif
				p->type = PARTICLES_EXPLODE;

				if (p->dir[VX] < 0) p->size |= 0x80, p->dir[VX] = - p->dir[VX];
				if (p->dir[VZ] < 0) p->size |= 0x40, p->dir[VZ] = - p->dir[VZ];
			}
		}
	}
}

void particlesSparks(Map map, int blockId, vec4 pos)
{
	Particle p = particlesAlloc();

	memset(p, 0, sizeof *p);
	memcpy(p->loc, pos, sizeof p->loc);
	p->time = curTime + 100000;
	p->UV = (31 * 16 + ((3*16+8) << 9)) | (0xf0 << 19) | (PARTICLES_SPARK << 27);
	p->size = 2 + rand() % 8;
	int i;
	for (i = 0, p->seed = 0; i < 64; i ++)
	{
		if (rand() < RAND_MAX/4+i*RAND_MAX/96)
			p->seed |= 1ULL << particles.spiral[i];
	}
}

/* move particles */
int particlesAnimate(Map map)
{
	ParticleList list;
	Particle p;
	float * buf;
	int i, count;

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
		if (list->count == 0) continue;
		uint32_t usage[4] = {
			list->usage[0] ^ 0xffffffff,
			list->usage[1] ^ 0xffffffff,
			list->usage[2] ^ 0xffffffff,
			list->usage[3] ^ 0xffffffff
		};
		for (i = list->count; i > 0; i --)
		{
			int nth = mapFirstFree(usage, 4);
			p = list->buffer + nth;
			if (p->time < curTime)
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
			switch (p->type) {
			case PARTICLES_EXPLODE:
				info[1] = p->light;
				info[2] = 0;
				break;
			case PARTICLES_SPARK:
				info[1] = p->seed >> 32;
				info[2] = p->seed & 0xffffffff;
			}
			buf += 6;
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
				if (! (b->type == SOLID || b->type == TRANS || b->type == CUST))
				{
					p->light = light;
					if (p->brake[VY] == 0)
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
