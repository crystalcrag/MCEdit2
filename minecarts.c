/*
 * minecarts.c : manage minecarts (furnace, regular, hopper) entities (physics, movements, collision response).
 *
 * written by T.Pierron, dec 2021
 */

#define ENTITY_IMPL
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <malloc.h>
#include "entities.h"
#include "minecarts.h"
#include "globals.h"

extern int8_t railsNeigbors[]; /* from blockUpdate.c */

static void getRailCoord(float railCoord[6], int data)
{
	int8_t * neighbor = railsNeigbors + data * 8, i;
	memcpy(railCoord + 3, railCoord, 12);
	for (i = 0; i < 2; i ++, neighbor += 4, railCoord += 3)
	{
		switch (neighbor[3]) {
		case SIDE_SOUTH: railCoord[VZ] += 0.5f; break;
		case SIDE_EAST:  railCoord[VX] += 0.5f; break;
		case SIDE_NORTH: railCoord[VZ] -= 0.5f; break;
		case SIDE_WEST:  railCoord[VX] -= 0.5f;
		}
		if (neighbor[VY])
			railCoord[VY] ++;
	}
}

static Bool minecartGetNextCoord(vec4 start, float dest[3], struct BlockIter_t iter, float dist, int opposite)
{
	uint8_t oldDir = 255;
	float   cosa = cosf(dest[0]);
	float   sina = sinf(dest[0]);
	memcpy(dest, start, 12);
	for (;;)
	{
		int blockId = getBlockId(&iter);
		if (blockIds[blockId >> 4].special != BLOCK_RAILS)
		{
			/* can be one block below */
			mapIter(&iter, 0, -1, 0);
			int check = getBlockId(&iter);
			if (blockIds[check >> 4].special != BLOCK_RAILS)
			{
				/* no rails below, get back then */
				mapIter(&iter, 0, 1, 0);
				goto skip_check;
			}
			else blockId = check;
		}
		uint8_t data = blockId & 15;
		if ((blockId >> 4) != RSRAILS)
			/* data & 8 is powered state for other type of rails: don't care here */
			data &= 7;

		vec4 next = {iter.ref->X + iter.x + 0.5f, iter.yabs + RAILS_THICKNESS, iter.ref->Z + iter.z + 0.5f};
		int8_t * neighbor = railsNeigbors + data * 8;
		if (oldDir == 255)
		{
			float railCoord[6];
			memcpy(railCoord, next, 12);
			getRailCoord(railCoord, data);
			/* advance in the direction of the cart and check which point from rail is closer == direction to go */
			float dx1 = start[VX] + cosa;
			float dz1 = start[VZ] + sina;
			float dx2 = dx1 - railCoord[VX+3];
			float dz2 = dz1 - railCoord[VZ+3];
			dx1 -= railCoord[VX];
			dz1 -= railCoord[VZ];
			if (opposite ^ (dx1 * dx1 + dz1 * dz1 > dx2 * dx2 + dz2 * dz2))
				neighbor += 4;
		}
		else if (neighbor[3] == opp[oldDir]) neighbor += 4;
		oldDir = neighbor[3];

		mapIter(&iter, relx[oldDir], 0, relz[oldDir]);
		switch (oldDir) {
		case SIDE_SOUTH: next[VZ] += 0.5f; break;
		case SIDE_EAST:  next[VX] += 0.5f; break;
		case SIDE_NORTH: next[VZ] -= 0.5f; break;
		case SIDE_WEST:  next[VX] -= 0.5f;
		}
		if (neighbor[VY])
			next[VY] ++, mapIter(&iter, 0, 1, 0);
		/* assumes that entity center is on track */
		float remain = 0;
		switch (data) {
		case RAILS_NS:   remain = fabsf(dest[VZ] - next[VZ]); break;
		case RAILS_EW:   remain = fabsf(dest[VX] - next[VX]); break;
		case RAILS_ASCN:
		case RAILS_ASCS: remain = fabsf(dest[VZ] - next[VZ]) * M_SQRT2f; break;
		case RAILS_CURVED_SE:
		case RAILS_CURVED_SW:
		case RAILS_CURBED_NW:
		case RAILS_CURSET_NE:
		case RAILS_ASCE:
		case RAILS_ASCW: remain = fabsf(dest[VX] - next[VX]) * M_SQRT2f; break;
		skip_check:
		default: /* use current minecart orient */
			if (opposite) dist = -dist;
			dest[VX] += cosa * dist;
			dest[VZ] += sina * dist;
			dest[VY] = iter.yabs;
			return True;
		}
		if (dist < remain)
		{
			dist /= remain;
			dest[VX] += (next[VX] - dest[VX]) * dist;
			dest[VY] += (next[VY] - dest[VY]) * dist;
			dest[VZ] += (next[VZ] - dest[VZ]) * dist;
			break;
		}
		else dist -= remain, memcpy(dest, next, 12);
	}
	return True;
}

