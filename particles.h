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
void particlesChunkUpdate(Map map, ChunkData cd);

#define PARTICLES_VBO_SIZE        (5*4)
#define PARTICLES_MAX             1024

/*
 * private stuff below
 */
typedef struct Particle_t *       Particle;
typedef struct Particle_t         Particle_t;
typedef struct ParticleList_t *   ParticleList;
typedef struct Emitter_t *        Emitter;

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
	float    loc[3];
	uint8_t  type;
	uint8_t  inactive;
	uint16_t interval;
	uint16_t blockId;
	int16_t  next;
	int      time;
};

struct ParticleList_t
{
	ListNode   node;
	Particle_t buffer[128];
	uint32_t   usage[4];
	uint8_t    count;
};

struct ParticlePrivate_t
{
	ListHead buffers;          /* ParticleList */
	int      count;
	int      vbo;
	vec4     initpos;
	double   lastTime;
};

struct EmitterPrivate_t
{
	Emitter  buffer;           /* active emitters */
	DATA32   usage;            /* usage bitfield (ceil(count/32) items) */
	int      count;            /* items in <buffer> */
	DATA16   active;           /* index in <buffer>, sorted by time to spawn next particle */
	int      activeMax;        /* max capacity of <active> */
	int      cacheLoc[3];
	int16_t  startIds[27];     /* emitters for one ChunkData */
	uint8_t  offsets[27];      /* +/- 1 for x, Z, Y for locating chunk 0-26 */
	uint8_t  dirtyList;        /* <active> needs to be rebuilt */
};

#endif
