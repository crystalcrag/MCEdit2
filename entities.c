/*
 * entities.c : manage the list of active entity surrounding the player.
 *
 * see doc/internals.html to have an overview on how data structure works here,
 * otherwise you'll get lost.
 *
 * written by T.Pierron, apr 2021.
 */

#define ENTITY_IMPL
#include <malloc.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include "entities.h"
#include "blocks.h"
#include "maps.h"
#include "glad.h"

struct EntitiesPrivate_t entities;

static void hashAlloc(int);
static int  entityAddModel(int);

Bool entityInitStatic(void)
{
	/* pre-alloc some entities */
	hashAlloc(ENTITY_BATCH);
	/* already add model for unknown entity */
	entityAddModel(0);

	entities.shader = createGLSLProgram("entities.vsh", "entities.fsh", NULL);
	return entities.shader;
}

/*
 * quick and dirty hash table to associated id with bank+vbo
 */
#define EOL     0xffff

static int hashSearch(int id)
{
	if (entities.hash.count == 0)
		return 0;

	int i = id % entities.hash.max;
	EntityEntry entry = entities.hash.list + i;
	if (entry->VBObank == 0)
		return 0;

	do {
		if (entry->id == id)
			return entry->VBObank;
		i = entry->next;
		entry = entities.hash.list + i;
	}
	while (i != EOL);
	return 0;
}

static void hashAlloc(int max)
{
	max = roundToUpperPrime(max);
	entities.hash.list  = calloc(max, sizeof (struct EntityEntry_t));
	entities.hash.max   = max;
	entities.hash.count = 0;
}

static void hashInsert(int id, int VBObank)
{
	if ((entities.hash.count * 36 >> 5) >= entities.hash.max)
	{
		/* 90% full: expand hash table */
		EntityEntry entry, eof;
		EntityEntry old = entities.hash.list;
		int         max = entities.hash.max;

		/* redo from scratch */
		hashAlloc(max+1);

		for (entry = old, eof = entry + max; entry < eof; entry ++)
		{
			if (entry->VBObank > 0)
				hashInsert(entry->id, entry->VBObank);
		}
		free(old);
	}

	EntityEntry entry, last;
	int         index = id % entities.hash.max;

	for (entry = entities.hash.list + index, last = NULL; entry->VBObank > 0; entry = entities.hash.list + entry->next)
	{
		/* check if already inserted */
		if (entry->VBObank == 0)
			break;
		if (entry->id == id)
			return;
		if (entry->next == EOL)
		{
			EntityEntry eof = entities.hash.list + entities.hash.max;
			last = entry;
			do {
				entry ++;
				if (entry == eof) entry = entities.hash.list;
			} while (entry->VBObank > 0);
			break;
		}
	}
	if (last) last->next = entry - entities.hash.list;
	entry->VBObank = VBObank;
	entry->id      = id;
	entry->next    = EOL;
}

/*
 * entities bank for models
 */
extern int blockInvModelCube(DATA16 ret, BlockState b, DATA8 texCoord);

/* get vertex count for entity id */
static int entityModelCount(int id)
{
	if (id < ID(256, 0))
	{
		/* normal block */
		BlockState b = blockGetById(id);
		if (id == 0) return 36;
		switch (b->type) {
		case SOLID:
		case TRANS: return 36;
		case CUST:  return b->custModel ? b->custModel[-1] : 0;
		}
	}
	return 0;
}

static int entityGenModel(EntityBank bank, int id)
{
	glBindBuffer(GL_ARRAY_BUFFER, bank->vboModel);
	DATA16  buffer = glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
	VTXBBox bbox   = NULL;
	int     count  = 0;

	if (id < ID(256, 0))
	{
		extern uint8_t texCoord[];
		/* normal block */
		BlockState b = blockGetById(id);
		if (id == 0)
		{
			static struct BlockState_t unknownEntity = {
				0, CUBE, 0, NULL, 31,13,31,13,31,13,31,13,31,13,31,13
			};
			/* used by unknwon entity */
			count = blockInvModelCube(buffer, &unknownEntity, texCoord);
			bbox  = blockGetBBox(blockGetById(ID(1,0)));
		}
		else switch (b->type) {
		case SOLID:
		case TRANS:
			count = blockInvModelCube(buffer, b, texCoord);
			bbox  = blockGetBBox(b);
			break;
		case CUST:
			if (b->custModel)
			{
				count = b->custModel[-1];
				memcpy(buffer, b->custModel, count * BYTES_PER_VERTEX);
				bbox  = blockGetBBox(b);
			}
		}
	}
	glUnmapBuffer(GL_ARRAY_BUFFER);
	bank->vtxCount += count;

	if (count == 0) return 0;

	/* we need to keep track of what's in the bank VBO */
	EntityModel models;
	int max = (bank->modelCount + (ENTITY_BATCH-1)) & ~(ENTITY_BATCH-1);
	if (bank->modelCount + 1 > max)
	{
		max += ENTITY_BATCH;
		models = realloc(bank->models, max * sizeof *models);
		if (models == NULL) return 0;
		bank->models = models;
	}
	max = bank->modelCount;
	models = bank->models + max;
	models->first = bank->vtxCount - count;
	models->count = count;
	models->bbox  = bbox;
	bank->modelCount ++;

	/* VBObank: 6 first bits are for bank number, 10 next are for model number (index in bank->models) */
	for (max <<= 6; bank->node.ln_Prev; max ++, PREV(bank));

	hashInsert(id, max);

	return max;
}

