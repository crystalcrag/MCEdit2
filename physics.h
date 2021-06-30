/*
 * physics.h: handle collision/movement within the voxel space.
 *
 * Written by T.Pierron, may 2021.
 */

#ifndef MCPHYSICS_H
#define MCPHYSICS_H

#include "maps.h"

int  physicsCheckCollision(Map map, vec4 start, vec4 end, VTXBBox bbox, float autoClimb);
Bool physicsCheckOnGround(Map map, vec4 start, VTXBBox bbox);


#endif
