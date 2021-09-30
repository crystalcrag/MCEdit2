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
#include "physics.h"


float physicsSweptAABB(float bboxStart[6], vec4 dir, float block[6], DATA8 normal)
{
	/* check if <block> can be intersected */
	float  invEntry, entry;
	float  invExit,  exit;
	float  entryTime = -1, exitTime = 2;
	int8_t i, axis;

	for (i = axis = 0; i < 3; i ++)
	{
		if (dir[i] == 0)
		{
			if (bboxStart[i+3] <= block[i] || block[i+3] <= bboxStart[i])
			    /* not in the way: can't collide */
				return 2;
			else
				continue;
		}

		if (dir[i] > 0)
			invEntry = block[i]   - bboxStart[i+3],
			invExit  = block[i+3] - bboxStart[i];
		else
			invEntry = block[i+3] - bboxStart[i],
			invExit  = block[i]   - bboxStart[i+3];

		/* time of collision */
		entry = invEntry / dir[i];
		exit  = invExit / dir[i];
		if (entry < 0) continue;
		if (entry > 1) return 2;

		/* entryTime: max of entry[], exitTime: min of exit[] */
		if (entryTime < entry) entryTime = entry, axis = i;
		if (exitTime  > exit)  exitTime  = exit;
	}

	/* no collision with this block */
	if (entryTime > exitTime || entryTime < 0)
		return 2;

	/* finally compute the normal of the collision */
	*normal = axis;

	return entryTime;
}

