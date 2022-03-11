/*
 * physics.c: simulate physics (collision and movement) for entities
 *
 * collision is implemented with a swept AABB, and a sliding correction, inspired by:
 * https://www.gamedev.net/tutorials/programming/general-and-gameplay-programming/swept-aabb-collision-detection-and-response-r3084/
 *
 * written by T.Pierron, june 2021.
 */

#define ENTITY_IMPL
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <malloc.h>
#include "entities.h"
#include "physics.h"
#include "player.h"
#include "minecarts.h"
#include "mapUpdate.h"
#include "globals.h"


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

		/* entities pushing each other at non exact floating point position will cause weird values for invEntry */
		if (fabsf(invEntry) < 0.001f) invEntry = 0;
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

	return entryTime < EPSILON ? 0 : entryTime;
}

/*
 * try to move bounding box <bbox> from <start> to <end>, changing end if movement is blocked
 * returns a bitfield (1 << (VX|VY|VZ)) of sides blocking movement.
 */
int physicsCheckCollision(Map map, vec4 start, vec4 end, ENTBBox bbox, float autoClimb)
{
	static uint8_t priority[] = {1, 0, 2};
	struct BlockIter_t iter;
	float   minMax[6];
	float   elevation;
	float   dir[3];
	float   shortestDist;
	uint8_t curAxis;
	int     ret;
	int8_t  i, j, k;

	memcpy(minMax,   bbox->pt1, 12);
	memcpy(minMax+3, bbox->pt2, 12);

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
	end[VT] = 1;
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
							bboxFloat[m]   = FROMVERTEX(blockBBox->pt1[m]) + rel[m];
							bboxFloat[3+m] = FROMVERTEX(blockBBox->pt2[m]) + rel[m];
						}
						/* bounding box not intersecting broad rect: ignore bbox */
						if (bboxFloat[VX] >= broad[VX+3] || bboxFloat[VX+3] <= broad[VX] ||
						    bboxFloat[VY] >= broad[VY+3] || bboxFloat[VY+3] <= broad[VY] ||
						    bboxFloat[VZ] >= broad[VZ+3] || bboxFloat[VZ+3] <= broad[VZ])
						    continue;
						dist = physicsSweptAABB(minMax, dir, bboxFloat, &axis);
						if (dist < 1 && elevation < bboxFloat[VY+3])
							elevation = bboxFloat[VY+3];
						if (dist < shortestDist || (dist == 0 && priority[axis] > priority[curAxis]))
							shortestDist = dist, curAxis = axis;
					}
				}

				if (iter.blockIds)
				{
					/* check special physics property of block we are intersecting */
					Block block = &blockIds[iter.blockIds[iter.offset]];
					if (block->viscosity > 0 && end[VT] > block->viscosity)
						end[VT] = block->viscosity;
					/* note: we cannot do a more precise check, because we need final position first */
					if (block->id == 65)
						ret |= INSIDE_LADDER;
					if (block->special == BLOCK_PLATE)
						ret |= INSIDE_PLATE;
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

	/* also need to check for collision with entity */
	if (shortestDist > 0)
	{
		int count;
		Entity * list = quadTreeIntersect(broad, &count, ENFLAG_FIXED | ENFLAG_HASBBOX);
		if (count > 0)
		{
			for (i = 0; i < count; i ++)
			{
				Entity entity = list[i];
				if ((entity->enflags & ENFLAG_FIXED) == 0)
				{
					if (entity->entype == ENTYPE_MINECART && minecartPush(entity, broad, dir))
						/* minecart was pushed out of the way */
						continue;
					else
						fprintf(stderr, "minecart can't move\n");
				}
				float dist = ENTITY_SCALE(entity);
				float SZX = entity->szx * dist;
				float SZY = entity->szy * dist;
				float SZZ = entity->szz * dist;
				float bboxFloat[6] = {
					[0] = entity->pos[VX] - SZX,   [3] = entity->pos[VX] + SZX,
					[1] = entity->pos[VY] - SZY,   [4] = entity->pos[VY] + SZY,
					[2] = entity->pos[VZ] - SZZ,   [5] = entity->pos[VZ] + SZZ
				};
				uint8_t axis;
				dist = physicsSweptAABB(minMax, dir, bboxFloat, &axis);
				if (dist < 1 && elevation < bboxFloat[VY+3])
					elevation = bboxFloat[VY+3];
				if (dist < shortestDist || (dist == 0 && priority[axis] > priority[curAxis]))
					shortestDist = dist, curAxis = axis;
			}
		}
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
		else dir[curAxis] = 0, ret |= 1 << curAxis;

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
		float boxmin = FROMVERTEX(bbox->pt1[i]) + pt[i];
		float boxmax = FROMVERTEX(bbox->pt2[i]) + pt[i];

		inter[i]   = boxmin > minMax[i]   ? boxmin : minMax[i];
		inter[3+i] = boxmax < minMax[3+i] ? boxmax : minMax[3+i];
	}
	return inter[VX] < inter[VX+3] && inter[VY] < inter[VY+3] && inter[VZ] < inter[VZ+3];
}

