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

static Bool minecartGetNextCoord(vec4 start, float dest[3], struct BlockIter_t iter, float dist, int dir)
{
	uint8_t data;
	uint8_t oldDir = 255;
	memcpy(dest, start, 12);
	for (;;)
	{
		data = iter.blockIds[DATA_OFFSET + (iter.offset >> 1)];
		if (iter.offset & 1) data >>= 4;
		else data &= 15;
		if (iter.blockIds[iter.offset] != RSRAILS)
			data &= 7; /* &8 is usually powered state: don't care here */

		int8_t * neighbor = railsNeigbors + data * 8;
		if (oldDir == 255 ? dir > 0 : neighbor[3] == opp[oldDir]) neighbor += 4;
		oldDir = neighbor[3];

		mapIter(&iter, relx[oldDir], rely[oldDir], relz[oldDir]);
		vec4 next = {iter.ref->X + iter.x, iter.yabs, iter.ref->Z + iter.z};
		float remain = 0;
		/* assumes that entity center is on track */
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
		case RAILS_ASCW: remain = fabsf(dest[VX] - next[VX]) * M_SQRT2f;
		default: return False;
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
		/* no rails under: keep whatever orient it has */
		if (b->special != BLOCK_RAILS) return;
	}

	float coord[6];
	float dist = entity->szx * (0.25f/BASEVTX);

	if (minecartGetNextCoord(entity->motion, coord,   iter, dist, 0) &&
	    minecartGetNextCoord(entity->motion, coord+3, iter, dist, 4))
	{
		entity->pos[VX] = (coord[VX] + coord[VX+3]) * 0.5f;
		entity->pos[VY] = (coord[VY] + coord[VY+3]) * 0.5f;
		entity->pos[VZ] = (coord[VZ] + coord[VZ+3]) * 0.5f;
		coord[VX+3] -= coord[VX];
		entity->rotation[0] = atan2(coord[VZ+3] - coord[VZ], coord[VX+3]);
	//	entity->rotation[1] = atan2(coord[VY+3] - coord[VY], coord[VX+3]);
	}
}

/* extract info from NBT structure */
int minecartParse(NBTFile file, Entity entity)
{
	entity->enflags |= ENTITY_TEXENTITES;
	int modelId = entityAddModel(ITEMID(ENTITY_MINECART, 0), 0, NULL, &entity->szx, MODEL_DONT_SWAP);
	/* entity->pos is position on screen, ->motion is where it is on rail */
	memcpy(entity->motion, entity->pos, sizeof entity->motion);
	/* entity position is at the bottom of minecart */
	//minecartSetOrient(entity);
	entity->pos[VY] += (entity->szy >> 1) * (1.0f/BASEVTX);
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

	entity->VBObank = entityGetModelId(entity);
	quadTreeInsertItem(entity);

	entity->tile = nbt.mem;
	entity->pos[VT] = 2;
	entity->rotation[3] = 1; /* scale */
	entity->enflags |= ENTITY_TEXENTITES;
	entity->enflags &= ~ENFLAG_FULLLIGHT;
	entityGetLight(c, entity->pos, entity->light, False, 0);
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
	lines[4] = pos[VX]; lines[6] = pos[VX] + (lines[1] - lines[3]);
	lines[5] = pos[VZ]; lines[7] = pos[VZ] + (lines[2] - lines[0]);

	lineIntersect(lines);
	points = lines + 2;
	points[VX] = lines[0] - 0.5f;
	points[VZ] = lines[1] - 0.5f;
	points[VY] = (int) pos[VY];
	points[VX+3] = lines[0] + 0.5f;
	points[VZ+3] = lines[0] + 0.5f;
	points[VY+3] = points[VY]+1;

	/* check if there are entities in the way */
	quadTreeIntersect(points, &i, 0);
	if (i == 0)
	{
		TEXT techName[32];
		points[VX] = lines[0];
		points[VZ] = lines[1];
		points[VY] += 1/16.f; /* sit right on top of rail */
		itemGetTechName(itemId, techName, sizeof techName, False);
		minecartCreate(points, techName);
		return True;
	}
	return False;
}