static int entityAddModel(int id)
{
	EntityBank bank;
	int modelId = hashSearch(id);
	if (modelId > 0) return modelId;

	/* not yet in cache, add it on the fly */
	int count = entityModelCount(id);
	if (count == 0) return ENTITY_UNKNOWN;

	/* check for a free place */
	for (bank = HEAD(entities.banks); bank && bank->vtxCount + count < BANK_SIZE; NEXT(bank));

	if (bank == NULL)
	{
		bank = calloc(sizeof *bank, 1);
		bank->models = malloc(sizeof *bank->models * ENTITY_BATCH);
		ListAddTail(&entities.banks, &bank->node);

		glGenVertexArrays(1, &bank->vao);
		glGenBuffers(3, &bank->vboModel); /* + vboLoc and vboMDAI */

		/* same vertex format than blocks.vsh */
		glBindVertexArray(bank->vao);
		glBindBuffer(GL_ARRAY_BUFFER, bank->vboModel);
		glBufferData(GL_ARRAY_BUFFER, BANK_SIZE * BYTES_PER_VERTEX, NULL, GL_STATIC_DRAW);
		glVertexAttribIPointer(0, 3, GL_UNSIGNED_SHORT, BYTES_PER_VERTEX, 0);
		glEnableVertexAttribArray(0);
		glVertexAttribIPointer(1, 2, GL_UNSIGNED_SHORT, BYTES_PER_VERTEX, (void *) 6);
		glEnableVertexAttribArray(1);
		glBindBuffer(GL_ARRAY_BUFFER, bank->vboLoc);
		glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 0, 0);
		glEnableVertexAttribArray(2);
		glVertexAttribDivisor(2, 1);
		glBindVertexArray(0);
	}

	/* check if it has already been generated */
	return entityGenModel(bank, id);
}

static Entity entityAlloc(uint16_t * entityLoc)
{
	EntityBuffer buffer;
	uint16_t i;

	for (buffer = HEAD(entities.list), i = 0; buffer && buffer->count == ENTITY_BATCH; NEXT(buffer), i += ENTITY_BATCH);

	if (buffer == NULL)
	{
		buffer = malloc(sizeof * buffer);
		buffer->count = 0;
		memset(buffer->usage, 0, sizeof buffer->usage);
		ListAddTail(&entities.list, &buffer->node);
	}

	buffer->count ++;
	int slot = mapFirstFree(buffer->usage, ENTITY_BATCH>>5);
	*entityLoc = i | slot;

	return buffer->entities + slot;
}

/* get model to use for rendering this entity */
static int entityGetModelId(Entity entity)
{
	STRPTR id = entity->name;
	return 0;

	if (strncmp(id, "minecraft:", 10) == 0)
		id += 10;

	if (strcmp(id, "falling_block") == 0)
	{
		STRPTR block = NULL;
		int    data  = 0;
		int    off;

		NBTFile_t nbt = {.mem = entity->tile};
		NBTIter_t prop;
		NBT_IterCompound(&prop, entity->tile);
		while ((off = NBT_Iter(&prop)) >= 0)
		{
			switch (FindInList("Data,Block", prop.name, 0)) {
			case 0: data  = NBT_ToInt(&nbt, off, 0); break;
			case 1: block = NBT_Payload(&nbt, off);
			}
		}
		if (block)
			return entityAddModel(itemGetByName(block, False) | data);
	}
	return ENTITY_UNKNOWN;
}

