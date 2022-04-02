/*
 * minecarts.c : manage minecarts entities (physics, movements, collision response).
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
#include "render.h"
#include "minecarts.h"
#include "undoRedo.h"
#include "globals.h"

extern int8_t railsNeigbors[]; /* from blockUpdate.c */

/* physics collision check for minecarts: blocks below rails must not be cheched for collision. */
static int minecartValidateBlocks(struct BlockIter_t iter, int dx, int dy, int dz)
{
	int ret = 0;

	mapIter(&iter, 0, dy, 0);
	int8_t i, j, k, set;
	/* scan on Z axis */
	for (i = 0; ; )
	{
		/* scan on X axis */
		for (j = 0; ; )
		{
			/* scan on Y axis */
			for (k = 0, set = 1; ; )
			{
				if (iter.blockIds)
				{
					Block b = &blockIds[iter.blockIds[iter.offset]];
					if (b->special == BLOCK_RAILS) set = 0;
				}
				if (set)
					/* blocks are scanned Z, X, Y here, but Y, Z, X in physics */
					ret |= 1 << (j + ((dx+1) * (i + k*(dz+1))));
				k ++;
				if (k > dy) break;
				mapIter(&iter, 0, -1, 0);
			}
			j ++;
			if (j > dx) break;
			mapIter(&iter, 0, dy, 1);
		}
		i ++;
		if (i > dz) break;
		mapIter(&iter, -dx, dy, 1);
	}
//	fprintf(stderr, "valid = %x (%d, %d, %d) - ", ret, dx+1, dy+1, dz+1);
	return ret;
}

static struct ENTBBox_t minecartBBoxEW = {
	.pt1 = {-0.6, 0,   -0.45},
	.pt2 = { 0.6, 0.6,  0.45},
	.push = 1
};

static struct ENTBBox_t minecartBBoxNS = {
	.pt1 = {-0.45, 0,   -0.6},
	.pt2 = { 0.45, 0.6,  0.6},
	.push = 1
};

enum
{
	ENFLAG_OFFRAILS    = 0x8000,
	ENFLAG_POWRAIL_ON  = 0x4000,
	ENFLAG_POWRAIL_OFF = 0x2000,
	ENFLAG_DETECTOR    = 0x1000
};

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

/*
 * main function to move minecarts: from <start> try to advance [opposite == 0] / backtrack [opposite > 0]
 * result will be written in <dest>, return value is a combination of ENFLAGS_*
 */