/* check if there are any bounding boxes that prevent player from falling */
Bool physicsCheckOnGround(Map map, vec4 start, ENTBBox bbox)
{
	struct BlockIter_t iter;
	float  minMax[6];
	int8_t i, j;

	for (i = 0; i < 3; i ++)
	{
		minMax[i]   = bbox->pt1[i] + start[i] + EPSILON;
		minMax[3+i] = bbox->pt2[i] + start[i] - EPSILON;
	}

	int8_t dx = (int) minMax[VX+3] - (int) minMax[VX];
	int8_t dz = (int) minMax[VZ+3] - (int) minMax[VZ];

	minMax[VY] -= 3*EPSILON;

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
	int count;
	minMax[VY+3] = minMax[VY] + 0.1f;
	quadTreeIntersect(minMax, &count, ENFLAG_HASBBOX | ENFLAG_FIXED);
	if (count > 0) return True;
	return False;
}

/* physicsCheckCollision detected we are near a ladder, check if we can climb it */
int physicsCheckIfCanClimb(Map map, vec4 pos, ENTBBox bbox)
{
	int8_t i, j, k, ladder;
	float  broad[6];
	for (i = 0; i < 3; i ++)
	{
		broad[i]   = pos[i] + bbox->pt1[i];
		broad[3+i] = pos[i] + bbox->pt2[i];
	}

	int8_t dx = (int) broad[VX+3] - (int) broad[VX];
	int8_t dy = (int) broad[VY+3] - (int) broad[VY];
	int8_t dz = (int) broad[VZ+3] - (int) broad[VZ];

	struct BlockIter_t iter;
	mapInitIter(map, &iter, broad, False);
	for (i = ladder = 0; ; )
	{
		next_layer:
		for (j = 0; ; )
		{
			for (k = 0; ; )
			{
				int id = getBlockId(&iter);
				if ((id >> 4) != 65) goto bail;

				static uint8_t enlargeAxis[] = {5, 8, 2, 5, 0, 3, 8, 8};
				VTXBBox box = blockGetBBox(blockGetById(id));
				uint8_t axis = enlargeAxis[id&7];

				if (axis >= 6) goto bail;

				vec4 rel = {iter.ref->X + iter.x, iter.yabs, iter.ref->Z + iter.z};
				float bboxFloat[6];
				for (id = 0; id < 3; id ++)
				{
					bboxFloat[id]   = rel[id] + FROMVERTEX(box->pt1[id]);
					bboxFloat[3+id] = rel[id] + FROMVERTEX(box->pt2[id]);
				}
				/* bbox of "active" ladder will be half of a full block (ie: vertical slab) */
				if (axis < 3)
					bboxFloat[axis] -= (bboxFloat[axis+3] - bboxFloat[axis]) * 7;
				else
					bboxFloat[axis] += (bboxFloat[axis] - bboxFloat[axis-3]) * 7;
				/* check if intersecting entity bbox */
				if (bboxFloat[VX] >= broad[VX+3] || bboxFloat[VX+3] <= broad[VX] ||
					bboxFloat[VY] >= broad[VY+3] || bboxFloat[VY+3] <= broad[VY] ||
					bboxFloat[VZ] >= broad[VZ+3] || bboxFloat[VZ+3] <= broad[VZ])
				{
					bail:
					k ++;
					if (k > dx) break;
					mapIter(&iter, 1, 0, 0);
				}
				else
				{
					/* one ladder per Y layer within entity bbox is all we need */
					ladder = (ladder << 1) | 1;
					i ++;
					if (i > dy) goto break_all;
					mapIter(&iter, -k, 1, -j);
					goto next_layer;
				}
			}
			j ++;
			if (j > dz) break;
			mapIter(&iter, -dx, 0, 1);
		}
		i ++;
		if (i > dy) break;
		mapIter(&iter, -dx, 1, -dz);
	}
	break_all:
	/* check if (ladder+1) is a power of 2: this means there are no gaps in the ladder column */
	return ladder > 0 && ((ladder + 1) & ladder) == 0;
}

