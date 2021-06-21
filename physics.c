/*
 * physics.c: simulate physics (collision and movement) for entities
 *
 * collision is implementing using a swept AABB, with a sliding correction, inspired by:
 * https://www.gamedev.net/tutorials/programming/general-and-gameplay-programming/swept-aabb-collision-detection-and-response-r3084/
 *
 * Written by T.Pierron, June 2021.
 */


#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "entities.h"

static Bool intersectBBox(BlockIter iter, VTXBBox bbox, float minMax[6], float inter[6], float rel[3])
{
	float  pt[3] = {iter->ref->X + iter->x - rel[0], iter->yabs - rel[1], iter->ref->Z + iter->z - rel[2]};
	int8_t i;

	for (i = 0; i < 3; i ++)
	{
		float boxmin = (bbox->pt1[i] - ORIGINVTX) * (1./BASEVTX) + pt[i];
		float boxmax = (bbox->pt2[i] - ORIGINVTX) * (1./BASEVTX) + pt[i];

		inter[i]   = boxmin > minMax[i]   ? boxmin : minMax[i];
		inter[3+i] = boxmax < minMax[3+i] ? boxmax : minMax[3+i];
	}
	return inter[VX] < inter[VX+3] && inter[VY] < inter[VY+3] && inter[VZ] < inter[VZ+3];
}

float physicsSweptAABB(float bboxStart[6], vec4 dir, float block[6], DATA8 normal)
{
	/* check if <block> can be intersected */
	float  invEntry[3], entry[3];
	float  invExit[3], exit[3];
	float  entryTime = -1, exitTime = 2;
	int8_t i, axis;

	for (i = axis = 0; i < 3; i ++)
	{
		if (dir[i] == 0)
		{
			if (bboxStart[i] + bboxStart[i+3] <= block[i] ||
			    block[i] + block[i+3] <= bboxStart[i])
			    /* not in the way: can't collide */
				return 2;
			else
				continue;
		}

		if (dir[i] > 0)
			invEntry[i] = block[i] - (bboxStart[i] + bboxStart[i+3]),
			invExit[i]  = block[i] + block[i+3] - bboxStart[i];
		else
			invEntry[i] = block[i] + block[i+3] - bboxStart[i],
			invExit[i]  = block[i] - (bboxStart[i] + bboxStart[i+3]);

		/* time of collision */
		entry[i] = invEntry[i] / dir[i];
		exit[i]  = invExit[i] / dir[i];
		if (entry[i] < 0) continue;
		if (entry[i] > 1) return 2;

		/* entryTime: max of entry[], exitTime: min of exit[] */
		if (entryTime < entry[i]) entryTime = entry[axis = i];
		if (exitTime  < exit[i])  exitTime  = exit[i];
	}

	/* no collision with this block */
	if (entryTime > exitTime || entryTime < 0)
		return 2;

	/* finally compute the normal of the collision */
	*normal = axis;

	return entryTime;
}

