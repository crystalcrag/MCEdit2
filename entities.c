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
#include <ctype.h>
#include <stdio.h>
#include <math.h>
#include "SIT.h"
#include "entities.h"
#include "redstone.h"
#include "blockUpdate.h"
#include "MCEdit.h"
#include "mapUpdate.h"
#include "cartograph.h"
#include "render.h"
#include "globals.h"
#include "glad.h"

struct EntitiesPrivate_t entities;
struct Paintings_t       paintings;

static void hashAlloc(int);
static int  entityAddModel(int blockId, int cnx, CustModel);

static struct VTXBBox_t entitiesBBox[] = {
	{  BOX(1.0, 1.0, 1.0), .sides = 63, .aabox = 1},  /* ENTITY_UNKNOWN */
	{BOXCY(0.6, 1.8, 0.6), .sides = 63, .aabox = 2},  /* ENTITY_PLAYER */
};
static vec4 pos_000;

VTXBBox entityGetBBox(int id)
{
	if (id < 0 || id >= DIM(entitiesBBox))
		return entitiesBBox;

	return entitiesBBox + id;
}


static VTXBBox entityAllocBBox(void)
{
	BBoxBuffer buf = HEAD(entities.bbox);

	if (buf == NULL || buf->count == ENTITY_BATCH)
	{
		buf = malloc(sizeof *buf);
		buf->count = 0;
		ListAddHead(&entities.bbox, &buf->node);
	}

	VTXBBox bbox = buf->bbox + (buf->count ++);

	memset(bbox, 0, sizeof *bbox);
	bbox->sides = 63;
	bbox->aabox = 1;

	return bbox;
}

/* pre-create some entities from entities.js */
static Bool entityCreateModel(const char * file, STRPTR * keys, int lineNum)
{
	STRPTR id, name, model;
	int    modelId, index;
	DATA8  loc;

	id    = jsonValue(keys, "id");
	model = jsonValue(keys, "model");
	if (! id || ! model || *model != '[')
	{
		SIT_Log(SIT_ERROR, "%s: missing property %s for entity on line %d\n", file, id ? "model" : "id", lineNum);
		return False;
	}

	index = StrCount(model, ',') + 1;
	struct CustModel_t cust = {.vertex = index, .model = alloca(index * 4)};

	/* convert model text to float */
	for (index = 0, model ++; index < cust.vertex && IsDef(model); index ++)
	{
		float val = strtof(model, &model);
		while (isspace(*model)) model ++;
		if (*model == ',')
			 model ++;
		while (isspace(*model)) model ++;
		cust.model[index] = val;
	}

	switch (FindInList("painting,item_frame", id, 0)) {
	case 0:
		name = jsonValue(keys, "name");
		if (! name) return False;
		if (paintings.names[0]) StrCat(paintings.names, sizeof paintings.names, 0, ",");
		StrCat(paintings.names, sizeof paintings.names, 0, name);
		modelId = ENTITY_PAINTINGID + paintings.count;
		loc = paintings.location + paintings.count * 4;
		paintings.count ++;
		cust.U = PAINTINGS_TILE_X * 16;
		cust.V = PAINTINGS_TILE_Y * 16;
		cust.bbox = entityAllocBBox();
		/* also store painting location in texture (needed by paintings selector interface) */
		index = cust.model[13];
		loc[0] = (index % 513 >> 4);
		loc[1] = (index / 513 >> 4);
		loc[2] = cust.model[1] * 0.0625f + loc[0];
		loc[3] = cust.model[2] * 0.0625f + loc[1];
		break;
	case 1:
		name = jsonValue(keys, "full");
		modelId = name && atoi(name) == 1 ? ENTITY_ITEMFRAME_FULL : ENTITY_ITEMFRAME;
		cust.bbox = entityAllocBBox();
		break;
	default:
		SIT_Log(SIT_ERROR, "%s: unknown entity type %s on line %d", file, id, lineNum);
		return False;
	}

	entityAddModel(modelId, 0, &cust);

	return True;
}

Bool entityInitStatic(void)
{
	/* pre-alloc some entities */
	hashAlloc(ENTITY_BATCH);
	/* already add model for unknown entity */
	entityAddModel(0, 0, NULL);

	/* parse entity description models */
	if (! jsonParse(RESDIR "entities.js", entityCreateModel))
		return False;

	entities.shader = createGLSLProgram("entities.vsh", "entities.fsh", NULL);
	return entities.shader;
}

/*
 * quick and dirty hash table to associate entity id with bank+vbo
 */
#define EOL     0xffff

/* will return model location in entites.banks: 6bits: bank index, remain: model index */
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

		entities.hash.count = 0;
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
	entities.hash.count ++;
}

/*
 * entities bank for models
 */
extern int blockInvModelCube(DATA16 ret, BlockState b, DATA8 texCoord);

/* get vertex count for entity id */
static int entityModelCount(int id, int cnx)
{
	if (id & ENTITY_ITEM) /* same as normal block/item */
		id &= ~ENTITY_ITEM;
	if (id < ID(256, 0))
	{
		/* normal block */
		BlockState b = blockGetById(id);
		if (id == 0) return 36; /* unknown entity: a cube */
		switch (b->type) {
		case SOLID:
			if (b->special == BLOCK_STAIRS)
			{
				/* use inventory item for this */
				DATA16 model = blockIds[id>>4].model;
				return model ? model[-1] : 0;
			}
			// else no break;
		case TRANS: return 36;
		case CUST:
			if (b->custModel == NULL) return 36; /* assume cube, if no model */
			if (cnx == 0)
			{
				id = b->custModel[-1];
				if (b->special == BLOCK_SOLIDOUTER)
					id += 36;
			}
			else id = blockInvCountVertex(b->custModel, cnx);
			return id;
		}
	}
	else return itemGenMesh(id, NULL);
	return 0;
}

static int entityGenModel(EntityBank bank, int id, int cnx, CustModel cust)
{
	glBindBuffer(GL_ARRAY_BUFFER, bank->vboModel);
	DATA16   buffer = glMapBuffer(GL_ARRAY_BUFFER, GL_READ_WRITE);
	VTXBBox  bbox   = NULL;
	int      count  = 0;
	int      item   = 0;
	uint8_t  cpBBox = 0;
	uint16_t U, V;
	id &= ~ENTITY_ITEM;

	U = V = 0;
	buffer += bank->vtxCount * INT_PER_VERTEX;
	if (id < ID(256, 0))
	{
		/* normal block */
		BlockState b = blockGetById(id);
		if (id == 0)
		{
			/* used by unknown entity */
			static struct BlockState_t unknownEntity = {
				0, CUBE3D, 0, NULL, 31,13,31,13,31,13,31,13,31,13,31,13
			};
			count = blockInvModelCube(buffer, &unknownEntity, texCoord);
		}
		else if ((b->inventory & MODELFLAGS) == ITEM2D)
		{
			count = itemGenMesh(id, buffer);
		}
		else switch (b->type) {
		case SOLID:
			if (b->special == BLOCK_STAIRS)
			{
				/* use inventory item for these */
				DATA16 model = blockIds[id>>4].model;
				if (model)
				{
					count = model[-1];
					memcpy(buffer, model, count * BYTES_PER_VERTEX);
					break;
				}
			}
			// no break;
		case TRANS:
			count  = blockInvModelCube(buffer, b, texCoord);
			bbox   = blockGetBBox(b);
			cpBBox = 1;
			break;
		case CUST:
			if (b->custModel)
			{
				if (cnx > 0)
				{
					/* filter some parts out */
					count = blockInvCopyFromModel(buffer, b->custModel, cnx);
				}
				else /* grab entire model */
				{
					count = b->custModel[-1];
					memcpy(buffer, b->custModel, count * BYTES_PER_VERTEX);
				}
				bbox = blockGetBBox(b);
				cpBBox = 1;
				if (b->special == BLOCK_SOLIDOUTER)
					count += blockInvModelCube(buffer + count * INT_PER_VERTEX, b, texCoord);
			}
			else count = blockInvModelCube(buffer, b, texCoord);
		}
	}
	else if (cust)
	{
		count = blockCountModelVertex(cust->model, cust->vertex);
		blockParseModel(cust->model, cust->vertex, buffer);
		bbox = cust->bbox;
		U = cust->U;
		V = cust->V;
	}
	else count = itemGenMesh(id, buffer), item = 1;

	if (! bbox)
	{
		bbox = entityAllocBBox();
	}
	else if (cpBBox)
	{
		/* bbox currently points to a VTXBBox used by physics: do not modify the original */
		VTXBBox newBB = entityAllocBBox();
		memcpy(newBB, bbox, sizeof *newBB);
		bbox = newBB;
	}

	/* entities have their position centered in the middle of their bbox */
	blockCenterModel(buffer, count, U, V, ! item, bbox);
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

	//fprintf(stderr, "model %d, first: %d, count: %d, id: %x\n", max >> 6, models->first, count, id);

	hashInsert(id, max);

	return max;
}