/* set entity orient (X and Y) according to rails configuration */
static void minecartSetOrient(Entity entity)
{
	struct BlockIter_t iter;
	mapInitIter(globals.level, &iter, entity->motion, False);

	/* try to locate previous and next rail (will define yaw/pitch of current pos) */
	Block b = &blockIds[iter.blockIds[iter.offset]];
	if (b->id == 0)
	{
		mapIter(&iter, 0, -1, 0);
		b = &blockIds[iter.blockIds[iter.offset]];
		/* no rails under: keep orient, but compute position anyway */
		if (b->special != BLOCK_RAILS)
			mapIter(&iter, 0, 1, 0);;
	}

	float coord[6];
	float dist = 0.5f;

	coord[0] = coord[3] = entity->rotation[0];
	if (minecartGetNextCoord(entity->motion, coord,   iter, dist, 0) &&
	    minecartGetNextCoord(entity->motion, coord+3, iter, dist, 1))
	{
		entity->pos[VX] = (coord[VX] + coord[VX+3]) * 0.5f;
		entity->pos[VY] = (coord[VY] + coord[VY+3]) * 0.5f;
		entity->pos[VZ] = (coord[VZ] + coord[VZ+3]) * 0.5f;
		coord[VX] -= coord[VX+3];
		coord[VZ] -= coord[VZ+3];
		coord[VY] -= coord[VY+3];
		entity->rotation[0] = normAngle(atan2f(coord[VZ], coord[VX]));
		entity->rotation[2] = normAngle(atan2f(coord[VY], sqrtf(coord[VX] * coord[VX] + coord[VZ] * coord[VZ])));

		/* need to offset the minecart by half its height along its normal (from its path: coord - coord+3) */
		coord[VX+3] = cosf(entity->rotation[0]+M_PI_2f);
		coord[VZ+3] = sinf(entity->rotation[0]+M_PI_2f);
		coord[VY+3] = 0;
		vecCrossProduct(coord, coord, coord+3);
		vecNormalize(coord, coord);
		dist = (entity->szy >> 1) * (1.0f/BASEVTX);
		entity->pos[VX] -= dist * coord[VX];
		entity->pos[VY] -= dist * coord[VY];
		entity->pos[VZ] -= dist * coord[VZ];

		//fprintf(stderr, "rotation X = %d\n", (int) (entity->rotation[1] * RAD_TO_DEG));
		//fprintf(stderr, "minecart coord = %g, %g, %g, angle = %d\n", PRINT_COORD(entity->motion),
		//	(int) (entity->rotation[0] * RAD_TO_DEG));
	}
}

static int minecartPushUpTo(vec4 pos, float dest[3], BlockIter iter, float XZ, int dir)
{
	#if 0
	uint8_t axis = dir & 1 ? VX : VZ;
	uint8_t oldDir = 255;
	for (;;)
	{
		int blockId = getBlockId(iter);
		if (blockIds[blockId >> 4].special != BLOCK_RAILS)
		{
			/* can be one block below */
			mapIter(iter, 0, -1, 0);
			blockId = getBlockId(iter);
		}
		uint8_t data = blockId & 15;
		if ((blockId >> 4) != RSRAILS)
			data &= 7; /* powered state for other rails */

		float railPos[6] = {iter->ref->X + iter->x + 0.5f, iter->yabs, iter->ref->Z + iter->z + 0.5f};
		getRailCoord(railPos, data);

		float pos1 = railPos[axis];
		float pos2 = railPos[axis+3];
		if (pos1 > pos2)
		{
			float tmp;
			swap_tmp(pos1, pos2, tmp);
			swap(dirs[0], dirs[1]);
		}

		if (XZ < pos1)
		{
			i = dirs[0];
			if (i == oldDir)
				/* back to where we came from: not good */
				return 0;
			mapIter(iter, relx[i], rely[i], relz[i]);
			oldDir = opp[i];
		}
		else if (XZ > pos2)
		{
			i = dirs[1];
			if (i == oldDir)
				return 0;
			mapIter(iter, relx[i], rely[i], relz[i]);
			oldDir = opp[i];
		}
		else if (fabsf(pos1 - pos2) < EPSILON)
		{
			/* cannot reach */
			return 0;
		}
		else /* can get closer */
		{
			dest[axis] = XZ;
			pos2 -= pos1;
			pos1 = fabsf(pos[axis] - XZ);
			axis = 2 - axis;
			dest[axis] = pos[axis] + (railPos[axis+3] - railPos[axis]) * pos1 / pos2;
			dest[VY] = railPos[VY] + RAILS_THICKNESS;
			return 1;
		}
	}
	#endif
	return 0;
}