static int minecartGetNextCoord(vec4 start, float dest[3], struct BlockIter_t iter, float dist, int opposite)
{
	uint8_t oldDir = 255;
	float   cosa = cosf(dest[0]);
	float   sina = sinf(dest[0]);
	int     ret  = 0;
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
		switch (blockId >> 4) {
		case RSRAILS: break;
		case RSPOWERRAILS:
			ret |= data & 8 ? ENFLAG_POWRAIL_ON : ENFLAG_POWRAIL_OFF;
			data &= 7;
			break;
		case RSDETECTORRAIL:
			ret |= ENFLAG_DETECTOR;
		default:
			/* data & 8 is powered state for other type of rails: don't care here */
			data &= 7;
		}

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
		case RAILS_CURVED_NW:
		case RAILS_CURVED_NE:
		case RAILS_ASCE:
		case RAILS_ASCW: remain = fabsf(dest[VX] - next[VX]) * M_SQRT2f; break;
		skip_check:
		default: /* use current minecart orient */
			if (opposite) dist = -dist;
			dest[VX] += cosa * dist;
			dest[VZ] += sina * dist;
			dest[VY] = iter.yabs;
			return ret | ENFLAG_OFFRAILS;
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
	return ret;
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
			mapIter(&iter, 0, 1, 0);
	}

	float coord[6];

	coord[0] = coord[3] = entity->rotation[0];
	/* both function call need to be evaluated!! */
	int flags = minecartGetNextCoord(entity->motion, coord,   iter, 0.5f, 0) |
	            minecartGetNextCoord(entity->motion, coord+3, iter, 0.5f, 1);

	/* prevent the bottom of the minecart from scraping the block below when reaching the top of a slope */
	float dist = (coord[VY] + coord[VY+3]) * 0.5f;
	if (dist < entity->motion[VY])
	{
		/* this simple trick will reduce the turn radius when getting on top of a slope */
		coord[0] = coord[3] = entity->rotation[0];
		flags = minecartGetNextCoord(entity->motion, coord,   iter, 0.25f, 0) &
		        minecartGetNextCoord(entity->motion, coord+3, iter, 0.25f, 1);
		dist = (coord[VY] + coord[VY+3]) * 0.5f;
	}

	entity->enflags = (entity->enflags & 0xfff) | flags;
	entity->pos[VX] = (coord[VX] + coord[VX+3]) * 0.5f;
	entity->pos[VZ] = (coord[VZ] + coord[VZ+3]) * 0.5f;
	entity->pos[VY] = dist;

	coord[VX] -= coord[VX+3];
	coord[VZ] -= coord[VZ+3];
	coord[VY] -= coord[VY+3];
	entity->rotation[0] = normAngle(atan2f(coord[VZ], coord[VX]));
	entity->rotation[2] = normAngle(atan2f(coord[VY], sqrtf(coord[VX] * coord[VX] + coord[VZ] * coord[VZ])));

	/* need to offset the minecart by half its height along its normal (from its path: coord - coord+3) */
	coord[VX+3] = cosf(entity->rotation[0]+M_PI_2f);
	coord[VZ+3] = sinf(entity->rotation[0]+M_PI_2f);
	coord[VY+3] = 0;
	vec4 normal;
	vecCrossProduct(normal, coord, coord+3);
	vecNormalize(normal, normal);
	dist = (entity->szy >> 1) * (1.0f/BASEVTX);
	entity->pos[VX] -= dist * normal[VX];
	entity->pos[VY] -= dist * normal[VY];
	entity->pos[VZ] -= dist * normal[VZ];

	//fprintf(stderr, "rotation X = %d\n", (int) (entity->rotation[1] * RAD_TO_DEG));
	//fprintf(stderr, "minecart coord = %g, %g, %g, angle = %d\n", PRINT_COORD(entity->motion),
	//	(int) (entity->rotation[0] * RAD_TO_DEG));
}

static inline float diffAngle(float angle)
{
	if (angle >  M_PIf) angle -= 2*M_PIf;
	if (angle < -M_PIf) angle += 2*M_PIf;
	if (angle < 0) angle = -angle;
	return angle;
}

