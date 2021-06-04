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


/* try to move bounding box <bbox> from <start> to <end>, changing end if movement is blocked */
void physicsCheckCollision(Map map, vec4 start, vec4 end, VTXBBox bbox)
{
	struct BlockIter_t iter;
	float * inter;
	float   bboxes[6 * 20];
	float   minMax[6];
	float   dir[3];
	int8_t  i, j, k, count, corner;

	/* get the corner by which we entered the bounding box */
	vecSub(dir, end, start);
	corner = 0;
	if (dir[VX] < 0) corner |= 1;
	if (dir[VY] < 0) corner |= 2;
	if (dir[VZ] < 0) corner |= 4;
	memset(dir, 0, sizeof dir);

	for (i = 0; i < 3; i ++)
	{
		minMax[i]   = (bbox->pt1[i] - ORIGINVTX) * (1./BASEVTX);
		minMax[3+i] = (bbox->pt2[i] - ORIGINVTX) * (1./BASEVTX);
	}

	int8_t dx = (int) (minMax[VX+3] + end[VX]) - (int) (minMax[VX] + end[VX]);
	int8_t dy = (int) (minMax[VY+3] + end[VY]) - (int) (minMax[VY] + end[VY]);
	int8_t dz = (int) (minMax[VZ+3] + end[VZ]) - (int) (minMax[VZ] + end[VZ]);

	/* first: gather all the bounding boxes we intersect */
	vecAdd(bboxes, end, minMax);
	mapInitIter(map, &iter, bboxes, False);
	for (i = count = 0; ; )
	{
		for (j = 0; ; )
		{
			for (k = 0; ; )
			{
				/* check if entity bbox (minMax) collides with any block in the voxel space */
				int nb;
				bbox = mapGetBBox(&iter, &nb);
				for (inter = bboxes + count * 6; nb > 0; nb --, bbox ++)
				{
					if (! intersectBBox(&iter, bbox, minMax, inter, end))
						continue;
					count ++;
					inter += 6;
					if (count == 20) goto break_all;
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
	break_all:
	for (i = 0, inter = bboxes; i < count; i ++, inter += 6)
	{
		/* order from min to max */
		uint8_t order[3];
		float   size[3];

		vecSub(size, inter+3, inter);
		{
			float dx = fabsf(corner & 1 ? inter[VX+3] - minMax[VX] : inter[VX] - minMax[VX+3]);
			float dz = fabsf(corner & 4 ? inter[VZ+3] - minMax[VZ] : inter[VZ] - minMax[VZ+3]);
			if (size[VX] < size[VZ] && dx > dz) size[VZ] = 0, fprintf(stderr, "cancelling dx\n"); else
			if (size[VZ] < size[VX] && dz > dx) size[VX] = 0, fprintf(stderr, "cancelling dz\n");
		}
		if (size[VY] < size[VZ])
			memcpy(order, size[VX] < size[VY] ? "\0\2\1" : "\1\0\2", 3);
		else
			memcpy(order, size[VX] < size[VZ] ? "\0\2\1" : "\2\0\1", 3);

		if (size[order[1]] > size[order[2]])
			order[1] = order[2];

		for (j = 0; j < 2; j ++)
		{
			/* check if face is visible */
			float * next = bboxes;
			k = 0;
			switch (order[j]) {
			case VX:
				for (; k < count; k ++, next += 6)
					if (k != i && inter[VZ] >= next[VZ]   && inter[VZ+3] <= next[VZ+3]
					           && inter[VY] <  next[VY+3] && inter[VY+3] >  next[VY]) break;
				break;
			case VZ:
				for (; k < count; k ++, next += 6)
					if (k != i && inter[VX] >= next[VX]   && inter[VX+3] <= next[VX+3]
					           && inter[VY] <  next[VY+3] && inter[VY+3] >  next[VY]) break;
				break;
			case VY:
				for (; k < count; k ++, next += 6)
					if (k != i && inter[VX] >= next[VX] && inter[VX+3] <= next[VX+3] &&
					              inter[VZ] >= next[VZ] && inter[VZ+3] <= next[VZ+3]) break;
			}
			if (k == count) break;
		}
		if (j < 2)
		{
			float delta;
			k = order[j];
			if (corner & (1 << k))
			{
				delta = inter[k+3] - minMax[k];
				if (dir[k] < delta) dir[k] = delta;
			}
			else
			{
				delta = inter[k] - minMax[k+3];
				if (dir[k] > delta) dir[k] = delta;
			}
		}
	}
	end[VX] += dir[VX];
	end[VY] += dir[VY];
	end[VZ] += dir[VZ];
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
			int count;
			VTXBBox bbox = mapGetBBox(&iter, &count);
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
