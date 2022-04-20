/*
 * physics.h: handle collision/movement within the voxel space.
 *
 * Written by T.Pierron, may 2021.
 */

#ifndef MCPHYSICS_H
#define MCPHYSICS_H

#include "maps.h"

typedef struct PhysicsEntity_t *         PhysicsEntity;

typedef int (ValidBlockCb_t)(struct BlockIter_t iter, int dx, int dy, int dz);

int  physicsCheckCollision(Map, vec4 start, vec4 end, ENTBBox bbox, float autoClimb, ValidBlockCb_t);
void physicsEntityMoved(Map, APTR self, vec4 start, vec4 end);
Bool physicsCheckOnGround(Map, vec4 start, ENTBBox bbox);
void physicsInitEntity(PhysicsEntity entity, int block);
Bool physicsMoveEntity(Map, PhysicsEntity, float speed);
int  physicsCheckIfCanClimb(Map, vec4 pos, ENTBBox bbox);
void physicsCheckPressurePlate(Map, vec4 start, vec4 end, ENTBBox bbox);
void physicsChangeEntityDir(PhysicsEntity, float friction);
void physicsShoveEntity(PhysicsEntity, float friction, int side);

struct PhysicsEntity_t
{
	float   density;           /* will rise in the air (density < 1 microgram/cm³) or fall down otherwise */
	float   dir[3];            /* current movement direcion */
	float   loc[4];            /* current position */
	float   friction[3];       /* how dir[] will change over time */
	uint8_t physFlags;         /* PHYSFLAG_* */
	uint8_t light;             /* blocklight (bit0~3) skylight (bit4~7) (mostly used by particles) */
	uint8_t negXZ;             /* &1: dir[VX] is negative, &2: dir[Vz] is negative */
	uint8_t rebound;           /* rebound when hitting the ground XXX need more robust logic than this */
	ENTBBox bbox;              /* bounding box of entity */
};


enum
{
	PHYSFLAG_VYBLOCKED  = 1,   /* hit the ground */
	PHYSFLAG_OVERHOPPER = 2,   /* over a hopper, should be grabbed */
};

enum                           /* special bit field returned by physicsCheckCollision() */
{
	INSIDE_LADDER = 8,
	INSIDE_PLATE  = 16,
	INSIDE_HOPPER = 32,
	SOFT_COLLISON = 64,        /* do not reset velocity, target might get out of the way */
};

#endif