/* try to move bounding box <bbox> from <start> to <end>, changing end if movement is blocked */
int physicsCheckCollision(Map map, vec4 start, vec4 end, VTXBBox bbox, float autoClimb)
{
	struct BlockIter_t iter;
	float   minMax[6];
	float   elevation;
	float   dir[3];
	float   shortestDist;
	uint8_t curAxis;
	int     ret;
	int8_t  i, j, k;

	for (i = 0; i < 3; i ++)
	{
		minMax[i]   = (bbox->pt1[i] - ORIGINVTX) * (1./BASEVTX);
		minMax[3+i] = (bbox->pt2[i] - ORIGINVTX) * (1./BASEVTX);
	}

	/* compute broad phase box */
	float broad[6] = {
		fminf(start[VX], end[VX]) + minMax[VX],   fminf(start[VY], end[VY]) + minMax[VY],   fminf(start[VZ], end[VZ]) + minMax[VZ],
		fmaxf(start[VX], end[VX]) + minMax[VX+3], fmaxf(start[VY], end[VY]) + minMax[VY+3], fmaxf(start[VZ], end[VZ]) + minMax[VZ+3]
	};
	int8_t dx = (int) broad[VX+3] - (int) broad[VX];
	int8_t dy = (int) broad[VY+3] - (int) broad[VY];
	int8_t dz = (int) broad[VZ+3] - (int) broad[VZ];

	vecSub(dir, end, start);           /* dir = end - start */
	vecAdd(minMax,   minMax,   start); /* minMax += start */
	vecAdd(minMax+3, minMax+3, start); /* minMax+3 += start */
	elevation = 0;
	ret = 0;

	/* first: find the closest box intersected */
	mapInitIter(map, &iter, broad, False);
	for (i = 0, shortestDist = 2, curAxis = 0; ; )
	{
		for (j = 0; ; )
		{
			for (k = 0; ; )
			{
				/* check if entity bbox (minMax) collides with any block in the voxel space */
				int count, cnxFlags;
				VTXBBox blockBBox = mapGetBBox(&iter, &count, &cnxFlags);

				if (blockBBox)
				{
					vec4 rel = {iter.ref->X + iter.x, iter.yabs, iter.ref->Z + iter.z};
					for (; count > 0; count --, blockBBox ++)
					{
						uint8_t idx = blockBBox->flags & 0x7f;
						if (idx > 0 && (cnxFlags & (1 << (idx - 1))) == 0) continue;

						uint8_t axis, m;
						float   bboxFloat[6];
						float   dist;

						for (m = 0; m < 3; m ++)
						{
							bboxFloat[m]   = (blockBBox->pt1[m] - ORIGINVTX) * (1./BASEVTX) + rel[m];
							bboxFloat[3+m] = (blockBBox->pt2[m] - ORIGINVTX) * (1./BASEVTX) + rel[m];
						}
						/* bounding box not intersecting broad rect: ignore bbox */
						if (bboxFloat[VX] >= broad[VX+3] || bboxFloat[VX+3] <= broad[VX] ||
						    bboxFloat[VY] >= broad[VY+3] || bboxFloat[VY+3] <= broad[VY] ||
						    bboxFloat[VZ] >= broad[VZ+3] || bboxFloat[VZ+3] <= broad[VZ])
						    continue;
						dist = physicsSweptAABB(minMax, dir, bboxFloat, &axis);
						if (dist < 1 && elevation < bboxFloat[VY+3])
							elevation = bboxFloat[VY+3];
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
		uint8_t check = 0;
		end[VX] = start[VX] + (broad[VX] = dir[VX] * shortestDist);
		end[VY] = start[VY] + (broad[VY] = dir[VY] * shortestDist);
		end[VZ] = start[VZ] + (broad[VZ] = dir[VZ] * shortestDist);

		/* we might still have some velocity left */
		dir[VX] -= broad[VX];
		dir[VY] -= broad[VY];
		dir[VZ] -= broad[VZ];

		/* check if we can auto-climb the collision */
		if (dir[VY] == 0 && autoClimb > 0 && elevation > minMax[VY] && elevation - minMax[VY] - EPSILON <= autoClimb)
		{
			broad[VY+3] = end[VY];
			end[VY] += elevation - minMax[VY];
			broad[VX+3] = end[curAxis];
			check = 1;
			autoClimb = 0;
		}
		/* axis we collided with, we can't go further in this direction */
		else dir[curAxis] = 0, ret = 1 << curAxis;

		/* repeat this on the velocity left */
		if (fabsf(dir[VX]) > EPSILON ||
		    fabsf(dir[VY]) > EPSILON ||
		    fabsf(dir[VZ]) > EPSILON)
		{
			/* <end> now becomes new start */
			memcpy(minMax, end, 12);
			vecAdd(end, minMax, dir);
			ret |= physicsCheckCollision(map, minMax, end, bbox, autoClimb);
			if (check)
			{
				if (broad[VX+3] == end[curAxis])
				{
					/* failed to auto-climb: cancel movement */
					end[VY] = broad[VY+3];
				}
				else ret |= 2;
			}
		}
	}
	return ret;
}

static Bool intersectBBox(BlockIter iter, VTXBBox bbox, float minMax[6], float inter[6])
{
	float  pt[3] = {iter->ref->X + iter->x, iter->yabs, iter->ref->Z + iter->z};
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

Bool physicsCheckOnGround(Map map, vec4 start, VTXBBox bbox)
{
	struct BlockIter_t iter;
	float  minMax[6];
	int8_t i, j;

	for (i = 0; i < 3; i ++)
	{
		minMax[i]   = (bbox->pt1[i] - ORIGINVTX) * (1./BASEVTX) + start[i];
		minMax[3+i] = (bbox->pt2[i] - ORIGINVTX) * (1./BASEVTX) + start[i];
	}

	int8_t dx = (int) minMax[VX+3] - (int) minMax[VX];
	int8_t dz = (int) minMax[VZ+3] - (int) minMax[VZ];

	minMax[VY] -= 2*EPSILON;

	mapInitIter(map, &iter, minMax, False);
	for (i = 0; ; )
	{
		for (j = 0; ; )
		{
			int count, cnxFlags;
			VTXBBox blockBBox = mapGetBBox(&iter, &count, &cnxFlags);
			for (; count > 0; count --, blockBBox ++)
			{
				float inter[6];
				uint8_t idx = blockBBox->flags & 0x7f;
				if (idx > 0 && (cnxFlags & (1 << (idx - 1))) == 0) continue;

				if (intersectBBox(&iter, blockBBox, minMax, inter) && inter[4] > EPSILON)
					return True;
			}
			j ++;
			if (j > dx) break;
			mapIter(&iter, 1, 0, 0);
		}
		i ++;
		if (i > dz) break;
		mapIter(&iter, -dx, 0, 1);
	}
	return False;
}

void physicsInitEntity(PhysicsEntity entity, int blockId)
{
	float density = blockIds[blockId>>4].density - blockIds[0].density;

	entity->friction[VX] = 0.0001;
	entity->friction[VZ] = 0.0001;
	/* gravity: material heavier than air will "sink", lighter than air will rise */
	entity->friction[VY] = 0.02 * (1/5.) * density;
	/* note: 1/5 because 0.02 was calibrated for stone */
	entity->density = density;

	/* avoid dealing with negative numbers */
	if (entity->dir[VX] < 0) entity->negXZ |= 1, entity->dir[VX] = - entity->dir[VX];
	if (entity->dir[VZ] < 0) entity->negXZ |= 2, entity->dir[VZ] = - entity->dir[VZ];
}

static void physicsChangeEntityDir(PhysicsEntity entity)
{
	float angle = RandRange(0, 2 * M_PI);
	entity->dir[VY] = 0;
	entity->dir[VX] = cosf(angle) * 0.01;
	entity->dir[VZ] = sinf(angle) * 0.01;
	entity->friction[VZ] = -0.001;
	entity->friction[VX] = -0.001;
	if (entity->dir[VX] < 0) entity->negXZ |= 1, entity->dir[VX] = - entity->dir[VX];
	if (entity->dir[VZ] < 0) entity->negXZ |= 2, entity->dir[VZ] = - entity->dir[VZ];
}


/* move particles according to their parameters */
void physicsMoveEntity(Map map, PhysicsEntity entity, float speed)
{
	float inc, oldLoc[3], DY;
	memcpy(oldLoc, entity->loc, sizeof oldLoc);

	inc = entity->dir[VX] * speed; entity->loc[VX] += entity->negXZ & 1 ? -inc : inc;
	inc = entity->dir[VZ] * speed; entity->loc[VZ] += entity->negXZ & 2 ? -inc : inc;
	entity->loc[VY] += DY = entity->dir[VY] * speed;

	/* that's why we don't want to deal with negative values in <dir> */
	entity->dir[VX] -= entity->friction[VX] * speed; if (entity->dir[VX] < 0) entity->dir[VX] = 0;
	entity->dir[VZ] -= entity->friction[VZ] * speed; if (entity->dir[VZ] < 0) entity->dir[VZ] = 0;
	entity->dir[VY] -= entity->friction[VY] * speed;

	if (entity->VYblocked)
	{
		/* increase friction if sliding on ground */
		entity->friction[VX] += 0.0005 * speed;
		entity->friction[VZ] += 0.0005 * speed;
	}
	else entity->friction[VY] += 0.003 * speed * entity->density;

	/* check collision */
	int axis = physicsCheckCollision(map, oldLoc, entity->loc, entity->bbox, False);

	if (axis & 2)
	{
		if (! entity->VYblocked)
		{
			entity->VYblocked = 1;
			if (DY > 0)
			{
				/* hit a ceiling */
				if (entity->density > blockIds[0].density)
					/* make it fall down */
					entity->friction[VY] = 0.02;
				else /* find a hole in the ceiling */
					physicsChangeEntityDir(entity);
			}
			else /* hit the ground */
			{
				entity->dir[VY] = -0.1 * speed;
				entity->friction[VY] = 0.02 * (1/5.) * (entity->density - blockIds[0].density);
			}
		}
	}
	else entity->VYblocked = 0;

	#if 0
	vec4 pos = {floorf(p->loc[VX]), floorf(p->loc[VY]), floorf(p->loc[VZ])};
	if (pos[VX] != old[VX] ||
		pos[VY] != old[VY] ||
		pos[VZ] != old[VZ])
	{
		uint8_t light;
		Block b = blockIds + particlesGetBlockInfo(map, pos, &light);
		if (! (b->type == SOLID || b->type == TRANS || b->type == CUST) || b->bboxPlayer == BBOX_NONE)
		{
			p->light = light;
			if (! p->onGround)
			{
				/* check if block above has changed */
				pos[VY] += 1;
				Block b = blockIds + particlesGetBlockInfo(map, pos, &light);
				if (p->dir[VY] >= 0)
				if (! (b->type == SOLID || b->type == TRANS || b->type == CUST) || b->bboxPlayer == BBOX_NONE)
					p->brake[VX] = p->brake[VZ] = 0.001, p->dir[VY] = 0.02;
			}
			else p->brake[VY] = 0.02;
			continue;
		}
		/* inside a solid block: light will be 0 */

		if (pos[VX] != old[VX]) p->dir[VX] = p->brake[VX] = 0, p->loc[VX] = buf[-5]; else
		if (pos[VZ] != old[VZ]) p->dir[VZ] = p->brake[VZ] = 0, p->loc[VZ] = buf[-3]; else
		if (pos[VY] >  old[VY])
		{
			/* hit a ceiling */
			p->dir[VY] = 0;
			p->loc[VY] = buf[-4];
			if (type != PARTICLE_SMOKE)
				p->brake[VY] = 0.02;
			else
				particleChangeDir(p);
		}
		else if (pos[VY] < old[VY])
		{
			/* hit the ground */
			p->dir[VY] = 0;
			p->loc[VY] = buf[-4]; // = pos[VY]+0.95;
			p->brake[VY] = 0;
			p->onGround = 1;
		}
	}
	#endif
}