/* try to move bounding box <bbox> from <start> to <end>, changing end if movement is blocked */
void physicsCheckCollision(Map map, vec4 start, vec4 end, VTXBBox bbox)
{
	struct BlockIter_t iter;
	float   minMax[6];
	float   dir[3];
	float   shortestDist;
	uint8_t curAxis;
	int8_t  i, j, k;

	for (i = 0; i < 3; i ++)
	{
		dir[i] = (bbox->pt2[i] - ORIGINVTX) * (1./BASEVTX);
		minMax[i]   = (bbox->pt1[i] - ORIGINVTX) * (1./BASEVTX);
		minMax[3+i] = dir[i] - minMax[i];
	}

	/* compute broad phase box */
	float min[3];
	int8_t dx = (int) (fmaxf(start[VX], end[VX]) + dir[VX]) - (int) (min[0] = fminf(start[VX], end[VX]) + minMax[VX]);
	int8_t dy = (int) (fmaxf(start[VY], end[VY]) + dir[VY]) - (int) (min[1] = fminf(start[VY], end[VY]) + minMax[VY]);
	int8_t dz = (int) (fmaxf(start[VZ], end[VZ]) + dir[VZ]) - (int) (min[2] = fminf(start[VZ], end[VZ]) + minMax[VZ]);

	vecSub(dir, end, start);
	vecAdd(minMax, minMax, start);

	/* first: find the closest box intersected */
	mapInitIter(map, &iter, min, False);
	for (i = 0, shortestDist = 2, curAxis = 0; ; )
	{
		for (j = 0; ; )
		{
			for (k = 0; ; )
			{
				/* check if entity bbox (minMax) collides with any block in the voxel space */
				int count, cnxFlags;
				VTXBBox voxel = mapGetBBox(&iter, &count, &cnxFlags);

				if (voxel)
				{
					vec4 rel = {iter.ref->X + iter.x, iter.yabs, iter.ref->Z + iter.z};
					for (; count > 0; count --, voxel ++)
					{
						uint8_t idx = voxel->flags & 0x7f;
						if (idx > 0 && (cnxFlags & (1 << (idx - 1))) == 0) continue;

						uint8_t axis, m;
						float   bboxFloat[6];
						float   dist;

						for (m = 0; m < 3; m ++)
						{
							bboxFloat[m]   = (voxel->pt1[m] - ORIGINVTX) * (1./BASEVTX);
							bboxFloat[3+m] = (voxel->pt2[m] - ORIGINVTX) * (1./BASEVTX) - bboxFloat[m];
							bboxFloat[m]  += rel[m];
						}
						dist = physicsSweptAABB(minMax, dir, bboxFloat, &axis);
						if (dist < shortestDist)
							shortestDist = dist, curAxis = axis;
					}
				}

				k ++;
				if (k > dx) break;
				mapIter(&iter, 1, 0, 0);
			}
			j ++;
			if (j > dz) break;
			mapIter(&iter, -dx, 0, 1);
		}
		i ++;
		if (i > dy) break;
		mapIter(&iter, -dx, 1, -dz);
	}

	/* next: analyze what we gathered and try to find the minimum distance to avoid boxes collided */
	if (shortestDist < 1)
	{
		end[VX] = start[VX] + (min[VX] = dir[VX] * shortestDist);
		end[VY] = start[VY] + (min[VY] = dir[VY] * shortestDist);
		end[VZ] = start[VZ] + (min[VZ] = dir[VZ] * shortestDist);

		/* we might still have some velocity left */
		dir[VX] -= min[VX];
		dir[VY] -= min[VY];
		dir[VZ] -= min[VZ];
		/* axis we collided with, we can't go further in this direction */
		dir[curAxis] = 0;

		/* repeat this on the velocity left */
		if (fabsf(dir[VX]) > EPSILON ||
		    fabsf(dir[VY]) > EPSILON ||
		    fabsf(dir[VZ]) > EPSILON)
		{
			memcpy(start, end, 12);
			vecAdd(end, start, dir);
			physicsCheckCollision(map, start, end, bbox);
		}
	}
}

Bool physicsCheckOnGround(Map map, vec4 start, VTXBBox bbox)
{
	struct BlockIter_t iter;
	float  minMax[6];
	int8_t i, j;

	for (i = 0; i < 3; i ++)
	{
		minMax[i]   = (bbox->pt1[i] - ORIGINVTX) * (1./BASEVTX) + start[i] - 2*EPSILON;
		minMax[3+i] = (bbox->pt2[i] - ORIGINVTX) * (1./BASEVTX) + start[i] - 2*EPSILON;
	}

	int8_t dx = (int) minMax[VX+3] - (int) minMax[VX];
	int8_t dz = (int) minMax[VZ+3] - (int) minMax[VZ];

	mapInitIter(map, &iter, minMax, False);
	for (i = 0; ; )
	{
		for (j = 0; ; )
		{
			int count, cnxFlags;
			VTXBBox bbox = mapGetBBox(&iter, &count, &cnxFlags);
			if (bbox)
			{
				float inter[6];

				if (bbox && intersectBBox(&iter, bbox, minMax, inter, start) && inter[4] > EPSILON)
					return True;
			}
			j ++;
			if (j > dx) break;
			mapIter(&iter, 1, 0, 0);
		}
		i ++;
		if (i > dz) break;
		mapIter(&iter, 1-dx, 0, 1);
	}
	return False;
}