static void entityFillLocation(Entity ent, vec4 loc)
{
	loc[0] = ent->pos[0] - 0.5;
	loc[1] = ent->pos[1] - 0.5;
	loc[2] = ent->pos[2] - 0.5;
	loc[3] = ent->select ? 511 : 255; /* skylight/blocklight */
}

static void entityAddToCommandList(Entity entity)
{
	EntityBank bank;
	int VBObank = entity->VBObank;
	int i;

	for (i = VBObank & 63, bank = HEAD(entities.banks); i > 0; i --, NEXT(bank));

	bank->mdaiCount ++;
	if (bank->dirty) return;
	if (bank->mdaiCount < bank->mdaiMax)
	{
		EntityModel model = bank->models + (VBObank >> 6);
		MDAICmd_t   cmd   = {.count = model->count, .baseInstance = bank->mdaiCount-1, .instanceCount = 1, .first = model->count};
		int         slot  = mapFirstFree(bank->mdaiUsage, bank->mdaiMax >> 5);
		vec4        loc;

		entity->mdaiSlot = slot;
		entityFillLocation(entity, loc);
		glBindBuffer(GL_ARRAY_BUFFER, bank->vboLoc);
		glBufferSubData(GL_ARRAY_BUFFER, slot * 16, 16, loc);
		glBindBuffer(GL_ARRAY_BUFFER, bank->vboMDAI);
		glBufferSubData(GL_ARRAY_BUFFER, slot * 16, 16, &cmd);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
	}
	else bank->dirty = 1; /* redo the list from scratch */
}

/* extract information from NBT records */
void entityParse(Chunk c, NBTFile nbt, int offset)
{
	/* <offset> points to a TAG_List_Compound of entities */
	NBTIter_t list;
	Entity    prev = NULL;
	NBT_InitIter(nbt, offset, &list);
	while ((offset = NBT_Iter(&list)) >= 0)
	{
		STRPTR id;
		float  pos[8];
		int    off;

		/* iterate over the properties of one entity */
		NBTIter_t entity;
		NBT_InitIter(nbt, offset, &entity);
		memset(pos, 0, sizeof pos); id = NULL;
		while ((off = NBT_Iter(&entity)) >= 0)
		{
			switch (FindInList("Pos,Motion,Rotation,id", entity.name, 0)) {
			case 0: NBT_ConvertToFloat(nbt, off, pos,   3); break;
			case 1: NBT_ConvertToFloat(nbt, off, pos+3, 3); break;
			case 2: NBT_ConvertToFloat(nbt, off, pos+6, 2); break;
			case 3: id = NBT_Payload(nbt, off);
			}
		}

		if (id && !(pos[0] == 0 && pos[1] == 0 && pos[2] == 0))
		{
			uint16_t next;
			Entity entity = entityAlloc(&next);

			if (prev)
				prev->next = next;
			prev = entity;
			if (c->entityList == ENTITY_END)
				c->entityList = next;

			pos[VY] += 0.5; /* not sure why */
			memcpy(entity->pos, pos, sizeof pos);
			entity->tile = nbt->mem + offset;
			entity->next = ENTITY_END;
			entity->select = 0;
			entity->name = id;
			entity->VBObank = entityGetModelId(entity);
			entityAddToCommandList(entity);
		}
	}
}

static Entity entityGetById(int id)
{
	EntityBuffer buffer;
	int i = id >> ENTITY_SHIFT;

	for (buffer = HEAD(entities.list); i > 0; i --, NEXT(buffer));

	return buffer->entities + (id & (ENTITY_BATCH-1));
}

static EntityModel entityGetModelById(Entity entity)
{
	EntityBank bank;
	int i = entity->VBObank;

	for (bank = HEAD(entities.banks); i & 63; NEXT(bank));

	return bank->models + (i >> 6);
}

void entityDebug(int id)
{
	Entity entity = entityGetById(id);

	fprintf(stderr, "entity %s at %g, %g, %g. NBT data:\n", entity->name, entity->pos[0], entity->pos[1], entity->pos[2]);
	NBTFile_t nbt = {.mem = entity->tile};
	NBTIter_t iter;
	int       off;

	NBT_IterCompound(&iter, entity->tile);
	while ((off = NBT_Iter(&iter)) >= 0)
	{
		NBT_Dump(&nbt, off, 3, stderr);
	}
}

/* used by tooltip */
void entityInfo(int id, STRPTR buffer, int max)
{
	Entity entity = entityGetById(id);

	sprintf(buffer, "X: %.1f\nY: %.1f\nZ: %.1f\n<dim>Entity:</dim> %s", entity->pos[0], entity->pos[1], entity->pos[2], entity->name);
}

