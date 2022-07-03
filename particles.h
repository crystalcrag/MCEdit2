/*
 * particles.h : public functions to create/manage various particle effects.
 *
 * written by T.Pierron, dec 2020
 */

#ifndef PARTICLES_H
#define PARTICLES_H

#include "UtilityLibLite.h"
#include "utils.h"
#include "physics.h"

Bool particlesInit(void);
void particleDelAll(void);
void particlesRender(void);
void particlesExplode(Map map, int count, int blockId, vec4 pos);
int  particlesAnimate(Map map);
void particlesChunkUpdate(Map map, ChunkData cd);
Bool particleCanSpawn(struct BlockIter_t iter, int blockId, int particleType);

#define PARTICLES_VBO_SIZE        (5*4)
#define PARTICLES_MAX             1024

/*
 * private stuff below
 */
#ifdef PARTICLES_IMPL
typedef struct Particle_t *       Particle;
typedef struct Particle_t         Particle_t;
typedef struct ParticleList_t *   ParticleList;
typedef struct Emitter_t *        Emitter;
typedef struct PhysicsEntity_t    PHYSENT_t;

struct Particle_t
{
	PHYSENT_t physics;            /* collision detection */
	uint32_t  UV;                 /* tex coord to use */
	uint8_t   size;
	uint8_t   delay;
	uint16_t  ttl;                /* time to live (ms) */
	uint16_t  color;              /* color modulation (offset in terrain.png) */
	int       time;
};

struct Emitter_t
{
	ChunkData cd;                 /* world coord */
	uint8_t   Y;
	uint8_t   type;               /* PARTICLE_* (declared in blocks.h) */
	uint16_t  interval;           /* time in ms before creating a new particle */

	int16_t   next;               /* linked list of emitters within a chunk (offset in emitters.buffer) */
	uint16_t  count;              /* number of emitters in the area */

	uint32_t  area;               /* used by DUST and DRIP: area where blocks might be (loc[] is at 0,0 of a XZ layer in the ChunkData) */
	                              /* contains bitfield where nth bit set to 1 means a the nth Z row contains a block emitter */
	uint32_t  time;
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
	ListHead buffers;             /* ParticleList */
	int      count;
	double   lastTime;
	int      shader;              /* OpenGL stuff */
	int      vao, vbo;
};

struct EmitterPrivate_t
{
	Emitter  buffer;              /* active emitters */
	DATA32   usage;               /* usage bitfield (ceil(count/32) items) */
	int      count;               /* items in <buffer> */
	int      max;                 /* max capacity of <buffer> */
	DATA16   active;              /* index in <buffer>, sorted by time to spawn next particle */
	int      activeMax;           /* max capacity of <active> */
	int      cacheLoc[3];
	int16_t  startIds[27];        /* emitters for one ChunkData ordered XZY */
	uint8_t  offsets[27];         /* +/- 1 for X, Z, Y for locating chunk 0-26 */
	uint8_t  dirtyList;           /* <active> needs to be rebuilt */
};
#endif
#endif