/* player might have activated/exited a pressure plate */
void physicsCheckPressurePlate(Map map, vec4 start, vec4 end, ENTBBox bbox)
{
	int8_t i, j, k;
	float  entityBBox[6];
	float  broad[6];

	memcpy(entityBBox,   bbox->pt1, 12);
	memcpy(entityBBox+3, bbox->pt2, 12);

	for (i = 0; i < 3; i ++)
	{
		broad[i]   = fminf(start[i], end[i]) + entityBBox[i];
		broad[3+i] = fmaxf(start[i], end[i]) + entityBBox[i+3];
	}

	int8_t dx = (int) broad[VX+3] - (int) broad[VX];
	int8_t dy = (int) broad[VY+3] - (int) broad[VY];
	int8_t dz = (int) broad[VZ+3] - (int) broad[VZ];

	vecAdd(entityBBox,   entityBBox,   end);
	vecAdd(entityBBox+3, entityBBox+3, end);

	/* scan all pressure plates intersected */
	struct BlockIter_t iter;
	mapInitIter(map, &iter, broad, False);
	for (i = 0; ; )
	{
		for (j = 0; ; )
		{
			for (k = 0; ; )
			{
				Block b = &blockIds[iter.blockIds[iter.offset]];
				if (b->special == BLOCK_PLATE)
					mapUpdatePressurePlate(&iter, entityBBox);
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

void physicsInitEntity(PhysicsEntity entity, int blockId)
{
	float density = blockIds[isBlockId(blockId) ? (blockId>>4) : 1].density - blockIds[0].density;

	entity->friction[VX] = 0.0001;
	entity->friction[VZ] = 0.0001;
	/* gravity: material heavier than air will "sink", lighter than air will rise */
	entity->friction[VY] = 0.02f * (1/5.f) * density;
	/* note: 1/5 because 0.02 was calibrated for stone */
	entity->density = density;

	/* avoid dealing with negative numbers */
	if (entity->dir[VX] < 0) entity->negXZ |= 1, entity->dir[VX] = - entity->dir[VX];
	if (entity->dir[VZ] < 0) entity->negXZ |= 2, entity->dir[VZ] = - entity->dir[VZ];
}

void physicsChangeEntityDir(PhysicsEntity entity, float friction)
{
	float angle = RandRange(0, 2 * M_PI);
	entity->dir[VY] = 0;
	entity->dir[VX] = cosf(angle) * 0.01f;
	entity->dir[VZ] = sinf(angle) * 0.01f;
	entity->friction[VZ] = friction;
	entity->friction[VX] = friction;
	entity->negXZ = 0;
	if (entity->dir[VX] < 0) entity->negXZ |= 1, entity->dir[VX] = - entity->dir[VX];
	if (entity->dir[VZ] < 0) entity->negXZ |= 2, entity->dir[VZ] = - entity->dir[VZ];
}

void physicsShoveEntity(PhysicsEntity entity, float friction, int side)
{
	if (side <= SIDE_WEST)
	{
		static float offset[] = {M_PI_2f, 0, M_PIf+M_PI_2f, M_PIf};
		float angle = RandRange(-M_PIf/8, M_PIf/8) + offset[side];
		entity->dir[VX] = cosf(angle) * 0.1f;
		entity->dir[VZ] = sinf(angle) * 0.1f;
		entity->friction[VZ] = friction;
		entity->friction[VX] = friction;
		fprintf(stderr, "angle = %d\n", (int) (angle * 180 / M_PIf));
		entity->negXZ = 0;
		if (entity->dir[VX] < 0) entity->negXZ |= 1, entity->dir[VX] = - entity->dir[VX];
		if (entity->dir[VZ] < 0) entity->negXZ |= 2, entity->dir[VZ] = - entity->dir[VZ];
	}
}

/* move particles according to their parameters */
Bool physicsMoveEntity(Map map, PhysicsEntity entity, float speed)
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
		entity->friction[VX] += 0.0005f * speed;
		entity->friction[VZ] += 0.0005f * speed;
	}
	else entity->friction[VY] += 0.003f * speed * entity->density;

	/* check collision */
	int axis = physicsCheckCollision(map, oldLoc, entity->loc, entity->bbox, 0);

	if (axis & 2)
	{
		if (entity->rebound == 255)
		{
			entity->VYblocked = 1;
			entity->rebound = 0;
			entity->dir[VY] = 0;
		}
		else if (entity->rebound)
		{
			float dir = -entity->dir[VY] * RandRange(0.3f, 0.4f) / entity->rebound;
			physicsChangeEntityDir(entity, 0.0001);
			entity->dir[VX] *= 2;
			entity->dir[VZ] *= 2;
			entity->dir[VY] = dir;
			entity->rebound = 255;
			entity->friction[VX] *= 2;
			entity->friction[VZ] *= 2;
			entity->friction[VY] *= 2;
		}
		else if (! entity->VYblocked)
		{
			entity->VYblocked = 1;
			if (DY > 0)
			{
				/* hit a ceiling */
				if (entity->density > blockIds[0].density)
					/* make it fall down */
					entity->friction[VY] = 0.02;
				else /* find a hole in the ceiling */
					physicsChangeEntityDir(entity, -0.001);
			}
			else /* hit the ground */
			{
				entity->dir[VY] = 0;
				entity->friction[VY] = 0;
				entity->friction[VX] *= 2;
				entity->friction[VZ] *= 2;
			}
		}
	}
	else entity->VYblocked = 0;

	return floorf(oldLoc[VX]) != floorf(entity->loc[VX]) ||
	       floorf(oldLoc[VY]) != floorf(entity->loc[VY]) ||
	       floorf(oldLoc[VZ]) != floorf(entity->loc[VZ]);
}

/*
 * entity moved: check if other entities must be moved along
 */

static Bool physicsPushEntity(float broad[6], vec4 pos, float size[3], char dir[3], PhysicsEntity phys)
{
	if (fminf(pos[VX] + size[VX], broad[VX+3]) - fmaxf(pos[VX] - size[VX], broad[VX]) < EPSILON ||
	    fminf(pos[VY] + size[VY], broad[VY+3]) - fmaxf(pos[VY] - size[VY], broad[VY]) < EPSILON ||
	    fminf(pos[VZ] + size[VZ], broad[VZ+3]) - fmaxf(pos[VZ] - size[VZ], broad[VZ]) < EPSILON)
		/* does not intersect */
	    return False;

	float endPos[3];
	uint8_t axis = 0;

	if (dir[VY] == 0) endPos[VY] = pos[VY]; else
	if ((broad[VY] + broad[VY+3]) * 0.5f < pos[VY]) endPos[VY] = broad[VY+3] + size[VY], axis = 2;
	else endPos[VY] = broad[VY] - size[VY], axis = 2;

	if (dir[VX] < 0) endPos[VX] = broad[VX]   - size[VX], axis |= 1; else
	if (dir[VX] > 0) endPos[VX] = broad[VX+3] + size[VX], axis |= 1; else endPos[VX] = pos[VX];
	if (dir[VZ] < 0) endPos[VZ] = broad[VZ]   - size[VZ], axis |= 4; else
	if (dir[VZ] > 0) endPos[VZ] = broad[VZ+3] + size[VZ], axis |= 4; else endPos[VZ] = pos[VZ];

	//fprintf(stderr, "pushing entity from %g to %g\n", (double) pos[VY], (double) endPos[VY]);

	//physicsCheckCollision(map, pos, endPos, bbox, 0.5);

	/* entity is already moving due to external forces: cancels its movement in the direction it is pushed */
	if (phys)
	{
		float force;
		memcpy(phys->loc, endPos, 12);
		if (axis & 1) /* X */
		{
			force = endPos[VX] - pos[VX];
			if (fabsf(force) > phys->dir[VX])
			{
				if (force < 0) phys->negXZ |= 1, force = -force;
				else phys->negXZ &= ~1;
				phys->dir[VX] = force;
				phys->friction[VX] = 0.01;
			}
		}
		if (axis & 4) /* Z */
		{
			force = endPos[VZ] - pos[VZ];
			if (fabsf(force) > phys->dir[VZ])
			{
				if (force < 0) phys->negXZ |= 2, force = -force;
				else phys->negXZ &= ~2;
				phys->dir[VZ] = force;
				phys->friction[VZ] = 0.01;
			}
		}
		if (axis & 2)
		{
			phys->dir[VY] = endPos[VZ] - pos[VZ];
			phys->friction[VY] = 0.004;
		}
	}
	memcpy(pos, endPos, 12);

	return True;
}

void physicsEntityMoved(Map map, APTR self, vec4 start, vec4 end)
{
	float broad[6];
	float size[3];
	char  dir[3];
	int   i, count;

	{
		Entity entity = self;
		float scale = ENTITY_SCALE(entity);
		size[VX] = entity->szx * scale;
		size[VY] = entity->szy * scale;
		size[VZ] = entity->szz * scale;
	}

	/* compute broad phase box */
	for (i = 0; i < 3; i ++)
	{
		/* <start> and <end> are the center of entity: bbox must be centered */
		broad[i]   = fminf(start[i], end[i]) - size[i];
		broad[3+i] = fmaxf(start[i], end[i]) + size[i];
		float diff = end[i] - start[i];
		dir[i] = diff < -EPSILON ? -1 : diff > EPSILON ? 1 : 0;
	}
	/* add a tiny amount on VY to check if there are entities that sit on top this one */
	broad[VY+3] += 0.0625f;

	Entity * list = quadTreeIntersect(broad, &count, ENFLAG_FIXED | ENFLAG_EQUALZERO);
	if (count > 0)
	{
		/* make a copy of the list (return value is static) */
		list = memcpy(alloca(count * sizeof *list), list, count * sizeof *list);
		for (i = 0; i < count; i ++)
		{
			Entity entity = list[i];
			if (entity == self || (entity->enflags & ENFLAG_FIXED)) continue;

			float scale = ENTITY_SCALE(entity);

			if (entity->pos[VY] > broad[VY+3] - 0.0625f)
			{
				/* entity is on top of broad[]: not going to be pushed, but check if it needs to be affected by gravity */
				if (entity->private == NULL && entity->pos[VY] - (entity->szy >> 1) * scale > broad[VY+3] - 0.0625f)
				{
					entityInitMove(entity, SIDE_BOTTOM, 1);
					//fprintf(stderr, "need physics update\n");
				}
				continue;
			}

			float bbox[] = {
				entity->szx * scale,
				entity->szy * scale,
				entity->szz * scale
			};
			float oldPos[3];
			memcpy(oldPos, entity->pos, 12);

			if (physicsPushEntity(broad, entity->pos, bbox, dir, entity->private))
			{
				entityUpdateInfo(entity, oldPos);
			}
		}
	}
	/* check for players XXX needs to be stored in the quadtree */
	Player p;
	for (p = HEAD(map->players); p; NEXT(p))
	{
		float bbox[] = {
			playerBBox.pt2[VX],
			playerBBox.pt2[VY] * 0.5f,
			playerBBox.pt2[VZ]
		};
		float pos[3];
		memcpy(pos, p->pos, sizeof pos);
		/* player pos is at feet level, we need center */
		pos[VY] += bbox[VY];
		if (physicsPushEntity(broad, pos, bbox, dir, NULL))
		{
			/* we'll need to check collision before we can set the new coord */
			memcpy(p->pushedTo, pos, sizeof pos);
			p->pushedTo[VY] -= bbox[VY];
			p->keyvec |= PLAYER_PUSHED;
		}
	}
}
