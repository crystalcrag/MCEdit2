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
void particlesCreate(Map map, int effect, int count, int blockId, vec4 pos);
int  particlesAnimate(Map map);
void particleSetBlock(vec4 pos, int blockId);

enum /* possible values for <effect> */
{
	PARTICLES_EXPLODE,
};

#define PARTICLES_VBO_SIZE        (5*4)

/*
 * private stuff below
 */
typedef struct Particle_t *       Particle;
typedef struct Particle_t         Particle_t;
typedef struct ParticleList_t *   ParticleList;

struct Particle_t
{
	float    dir[3];
	float    loc[3];
	float    brake[3];
	float    UV;
	uint16_t blockId;
	uint8_t  light;
	uint8_t  size;
	int      time;
};

struct ParticleList_t
{
	ListNode   node;
	Particle_t buffer[100];
	int        usage;
};

struct ParticlePrivate_t
{
	ListHead buffers;   /* ParticleList */
	int      count;
	int      vbo;
	vec4     initpos;
	double   lastTime;
};


#endif
