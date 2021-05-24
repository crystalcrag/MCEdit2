/*
 * physics.c: simulate physics (collision and movement) for entities
 *
 * note: this isnot a rigid body simulation, just an attempt to mimic minecraft physics.
 *
 * Written by T.Pierron, May 2021.
 */


#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "entities.h"

extern int8_t relx[], rely[], relz[], opp[];

static Bool isFaceVisible(struct BlockIter_t iter, int side, VTXBBox bbox)
{
	mapIter(&iter, relx[side], rely[side], relz[side]);
	Block b = blockIds + iter.blockIds[iter.offset];
	return b->type != SOLID;
}

static int intersectBBox(BlockIter iter, VTXBBox bbox, float min[3], float max[3], float inter[6])
{
	float  pt[3] = {iter->ref->X + iter->x, iter->yabs, iter->ref->Z + iter->z};
	int8_t i;

	for (i = 0; i < 3; i ++)
	{
		float boxmin = (bbox->pt1[i] - ORIGINVTX) * (1./BASEVTX) + pt[i];
		float boxmax = (bbox->pt2[i] - ORIGINVTX) * (1./BASEVTX) + pt[i];

		inter[i]   = boxmin > min[i] ? boxmin : min[i];
		inter[3+i] = (boxmax < max[i] ? boxmax : max[i]) - inter[i];
	}

	if (inter[3] > EPSILON && inter[4] > EPSILON && inter[5] > EPSILON)
	{
		/* which axis has been intersected */
		int8_t avoid = 0;
		do {
			if (fabsf(inter[VX] - min[VX]) < EPSILON && (avoid &  8) == 0) i = SIDE_WEST; else
			if (fabsf(inter[VZ] - min[VZ]) < EPSILON && (avoid &  4) == 0) i = SIDE_NORTH; else
			if (fabsf(inter[VY] - min[VY]) < EPSILON && (avoid & 32) == 0) i = SIDE_BOTTOM; else
			if (fabsf(inter[VZ] + inter[VZ+3] - max[VZ]) < EPSILON && (avoid &  1) == 0) i = SIDE_SOUTH; else
			if (fabsf(inter[VX] + inter[VX+3] - max[VX]) < EPSILON && (avoid &  2) == 0) i = SIDE_EAST; else
			if (fabsf(inter[VY] + inter[VY+3] - max[VY]) < EPSILON && (avoid & 16) == 0) i = SIDE_TOP; else return -1;
			if (isFaceVisible(*iter, opp[i], bbox)) break;
			avoid |= 1 << i;
		} while (avoid != 63);
		return i;
	}
	return -1;
}

/* try to move bounding box <bbox> from <start> to <end>, changing end if movement is blocked */
void physicsCheckCollision(Map map, vec4 start, vec4 end, VTXBBox bbox)
{
	struct BlockIter_t iter;
	float  min[3];
	float  max[3];
	int8_t i, j, k;

	for (i = 0; i < 3; i ++)
	{
		min[i] = (bbox->pt1[i] - ORIGINVTX) * (1./BASEVTX) + end[i];
		max[i] = (bbox->pt2[i] - ORIGINVTX) * (1./BASEVTX) + end[i];
	}

	int8_t dx = (int) max[VX] - (int) min[VX];
	int8_t dy = (int) max[VY] - (int) min[VY];
	int8_t dz = (int) max[VZ] - (int) min[VZ];

	mapInitIter(map, &iter, min, False);
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
					float  inter[6];
					int8_t side;
					bbox = blockGetBBox(blockGetById(id));

					if (bbox && (side = intersectBBox(&iter, bbox, min, max, inter)) >= 0)
					{
						static uint8_t side2axis[] = {2, 0, 2, 0, 1, 1};
						static uint8_t side2area[] = {0, 0, 1, 1, 0, 1};
						int8_t axis = side2axis[side];
						float diff = end[axis];
						diff -= end[axis] = side2area[side] ? inter[axis] + inter[axis+3] + diff - min[axis] : inter[axis] - max[axis] + diff;
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
			mapIter(&iter, -dx, 0, 1);
		}
		i ++;
		if (i > dy) break;
		mapIter(&iter, -dx, 1, -dz);
	}
}

Bool physicsCheckOnGround(Map map, vec4 start, VTXBBox bbox)
{
	struct BlockIter_t iter;
	float  min[3];
	float  max[3];
	int8_t i, j;

	for (i = 0; i < 3; i ++)
	{
		min[i] = (bbox->pt1[i] - ORIGINVTX) * (1./BASEVTX) + start[i] - 2*EPSILON;
		max[i] = (bbox->pt2[i] - ORIGINVTX) * (1./BASEVTX) + start[i] - 2*EPSILON;
	}

	int8_t dx = (int) max[VX] - (int) min[VX];
	int8_t dz = (int) max[VZ] - (int) min[VZ];

	mapInitIter(map, &iter, min, False);
	for (i = 0; ; )
	{
		for (j = 0; ; )
		{
			int id = getBlockId(&iter);
			Block b = blockIds + (id >> 4);
			if (b->bboxPlayer != BBOX_NONE)
			{
				VTXBBox bbox = blockGetBBox(blockGetById(id));
				float   inter[6];

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