/* mark new entity as selected and unselect old if any */
static void entitySetSelection(Entity entity)
{
	if (entities.selected != entity)
	{
		EntityBank bank;
		int i;
		for (i = entity->VBObank & 63, bank = HEAD(entities.banks); i > 0; i --, NEXT(bank));

		if (! bank->dirty)
		{
			/* selection flag is set directly in VBO meta-data */
			float val = 255;
			glBindBuffer(GL_ARRAY_BUFFER, bank->vboLoc);
			if (entities.selected)
			{
				glBufferSubData(GL_ARRAY_BUFFER, entities.selected->mdaiSlot * 16 - 4, 4, &val);
				entities.selected->select = 0;
			}
			val = 511;
			if (entity)
				glBufferSubData(GL_ARRAY_BUFFER, entity->mdaiSlot * 16 - 4, 4, &val);
		}
		if (entity) entity->select = 1;
		entities.selected = entity;
	}
}

#if 0
/* get bounding box of entity for one face */
static void entityGetBBoxForFace(Entity entity, int side, float points[6], vec4 norm)
{
	extern uint8_t vertex[];
	extern uint8_t cubeIndices[];
	extern int8_t  normals[];
	DATA8 pt = vertex + cubeIndices[side * 4 + 1];
	points[VX] = pt[0] - 0.5 + entity->pos[VX];
	points[VY] = pt[1] - 0.5 + entity->pos[VY];
	points[VZ] = pt[2] - 0.5 + entity->pos[VZ];

	pt = vertex + cubeIndices[side * 4 + 3];
	points[VX+3] = pt[0] - 0.5 + entity->pos[VX];
	points[VY+3] = pt[1] - 0.5 + entity->pos[VY];
	points[VZ+3] = pt[2] - 0.5 + entity->pos[VZ];

	if (points[0] > points[3]) norm[0] = points[0], points[0] = points[3], points[3] = norm[0];
	if (points[2] > points[5]) norm[0] = points[2], points[2] = points[5], points[5] = norm[0];

	int8_t * normal = normals + side * 4;
	norm[VX] = normal[VX];
	norm[VY] = normal[VY];
	norm[VZ] = normal[VZ];
	norm[VT] = 1;
}
#endif

int intersectRayPlane(vec4 P0, vec4 u, vec4 V0, vec norm, vec4 I);

/* check if vector <dir> intersects an entity bounding box (from position <camera>) */
int entityRaycast(Chunk c, vec4 dir, vec4 camera, vec4 cur, vec4 ret_pos)
{
	float maxDist = cur ? vecDistSquare(camera, cur) : 1e6;
	int   flags = (dir[VX] < 0 ? 2 : 8) | (dir[VY] < 0 ? 16 : 32) | (dir[VZ] < 0 ? 1 : 4);
	int   i, id;
	Chunk chunks[4];

	if ((c->cflags & CFLAG_GOTDATA) == 0)
		return 0;

	chunks[0] = c;
	chunks[1] = c + chunkNeighbor[c->neighbor + (flags & 2 ? 8 : 2)];
	chunks[2] = c + chunkNeighbor[c->neighbor + (flags & 1 ? 4 : 1)];
	chunks[3] = c + chunkNeighbor[c->neighbor + ((flags & 15) ^ 15)];

	/* scan 4 nearby chunk */
	for (i = 0; i < 4; i ++)
	{
		c = chunks[i];
		if (c->entityList == ENTITY_END) continue;
		Entity list = entityGetById(id = c->entityList);
		for (;;)
		{
			/* just a quick heuristic to get rid of most entities */
			if (vecDistSquare(camera, list->pos) < maxDist * 1.5)
			{
				float points[6];
				vec4  norm, inter;
				int   j;
				/* assume rectangular bounding box (not necessarily axis aligned though) */
				for (j = 0; j < 6; j ++)
				{
					/* XXX need to rely on cross-product instead */
					if ((flags & (1 << j)) == 0) continue;

					EntityModel model = entityGetModelById(list);
					blockGetBoundsForFace(model->bbox, j, points, points+3, list->pos, 0);
					if (intersectRayPlane(camera, dir, points, norm, inter))
					{
						/* check if points is contained within the face */
						if (points[VX] <= inter[VX] && inter[VX] <= points[VX+3] &&
							points[VY] <= inter[VY] && inter[VY] <= points[VY+3] &&
							points[VZ] <= inter[VZ] && inter[VZ] <= points[VZ+3] &&
							/* <inter> is the exact coordinate */
							vecDistSquare(camera, inter) < maxDist)
						{
							memcpy(ret_pos, list->pos, 12);
							entitySetSelection(list);
							return id;
						}
					}
				}
			}
			if (list->next == ENTITY_END)
			{
				if (entities.selected > 0)
					entitySetSelection(NULL);
				break;
			}
			list = entityGetById(id = list->next);
		}
	}
	if (entities.selected > 0)
		entitySetSelection(NULL);
	return 0;
}

