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
		loc[2] = cust.model[1] * 0.0625 + loc[0];
		loc[3] = cust.model[2] * 0.0625 + loc[1];
		break;
	case 1:
		modelId = ENTITY_ITEMFRAME;
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
				/* use inventory item for this */
				DATA16 model = blockIds[id>>4].model;
				if (model)
				{
					count = model[-1];
					memcpy(buffer, model, count * BYTES_PER_VERTEX);
					break;
				}
			}
		case TRANS:
			count = blockInvModelCube(buffer, b, texCoord);
			bbox  = blockGetBBox(b);
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
				bbox  = blockGetBBox(b);
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
		bbox = entityAllocBBox();

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
		/* 9 floats for 3x3 model matrix */
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

	return memset(buffer->entities + slot, 0, sizeof (struct Entity_t));
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
		entity->pos[VY] += 0.5; // XXX not sure why
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
			STRPTR  id = NBT_Payload(&nbt, NBT_FindNode(&nbt, item, "id"));
			uint8_t data = NBT_ToInt(&nbt, NBT_FindNode(&nbt, item, "Damage"), 0);
			int blockId = itemGetByName(id, True);

			if (blockId > 0)
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
				entity->pos[VY] += 0.25;
			return entityAddModel(entity->blockId = blockId | data, 0, NULL);
		}
	}
	return ENTITY_UNKNOWN;
}