int minecartPush(Entity entity, float broad[6])
{
	float scale = ENTITY_SCALE(entity);
	float size[3] = {
		entity->szx * scale, 0,
		entity->szz * scale
	};
	vec pos = entity->motion;

	float inter[] = {
		fminf(pos[VX] + size[VX], broad[VX+3]) - fmaxf(pos[VX] - size[VX], broad[VX]), 0,
		fminf(pos[VZ] + size[VZ], broad[VZ+3]) - fmaxf(pos[VZ] - size[VZ], broad[VZ])
	};

	/* try to push the minecart out of the broad bbox */
	uint8_t axis = inter[0] > inter[1] ? VZ : VX;
	uint8_t push;
	float   dest[3], dist;

	if (pos[axis] + size[axis] < broad[axis+3])
	{
		dist = pos[axis] - inter[axis];
		push = axis == VX ? SIDE_WEST : SIDE_NORTH;
	}
	else if (pos[axis] - size[axis] > broad[axis])
	{
		dist = pos[axis] + inter[axis];
		push = axis == VX ? SIDE_EAST : SIDE_SOUTH;
	}
	else return 0;

	struct BlockIter_t iter;
	mapInitIter(globals.level, &iter, entity->pos, False);
	if (minecartPushUpTo(pos, dest, &iter, dist, push))
	{
		fprintf(stderr, "moving to %g, %g, %g\n", PRINT_COORD(dest));
		float oldPos[3];
		memcpy(oldPos, entity->pos, 12);
		memcpy(entity->motion, dest, 12);
		memcpy(entity->pos, dest, 12);
		minecartSetOrient(entity);
		entityUpdateInfo(entity, oldPos);
		return 1;
	}
	return 0;
}

/* extract info from NBT structure */
int minecartParse(NBTFile file, Entity entity)
{
	entity->enflags |= ENFLAG_TEXENTITES | ENFLAG_HASBBOX | ENFLAG_USEMOTION;
	entity->entype = ENTYPE_MINECART;
	int modelId = entityAddModel(ITEMID(ENTITY_MINECART, 0), 0, NULL, &entity->szx, MODEL_DONT_SWAP);
	/* entity->pos is position on screen, ->motion is where it is on the rail */
	memcpy(entity->motion, entity->pos, sizeof entity->motion);
	/* entity position is at the bottom of minecart */
	minecartSetOrient(entity);
	return modelId;
}

/* create an entity */
static Bool minecartCreate(vec4 pos, STRPTR tech)
{
	NBTFile_t nbt = {.page = 127};
	Entity    entity;
	uint16_t  slot;
	Chunk     c;

	c = mapGetChunk(globals.level, pos);
	if (c == NULL) return False;

	entity = entityAlloc(&slot);
	memcpy(entity->motion, pos, 12);
	memcpy(entity->pos,    pos, 12);

	worldItemCreateGeneric(&nbt, entity, tech);
	NBT_Add(&nbt, TAG_Compound_End);

	entity->next = c->entityList;
	entity->name = NBT_Payload(&nbt, NBT_FindNode(&nbt, 0, "id"));
	c->entityList = slot;

	/* orient minecart according to player orientation */
	entity->rotation[0] = globals.yawPitch[0];
	entity->VBObank = entityGetModelId(entity);
	quadTreeInsertItem(entity);

	entity->tile = nbt.mem;
	/* entity texture bank (for shader) */
	entity->pos[VT] = 2;
	entity->rotation[3] = 1;
	entity->enflags |= ENFLAG_TEXENTITES | ENFLAG_HASBBOX | ENFLAG_USEMOTION;
	entity->enflags &= ~ENFLAG_FULLLIGHT;
	entity->entype = ENTYPE_MINECART;
	entity->chunkRef = c;
	entityGetLight(c, entity->pos, entity->light, False);
	entityAddToCommandList(entity);

	/* flag chunk for saving later */
	entityMarkListAsModified(globals.level, c);
//	renderAddModif();
	return True;
}