static int entityAddModel(int id, int cnx, CustModel cust)
{
	EntityBank bank;
	int modelId = hashSearch(id | (cnx << 16));
	if (modelId > 0) return modelId;

	/* not yet in cache, add it on the fly */
	int count = cust ? blockCountModelVertex(cust->model, cust->vertex) : entityModelCount(id, cnx);
	if (count == 0) return ENTITY_UNKNOWN;

	/* check for a free place */
	for (bank = HEAD(entities.banks); bank && bank->vtxCount + count > BANK_SIZE; NEXT(bank));

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
		/* 3 uint16_t for vertex position (rel to info) */
		glVertexAttribIPointer(0, 3, GL_UNSIGNED_SHORT, BYTES_PER_VERTEX, 0);
		glEnableVertexAttribArray(0);
		/* 2 uint16_t texture coord, normal */
		glVertexAttribIPointer(1, 2, GL_UNSIGNED_SHORT, BYTES_PER_VERTEX, (void *) 6);
		glEnableVertexAttribArray(1);
		glBindBuffer(GL_ARRAY_BUFFER, bank->vboLoc);
		/* 3 floats for model position, 1 for meta data */
		glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, INFO_SIZE, 0);
		glEnableVertexAttribArray(2);
		glVertexAttribDivisor(2, 1);
		/* 4 floats for 3 rotations and 1 scaling */
		glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, INFO_SIZE, (void *) 16);
		glEnableVertexAttribArray(3);
		glVertexAttribDivisor(3, 1);
		/* 24 uint8_t for lighting */
		glVertexAttribIPointer(4, 3, GL_UNSIGNED_INT, INFO_SIZE, (void *) 32);
		glEnableVertexAttribArray(4);
		glVertexAttribDivisor(4, 1);

		glVertexAttribIPointer(5, 3, GL_UNSIGNED_INT, INFO_SIZE, (void *) 44);
		glEnableVertexAttribArray(5);
		glVertexAttribDivisor(5, 1);
		glBindVertexArray(0);
	}

	/* check if it has already been generated */
	return entityGenModel(bank, id, cnx, cust);
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

	Entity entity = memset(buffer->entities + slot, 0, sizeof (struct Entity_t));
	entity->mdaiSlot = MDAI_INVALID_SLOT;
	return entity;
}

/* get model to use for rendering this entity */
static int entityGetModelId(Entity entity)
{
	STRPTR id = entity->name;
	NBTFile_t nbt = {.mem = entity->tile};

	/* block pushed by piston */
	if (entity->blockId > 0)
		return entityAddModel(entity->blockId, nbt.mem ? NBT_ToInt(&nbt, NBT_FindNode(&nbt, 0, "blockCnx"), 0) : 0, NULL);

	if (strncmp(id, "minecraft:", 10) == 0)
		id += 10;

	if (strcmp(id, "falling_block") == 0)
	{
		STRPTR block = NULL;
		int    data  = 0;
		int    off;

		NBTIter_t prop;
		entity->pos[VY] += 0.5f; // XXX not sure why
		NBT_IterCompound(&prop, entity->tile);
		while ((off = NBT_Iter(&prop)) >= 0)
		{
			switch (FindInList("Data,Block", prop.name, 0)) {
			case 0: data  = NBT_ToInt(&nbt, off, 0); break;
			case 1: block = NBT_Payload(&nbt, off);
			}
		}
		if (block)
			return entityAddModel(entity->blockId = itemGetByName(block, False) | data, 0, NULL);
	}
	else if (strcmp(id, "painting") == 0)
	{
		int off = NBT_FindNode(&nbt, 0, "Motive");
		if (off >= 0)
		{
			off = FindInList(paintings.names, NBT_Payload(&nbt, off), 0);
			if (off >= 0) return hashSearch(ENTITY_PAINTINGID + off);
		}
	}
	else if (strcmp(id, "item_frame") == 0)
	{
		int item = NBT_FindNode(&nbt, 0, "Item");
		if (item >= 0)
		{
			STRPTR  tech = NBT_Payload(&nbt, NBT_FindNode(&nbt, item, "id"));
			int     data = NBT_ToInt(&nbt, NBT_FindNode(&nbt, item, "Damage"), 0);
			int     blockId = itemGetByName(tech, True);

			if (blockId == ID(358, 0))
			{
				/*
				 * filled map will use a slightly bigger model and need a cartograph entry to render the map bitmap
				 * note: data in this case refers to id in data/map_%d.dat, this number can be arbitrary large.
				 */
				entity->map = 1;
				entity->blockId = ENTITY_ITEMFRAME | ENTITY_ITEM | data;
				return hashSearch(ENTITY_ITEMFRAME_FULL);
			}
			else if (blockId > 0)
			{
				/* we will have to alloc another entity for the item in the frame */
				entity->blockId = ENTITY_ITEMFRAME | ENTITY_ITEM | blockId | data;
			}
		}
		/* item will be allocated later */
		return hashSearch(ENTITY_ITEMFRAME);
	}
	else if (strcmp(id, "item") == 0)
	{
		/* item laying in the world */
		int desc = NBT_FindNode(&nbt, 0, "Item");
//		int count = NBT_ToInt(&nbt, NBT_FindNode(&nbt, desc, "Count"), 1);
		int data = NBT_ToInt(&nbt, NBT_FindNode(&nbt, desc, "Damage"), 0);
		int blockId = itemGetByName(NBT_Payload(&nbt, NBT_FindNode(&nbt, desc, "id")), False);

		if (blockId >= 0)
		{
			entity->rotation[3] = 0.5; /* scale actually */
			if (blockId < ID(256, 0))
				entity->pos[VY] += 0.25f;
			return entityAddModel(entity->blockId = blockId | data, 0, NULL);
		}
	}
	return ENTITY_UNKNOWN;
}

