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
	return inter[0] < inter[3] && inter[1] < inter[4] && inter[2] < inter[5];
}

int sortBoxes(const void * item1, const void * item2)
{
	DATA16 box1 = (DATA16) item1;
	DATA16 box2 = (DATA16) item2;
	int    diff;
	diff = (box1[2] & 3) < 2 ? box1[0] - box2[0] : box2[0] - box1[0];
	if (diff == 0)
		diff = box1[2] & 1 ? box2[1] - box1[1] : box1[1] - box2[1];
	return diff;
}

/* try to move bounding box <bbox> from <start> to <end>, changing end if movement is blocked */
void physicsCheckCollision(Map map, vec4 start, vec4 end, VTXBBox bbox)
{
	struct BlockIter_t iter;
	float    bboxes[6 * 20];
	uint16_t order[3 * 20];
	float    minMax[6];
	float    normx, normz;
	int8_t   i, j, k;
	int8_t   count, corner;

	/* get the corner by which we entered the bounding box */
	vecSub(minMax, end, start);
	corner = 0;
	if (minMax[VX] < 0) corner |= 1;
	if (minMax[VZ] < 0) corner |= 2;

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
	normx = 65534 / (minMax[VX+3] - minMax[VX]);
	normz = 65534 / (minMax[VZ+3] - minMax[VZ]);
	for (i = 0, count = 0; ; )
	{
		for (j = 0; ; )
		{
			for (k = 0; ; )
			{
				/* check if bbox collides with any block it intersects in voxel space */
				bbox = mapGetBBox(&iter);
				float * inter;
				if (bbox && intersectBBox(&iter, bbox, minMax, inter = bboxes + count * 6, end))
				{
					/* sort insert XXX depend on dir for <inter> */
					DATA16 p = order + count * 3;
					p[0] = corner < 2 ? lroundf((inter[VZ] - minMax[VZ]) * normz) :
					                    lroundf((inter[VZ+3] - minMax[VZ]) * normz);
					p[1] = corner & 1 ? lroundf((inter[VX+3] - minMax[VZ]) * normx) :
					                    lroundf((inter[VX] - minMax[VX]) * normx);
					p[2] = corner | (count << 2);
					count ++;
					//fprintf(stderr, "intersection at %d,%d,%d: %d\n", iter.ref->X + iter.x, iter.yabs, iter.ref->Z + iter.z, iter.blockIds[iter.offset]);
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
	if (count > 0)
	{
		/* try to maximize the area */
		float * inter = NULL;
		float   area1 = 0;
		float   box[6]; /* X1, Z1, X2, Z2 */
		float   size[3];
		box[VX] = box[VX+3] = corner & 1 ? minMax[VX+3] : minMax[VX];
		box[VZ] = box[VZ+3] = corner & 2 ? minMax[VZ+3] : minMax[VZ];
		box[VY] = box[VY+3] = 0;
		vecSub(size, minMax+3, minMax);
		qsort(order, count, 6, sortBoxes);

		/* WAY too annoying to generalize */
		switch (corner) {
		case 0: /* xmin - zmin */
		case 2: /* xmin - zmax */
			/* first box one is special */
			box[VX+3] = box[VX] + size[VX];
			for (i = 0, j = 2; i < count; i ++, j += 3)
			{
				float * next;
				inter = bboxes + (order[j] >> 2) * 6;

				if (inter[VX] >= box[VX+3]) continue;
				area1 = (box[VX+3] - box[VX]) * (corner == 0 ? (box[VZ+3] = inter[VZ]) - box[VZ] : box[VZ] - (box[VZ+3] = inter[VZ+3]));
				/* find next stop */
				for (i ++, j += 3; i <= count; i ++, j += 3)
				{
					next = i == count ? minMax : bboxes + (order[j] >> 2) * 6;
					if (next[VX] >= inter[VX]) continue;
					float area2 = (inter[VX] - box[VX]) * (corner == 0 ? next[VZ] - box[VZ] : box[VZ] - next[VZ+3]);
					if (area2 > area1)
					{
						box[VX+3] = inter[VX];
						box[VZ+3] = corner == 0 ? next[VZ] : next[VZ+3];
						inter = NULL;
					}
					break;
				}
			}
			if (inter && (inter[VX] - box[VX]) * size[VZ] > area1)
				box[VX+3] = inter[VX], box[VZ+3] = corner == 0 ? size[VZ] + box[VZ] : box[VZ] - size[VZ];
			break;

		case 1: /* xmax - zmin */
		case 3: /* xmax - zmax */
			box[VX+3] = box[VX] - size[VX];
			for (i = 0, j = 2; i < count; i ++, j += 3)
			{
				float * next;
				inter = bboxes + (order[j] >> 2) * 6;

				if (inter[VX+3] <= box[VX+3]) continue;
				area1 = (box[VX] - box[VX+3]) * (corner == 1 ? (box[VZ+3] = inter[VZ]) - box[VZ] : box[VZ] - (box[VZ+3] = inter[VZ+3]));
				/* find next stop */
				for (i ++, j += 3; i < count; i ++, j += 3)
				{
					next = bboxes + (order[j] >> 2) * 6;
					if (next[VX] <= inter[VX]) continue;
					float area2 = (box[VX] - inter[VX+3]) * (corner == 1 ? next[VZ] - box[VZ] : box[VZ] - next[VZ+3]);
					if (area2 > area1)
					{
						box[VX+3] = inter[VX+3];
						box[VZ+3] = corner == 0 ? next[VZ] : next[VZ+3];
						inter = NULL;
					}
					break;
				}
			}
			if (inter && (box[VX] - inter[VX+3]) * size[VZ] > area1)
				box[VX+3] = inter[VX+3], box[VZ+3] = corner == 1 ? box[VZ] + size[VZ] : box[VZ] - size[VZ];
		}
		/* now we can correct <end> to cancel collision */
		end[VX] += corner & 1 ? box[VX+3] - minMax[VX] : box[VX+3] - minMax[VX+3];
		end[VZ] += corner & 2 ? box[VZ+3] - minMax[VZ] : box[VZ+3] - minMax[VZ+3];
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
			VTXBBox bbox = mapGetBBox(&iter);
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