/* check if player is within possible movement range of minecart */
int minecartPush(Entity entity, float broad[6], vec dir)
{
	if (fabsf(dir[VX]) < EPSILON && fabsf(dir[VZ]) < EPSILON)
		return 0;

	/* these tests are particularly useful if minecart is not axis aligned (S,E,N,W) */
	float moveAngle = atan2f(dir[VZ], dir[VX]);
	if (moveAngle < 0) moveAngle += M_PIf * 2;
	float diff = diffAngle(entity->rotation[0] - moveAngle);
	float max = 0.1;

	//fprintf(stderr, "angle = %d - dir = %g, %g, %g\n", (int) (moveAngle * RAD_TO_DEG), PRINT_COORD(dir));

	if (diff > M_PI_4f)
	{
		/* no, but try opposite direction */
		diff = diffAngle(entity->rotation[0] + M_PIf - moveAngle);
		if (diff > M_PI_4f)
		{
			/* nope, can't move: but if minecart is not on rails, turn it in the direction of movement */
			if ((entity->enflags & ENFLAG_OFFRAILS) == 0)
				return 0;
			else
				max = 0.05f;
		}
	}

	float scale = ENTITY_SCALE(entity);
	float size[3] = {
		entity->szx * scale, 0,
		entity->szz * scale
	};
	vec pos = entity->motion;

	float inter[] = {
		fminf(pos[VX] + size[VX], broad[VX+3]) - fmaxf(pos[VX] - size[VX], broad[VX]),
		fminf(pos[VZ] + size[VZ], broad[VZ+3]) - fmaxf(pos[VZ] - size[VZ], broad[VZ])
	};

	/* try to push the minecart out of the broad bbox */
	uint8_t axis = inter[0] > inter[1] ? VZ : VX;

	if (pos[axis] + size[axis] < broad[axis+3])
	{
		diff = axis == VX ? M_PIf : M_PI_2f + M_PIf;
	}
	else if (pos[axis] - size[axis] > broad[axis])
	{
		diff = axis == VX ? 0 : M_PI_2f;
	}
	else if ((entity->enflags & ENFLAG_OFFRAILS) == 0)
	{
		return 0;
	}
	else max = 0.05f;

	diff = diffAngle(entity->rotation[0] - diff);
	/* moving perpendicular to its path: can't do */
	if (fabsf(diff - M_PI_2f) < EPSILON)
	{
		if ((entity->enflags & ENFLAG_OFFRAILS) == 0)
			return 0;
		else
			max = 0.05f;
	}

	if ((entity->enflags & ENFLAG_INANIM) == 0)
		//fprintf(stderr, "minecart can move\n"),
		entityInitMove(entity, UPDATE_BY_RAILS, 0);

	PhysicsEntity physics = entity->private;

	physics->negXZ = diff < 60 * DEG_TO_RAD ? 0 : 1;
	float dist = 0.05f + physics->dir[0];
	if (entity->enflags & ENFLAG_OFFRAILS) max *= 0.5f;
	if (dist > physics->dir[0])
		physics->dir[0] = dist;
	if (physics->dir[0] > max)
		physics->dir[0] = max;
	physics->dir[1] = moveAngle;
	physics->friction[0] = 0.001;
//	fprintf(stderr, "dir = %g, max = %g\n", physics->dir[0], max);
	return 1;
}

static float moveToAngle(float from, float to, float step)
{
	float diff = to - from;
	if (fabsf(diff) < step)
		return to;

	if (fabsf(diff) >= M_PIf)
		diff = -diff;

	if (diff < 0) return from -= step;
	else          return from += step;
	return normAngle(from);
}

/* minecart is moving, update position and orientation */
Bool minecartUpdate(Entity entity, float deltaFrame)
{
	struct BlockIter_t iter;
	float dest[3];
	PhysicsEntity physics = entity->private;

	/* major lag spike :-/ */
	if (deltaFrame > 1) deltaFrame = 1;

	mapInitIter(globals.level, &iter, entity->motion, False);
	dest[0] = entity->rotation[0];
	uint8_t quadrant = (dest[0] + M_PI_4f) * (1/M_PI_2f);
	float   speed = physics->dir[0];
	/* this will cap the speed, but not the momemtum */
	if (speed > 1) speed = 1;
	minecartGetNextCoord(entity->motion, dest, iter, speed * deltaFrame, physics->negXZ);

//	fprintf(stderr, "from %g,%g,%g to %g,%g,%g (opp: %d)\n", PRINT_COORD(entity->motion), PRINT_COORD(dest), physics->negXZ);

	/* check for collision with terrain/entities */

	/* XXX removing this flag will avoid checking collision with this entity */
	entity->enflags &= ~ENFLAG_HASBBOX;
	physicsCheckCollision(globals.level, entity->motion, dest, quadrant & 1 ? &minecartBBoxNS : &minecartBBoxEW, 0, minecartValidateBlocks);
	entity->enflags |= ENFLAG_HASBBOX;

//	fprintf(stderr, "collision = %d\n", ret);
	if (entity->enflags & ENFLAG_OFFRAILS)
	{
		/* pushing on ground: rotate minecart "freely" (with some resistance */
		//fprintf(stderr, "from %d to %d = ", (int) (entity->rotation[0] * RAD_TO_DEG), (int) (physics->dir[1] * RAD_TO_DEG));
		entity->rotation[0] = moveToAngle(entity->rotation[0], physics->negXZ ? normAngle(physics->dir[1]+M_PIf) : physics->dir[1], 1 * DEG_TO_RAD);
		//fprintf(stderr, "%d\n", (int) (entity->rotation[0] * RAD_TO_DEG));
	}

	float oldPos[3];
	memcpy(oldPos, entity->pos, 12);
	memcpy(entity->motion, dest, 12);
	memcpy(entity->pos, dest, 12);
	minecartSetOrient(entity);
//	fprintf(stderr, "minecart dest = %g,%g (from %g to %g)\n", entity->pos[VX] - 0.625f, entity->pos[VX] + 0.625f, entity->pos[VX], oldVX);

	if (entity->enflags & ENFLAG_POWRAIL_OFF)
	{
		/* contrary to uphill momemtum, direction won't be reversed */
		physics->dir[0] -= 0.05f * deltaFrame;
		if (physics->dir[0] < 0) physics->dir[0] = 0;
	}
	else if (oldPos[VY] > entity->pos[VY] || (entity->enflags & ENFLAG_POWRAIL_ON))
	{
		/* minecart going down: gain momentum */
		physics->dir[0] += 0.05f * deltaFrame;
	}
	else if (oldPos[VY] < entity->pos[VY])
	{
		/* going uphill: losing momentum */
		physics->dir[0] -= 0.05f * deltaFrame;
		if (physics->dir[0] < 0)
		{
			/* reserve direction */
			physics->dir[0] = - physics->dir[0];
			physics->negXZ = ! physics->negXZ;
		}
	}

	entityUpdateInfo(entity, oldPos);
	if (entity->enflags & ENFLAG_OFFRAILS)
		physics->friction[0] += 0.02f * deltaFrame;
	else
		physics->friction[0] += 0.001f * deltaFrame;
	physics->dir[0] -= physics->friction[0] * deltaFrame;

//	fprintf(stderr, "dir = %g, friction = %g\n", (double) physics->dir[0], physics->friction[0]);
	/* return True if minecart has stopped */
	return physics->dir[0] > 0;
}

