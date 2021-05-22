/*
 * physics.c: simulate physics (collision and movement) for entities
 *
 * note: this isnot a rigid body simulation, just an attempt to mimic minecraft physics.
 *
 * Written by T.Pierron, May 2021.
 */


#include <malloc.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "entities.h"

static Bool intersectBBox(BlockIter iter, VTXBBox bbox, double min[3], double max[3], double inter[6])
{
	double pt[3] = {iter->ref->X + iter->x, iter->yabs, iter->ref->Z + iter->z};
	int8_t i;

	for (i = 0; i < 3; i ++)
	{
		double boxmin = (bbox->pt1[i] - ORIGINVTX) * (1./BASEVTX) + pt[i];
		double boxmax = (bbox->pt2[i] - ORIGINVTX) * (1./BASEVTX) + pt[i];

		inter[i]   = boxmin > min[i] ? boxmin : min[i];
		inter[3+i] = (boxmax < max[i] ? boxmax : max[i]) - inter[i];
	}

	return inter[3] > EPSILON && inter[4] > EPSILON && inter[5] > EPSILON;
}

/* try to move bounding box <bbox> from <start> to <end>, changing end if movement is blocked */
void physicsCheckCollision(Map map, vec4 start, vec4 end, VTXBBox bbox)
{
	struct BlockIter_t iter;
	double min[3];
	double max[3];
	double dir[3];
	int8_t i, j, k;

	for (i = 0; i < 3; i ++)
	{
		min[i] = (bbox->pt1[i] - ORIGINVTX) * (1./BASEVTX) + end[i];
		max[i] = (bbox->pt2[i] - ORIGINVTX) * (1./BASEVTX) + end[i];
		dir[i] = end[i] - start[i];
	}

	int8_t dx = (int) max[VX] - (int) min[VX];
	int8_t dy = (int) max[VY] - (int) min[VY];
	int8_t dz = (int) max[VZ] - (int) min[VZ];

	mapInitIter(map, &iter, (float[3]){min[0], min[1], min[2]}, False);
	for (i = 0; ; )
	{
		for (j = 0; ; )
		{
			for (k = 0; ; )
			{
				/* check if bbox collides with any block it intersects in voxel space */
				int id = getBlockId(&iter);
				Block b = blockIds + (id >> 4);
				if (b->bboxPlayer != BBOX_NONE)
				{
					double inter[6];
					bbox = blockGetBBox(blockGetById(id));

					if (bbox && intersectBBox(&iter, bbox, min, max, inter))
					{
						/* bbox of entity intersect a block on the map */
						uint8_t axis = 0;
						if (inter[3] < inter[4])
							axis = inter[3] < inter[5] ? VX : VZ;
						else
							axis = inter[4] < inter[5] ? VY : VZ;
						double diff = end[axis];
						diff -= end[axis] = dir[axis] < 0 ? inter[axis] + inter[axis+3] : inter[axis];
						min[axis] -= diff;
						max[axis] -= diff;
					}
				}
				k ++;
				if (k > dx) break;
				mapIter(&iter, 1, 0, 0);
			}
			j ++;
			if (j > dz) break;
			mapIter(&iter, 1-dx, 0, 1);
		}
		i ++;
		if (i > dy) break;
		mapIter(&iter, 1-dx, 1, 1-dz);
	}
}

Bool physicsCheckOnGround(Map map, vec4 start, VTXBBox bbox)
{
	/* XXX should switch to double precision here :-/ */
	struct BlockIter_t iter;
	double min[3];
	double max[3];
	int8_t i, j;

	for (i = 0; i < 3; i ++)
	{
		min[i] = (bbox->pt1[i] - ORIGINVTX) * (1./BASEVTX) + start[i] - 2*EPSILON;
		max[i] = (bbox->pt2[i] - ORIGINVTX) * (1./BASEVTX) + start[i] - 2*EPSILON;
	}

	int8_t dx = (int) max[VX] - (int) min[VX];
	int8_t dz = (int) max[VZ] - (int) min[VZ];

	mapInitIter(map, &iter, (float[3]){min[0], min[1], min[2]}, False);
	for (i = 0; ; )
	{
		for (j = 0; ; )
		{
			int id = getBlockId(&iter);
			Block b = blockIds + (id >> 4);
			if (b->bboxPlayer != BBOX_NONE)
			{
				VTXBBox bbox = blockGetBBox(blockGetById(id));
				double  inter[6];

				if (bbox && intersectBBox(&iter, bbox, min, max, inter) && inter[4] > EPSILON)
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
