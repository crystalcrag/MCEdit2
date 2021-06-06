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
				int nb, cnxFlags;
				bbox = mapGetBBox(&iter, &nb, &cnxFlags);
				for (inter = bboxes + count * 6; nb > 0; nb --, bbox ++)
				{
					uint8_t idx = bbox->flags & 0x7f;
					if (idx > 0 && (cnxFlags & (1 << (idx - 1))) == 0) continue;
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

	/* first check if we can move the bounding box up (ie: climb stairs, half slab, ...) */
	if (fabsf(dir[VY]) < EPSILON)
	{
		float dy = 0;
		for (i = 0, inter = bboxes; i < count; i ++, inter += 6)
		{
			/* entity can passively walk up 0.5 blocks */
			float diff = inter[VY+3] - minMax[VY];
			if (diff > 0.5) break;
			if (dy < diff) dy = diff;
		}
		if (i < count)
		{
			/* yes, check if there are enough vertical space */
			end[VY] += dy;
		}
	}

	for (i = 0, inter = bboxes; i < count; i ++, inter += 6)
	{
		/* order from min to max */
		float size[3];

		vecSub(size, inter+3, inter);
		{
			float dx = fabsf(corner & 1 ? inter[VX+3] - minMax[VX] : inter[VX] - minMax[VX+3]);
			float dy = fabsf(corner & 2 ? inter[VY+3] - minMax[VY] : inter[VY] - minMax[VY+3]);
			float dz = fabsf(corner & 4 ? inter[VZ+3] - minMax[VZ] : inter[VZ] - minMax[VZ+3]);
			if ((size[VY] < size[VZ] && dz > dy) ||
			    (size[VY] < size[VX] && dx > dy)) j = VY; else
			if (size[VX] <= size[VZ] && dx > dz)  j = VZ; else
			if (size[VZ] <= size[VX] && dz > dx)  j = VX; else
			if (size[VY] < size[VZ]) j = size[VX] < size[VY] ? VX : VY;
			else j = size[VX] < size[VZ] ? VX : VZ;
		}

		/* check if face is visible */
		float * next = bboxes;
		k = 0;
		switch (j) {
		case VX:
			for (; k < count; k ++, next += 6)
				if (k != i && inter[VZ] >= next[VZ]   && inter[VZ+3] <= next[VZ+3] && (corner & 1 ? next[VX] >= inter[VX] : next[VX+3] <= inter[VX+3])
						   && inter[VY] <  next[VY+3] && inter[VY+3] >  next[VY]) break;
			break;
		case VZ:
			for (; k < count; k ++, next += 6)
				if (k != i && inter[VX] >= next[VX]   && inter[VX+3] <= next[VX+3] && (corner & 4 ? next[VZ] >= inter[VZ] : next[VZ+3] <= inter[VZ+3])
						   && inter[VY] <  next[VY+3] && inter[VY+3] >  next[VY]) break;
			break;
		case VY:
			for (; k < count; k ++, next += 6)
				if (k != i && inter[VX] >= next[VX] && inter[VX+3] <= next[VX+3] &&
							  inter[VZ] >= next[VZ] && inter[VZ+3] <= next[VZ+3]) break;
		}
		if (k == count)
		{
			float delta;
			if (corner & (1 << j))
			{
				delta = inter[j+3] - minMax[j];
				if (fabsf(delta) <= size[j] && dir[j] < delta) dir[j] = delta;
			}
			else
			{
				delta = inter[j] - minMax[j+3];
				if (fabsf(delta) <= size[j] && dir[j] > delta) dir[j] = delta;
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