/* from https://en.wikipedia.org/wiki/Line%E2%80%93line_intersection */
static void lineIntersect(float points[8])
{
	float t = (points[0] - points[4]) * (points[5] - points[7]) - (points[1] - points[5]) * (points[4] - points[6]);
	float u = (points[0] - points[2]) * (points[5] - points[7]) - (points[1] - points[3]) * (points[4] - points[6]);

	t /= u;

	points[0] += t * (points[2] - points[0]);
	points[1] += t * (points[3] - points[1]);
}

/* user click with a minecart in its hand: check if we can create an entity */
Bool minecartTryUsing(ItemID_t itemId, vec4 pos, int pointToBlock)
{
	Block b = &blockIds[pointToBlock >> 4];

	if (b->special != BLOCK_RAILS)
		return False;

	DATA8 side;
	float lines[8];
	vec   points;
	int   i, data;

	/* click on a rail: find the location where to place minecart */
	memset(lines, 0, sizeof lines);
	lines[0] = lines[2] = (int) pos[VX] + 0.5f;
	lines[1] = lines[3] = (int) pos[VZ] + 0.5f;
	data = pointToBlock & 15;
	if (b->id != RSRAILS) data &= 7;
	for (i = 0, side = railsNeigbors + data * 8 + 3, points = lines; i < 2; i ++, side += 4, points += 2)
	{
		switch (*side) {
		case SIDE_SOUTH: points[1] += 0.5f; break;
		case SIDE_EAST:  points[0] += 0.5f; break;
		case SIDE_NORTH: points[1] -= 0.5f; break;
		case SIDE_WEST:  points[0] -= 0.5f;
		}
	}
	/* <pos> == raypicking intersection with rail */
	lines[4] = pos[VX]; lines[6] = pos[VX] + (lines[1] - lines[3]);
	lines[5] = pos[VZ]; lines[7] = pos[VZ] + (lines[2] - lines[0]);

	/* intersection between ideal rail path and normal (XZ plane only) */
	lineIntersect(lines);

	/* check if there are entities in the way at this location */
	points = lines + 2;
	points[VX] = lines[0] - 0.5f;
	points[VZ] = lines[1] - 0.5f;
	points[VY] = (int) pos[VY];
	points[VX+3] = lines[0] + 0.5f;
	points[VZ+3] = lines[1] + 0.5f;
	points[VY+3] = points[VY]+1;

	quadTreeIntersect(points, &i, 0);
	if (i == 0)
	{
		TEXT techName[32];
		points[VX] = lines[0];
		points[VZ] = lines[1];
		points[VY] += RAILS_THICKNESS; /* sit right on top of rail */
		itemGetTechName(itemId, techName, sizeof techName, False);
		minecartCreate(points, techName);
		return True;
	}
	return False;
}

void minecartPushManual(int entityId, int up)
{
	Entity entity = entityGetById(entityId-1);

	if (entity && entity->entype == ENTYPE_MINECART)
	{
		struct BlockIter_t iter;
		float dest[3];
		mapInitIter(globals.level, &iter, entity->motion, False);
		dest[0] = entity->rotation[0];
		if (minecartGetNextCoord(entity->motion, dest, iter, 0.05, 1-up))
		{
			float oldPos[3];
			memcpy(oldPos, entity->pos, 12);
			memcpy(entity->motion, dest, 12);
			memcpy(entity->pos, dest, 12);
			minecartSetOrient(entity);
			entityUpdateInfo(entity, oldPos);
			fprintf(stderr, "minecart coord = %g, %g, %g angle = %d\n", PRINT_COORD(entity->motion),
				(int) (entity->rotation[0] * RAD_TO_DEG));
		}
	}
}
