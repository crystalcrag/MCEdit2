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
static uint8_t side2axis[] = {2, 0, 2, 0, 1, 1};
static uint8_t side2area[] = {0, 0, 1, 1, 0, 1};

static Bool hasEnoughGapBetween(struct BlockIter_t iter, int side, VTXBBox bbox, float minMax[6])
{
	static uint8_t axisSrc[] = {5, 3, 2, 0, 4, 1}; /* S, E, N, W, T, B */
	static uint8_t axisDst[] = {2, 0, 5, 3, 1, 4};

	/* we are dealing with bounding box faces, not vertex data */
	mapIter(&iter, relx[side], rely[side], relz[side]);
	if (iter.blockIds == NULL) return True;

	VTXBBox neighbor = mapGetBBox(&iter);

	if (neighbor)
	{
		/* if gap is too small, we can ignore that intersection */
		int gap = bbox->pt1[axisSrc[side]] - neighbor->pt1[axisDst[side]];
		int axis = side2axis[side];
		switch (axis) {
		case 0: gap -= relx[side] * BASEVTX; break;
		case 1: gap -= rely[side] * BASEVTX; break;
		case 2: gap -= relz[side] * BASEVTX;
		}
		if (gap < 0) gap = -gap;
		return gap * (1./BASEVTX) > minMax[axis+3] - minMax[axis];
	}
	else return True;
}

static int intersectBBox(BlockIter iter, VTXBBox bbox, float minMax[6], float inter[6])
{
	float  pt[3] = {iter->ref->X + iter->x, iter->yabs, iter->ref->Z + iter->z};
	int8_t i;

	for (i = 0; i < 3; i ++)
	{
		float boxmin = (bbox->pt1[i] - ORIGINVTX) * (1./BASEVTX) + pt[i];
		float boxmax = (bbox->pt2[i] - ORIGINVTX) * (1./BASEVTX) + pt[i];

		inter[i]   = boxmin > minMax[i] ? boxmin : minMax[i];
		inter[3+i] = (boxmax < minMax[3+i] ? boxmax : minMax[3+i]) - inter[i];
	}

	if (inter[3] > EPSILON && inter[4] > EPSILON && inter[5] > EPSILON)
	{
		/* sort intersect axis from min to max: if we need to adjust something use the axis with the least amount of changes */
		uint8_t order[3];

		if (inter[VY+3] < inter[VZ+3])
			memcpy(order, inter[VX+3] < inter[VY+3] ? "\0\2\1" : "\1\0\2", 3);
		else
			memcpy(order, inter[VX+3] < inter[VZ+3] ? "\0\2\1" : "\2\0\1", 3);

		if (inter[order[1]+3] > inter[order[2]+3])
			i = order[1], order[1] = order[2], order[2] = i;

		/* which axis has been intersected */
		for (i = 0; i < 3; i ++)
		{
			int8_t side = -1;
			switch (order[i]) {
			case VX:
				if (fabsf(inter[VX] - minMax[VX]) < EPSILON)                 side = SIDE_WEST; else
				if (fabsf(inter[VX] + inter[VX+3] - minMax[VX+3]) < EPSILON) side = SIDE_EAST;
				break;
			case VY:
				if (fabsf(inter[VY] - minMax[VY]) < EPSILON)                 side = SIDE_BOTTOM; else
				if (fabsf(inter[VY] + inter[VY+3] - minMax[VY+3]) < EPSILON) side = SIDE_TOP;
				break;
			case VZ:
				if (fabsf(inter[VZ] - minMax[VZ]) < EPSILON)                 side = SIDE_NORTH; else
				if (fabsf(inter[VZ] + inter[VZ+3] - minMax[VZ+3]) < EPSILON) side = SIDE_SOUTH;
			}
			if (side >= 0 && hasEnoughGapBetween(*iter, opp[side], bbox, minMax))
				return side;
		}
	}
	return -1;
}

static Bool validAxis(struct BlockIter_t iter, VTXBBox bbox, float minMax[6], int axis, int count)
{
	float inter[6];
	while (count > 0)
	{
		mapIter(&iter, 0, 1, 0);
		uint8_t side = intersectBBox(&iter, bbox, minMax, inter);
		if (side >= 0 && side2axis[side] != axis)
			return False;
		count --;
	}
	return True;
}

/* try to move bounding box <bbox> from <start> to <end>, changing end if movement is blocked */
void physicsCheckCollision(Map map, vec4 start, vec4 end, VTXBBox bbox)
{
	struct BlockIter_t iter;
	float  minMax[6];
	int8_t i, j, k;

	for (i = 0; i < 3; i ++)
	{
		minMax[i]   = (bbox->pt1[i] - ORIGINVTX) * (1./BASEVTX) + end[i];
		minMax[3+i] = (bbox->pt2[i] - ORIGINVTX) * (1./BASEVTX) + end[i];
	}

	int8_t dx = (int) minMax[VX+3] - (int) minMax[VX];
	int8_t dy = (int) minMax[VY+3] - (int) minMax[VY];
	int8_t dz = (int) minMax[VZ+3] - (int) minMax[VZ];

	static int count;
	mapInitIter(map, &iter, minMax, False);
	for (i = 0; ; )
	{
		for (j = 0; ; )
		{
			for (k = 0; ; )
			{
				/* check if bbox collides with any block it intersects in voxel space */
				bbox = mapGetBBox(&iter);
				if (bbox)
				{
					float  inter[6];
					int8_t side;

					if (bbox && (side = intersectBBox(&iter, bbox, minMax, inter)) >= 0)
					{
						int8_t axis = side2axis[side];
						float diff = end[axis];
						/* VX and VZ axis: need to check remainder of column */
						if (axis == VY || i == dy || validAxis(iter, bbox, minMax, axis, dy - i))
						{
							diff -= end[axis] = side2area[side] ? inter[axis] + inter[axis+3] + diff - minMax[axis] : inter[axis] - minMax[axis+3] + diff;
							fprintf(stderr, "%d. shifting axis %d by %g\n", count, axis, diff);
							minMax[axis]   -= diff;
							minMax[axis+3] -= diff;
						}
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
	count ++;
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
			VTXBBox bbox = mapGetBBox(&iter);
			if (bbox)
			{
				float inter[6];

				if (bbox && intersectBBox(&iter, bbox, minMax, inter) && inter[4] > EPSILON)
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
