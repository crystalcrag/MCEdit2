/*
 * particles.h : public functions to create/manage various particle effects.
 *
 * written by T.Pierron, dec 2020
 */

#ifndef PARTICLES_H
#define PARTICLES_H

#include "UtilityLibLite.h"
#include "utils.h"
#include "maps.h"

void particlesInit(int vbo);
void particlesExplode(Map map, int count, int blockId, vec4 pos);
int  particlesAnimate(Map map);
void particleAddEmitter(vec4 pos, int type);

enum /* possible values for <effect> */
{
	PARTICLES_EXPLODE,
	PARTICLES_SPARK,
};

#define PARTICLES_VBO_SIZE        (6*4)

/*
 * private stuff below
 */
typedef struct Particle_t *       Particle;
typedef struct Particle_t         Particle_t;
typedef struct ParticleList_t *   ParticleList;
typedef struct Emitter_t *        Emitter;
typedef struct Emitter_t          Emitter_t;
typedef struct EmitterList_t *    EmitterList_t;

struct Particle_t
{
	float    dir[3];
	float    loc[3];
	float    brake[3];  /* PARTICLES_EXPLODE */
	uint64_t seed;
	uint32_t UV;
	uint8_t  light;
	uint8_t  size;
	uint8_t  type;
	int      time;
};

struct Emitter_t
{
	float    loc[3];
	uint8_t  type;
};

struct ParticleList_t
{
	ListNode   node;
	Particle_t buffer[128];
	uint32_t   usage[4];
	uint8_t    count;
};

struct EmitterList_t
{
	ListNode  node;
	Emitter_t emitters[64];
	uint32_t  usage[2];
};

struct ParticlePrivate_t
{
	ListHead buffers;   /* ParticleList */
	ListHead emitters;  /* EmitterList */
	int      count;
	int      vbo;
	vec4     initpos;
	double   lastTime;
	uint8_t  spiral[64];
};


#endif