static void entityAddToCommandList(Entity entity)
{
	EntityBank bank;
	int VBObank = entity->VBObank;
	int slot = entity->mdaiSlot;
	int i;

	for (i = BANK_NUM(VBObank), bank = HEAD(entities.banks); i > 0; i --, NEXT(bank));

	if (bank->mdaiCount < bank->mdaiMax)
	{
		if (slot == MDAI_INVALID_SLOT)
			slot = mapFirstFree(bank->mdaiUsage, bank->mdaiMax >> 5);
		if (bank->mdaiCount <= slot)
			bank->mdaiCount = slot + 1;

		if (! bank->dirty) /* else will be redone at once */
		{
			EntityModel model = bank->models + (VBObank >> 6);
			MDAICmd_t   cmd   = {.count = model->count, .baseInstance = slot, .instanceCount = 1, .first = model->first};

			entity->mdaiSlot = slot;
			glBindBuffer(GL_ARRAY_BUFFER, bank->vboLoc);
			glBufferSubData(GL_ARRAY_BUFFER, slot * INFO_SIZE, INFO_SIZE, entity->pos);
			glBindBuffer(GL_ARRAY_BUFFER, bank->vboMDAI);
			glBufferSubData(GL_ARRAY_BUFFER, slot * 16, 16, &cmd);
			glBindBuffer(GL_ARRAY_BUFFER, 0);
		}
	}
	else bank->mdaiCount ++,  bank->dirty = 1; /* redo the list from scratch */
}

static void entityGetLight(Chunk c, vec4 pos, DATA32 light, Bool full)
{
	int Y = CPOS(pos[1]);
	if (Y < 0)
	{
		memset(light, 0, LIGHT_SIZE);
	}
	else if (Y >= c->maxy)
	{
		memset(light, 0xf0, LIGHT_SIZE);
	}
	else if (full) /* grab the 27 block/sky light value and get max value on each corner */
	{
		extern uint8_t skyBlockOffset[];
		static uint8_t shiftValues[] = { /* 4 entries per face, ordered S,E,N,W,T,B */
			16,0,8,24, 16,0,8,24, 16,0,8,24, 16,0,8,24, 0,16,24,8, 16,0,8,24
		};
		struct BlockIter_t iter;
		uint8_t skyBlockLight[27];
		uint8_t x, y, z, i;
		ChunkData cd = c->layer[Y];
		mapInitIterOffset(&iter, cd, CHUNK_POS2OFFSET(c, pos));
		mapIter(&iter, -1, -1, -1);
		for (y = i = 0; ; )
		{
			for (z = 0; ; )
			{
				for (x = 0; ; )
				{
					skyBlockLight[i++] = mapGetSkyBlockLight(&iter);
					x ++;
					if (x == 3) break;
					mapIter(&iter, 1, 0, 0);
				}
				z ++;
				if (z == 3) break;
				mapIter(&iter, -2, 0, 1);
			}
			y ++;
			if (y == 3) break;
			mapIter(&iter, -2, 1, -2);
		}
		DATA8 p;
		memset(light, 0, 24);
		for (i = 0, p = skyBlockOffset; i < 24; i ++)
		{
			uint8_t max;
			for (x = max = 0; x < 4; x ++, p ++)
			{
				uint8_t val = skyBlockLight[*p];
				uint8_t sky = val & 0xf0; val &= 0x0f;
				if ((max & 0xf0) < sky) max = (max & 0x0f) | sky;
				if ((max & 0x0f) < val) max = (max & 0xf0) | val;
			}
			light[i >> 2] |= max << shiftValues[i];
		}
	}
	else /* single block */
	{
		struct BlockIter_t iter;
		ChunkData cd = c->layer[Y];
		mapInitIterOffset(&iter, cd, CHUNK_POS2OFFSET(c, pos));
		memset(light, mapGetSkyBlockLight(&iter), LIGHT_SIZE);
	}
}

static EntityModel entityGetModelById(int modelBank)
{
	EntityBank bank;
	int i;

	for (i = modelBank, bank = HEAD(entities.banks); BANK_NUM(i); i --, NEXT(bank));

	return bank->models + (i >> 6);
}

/* model for full frame is oriented in the XY plane: grab coord of south face and apply entity transformation */
static void entityGetFrameCoord(Entity entity, float vertex[12])
{
	EntityModel model = entityGetModelById(entity->VBObank);

	blockGetBoundsForFace(model->bbox, SIDE_SOUTH, vertex, vertex+3, pos_000, 0);
	/* we need 3 points because rotation will move them, and we need to preserve backface orientation */
	vertex[6] = vertex[0];
	vertex[7] = vertex[4];
	vertex[8] = vertex[5];

	mat4 rotate;
	if (entity->rotation[0] > 0)
		matRotate(rotate, entity->rotation[0], VY);
	else
		matIdent(rotate);

	/* need to use the same transformation order than entity.vsh */
	if (entity->rotation[1] > 0)
	{
		mat4 RX;
		matRotate(RX, entity->rotation[1], VX);
		matMult3(rotate, rotate, RX);
	}
	matMultByVec3(vertex,   rotate, vertex);
	matMultByVec3(vertex+3, rotate, vertex+3);
	matMultByVec3(vertex+6, rotate, vertex+6);

	float num = entity->rotation[3];
	vec3AddMult(vertex,   entity->pos, num);
	vec3AddMult(vertex+3, entity->pos, num);
	vec3AddMult(vertex+6, entity->pos, num);

	/* 4th point: simple geometry from previous 3 points */
	vertex[VX+9] = vertex[VX] + (vertex[VX+3] - vertex[VX+6]);
	vertex[VY+9] = vertex[VY] + (vertex[VY+3] - vertex[VY+6]);
	vertex[VZ+9] = vertex[VZ] + (vertex[VZ+3] - vertex[VZ+6]);

//	fprintf(stderr, "coord = %g,%g,%g - %g,%g,%g\n", vertex[VX], vertex[VY], vertex[VZ], vertex[VX+3], vertex[VY+3], vertex[VZ+3]);
//	fprintf(stderr, "        %g,%g,%g - %g,%g,%g\n", vertex[VX+6], vertex[VY+6], vertex[VZ+6], vertex[VX+9], vertex[VY+9], vertex[VZ+9]);
}

/* change model used by an entity (update MDAI VBO) */
static void entityResetModel(Entity entity)
{
	int VBObank = entityGetModelId(entity);
	int curBank = BANK_NUM(entity->VBObank);
	int newBank = BANK_NUM(VBObank);

	if (curBank != newBank)
	{
		/* argh, need to dereference old model :-/ */
		EntityBank bank;
		int i;
		for (i = curBank, bank = HEAD(entities.banks); BANK_NUM(i); i --, NEXT(bank));
		glBindBuffer(GL_ARRAY_BUFFER, bank->vboMDAI);
		glBufferSubData(GL_ARRAY_BUFFER, entity->mdaiSlot * MDAI_SIZE, MDAI_SIZE, pos_000 /* set to 0 */);
		/* mark slot as free */
		i = entity->mdaiSlot;
		bank->mdaiUsage[i>>5] ^= 1 << (i & 31);
		entity->mdaiSlot = MDAI_INVALID_SLOT;
	}
	entityAddToCommandList(entity);
}

