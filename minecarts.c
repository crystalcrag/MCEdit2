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

#if 0
static void minecartGetCoord(vec4 start, float dest[3], struct BlockIter_t iter, float dist)
{
	uint8_t data;
	vec4    pos;
	memcpy(pos, start, sizeof pos);
	do {
		data = iter.blockIds[DATA_OFFSET + (iter.offset >> 1)];
		if (iter.offset & 1) data >>= 4;
		else data &= 15;
		if (iter.blockIds[iter.offset] != RSRAILS)
			data &= 7; /* &8 is usually powered state: don't care here */

		float remain = 0;
		float factor = 1;
		uint8_t dir;
		/* assumes that entity center is on track */
		switch (data) {
		case RAILS_NS:
			remain = pos[VZ] - (int) pos[VZ];
			if (dist > 0) remain = 1 - remain, dir = 0;
			else dir = 4;
			break;
		case RAILS_EW:
			remain = pos[VX] - (int) pos[VX];
			if (dist > 0) remain = 1 - remain, dir = 0;
			else dir = 4;
			break;
		case RAILS_ASCE:
		case RAILS_ASCW:
			remain = pos[VX] - (int) pos[VX];
			factor = M_SQRT1_2f;
			if (dist > 0) remain = 1 - remain, dir = 0;
			else dir = 4;
			break;
		case RAILS_ASCN:
		case RAILS_ASCS:
			remain = pos[VZ] - (int) pos[VZ];
			factor = M_SQRT1_2f;
			if (dist > 0) remain = 1 - remain, dir = 0;
			else dir = 4;
			break;
		case RAILS_CURVED_SE:
			//
			break;
		case RAILS_CURVED_SW:
		case RAILS_CURBED_NW:
		case RAILS_CURSET_NE:
		}
	}
	while (dist > 0);
}

/* set entity orient (X and Y) according to rails configuration under */
void minecartSetOrient(Entity entity)
{
	struct BlockIter_t iter;
	int8_t prevNext[8];
	mapInitIter(globals.level, &iter, entity->pos, False);

	/* try to locate previous and next rail (will define slope of current pos) */
	Block b = &blockIds[iter.blockIds[iter.offset]];
	if (b->id == 0)
	{
		mapIter(&iter, 0, -1, 0);
		b = &blockIds[iter.blockIds[iter.offset]];
		/* no rails under: keep whatever orient it has */
		if (b->special != BLOCK_RAILS) return;
	}

	float coord[6];
	float radius = entity->szx * (0.5f/BASEVTX);

	minecartGetCoord(entity->pos, coord,   iter,  radius);
	minecartGetCoord(entity->pos, coord+3, iter, -radius);
}
#endif

int minecartParse(NBTFile file, Entity entity)
{
	entity->enflags |= ENTITY_TEXENTITES;
	int modelId = entityAddModel(ITEMID(ENTITY_MINECART, 0), 0, NULL, &entity->szx, MODEL_DONT_SWAP);
	/* entity position is at the bottom of minecart */
	entity->pos[VY] += (entity->szy >> 1) * (1.0f/BASEVTX);
//	minecartSetOrient(entity);
	return modelId;
}