/* extract info from NBT structure */
int minecartParse(NBTFile file, Entity entity, STRPTR unused)
{
	entity->enflags |= ENFLAG_TEXENTITES | ENFLAG_HASBBOX | ENFLAG_USEMOTION;
	entity->entype = ENTYPE_MINECART;
	int modelId = entityAddModel(ITEMID(ENTITY_MINECART, 0), 0, NULL, &entity->szx, MODEL_DONT_SWAP);
	/* entity->pos is position on screen, ->motion is where it is on the rail */
	memcpy(entity->motion, entity->pos, sizeof entity->motion);
	/* entity position is at the bottom of minecart */
	minecartSetOrient(entity);

	if (entity->rotation[VZ] != 0 && (entity->enflags & ENFLAG_OFFRAILS) == 0)
	{
		/* placed on a slope: init update */
		entityInitMove(entity, UPDATE_BY_RAILS, 0);
		PhysicsEntity physics = entity->private;

		physics->negXZ = entity->rotation[VZ] > M_PIf ? 0 : 1;
		physics->dir[0] = 0.001f;
		physics->friction[0] = 0.001f;
	}

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
	entity->tile = nbt.mem;
	c->entityList = slot;

	/* orient minecart according to player orientation */
	entity->rotation[0] = globals.yawPitch[0];
	entity->VBObank = entityGetModelId(entity);
	quadTreeInsertItem(entity);

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
	renderAddModif();
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
	lines[0] = lines[2] = floorf(pos[VX]) + 0.5f;
	lines[1] = lines[3] = floorf(pos[VZ]) + 0.5f;
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
	points[VY] = (int) pos[VY] + RAILS_THICKNESS;
	points[VX+3] = lines[0] + 0.5f;
	points[VZ+3] = lines[1] + 0.5f;
	points[VY+3] = points[VY]+0.6f;

	quadTreeIntersect(points, &i, ENFLAG_ANYENTITY);
	if (i == 0)
	{
		TEXT techName[32];
		points[VX] = lines[0];
		points[VZ] = lines[1];
		itemGetTechName(itemId, techName, sizeof techName, False);
		minecartCreate(points, techName);
		return True;
	}
	return False;
}