/* add the item within the frame in the list of entities to render */
static Entity entityItemFrameAddItem(Entity frame, int entityId)
{
	if (frame->map == 0)
	{
		uint16_t next;
		/* normal item in frame */
		Entity item = entityAlloc(&next);
		frame->next = next;
		item->ref = frame;
		item->next = ENTITY_END;
		item->blockId = frame->blockId & ~(ENTITY_ITEMFRAME | ENTITY_ITEM);
		item->tile = frame->tile;
		frame->blockId = 0;
		memcpy(item->motion, frame->motion, INFO_SIZE + 12);
		item->pos[VT] = 0; /* selection */
		item->rotation[3] = 0.4; /* scaling */
		if (item->blockId >= ID(256, 0))
			/* items are rendered in XZ plane, item frame are oriented in XY or ZY plane */
			item->rotation[1] = M_PI_2f - frame->rotation[1];
		item->VBObank = entityGetModelId(item);
		entityAddToCommandList(item);
		return item;
	}
	else /* filled map in frame */
	{
		float coord[12];
		int   VBObank = hashSearch(ENTITY_ITEMFRAME_FULL);
		if (frame->VBObank != VBObank)
		{
			/* map was placed in item frame: need to change frame model */
			frame->VBObank = VBObank;
			entityResetModel(frame);
		}
		entityGetFrameCoord(frame, coord);
		cartoAddMap(entityId, coord, frame->blockId & 0xffff, frame->light);
	}
	return frame;
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
		float  pos[11];
		int    off;

		/* iterate over the properties of one entity */
		NBTIter_t iter;
		NBT_InitIter(nbt, offset, &iter);
		memset(pos, 0, sizeof pos); id = NULL;
		pos[10] = 1;
		while ((off = NBT_Iter(&iter)) >= 0)
		{
			switch (FindInList("Motion,Pos,Rotation,id", iter.name, 0)) {
			case 0: NBT_ToFloat(nbt, off, pos,   3); break;
			case 1: NBT_ToFloat(nbt, off, pos+3, 3); break;
			case 2: NBT_ToFloat(nbt, off, pos+7, 2); break;
			case 3: id = NBT_Payload(nbt, off);
			}
		}

		if (id && !(pos[3] == 0 && pos[4] == 0 && pos[6] == 0))
		{
			uint16_t next;
			Entity entity = entityAlloc(&next);

			if (prev)
				prev->next = next;
			prev = entity;
			if (c->entityList == ENTITY_END)
				c->entityList = next;

			/* set entity->pos as well */
			memcpy(entity->motion, pos, sizeof pos);

			/* rotation also depends on how the initial model is oriented :-/ */
			entity->rotation[0] = fmod((360 - pos[7]) * M_PIf / 180, 2*M_PIf);
			entity->rotation[1] = - pos[8] * (2*M_PIf / 360);
			if (entity->rotation[1] < 0) entity->rotation[1] += 2*M_PIf;

			entity->tile = nbt->mem + offset;
			entity->next = ENTITY_END;
			entity->pos[VT] = 0;
			entity->name = id;
			entity->VBObank = entityGetModelId(entity);
			if (entity->VBObank == 0) /* unknwon entity */
				entity->pos[VY] += 0.5f;
			entityGetLight(c, pos+3, entity->light, entity->fullLight = entity->blockId > 0);
			entityAddToCommandList(entity);

			/* alloc an entity for the item in the frame */
			if (entity->blockId & ENTITY_ITEMFRAME)
				prev = entityItemFrameAddItem(entity, next+1);
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

#ifdef DEBUG /* stderr not available in release build */
void entityDebug(int id)
{
	Entity entity = entityGetById(id-1);

	fprintf(stderr, "entity %s at %g, %g, %g. NBT data:\n", entity->name, (double) entity->pos[0], (double) entity->pos[1], (double) entity->pos[2]);
	NBTFile_t nbt = {.mem = entity->tile};
	NBTIter_t iter;
	int       off;

	NBT_IterCompound(&iter, entity->tile);
	while ((off = NBT_Iter(&iter)) >= 0)
	{
		NBT_Dump(&nbt, off, 3, stderr);
	}
}
#endif

/* used by tooltip */
void entityInfo(int id, STRPTR buffer, int max)
{
	Entity entity = entityGetById(id-1);
	STRPTR name = NULL;
	int    count;

	count = sprintf(buffer, "<b>Entity</b>\nX: %g\nY: %g\nZ: %g\n", (double) entity->pos[0], (double) entity->pos[1], (double) entity->pos[2]);

	if ((id = entity->blockId & 0xffff) > 0)
	{
		if (id < ID(256, 0))
		{
			BlockState b = blockGetById(id);
			if (b) name = b->name;
		}
		else /* item */
		{
			ItemDesc desc = itemGetById(id);
			if (desc) name = desc->name;
		}
	}
	else if ((name = entity->name) == NULL)
	{
		name = "<unknown>";
	}
	if (name)
		count = StrCat(buffer, max, count, name);
	if (id > 0)
		count += sprintf(buffer + count, " <dim>(%d:%d)</dim>", id >> 4, id&15);

	if (fabsf(entity->rotation[0]) > EPSILON)
		count += sprintf(buffer + count, "\n<dim>Rotation Y:</dim> %g", (double) (entity->rotation[0] * RAD_TO_DEG));

	if (fabsf(entity->rotation[1]) > EPSILON)
		sprintf(buffer + count, "\n<dim>Rotation X:</dim> %g\n", (double) (entity->rotation[1] * RAD_TO_DEG));
}

/* mark new entity as selected and unselect old if any */
static void entitySetSelection(Entity entity, int entityId)
{
	if (entities.selected != entity)
	{
		EntityBank bank;
		int i;

		if (entities.selected)
		{
			/* unselect old */
			for (i = BANK_NUM(entities.selected->VBObank), bank = HEAD(entities.banks); i > 0; i --, NEXT(bank));
			if (! bank->dirty)
			{
				float val = 0;
				glBindBuffer(GL_ARRAY_BUFFER, bank->vboLoc);
				glBufferSubData(GL_ARRAY_BUFFER, entities.selected->mdaiSlot * INFO_SIZE + 12, 4, &val);
			}
			entities.selected->pos[VT] = 0;
			if (entities.selected->map)
				cartoSetSelect(entities.selectedId, False);
		}

		if (entity)
		{
			for (i = BANK_NUM(entity->VBObank), bank = HEAD(entities.banks); i > 0; i --, NEXT(bank));

			if (! bank->dirty)
			{
				/* selection flag is set directly in VBO meta-data */
				float val = 1;
				glBindBuffer(GL_ARRAY_BUFFER, bank->vboLoc);
				glBufferSubData(GL_ARRAY_BUFFER, entity->mdaiSlot * INFO_SIZE + 12, 4, &val);
			}
			if (entity->map)
				cartoSetSelect(entityId, True);
			entity->pos[VT] = 1;
		}
		entities.selected = entity;
		entities.selectedId = entityId;
	}
}

static void fillNormal(vec4 norm, int side)
{
	int8_t * normal = cubeNormals + side * 4;
	norm[VX] = normal[VX];
	norm[VY] = normal[VY];
	norm[VZ] = normal[VZ];
	norm[VT] = 1;
}

/*
 * Given a AABB rect:
 *  A +--------+ B
 *    |        |
 *  D +--------+ C
 * blockGetBoundsForFace() will give points A and C. We want A, B and D.
 */
static void AABBSplit(vec A, vec C, vec D, int norm)
{
	static uint8_t axisY[] = {VY, VY, VY, VY, VZ, VZ};
	uint8_t axis = axisY[norm];
	memcpy(D, A, 12);
	D[axis] = C[axis];
	C[axis] = A[axis];
}

/* check if <pos> is within rectangle formed by the 3 <points> */
static Bool pointIsInRect(vec points, vec4 pos)
{
	/* rect is not axis-aligned; method quoted from https://math.stackexchange.com/a/190373 */
	vec4 AM, AB, AD;
	vecSub(AM, pos,      points);
	vecSub(AB, points+3, points);
	vecSub(AD, points+6, points);

	float AMdotAB = vecDotProduct(AM, AB);
	float AMdotAD = vecDotProduct(AM, AD);

	return 0 < AMdotAB && AMdotAB <= vecDotProduct(AB, AB) &&
	       0 < AMdotAD && AMdotAD <= vecDotProduct(AD, AD);
}

int intersectRayPlane(vec4 P0, vec4 u, vec4 V0, vec norm, vec4 I);

static Bool entityInFrustum(vec4 pos)
{
	vec4 point;
	memcpy(point, pos, 12); point[VT] = 1;
	matMultByVec(point, globals.matMVP, point);
	return point[0] > -point[3] && point[0] < point[3] &&
	       point[1] > -point[3] && point[1] < point[3] &&
	       point[2] > -point[3] && point[2] < point[3];
}

/* check if vector <dir> intersects an entity bounding box (from position <camera>) */
int entityRaycast(Chunk c, vec4 dir, vec4 camera, vec4 cur, vec4 ret_pos)
{
	float maxDist = cur ? vecDistSquare(camera, cur) : 1e6f;
	int   flags = (dir[VX] < 0 ? 2 : 8) | (dir[VY] < 0 ? 16 : 32) | (dir[VZ] < 0 ? 1 : 4);
	int   i, id, curId;
	Chunk chunks[4];

	if ((c->cflags & CFLAG_GOTDATA) == 0)
		return 0;

	chunks[0] = c;
	chunks[1] = c + chunkNeighbor[c->neighbor + (flags & 2 ? 8 : 2)];
	chunks[2] = c + chunkNeighbor[c->neighbor + (flags & 1 ? 4 : 1)];
	chunks[3] = c + chunkNeighbor[c->neighbor + ((flags & 15) ^ 15)];

	/* scan 4 nearby chunk */
	for (i = 0, curId = -1; i < 4; i ++)
	{
		c = chunks[i];
		if (c->entityList == ENTITY_END) continue;
		Entity list = entityGetById(id = c->entityList);
		Entity best = NULL;
		for (;;)
		{
			/* just a quick heuristic to get rid of most entities */
			if (vecDistSquare(camera, list->pos) < maxDist * 1.5f && entityInFrustum(list->pos))
			{
				float points[9];
				mat4  rotation;
				vec4  norm, inter;
				int   j;

				/* order must be the same than entity.vsh */
				if (list->rotation[0] > 0)
					/* rotation along VY is CW, we want trigo here, hence the +3 */
					matRotate(rotation, list->rotation[0], VY);
				else
					matIdent(rotation);
				if (list->rotation[1] > 0)
				{
					mat4 RX;
					matRotate(RX, list->rotation[1], VX);
					matMult3(rotation, rotation, RX);
				}
				/* assume rectangular bounding box (not necessarily axis aligned though) */
				for (j = 0; j < 6; j ++)
				{
					fillNormal(norm, j);
					matMultByVec3(norm, rotation, norm);
					/* back-face culling */
					if (vecDotProduct(dir, norm) > 0) continue;

					EntityModel model = entityGetModelById(list->VBObank);
					blockGetBoundsForFace(model->bbox, j, points, points+3, pos_000, 0);
					AABBSplit(points, points + 3, points + 6, j);
					matMultByVec3(points,   rotation, points);
					matMultByVec3(points+3, rotation, points+3);
					matMultByVec3(points+6, rotation, points+6);
					float num = list->rotation[3];
					vec3AddMult(points,   list->pos, num);
					vec3AddMult(points+3, list->pos, num);
					vec3AddMult(points+6, list->pos, num);

					if (intersectRayPlane(camera, dir, points, norm, inter) &&
					    pointIsInRect(points, inter))
					{
						/* check if points are contained within the face */
						float dist = vecDistSquare(camera, inter);
						if (dist < maxDist)
						{
							maxDist = dist;
							memcpy(ret_pos, list->pos, 12);
							best = list;
							curId = id;
							/* need to check if there is a nearer entity first :-/ */
							break;
						}
					}
				}
			}
			if (list->next == ENTITY_END)
			{
				if (best)
				{
					entitySetSelection(best, curId+1);
					return curId+1;
				}
				if (entities.selected > 0)
					entitySetSelection(NULL, 0);
				break;
			}
			list = entityGetById(id = list->next);
		}
	}
	if (entities.selected > 0)
		entitySetSelection(NULL, 0);
	return 0;
}

/* remove any reference of this entity in all the data structure */
static uint16_t entityClear(EntityBuffer buf, int index)
{
	static MDAICmd_t clear = {0};
	EntityBank bank;

	Entity entity = buf->entities + index;
	buf->usage[index>>5] ^= 1 << (index & 31);
	buf->count --;
	entity->tile = NULL;

	for (index = BANK_NUM(entity->VBObank), bank = HEAD(entities.banks); index > 0; index --, NEXT(bank));
	index = entity->mdaiSlot;
	bank->mdaiUsage[index>>5] ^= 1 << (index & 31);
	glBindBuffer(GL_ARRAY_BUFFER, bank->vboMDAI);
	glBufferSubData(GL_ARRAY_BUFFER, index * 16, 16, &clear);

	return entity->next;
}

/* chunk <c> is about to be unloaded, remove all entities references we have here */
void entityUnload(Chunk c)
{
	int slot;
	for (slot = c->entityList; slot != ENTITY_END; )
	{
		int i;
		EntityBuffer buf;
		for (buf = HEAD(entities.list), i = slot >> ENTITY_SHIFT; i > 0; i --, NEXT(buf));
		slot &= ENTITY_BATCH-1;
		Entity entity = &buf->entities[slot];
		chunkDeleteTile(c, entity->tile);
		slot = entityClear(buf, slot);
	}
	c->entityList = ENTITY_END;
}

/* remove one entity from given chunk */
void entityDelete(Chunk c, DATA8 tile)
{
	DATA16 prev;
	int    slot;
	for (prev = &c->entityList, slot = *prev; slot != ENTITY_END; slot = *prev)
	{
		int i;
		EntityBuffer buf;
		for (buf = HEAD(entities.list), i = slot >> ENTITY_SHIFT; i > 0; i --, NEXT(buf));
		slot &= ENTITY_BATCH-1;
		Entity entity = buf->entities + slot;
		if (entity->tile == tile)
		{
			*prev = entity->next;
			entityClear(buf, slot);
			break;
		}
		prev = &entity->next;
	}
}

/* used to unlink entity from chunk's active list */
static DATA16 entityGetPrev(Chunk c, Entity entity, int id)
{
	uint16_t slot;
	DATA16   prev;

	for (prev = &c->entityList, slot = *prev; slot != ENTITY_END && slot != id; slot = *prev)
	{
		EntityBuffer buffer;
		int i = slot >> ENTITY_SHIFT;
		for (buffer = HEAD(entities.list); i > 0; i --, NEXT(buffer));
		prev = &buffer->entities[slot & (ENTITY_BATCH-1)].next;
	}
	return prev;
}

/* flag chunk for saving later */
static void entityMarkListAsModified(Map map, Chunk c)
{
	renderAddModif();
	mapAddToSaveList(map, c);
	if ((c->cflags & CFLAG_REBUILDETT) == 0)
		chunkUpdateEntities(c);
}

/* delete entity from memory and NBT */
void entityDeleteById(Map map, int id)
{
	EntityBuffer buffer;
	Entity entity;
	Chunk c;

	int i = id >> ENTITY_SHIFT;
	for (buffer = HEAD(entities.list); i > 0; i --, NEXT(buffer));
	id &= ENTITY_BATCH-1;
	entity = buffer->entities + id;
	c = mapGetChunk(map, entity->pos);

	if (c)
	{
		DATA16 prev;
		entityMarkListAsModified(map, c);

		/* unlink from chunk active entities */
		if (entity->ref || entity->map)
		{
			/* item in item frame: only delete this item */
			NBTFile_t nbt = {.mem = entity->tile};
			int item = NBT_FindNode(&nbt, 0, "Item");
			if (item >= 0)
			{
				/* unlink from chunk linked list */
				if (! entity->map)
				{
					prev = entityGetPrev(c, entity, id);
					*prev = entity->next;
				}

				nbt.mem = NBT_Copy(entity->tile);
				nbt.usage = NBT_Size(nbt.mem)+4; /* XXX want TAG_EndCompound too */
				NBT_Delete(&nbt, item, -1);
				// NBT_DumpCompound(&nbt);
				chunkDeleteTile(c, entity->tile);
				entity->name = NBT_Payload(&nbt, NBT_FindNode(&nbt, 0, "id"));
				/* item in frame is about to be deleted, modify item frame entity then */
				if (entity->ref)
					entity = entity->ref;
				/* NBT compound from item and frame is the same */
				entity->tile = nbt.mem;
				if (entity->map)
				{
					/* map removed from item frame: reset frame model to normal */
					entity->VBObank = hashSearch(ENTITY_ITEMFRAME);
					entity->map = 0;
					entityResetModel(entity);
					cartoDelMap(id+1);
				}
				else entityClear(buffer, id);
			}
		}
		else
		{
			prev = entityGetPrev(c, entity, id);
			*prev = entity->next;
			/* item in item frame: also delete */
			if (entity->next != ENTITY_END)
			{
				Entity item = entityGetById(entity->next);
				if (item && item->ref == entity)
				{
					*prev = item->next;
					entityClear(buffer, entity->next);
				}
			}

			chunkDeleteTile(c, entity->tile);
			entityClear(buffer, id);
		}
	}
}

/* count the entities in this linked list (used by chunk save) */
int entityCount(int start)
{
	int count, id;
	for (count = 0, id = start; id != ENTITY_END; count ++)
	{
		EntityBuffer buf;
		Entity       entity;
		int i = id >> ENTITY_SHIFT;
		for (buf = HEAD(entities.list); i > 0; i --, NEXT(buf));
		entity = &buf->entities[id & (ENTITY_BATCH-1)];
		/* entity->ref == item in item frame, item NBT will be saved with item frame */
		if (entity->ref) count --;
		id = entity->next;
	}
	return count;
}

/* used to save entities in chunk */
Bool entityGetNBT(NBTFile nbt, int * id)
{
	int curID = *id;
	if (curID == ENTITY_END)
		return False;
	Entity entity = entityGetById(*id);
	if (entity->ref)
		entity = entityGetById(entity->next);

	*id = entity->next;
	nbt->mem = entity->tile;
	nbt->usage = NBT_Size(entity->tile);
	return True;
}

/* add the pre-defined fields of entities in the <nbt> fragment */
static void entityCreateGeneric(NBTFile nbt, Entity entity, int itemId, int side)
{
	EntityUUID_t uuid;
	TEXT         id[64];
	double       pos64[3];
	float        rotation[2];
	DATA8        p, e;

	pos64[VX] = entity->pos[VX];
	pos64[VY] = entity->pos[VY];
	pos64[VZ] = entity->pos[VZ];
	rotation[0] = 360 - entity->rotation[0] * RAD_TO_DEG;
	rotation[1] = - entity->rotation[1] * RAD_TO_DEG;
	for (p = uuid.uuid8, e = p + sizeof uuid.uuid8; p < e; *p++ = rand());
	itemGetTechName(itemId, id, sizeof id);
	NBT_Add(nbt,
		TAG_List_Double, "Motion", 3,
		TAG_Byte,        "Facing", 0,
		TAG_Long,        "UUIDLeast", uuid.uuid64[0],
		TAG_Long,        "UUIDMost",  uuid.uuid64[1],
		TAG_Int,         "Dimension", 0,
		TAG_List_Float,  "Rotation", 2 | NBT_WithInit, rotation,
		TAG_List_Double, "Pos", 3 | NBT_WithInit, pos64,
		TAG_String,      "id", id,
		TAG_End
	);
}

static void entityFillPos(vec4 dest, vec4 src, int side, int orientX, vec size)
{
	static float orientY[] = {0, 90 * DEG_TO_RAD, 180 * DEG_TO_RAD, 270 * DEG_TO_RAD};
	enum {
		HALFVX,
		HALFVY,
		PLUSVZ,
		MINUSVZ
	};
	#define SHIFT(x, y, z)   (x | (y << 2) | (z << 4))
	static uint8_t shifts[] = { /* S, E, N, W, T, B */
		SHIFT(HALFVX,  HALFVY,  PLUSVZ),
		SHIFT(PLUSVZ,  HALFVY,  HALFVX),
		SHIFT(HALFVX,  HALFVY,  MINUSVZ),
		SHIFT(MINUSVZ, HALFVY,  HALFVX),
		SHIFT(HALFVX,  PLUSVZ,  HALFVY),
		SHIFT(HALFVX,  MINUSVZ, HALFVY),
	};
	#undef SHIFT

	int8_t * norm = cubeNormals + side * 4, i;
	uint8_t  shift = shifts[side];

	for (i = 0; i < 3; i ++)
		dest[i] = src[i] + (norm[i] <= 0 ? 0 : 1);

	for (i = 0; i < 3; i ++, shift >>= 2)
	{
		switch (shift & 3) {
		case HALFVX:  dest[i] += size[VX] * 0.5f; break;
		case HALFVY:  dest[i] += size[VY] * 0.5f; break;
		case PLUSVZ:  dest[i] += size[VZ] * 0.5f; break;
		case MINUSVZ: dest[i] -= size[VZ] * 0.5f;
		}
	}

	/* also fill rotation value */
	dest[3] = dest[6] = 0;
	dest[7] = 1;
	dest[4] = orientY[side >= SIDE_TOP ? opp[globals.direction] : side];
	dest[5] = orientX * DEG_TO_RAD;
	if (dest[5] < 0)
		dest[5] += 2*M_PIf;
}

/* get exact coordinates of entity model in world coordinates */
static void entityGetCoord(float outCoord[6], float posAndRot[6], VTXBBox bbox)
{
	uint8_t i;
	for (i = 0; i < 3; i ++)
	{
		outCoord[i] = FROMVERTEX(bbox->pt1[i]);
		outCoord[i+3] = FROMVERTEX(bbox->pt2[i]);
	}

	if (posAndRot[5] > 0)
	{
		mat4 rotateX;
		matRotate(rotateX, posAndRot[5], VX);
		matMultByVec3(outCoord,   rotateX, outCoord);
		matMultByVec3(outCoord+3, rotateX, outCoord+3);
	}

	if (posAndRot[4] > 0)
	{
		mat4 rotateY;
		matRotate(rotateY, posAndRot[4], VY);
		matMultByVec3(outCoord,   rotateY, outCoord);
		matMultByVec3(outCoord+3, rotateY, outCoord+3);
	}
	float tmp;
	if (outCoord[VX] > outCoord[VX+3]) swap_tmp(outCoord[VX], outCoord[VX+3], tmp);
	if (outCoord[VY] > outCoord[VY+3]) swap_tmp(outCoord[VY], outCoord[VY+3], tmp);
	if (outCoord[VZ] > outCoord[VZ+3]) swap_tmp(outCoord[VZ], outCoord[VZ+3], tmp);

	vecAdd(outCoord,   outCoord,   posAndRot);
	vecAdd(outCoord+3, outCoord+3, posAndRot);
}


/* check if bounding box of entity overlaps another entity/blocks */
static Bool entityFitIn(int entityId, float posAndRot[8], VTXBBox bbox)
{
	float coord[6];
	float diff[3];

	entityGetCoord(coord, posAndRot, bbox);
	vecSub(diff, coord + 3, coord);
	if (diff[0] < diff[1]) diff[0] = diff[1];
	if (diff[0] < diff[2]) diff[0] = diff[2];

	while (entityId != ENTITY_END)
	{
		Entity      entity = entityGetById(entityId);
		EntityModel model  = entityGetModelById(entity->VBObank);
		VTXBBox     size   = model->bbox;
		float       maxSz  = 0;
		uint8_t     i;

		if (entity->ref)
		{
			/* entity within another: check <ref> instead */
			entityId = entity->next;
			continue;
		}

		for (i = 0; i < 3; i ++)
		{
			float sz = (size->pt2[i] - size->pt1[i]) * (1.f/BASEVTX);
			if (maxSz < sz) maxSz = sz;
		}
		maxSz += diff[0];
		maxSz *= maxSz;

		/* quick heuristic */
		if (vecDistSquare(posAndRot, entity->pos) < maxSz)
		{
			/* need more expansive check */
			float coord2[6];
			entityGetCoord(coord2, entity->pos, size);
			if (coord[VX] < coord2[VX+3] && coord[VX+3] > coord2[VX] &&
				coord[VY] < coord2[VY+3] && coord[VY+3] > coord2[VY] &&
				coord[VZ] < coord2[VZ+3] && coord[VZ+3] > coord2[VZ])
				return False;
		}
		entityId = entity->next;
	}
	return True;
}



void entityCreatePainting(Map map, int id)
{
	NBTFile_t nbt = {.page = 127};
	STRPTR    name;
	Entity    entity;
	uint16_t  slot;
	Chunk     c;
	STRPTR    buffer;
	float     size[3];
	float     posAndRot[8];
	DATA8     loc;

	/* .location contains 4 bytes of each painting where they are location in terrain.png (in tile coord) */
	loc = paintings.location + id * 4;
	for (name = paintings.names; id > 0; name = strchr(name, ',') + 1, id --);
	buffer = name; name = strchr(name, ','); if (name) *name = 0;
	buffer = STRDUPA(buffer); if (name) *name = ',';
	size[VX] = loc[2] - loc[0];
	size[VY] = loc[3] - loc[1];
	size[VZ] = 1/16.;

	entityFillPos(posAndRot, entities.createPos, entities.createSide, 0, size);
	c = mapGetChunk(map, posAndRot);
	if (c == NULL) return; /* outside map? */
	if (! entityFitIn(c->entityList, posAndRot, entityGetModelById(hashSearch(ENTITY_ITEMFRAME))->bbox))
	{
		/* does not fit: cancel creation */
		fprintf(stderr, "can't fit painting in %g, %g, %g\n", (double) posAndRot[VX], (double) posAndRot[VY], (double) posAndRot[VZ]);
		return;
	}

	entity = entityAlloc(&slot);
	memcpy(entity->pos, posAndRot, sizeof posAndRot);
	entityCreateGeneric(&nbt, entity, ID(321, 0), entities.createSide);
	NBT_Add(&nbt,
		TAG_String, "Motive", buffer,
		TAG_Compound_End
	);

	entity->next = c->entityList;
	entity->name = NBT_Payload(&nbt, NBT_FindNode(&nbt, 0, "id"));
	c->entityList = slot;

	entity->tile = nbt.mem;
	entity->rotation[3] = 1;
	entity->VBObank = entityGetModelId(entity);
	entityGetLight(c, entity->pos, entity->light, entity->fullLight = False);
	entityAddToCommandList(entity);

	/* flag chunk for saving later */
	entityMarkListAsModified(map, c);
}

static int entityCreateItemFrame(Map map, vec4 pos, int side)
{
	NBTFile_t nbt = {.page = 127};
	Entity    entity;
	uint16_t  slot;
	Chunk     c;
	float     size[3];
	float     posAndRot[8];

	size[VX] = 1;
	size[VY] = 1;
	size[VZ] = 1/16.;

	entityFillPos(posAndRot, pos, side, side == SIDE_TOP ? -90 : side == SIDE_BOTTOM ? 90 : 0, size);
	c = mapGetChunk(map, posAndRot);

	if (! entityFitIn(c->entityList, posAndRot, entityGetModelById(hashSearch(ENTITY_ITEMFRAME))->bbox))
	{
		/* does not fit: cancel creation */
		fprintf(stderr, "can't fit item frame in %g, %g, %g\n", (double) posAndRot[VX], (double) posAndRot[VY], (double) posAndRot[VZ]);
		return 0;
	}

	entity = entityAlloc(&slot);
	memcpy(entity->pos, posAndRot, sizeof posAndRot);
	if (c == NULL) return 0; /* outside map? */
	entityCreateGeneric(&nbt, entity, ID(389, 0), side);
	NBT_Add(&nbt, TAG_Compound_End);

	entity->next = c->entityList;
	entity->name = NBT_Payload(&nbt, NBT_FindNode(&nbt, 0, "id"));
	c->entityList = slot;

	entity->tile = nbt.mem;
	entity->rotation[3] = 1;
	entity->VBObank = entityGetModelId(entity);
	entityGetLight(c, entity->pos, entity->light, entity->fullLight = True);
	entityAddToCommandList(entity);
	entityMarkListAsModified(map, c);
	return slot + 1;
}

/* add some pre-defined entity in the world map */
int entityCreate(Map map, int itemId, vec4 pos, int side)
{
	ItemDesc desc = itemGetById(itemId);
	if (desc == NULL) return 0;
	switch (FindInList("item_frame,painting", desc->tech, 0)) {
	case 0: /* empty item frame */
		return entityCreateItemFrame(map, pos, side);
	case 1: /* ask for a painting first */
		if (side >= SIDE_TOP) break;
		memcpy(entities.createPos, pos, 12);
		entities.createSide = side;
		mceditUIOverlay(MCUI_OVERLAY_PAINTING);
	}
	return 0;
}

/* action on entity */
void entityUseItemOn(Map map, int entityId, int itemId, vec4 pos)
{
	Entity entity = entityGetById(entityId - 1);

	if (entity && itemGetByName(entity->name, True) == ID(389, 0) /* item frame */)
	{
		NBTFile_t tile  = {.mem = entity->tile};
		int       item  = NBT_FindNode(&tile, 0, "Item");
		Chunk     chunk = mapGetChunk(map, entity->pos);
		/* check if there is already an item in it */
		if (chunk && item < 0)
		{
			/* no: add it to the item frame */
			TEXT buffer[64];
			tile.mem = NULL;
			tile.page = 127;
			NBT_Add(&tile,
				TAG_Raw_Data, NBT_Size(entity->tile), entity->tile,
				TAG_Compound, "Item",
					TAG_String, "id", itemGetTechName(itemId & ~15, buffer, sizeof buffer),
					TAG_Byte,   "Count", 1,
					TAG_Short,  "Damage", itemId & 15,
					TAG_Compound_End
			);
			NBT_Add(&tile, TAG_Compound_End); /* end of whole entity */
			//NBT_DumpCompound(&tile);
			chunkDeleteTile(chunk, entity->tile);
			entity->name = NBT_Payload(&tile, NBT_FindNode(&tile, 0, "id"));
			entity->tile = tile.mem;
			entity->blockId = itemId | ENTITY_ITEM;
			entity->map = (itemId >> 4) == 358; /* filled map XXX need a better mechanism */
			if (entity->map) entity->blockId &= ~0xfff0;
			uint16_t next = entity->next;
			entity = entityItemFrameAddItem(entity, entityId);
			entity->next = next;
			entityMarkListAsModified(map, chunk);
		}
	}
}

/* get the block id (if any) stored in this entity */
int entityGetBlockId(int id)
{
	Entity entity = entityGetById(id-1);

	if (entity)
	{
		if (entity->blockId > 0) return entity->blockId & 0xffff;
		return itemGetByName(entity->name, True);
	}
	return 0;
}

void entityAnimate(void)
{
	EntityAnim anim;
	int i, j, time = globals.curTime, finalize = 0;
	for (i = entities.animCount, anim = entities.animate; i > 0; i --)
	{
		Entity entity = anim->entity;
		int remain = anim->stopTime - time;
		if (remain > 0)
		{
			for (j = 0; j < 3; j ++)
				entity->pos[j] += (entity->motion[j] - entity->pos[j]) * (time - anim->prevTime) / remain;
			anim->prevTime = time;
			/* update VBO */
			EntityBank bank;
			for (j = BANK_NUM(entity->VBObank), bank = HEAD(entities.banks); j > 0; j --, NEXT(bank));
			glBindBuffer(GL_ARRAY_BUFFER, bank->vboLoc);
			glBufferSubData(GL_ARRAY_BUFFER, entity->mdaiSlot * INFO_SIZE, 12, entity->pos);
			anim ++;
		}
		else /* anim done: remove entity */
		{
			DATA8 tile = entity->tile;
			vec4  dest;
			memcpy(dest, entity->motion, 12);
			entities.animCount --;
			/* remove from list */
			Chunk c = mapGetChunk(globals.level, dest);
			memmove(anim, anim + 1, (i - 1) * sizeof *anim);
			entityDelete(c, tile);
			updateFinished(tile, dest);
			finalize = 1;
		}
	}
	if (finalize)
		updateFinished(NULL, NULL);
}

/* block entity */
void entityUpdateOrCreate(Chunk c, vec4 pos, int blockId, vec4 dest, int ticks, DATA8 tile)
{
	EntityAnim anim;
	Entity     entity;
	uint16_t   slot;

	/* check if it is already in the list */
	for (slot = entities.animCount, anim = entities.animate, entity = NULL; slot > 0; slot --, anim ++)
	{
		entity = anim->entity;
		if (entity->tile == tile) break;
	}

	if (slot == 0)
	{
		entity = entityAlloc(&slot);
		entity->next = c->entityList;
		c->entityList = slot;
	}

	memcpy(entity->pos, pos, 12);
	memcpy(entity->motion, dest, 12);
	entity->blockId = blockId;
	entity->tile = tile;
	entity->rotation[3] = 1;
	vecAddNum(entity->pos,    0.5f);
	vecAddNum(entity->motion, 0.5f);
	entity->VBObank = entityGetModelId(entity);
	entityGetLight(c, pos, entity->light, entity->fullLight = True);
	entityAddToCommandList(entity);

	/* push it into the animate list */
	if (entities.animCount == entities.animMax)
	{
		entities.animMax += ENTITY_BATCH;
		entities.animate = realloc(entities.animate, entities.animMax * sizeof *entities.animate);
	}
	anim = entities.animate + entities.animCount;
	entities.animCount ++;
	anim->prevTime = (int) globals.curTime;
	anim->stopTime =
	#ifdef DEBUG
		anim->prevTime + ticks * 10 * (1000 / TICK_PER_SECOND);
	#else
		anim->prevTime + ticks * (1000 / TICK_PER_SECOND);
	#endif
	anim->entity = entity;

//	fprintf(stderr, "adding entity %d at %p / %d\n", entities.animCount, tile, slot);
}

/* sky/block light has changed in this chunk */
void entityUpdateLight(Chunk c)
{
	Entity entity;
	int    id;
	for (id = c->entityList; id != ENTITY_END; id = entity->next)
	{
		uint32_t light[LIGHT_SIZE/4];
		entity = entityGetById(id);
		if (entity->ref)
			memcpy(light, entity->ref->light, sizeof light);
		else
			entityGetLight(c, entity->pos, light, entity->fullLight);
		if (memcmp(light, entity->light, LIGHT_SIZE))
		{
			EntityBank bank;
			int j;
			memcpy(entity->light, light, LIGHT_SIZE);
			for (j = BANK_NUM(entity->VBObank), bank = HEAD(entities.banks); j > 0; j --, NEXT(bank));
			glBindBuffer(GL_ARRAY_BUFFER, bank->vboLoc);
			glBufferSubData(GL_ARRAY_BUFFER, entity->mdaiSlot * INFO_SIZE + INFO_SIZE-LIGHT_SIZE, LIGHT_SIZE, light);
			if (entity->map)
				cartoUpdateLight(id, entity->light);
		}
	}
}

#ifdef DEBUG
void entityDebugCmd(Chunk c)
{
	Entity entity;
	int id;
	fprintf(stderr, "========================================\n");
	for (id = c->entityList; id != ENTITY_END; id = entity->next)
	{
		entity = entityGetById(id);

		fprintf(stderr, "entity %d at %g, %g, %g: ", id, (double) entity->pos[0], (double) entity->pos[1], (double) entity->pos[2]);

		if (entity->ref)
		{
			TEXT name[64];
			itemGetTechName(entity->blockId & 0xffff, name, sizeof name);
			fprintf(stderr, "item in frame: %s\n", name);
		}
		else
		{
			fprintf(stderr, "%s", entity->name);
			if (entity->map)
				fprintf(stderr, " (map)");
			fputc('\n', stderr);
		}
	}
}
#endif

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

			glBufferData(GL_ARRAY_BUFFER, bank->mdaiMax * INFO_SIZE, NULL, GL_STATIC_DRAW);
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
					if (BANK_NUM(cur->VBObank) != i) continue;
					cur->mdaiSlot = mapFirstFree(bank->mdaiUsage, max);
					memcpy(loc, cur->pos, INFO_SIZE);
					EntityModel model = bank->models + (cur->VBObank >> 6);
					cmd->baseInstance = inst;
					cmd->count = model->count;
					cmd->first = model->first;
					cmd->instanceCount = 1;
					loc += INFO_SIZE/4; cmd ++; inst ++;
				}
			}
			glUnmapBuffer(GL_ARRAY_BUFFER);
			glUnmapBuffer(GL_DRAW_INDIRECT_BUFFER);
			bank->dirty = 0;
		}

		/* piston head will overdraw piston block causing z-fighting XXX messes item frame and maps :-/ */
//		glEnable(GL_POLYGON_OFFSET_FILL);
//		glPolygonOffset(-1.0, -1.0);

//		float curtime = globals.curTime;
//		setShaderValue(entities.shader, "curtime", 1, &curtime);
		glEnable(GL_CULL_FACE);
		glCullFace(GL_BACK);
		glUseProgram(entities.shader);
		glBindVertexArray(bank->vao);
		glBindBuffer(GL_DRAW_INDIRECT_BUFFER, bank->vboMDAI);
		glMultiDrawArraysIndirect(GL_TRIANGLES, 0, bank->mdaiCount, 0);
		glDisable(GL_POLYGON_OFFSET_FILL);
	}
}
