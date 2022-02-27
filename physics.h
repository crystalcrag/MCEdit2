/*
 * physics.h: handle collision/movement within the voxel space.
 *
 * Written by T.Pierron, may 2021.
 */

#ifndef MCPHYSICS_H
#define MCPHYSICS_H

#include "maps.h"

typedef struct PhysicsEntity_t *         PhysicsEntity;

int  physicsCheckCollision(Map, vec4 start, vec4 end, VTXBBox bbox, float autoClimb);
void physicsEntityMoved(Map, APTR self, vec4 start, vec4 end);
Bool physicsCheckOnGround(Map, vec4 start, VTXBBox bbox, vec sizes);
void physicsInitEntity(PhysicsEntity entity, int block);
Bool physicsMoveEntity(Map, PhysicsEntity, float speed);
int  physicsCheckIfCanClimb(Map, vec4 pos, VTXBBox bbox);
void physicsCheckPressurePlate(Map, vec4 start, vec4 end, VTXBBox bbox);
void physicsChangeEntityDir(PhysicsEntity, float friction);
void physicsShoveEntity(PhysicsEntity, float friction, int side);

struct PhysicsEntity_t
{
	float   density;           /* will rise in the air (density < 1 microgram/cm�) or fall down otherwise */
	float   dir[3];            /* current movement direcion */
	float   loc[4];            /* current position */
	float   friction[3];       /* how dir[] will change over time */
	uint8_t VYblocked;         /* hit the ground */
	uint8_t light;             /* blocklight (bit0~3) skylight (bit4~7) */
	uint8_t negXZ;             /* &1: dir[VX] is negative, &2: dir[Vz] is negative */
	uint8_t rebound;           /* rebound when hitting the ground */
	VTXBBox bbox;              /* bounding box of entity */
};

enum                           /* special bit field returned by physicsCheckCollision() */
{
	INSIDE_LADDER = 8,
	INSIDE_PLATE  = 16,
};

#endif