static void entityAddToCommandList(Entity entity)
{
	EntityBank bank;
	int VBObank = entity->VBObank;
	int i, slot;

	for (i = VBObank & 63, bank = HEAD(entities.banks); i > 0; i --, NEXT(bank));

	if (bank->mdaiCount < bank->mdaiMax)
	{
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
		memset(light, 240, LIGHT_SIZE);
	}
	else if (full) /* grab the 27 block/sky light value and get max value on each corner */
	{
		extern uint8_t skyBlockOffset[];
		static uint8_t shiftValues[] = { /* 4 entries per face, ordered S,E,N,W,T,B */
			0,8,24,16, 0,8,24,16, 0,8,24,16, 0,8,24,16, 0,16,24,8, 16,0,8,25
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
		NBTIter_t entity;
		NBT_InitIter(nbt, offset, &entity);
		memset(pos, 0, sizeof pos); id = NULL;
		pos[10] = 1;
		while ((off = NBT_Iter(&entity)) >= 0)
		{
			switch (FindInList("Motion,Pos,Rotation,id", entity.name, 0)) {
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
			entity->rotation[0] = fmod((360 - pos[7]) * M_PI / 180, 2*M_PI);
			entity->rotation[1] = - pos[8] * (2*M_PI / 360);
			if (entity->rotation[1] < 0) entity->rotation[1] += 2*M_PI;

			entity->tile = nbt->mem + offset;
			entity->next = ENTITY_END;
			entity->pos[VT] = 0;
			entity->name = id;
			entity->VBObank = entityGetModelId(entity);
			if (entity->VBObank == 0) /* unknwon entity */
				entity->pos[1] += 0.5;
			entityGetLight(c, pos+3, entity->light, entity->blockId > 0);
			entityAddToCommandList(entity);

			/* alloc an entity for the item in the frame */
			if (entity->blockId & ENTITY_ITEMFRAME)
			{
				Entity item = entityAlloc(&next);
				prev->next = next;
				item->ref = entity;
				item->next = ENTITY_END;
				item->blockId = entity->blockId & ~ENTITY_ITEMFRAME;
				item->tile = entity->tile;
				entity->blockId = 0;
				memcpy(item->motion, entity->motion, INFO_SIZE + 12);
				item->rotation[3] = 0.4; /* scaling */
				if (item->blockId >= ID(256, 0))
					/* items are rendered in XZ plane, item frame are oriented in XY or ZY plane */
					item->rotation[1] = M_PI_2;
				item->VBObank = entityGetModelId(item);
				entityAddToCommandList(item);
				prev = item;
			}
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

	for (bank = HEAD(entities.banks); i & 63; i --, NEXT(bank));

	return bank->models + (i >> 6);
}

#ifdef DEBUG /* stderr not available in release build */
void entityDebug(int id)
{
	Entity entity = entityGetById(id-1);

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
#endif

/* used by tooltip */
void entityInfo(int id, STRPTR buffer, int max)
{
	Entity entity = entityGetById(id-1);
	STRPTR name;
	int    count;

	count = sprintf(buffer, "<b>Entity</b>\nX: %g\nY: %g\nZ: %g\n", entity->pos[0], entity->pos[1], entity->pos[2]);

	if ((id = entity->blockId) > 0)
	{
		id &= ~ENTITY_ITEM;
		BlockState b = blockGetById(id);
		if (b == NULL)
			name = NULL;
		else
			name = b->name;
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
		sprintf(buffer + count, "\n<dim>Rotation:</dim> %g\n", entity->rotation[0] * 180 / M_PI);
}

/* mark new entity as selected and unselect old if any */
static void entitySetSelection(Entity entity)
{
	if (entities.selected != entity)
	{
		EntityBank bank;
		int i;

		if (entities.selected)
		{
			for (i = entities.selected->VBObank & 63, bank = HEAD(entities.banks); i > 0; i --, NEXT(bank));
			if (! bank->dirty)
			{
				float val = 0;
				glBindBuffer(GL_ARRAY_BUFFER, bank->vboLoc);
				glBufferSubData(GL_ARRAY_BUFFER, entities.selected->mdaiSlot * INFO_SIZE + 12, 4, &val);
			}
			entities.selected->pos[VT] = 0;
		}

		if (entity)
		{
			for (i = entity->VBObank & 63, bank = HEAD(entities.banks); i > 0; i --, NEXT(bank));

			if (! bank->dirty)
			{
				/* selection flag is set directly in VBO meta-data */
				float val = 1;
				glBindBuffer(GL_ARRAY_BUFFER, bank->vboLoc);
				glBufferSubData(GL_ARRAY_BUFFER, entity->mdaiSlot * INFO_SIZE + 12, 4, &val);
			}
			entity->pos[VT] = 1;
		}
		entities.selected = entity;
	}
}

static void fillNormal(vec4 norm, int side)
{
	int8_t * normal = normals + side * 4;
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

	return 0 <= AMdotAB && AMdotAB <= vecDotProduct(AB, AB) &&
	       0 <= AMdotAD && AMdotAD <= vecDotProduct(AD, AD);
}

int intersectRayPlane(vec4 P0, vec4 u, vec4 V0, vec norm, vec4 I);

/* check if vector <dir> intersects an entity bounding box (from position <camera>) */
int entityRaycast(Chunk c, vec4 dir, vec4 camera, vec4 cur, vec4 ret_pos)
{
	float maxDist = cur ? vecDistSquare(camera, cur) : 1e6;
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
			if (vecDistSquare(camera, list->pos) < maxDist * 1.5)
			{
				float points[9];
				mat4  rotation;
				vec4  norm, inter;
				int   j;
				/* assume rectangular bounding box (not necessarily axis aligned though) */
				matRotate(rotation, list->rotation[0], VY);
				for (j = 0; j < 6; j ++)
				{
					static float pos_000[3];
					//if (globals.breakPoint)
					//	puts("here");
					fillNormal(norm, j);
					matMultByVec3(norm, rotation, norm);
					/* back-face culling */
					if (vecDotProduct(dir, norm) > 0) continue;

					EntityModel model = entityGetModelById(list);
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
					entitySetSelection(best);
					return curId+1;
				}
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

/* remove any reference of this entity in all the data structure */
static uint16_t entityClear(EntityBuffer buf, int index)
{
	static MDAICmd_t clear = {0};
	EntityBank bank;

	Entity entity = buf->entities + index;
	buf->usage[index>>5] ^= 1 << (index & 31);
	buf->count --;
	entity->tile = NULL;

	for (index = entity->VBObank & 63, bank = HEAD(entities.banks); index > 0; index --, NEXT(bank));
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
		mapAddToSaveList(map, c);
		if ((c->cflags & CFLAG_REBUILDETT) == 0)
			chunkUpdateEntities(c);

		/* unlink from chunk active entities */
		if (entity->ref)
		{
			/* item in item frame: only delete this item */
			NBTFile_t nbt = {.mem = entity->tile};
			int item = NBT_FindNode(&nbt, 0, "Item");
			if (item >= 0)
			{
				nbt.mem = NBT_Copy(entity->tile);
				nbt.usage = NBT_Size(nbt.mem);
				NBT_Delete(&nbt, item, 0);
				chunkDeleteTile(c, entity->tile);
				entityClear(buffer, id);
				entity->tile = nbt.mem;
			}
		}
		else
		{
			DATA16 prev;
			uint16_t slot;
			for (prev = &c->entityList, slot = *prev; slot != id; slot = *prev)
			{
				i = slot >> ENTITY_SHIFT;
				for (buffer = HEAD(entities.list); i > 0; i --, NEXT(buffer));
				prev = &buffer->entities[slot & (ENTITY_BATCH-1)].next;
			}
			*prev = entity->next;
		}

		chunkDeleteTile(c, entity->tile);
		entityClear(buffer, id);
	}
}

/* count the entities in this linked list (used by chunk save) */
int entityCount(int start)
{
	int count, id;
	for (count = 0, id = start; id != ENTITY_END; count ++)
	{
		EntityBuffer buf;
		int i = id >> ENTITY_SHIFT;
		for (buf = HEAD(entities.list); i > 0; i --, NEXT(buf));
		id = buf->entities[id & (ENTITY_BATCH-1)].next;
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

	*id = entity->next;
	nbt->mem = entity->tile;
	nbt->usage = NBT_Size(entity->tile);
	return True;
}

/* add the pre-defined fields of entities in the <nbt> fragment */
static void entityCreateGeneric(NBTFile nbt, Entity entity, int itemId, int side)
{
	static float orient[] = {0, 270, 180, 90, 0, 0};
	EntityUUID_t uuid;
	TEXT         id[64];
	double       pos64[3];
	DATA8        p, e;

	pos64[VX] = entity->pos[VX];
	pos64[VY] = entity->pos[VY];
	pos64[VZ] = entity->pos[VZ];
	entity->rotation[0] = orient[side];
	entity->rotation[1] = 0;
	for (p = uuid.uuid8, e = p + sizeof uuid.uuid8; p < e; *p++ = rand());
	itemGetTechName(itemId, id, sizeof id);
	NBT_Add(nbt,
		TAG_List_Double, "Motion", 3,
		TAG_Byte,        "Facing", 0,
		TAG_Long,        "UUIDLeast", uuid.uuid64[0],
		TAG_Long,        "UUIDMost",  uuid.uuid64[1],
		TAG_Int,         "Dimension", 1,
		TAG_List_Float,  "Rotation", 2 | NBT_WithInit, entity->rotation,
		TAG_List_Double, "Pos", 3 | NBT_WithInit, pos64,
		TAG_String,      "id", id,
		TAG_End
	);
	/* convert rotation back to trigonometric */
	entity->rotation[0] = (360 - entity->rotation[0]) * M_PI / 180;
	entity->rotation[1] = - entity->rotation[1] * (2*M_PI / 360);
}

static void entityFillPos(vec4 dest, vec4 src, int side, vec size)
{
	enum {
		HALFVX,
		HALFVY,
		PLUSVZ,
		MINUSVZ
	};
	#define SHIFT(x, y, z)   (x | (y << 2) | (z << 4))
	static uint8_t shifts[] = {
		SHIFT(HALFVX,  HALFVY, PLUSVZ),
		SHIFT(PLUSVZ,  HALFVY, HALFVX),
		SHIFT(HALFVX,  HALFVY, MINUSVZ),
		SHIFT(MINUSVZ, HALFVY, HALFVX)
	};
	#undef SHIFT

	fprintf(stderr, "side = %d\n", side);

	int8_t * norm = normals + side * 4, i;
	uint8_t  shift = shifts[side];

	for (i = 0; i < 3; i ++)
		dest[i] = src[i] + (norm[i] <= 0 ? 0 : 1);

	for (i = 0; i < 3; i ++, shift >>= 2)
	{
		switch (shift & 3) {
		case HALFVX:  dest[i] += size[VX] * 0.5; break;
		case HALFVY:  dest[i] += size[VY] * 0.5; break;
		case PLUSVZ:  dest[i] += size[VZ] * 0.5; break;
		case MINUSVZ: dest[i] -= size[VZ] * 0.5;
		}
	}
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
	DATA8     loc;

	loc = paintings.location + id * 4;
	for (name = paintings.names; id > 0; name = strchr(name, ',') + 1, id --);
	buffer = name; name = strchr(name, ','); if (name) *name = 0;
	buffer = STRDUPA(buffer); if (name) *name = ',';
	size[VX] = loc[2] - loc[0];
	size[VY] = loc[3] - loc[1];
	size[VZ] = 1/16.;

	entity = entityAlloc(&slot);
	entityFillPos(entity->pos, entities.createPos, entities.createSide, size);
	c = mapGetChunk(map, entity->pos);
	if (c == NULL) return; /* outside map? */
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
	entityGetLight(c, entity->pos, entity->light, True);
	entityAddToCommandList(entity);
}

static void entityCreateItemFrame(Map map, vec4 pos, int side)
{
	NBTFile_t nbt = {.page = 127};
	Entity    entity;
	uint16_t  slot;
	Chunk     c;
	float     size[3];

	size[VX] = 1;
	size[VY] = 1;
	size[VZ] = 1/16.;

	entity = entityAlloc(&slot);
	entityFillPos(entity->pos, pos, side, size);
	c = mapGetChunk(map, entity->pos);
	if (c == NULL) return; /* outside map? */
	entityCreateGeneric(&nbt, entity, ID(389, 0), side);
	NBT_Add(&nbt, TAG_Compound_End);

	entity->next = c->entityList;
	entity->name = NBT_Payload(&nbt, NBT_FindNode(&nbt, 0, "id"));
	c->entityList = slot;

	entity->tile = nbt.mem;
	entity->rotation[3] = 1;
	entity->VBObank = entityGetModelId(entity);
	entityGetLight(c, entity->pos, entity->light, True);
	entityAddToCommandList(entity);
}

/* add some pre-defined entity in the world map */
void entityCreate(Map map, int itemId, vec4 pos, int side)
{
	ItemDesc desc = itemGetById(itemId);
	if (desc == NULL) return;
	switch (FindInList("item_frame,painting", desc->tech, 0)) {
	case 0: /* empty item frame */
		entityCreateItemFrame(map, pos, side);
		break;
	case 1: /* ask for a painting first */
		if (side >= SIDE_TOP) return;
		memcpy(entities.createPos, pos, 12);
		entities.createSide = side;
		mceditUIOverlay(MCUI_OVERLAY_PAINTING);
	}
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
			for (j = entity->VBObank & 63, bank = HEAD(entities.banks); j > 0; j --, NEXT(bank));
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
	vecAddNum(entity->pos,    0.5);
	vecAddNum(entity->motion, 0.5);
	entity->VBObank = entityGetModelId(entity);
	entityGetLight(c, pos, entity->light, True);
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
		entityGetLight(c, entity->pos, light, entity->blockId > 0);
		if (memcmp(light, entity->light, LIGHT_SIZE) == 0)
		{
			EntityBank bank;
			int j;
			memcpy(entity->light, light, LIGHT_SIZE);
			for (j = entity->VBObank & 63, bank = HEAD(entities.banks); j > 0; j --, NEXT(bank));
			glBindBuffer(GL_ARRAY_BUFFER, bank->vboLoc);
			glBufferSubData(GL_ARRAY_BUFFER, entity->mdaiSlot * INFO_SIZE + INFO_SIZE-LIGHT_SIZE, LIGHT_SIZE, light);
		}
	}
}

#ifdef DEBUG
void entityDebugCmd(Chunk c)
{
	Entity entity;
	int id;
	for (id = c->entityList; id != ENTITY_END; id = entity->next)
	{
		entity = entityGetById(id);

		fprintf(stderr, "entity %d at %g, %g, %g: %s\n", id, entity->pos[0], entity->pos[1], entity->pos[2], entity->name);

		if (entity->blockId != ID(1, 0)) continue;
		#if 0
		EntityModel model;
		EntityBank  bank;
		uint32_t    light[2];
		int i;
		for (i = entity->VBObank & 63, bank = HEAD(entities.banks); i > 0; i --, NEXT(bank));
		model = bank->models + (entity->VBObank >> 6);
		int count = model->count * BYTES_PER_VERTEX;
		DATA16 buffer = alloca(count);

		light[0] = light[1] = 0;
		glBindBuffer(GL_ARRAY_BUFFER, bank->vboLoc);
		glGetBufferSubData(GL_ARRAY_BUFFER, entity->mdaiSlot * INFO_SIZE + 24, LIGHT_SIZE, light);

		glBindBuffer(GL_ARRAY_BUFFER, bank->vboModel);
		glGetBufferSubData(GL_ARRAY_BUFFER, model->first * BYTES_PER_VERTEX, count, buffer);
		while (count > 0)
		{
			vec4 pos = {
				(buffer[0] - 15360) * 0.00048828125 /* 1/2048. */,
				(buffer[1] - 15360) * 0.00048828125,
				(buffer[2] - 15360) * 0.00048828125
			};
			uint32_t corner = (pos[VX] <= 0.5 ? 0 : 1) + (pos[VZ] <= 0.5 ? 0 : 2) + (pos[VY] <= 0.5 ? 0 : 4);
			uint32_t meta;

			if (corner < 4)
				meta = light[0] >> (corner << 3);
			else
				meta = light[1] >> ((corner - 4) << 3);

			fprintf(stderr, "vertex = %g,%g,%g, NORM = %d, sky = %d, block = %d\n", pos[0], pos[1], pos[2], GET_NORMAL(buffer), (meta & 0xf0) >> 4, meta & 15);
			count  -= BYTES_PER_VERTEX;
			buffer += INT_PER_VERTEX;
		}
		#endif
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
					if ((cur->VBObank & 63) != i) continue;
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

		/* piston head will overdraw piston block causing z-fighting */
		glEnable(GL_POLYGON_OFFSET_FILL);
		glPolygonOffset(-1.0, 1.0);

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