/* chunk <c> is about to be unloaded, remove all entities references we have here */
void entityUnload(Chunk c)
{
	//
}

void entityDelete(Chunk c, DATA8 tile)
{
}

void entityAnimate(void)
{
	//
}

void entityDebugCmd(void)
{
	EntityBank bank = HEAD(entities.banks);

	glBindBuffer(GL_ARRAY_BUFFER, bank->vboLoc);
	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, bank->vboMDAI);

	float * loc = glMapBuffer(GL_ARRAY_BUFFER, GL_READ_ONLY);
	MDAICmd cmd = glMapBuffer(GL_DRAW_INDIRECT_BUFFER, GL_READ_ONLY);

	int i;
	for (i = 0; i < bank->mdaiCount; i ++, cmd ++, loc += 4)
	{
		fprintf(stderr, "%d. item at %g,%g,%g, model: %d, count: %d [%d-%d]\n", i, loc[0], loc[1], loc[2], cmd->first, cmd->count, cmd->baseInstance, cmd->instanceCount);
	}

	glUnmapBuffer(GL_ARRAY_BUFFER);
	glUnmapBuffer(GL_DRAW_INDIRECT_BUFFER);
}

void entityRender(void)
{
	EntityBank bank;
	uint8_t    i;
	for (bank = HEAD(entities.banks), i = 0; bank; NEXT(bank), i ++)
	{
		if (bank->mdaiCount == 0) continue;
		if (bank->dirty)
		{
			/* rebuild vboLoc and vboMDAI */
			glBindBuffer(GL_ARRAY_BUFFER, bank->vboLoc);
			glBindBuffer(GL_DRAW_INDIRECT_BUFFER, bank->vboMDAI);

			/* realloc buffers XXX intel card don't like resetting buffer before drawing */
			int max;
			bank->mdaiMax = (bank->mdaiCount + ENTITY_BATCH - 1) & ~ (ENTITY_BATCH-1);
			bank->mdaiUsage = realloc(bank->mdaiUsage, (max = bank->mdaiMax >> 5) * 4);
			memset(bank->mdaiUsage, 0, max * 4);

			glBufferData(GL_ARRAY_BUFFER, bank->mdaiMax * 16, NULL, GL_STATIC_DRAW);
			glBufferData(GL_DRAW_INDIRECT_BUFFER, bank->mdaiMax * 16, NULL, GL_STATIC_DRAW);

			float * loc = glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
			MDAICmd cmd = glMapBuffer(GL_DRAW_INDIRECT_BUFFER, GL_WRITE_ONLY);
			int     inst;

			EntityBuffer buffer;
			for (buffer = HEAD(entities.list), inst = 0; buffer; NEXT(buffer))
			{
				Entity cur;
				int    j;
				for (cur = buffer->entities, j = buffer->count; j > 0; cur ++)
				{
					if (cur->tile == NULL) continue; j --;
					if ((cur->VBObank & 63) != i) continue;
					cur->mdaiSlot = mapFirstFree(bank->mdaiUsage, max);
					entityFillLocation(cur, loc);
					EntityModel model = bank->models + (cur->VBObank >> 6);
					cmd->baseInstance = inst;
					cmd->count = model->count;
					cmd->first = model->first;
					cmd->instanceCount = 1;
					loc += 4; cmd ++; inst ++;
				}
			}
			glUnmapBuffer(GL_ARRAY_BUFFER);
			glUnmapBuffer(GL_DRAW_INDIRECT_BUFFER);
			bank->dirty = 0;
		}

		glEnable(GL_CULL_FACE);
		glCullFace(GL_BACK);
		glUseProgram(entities.shader);
		glBindVertexArray(bank->vao);
		glBindBuffer(GL_DRAW_INDIRECT_BUFFER, bank->vboMDAI);
		glMultiDrawArraysIndirect(GL_TRIANGLES, 0, bank->mdaiCount, 0);
	}
}
