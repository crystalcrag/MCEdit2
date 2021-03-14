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
int  particlesAnimate(Map map, vec4 camera);
void particlesAddEmitter(vec4 pos, int blockId, int type, int interval);
void particlesDelEmitter(vec4 pos);
void particlesDelFromChunk(int X, int Z);

#define PARTICLES_VBO_SIZE        (5*4)

/*
 * private stuff below
 */
typedef struct Particle_t *       Particle;
typedef struct Particle_t         Particle_t;
typedef struct ParticleList_t *   ParticleList;
typedef struct Emitter_t *        Emitter;
typedef struct Emitter_t          Emitter_t;
typedef struct EmitterList_t *    EmitterList;

struct Particle_t
{
	float    dir[3];
	float    loc[3];
	float    brake[3];
	uint32_t UV;
	uint8_t  light;
	uint8_t  size;
	uint8_t  onGround;
	uint16_t ttl;
	uint16_t color;
	int      time;
};

struct Emitter_t
{
	ListNode node;
	float    loc[3];
	uint8_t  type;
	uint8_t  index;
	uint8_t  inactive;
	uint16_t interval;
	uint16_t blockId;
	int      time;
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
	Emitter_t buffer[64];
	uint32_t  usage[2];
	uint8_t   count;
};

struct ParticlePrivate_t
{
	ListHead buffers;        /* ParticleList */
	ListHead emitters;       /* EmitterList */
	ListHead sortedEmitter;  /* Emitter */
	ListHead sortedInactive; /* Emitter */
	int      count;
	int      vbo;
	vec4     initpos;
	double   lastTime;
	uint8_t  spiral[64];
	uint8_t  ranges[8];
};


#endif
