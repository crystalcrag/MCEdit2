/*
 * physics.h: handle collision/movement within the voxel space.
 *
 * Written by T.Pierron, may 2021.
 */

#ifndef MCPHYSICS_H
#define MCPHYSICS_H

#include "maps.h"

typedef struct PhysicsEntity_t *         PhysicsEntity;

int  physicsCheckCollision(Map map, vec4 start, vec4 end, VTXBBox bbox, float autoClimb);
Bool physicsCheckOnGround(Map map, vec4 start, VTXBBox bbox);
void physicsInitEntity(PhysicsEntity entity, int block);
Bool physicsMoveEntity(Map, PhysicsEntity, float speed);

struct PhysicsEntity_t
{
	float   density;           /* will rise in the air (density < 1 microgram/cm³) or fall down otherwise */
	float   dir[3];            /* current movement direcion */
	float   loc[4];            /* current position */
	float   friction[3];       /* how dir[] will change over time */
	uint8_t VYblocked;         /* hit the ground */
	uint8_t light;             /* blocklight (bit0~3) skylight (bit4~7) */
	uint8_t negXZ;             /* &1: dir[VX] is negative, &2: dir[Vz] is negative */
	VTXBBox bbox;              /* bounding box of entity */
};

#endif
