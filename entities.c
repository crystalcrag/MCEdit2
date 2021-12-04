/*
 * entities.c : manage the list of active entity surrounding the player.
 *
 * see doc/internals.html to have an overview on how data structure works here.
 * this file mostly contains generic functions for manipulating entities.
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
#include "redstone.h" /* TICK_PER_SECOND */
#include "blockUpdate.h"
#include "mapUpdate.h"
#include "cartograph.h"
#include "render.h"
#include "globals.h"
#include "worldItems.h"
#include "glad.h"

struct EntitiesPrivate_t entities;
struct Paintings_t       paintings;

static void hashAlloc(int);

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
		modelId = ITEMID(ENTITY_PAINTINGS, paintings.count);
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
		modelId = name && atoi(name) == 1 ? ITEMID(ENTITY_ITEMFRAME_FULL,0) : ITEMID(ENTITY_ITEMFRAME,0);
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
int entityGetModelBank(ItemID_t id)
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

static void hashInsert(ItemID_t id, int VBObank)
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
static int entityModelCount(ItemID_t id, int cnx)
{
	if (id & ENTITY_ITEM) /* same as normal block/item */
		id &= ~ENTITY_ITEM;
	if (isBlockId(id))
	{
		/* normal block */
		id &= 0xffff;
		BlockState b = blockGetById(id);
		Block desc = &blockIds[id >> 4];
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
			if (desc->model) /* custom inventory model */
				return desc->model[-1];
			if (b->custModel == NULL) return 36; /* assume cube, if no model */
			if (cnx == 0)
			{
				cnx = b->custModel[-1];
				if (b->special == BLOCK_SOLIDOUTER)
					cnx += 36;
			}
			else cnx = blockInvCountVertex(b->custModel, cnx);
			return cnx;
		}
	}
	else return itemGenMesh(id, NULL);
	return 0;
}

/* generate vertex data for entity shader */
static int entityGenModel(EntityBank bank, ItemID_t itemId, int cnx, CustModel cust)
{
	glBindBuffer(GL_ARRAY_BUFFER, bank->vboModel);
	DATA16   buffer = glMapBuffer(GL_ARRAY_BUFFER, GL_READ_WRITE);
	VTXBBox  bbox   = NULL;
	int      count  = 0;
	int      item   = 0;
	uint8_t  cpBBox = 0;
	uint16_t U, V;
	itemId &= ~ENTITY_ITEM;

	U = V = 0;
	buffer += bank->vtxCount * INT_PER_VERTEX;
	if (isBlockId(itemId))
	{
		/* normal block */
		itemId &= 0xffff;
		BlockState b = blockGetById(itemId);
		Block desc = &blockIds[itemId>>4];
		if (itemId == 0)
		{
			/* used by unknown entity */
			static struct BlockState_t unknownEntity = {
				0, CUBE3D, 0, NULL, 31,13,31,13,31,13,31,13,31,13,31,13
			};
			count = blockInvModelCube(buffer, &unknownEntity, texCoord);
		}
		else if ((b->inventory & MODELFLAGS) == ITEM2D)
		{
			count = itemGenMesh(itemId, buffer);
		}
		else switch (b->type) {
		case SOLID:
			if (b->special == BLOCK_STAIRS)
			{
				/* use inventory item for these */
				DATA16 model = blockIds[itemId>>4].model;
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
			if (desc->model)
			{
				count = desc->model[-1];
				memcpy(buffer, desc->model, count * BYTES_PER_VERTEX);
			}
			else if (b->custModel)
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
	else count = itemGenMesh(itemId, buffer), item = 1;

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
	float maxSize;
	blockCenterModel(buffer, count, U, V, ! item, bbox, &maxSize);
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
	models->maxSize = maxSize;
	bank->modelCount ++;

	/* VBObank: 6 first bits are for bank number, 10 next are for model number (index in bank->models) */
	for (max <<= 6; bank->node.ln_Prev; max ++, PREV(bank));

	//fprintf(stderr, "model %d, first: %d, count: %d, id: %x\n", max >> 6, models->first, count, id);

	hashInsert(itemId, max);

	return max;
}

static void entityInitVAO(EntityBank bank, int vtxCount)
{
	glGenVertexArrays(1, &bank->vao);
	glGenBuffers(3, &bank->vboModel); /* + vboLoc and vboMDAI */

	/* same vertex format than items.vsh */
	glBindVertexArray(bank->vao);
	glBindBuffer(GL_ARRAY_BUFFER, bank->vboModel);
	glBufferData(GL_ARRAY_BUFFER, vtxCount * BYTES_PER_VERTEX, NULL, GL_STATIC_DRAW);
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

/* get modelId (modelId + bank), allocate one if it does not exist yet */
int entityAddModel(ItemID_t itemId, int cnx, CustModel cust)
{
	EntityBank bank;
	int modelId = entityGetModelBank(itemId | (cnx << 17));
	if (modelId > 0) return modelId;

	/* not yet in cache, add it on the fly */
	int count = cust ? blockCountModelVertex(cust->model, cust->vertex) : entityModelCount(itemId, cnx);
	if (count == 0) return ENTITY_UNKNOWN;

	/* check for a free place */
	for (bank = HEAD(entities.banks); bank && bank->vtxCount + count > BANK_SIZE; NEXT(bank));

	if (bank == NULL)
	{
		bank = calloc(sizeof *bank, 1);
		bank->models = malloc(sizeof *bank->models * ENTITY_BATCH);
		ListAddTail(&entities.banks, &bank->node);
		entityInitVAO(bank, BANK_SIZE);
	}

	/* check if it has already been generated */
	return entityGenModel(bank, itemId, cnx, cust);
}

/* entity found in a chunk: add it to a linked list */
Entity entityAlloc(uint16_t * entityLoc)
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
int entityGetModelId(Entity entity)
{
	STRPTR id = entity->name;
	NBTFile_t nbt = {.mem = entity->tile};

	/* block pushed by piston */
	if (entity->blockId > 0)
		return entityAddModel(entity->blockId, nbt.mem ? NBT_GetInt(&nbt, NBT_FindNode(&nbt, 0, "blockCnx"), 0) : 0, NULL);

	if (strncmp(id, "minecraft:", 10) == 0)
		id += 10;

	if (strcasecmp(id, "falling_block") == 0)
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
			case 0: data  = NBT_GetInt(&nbt, off, 0); break;
			case 1: block = NBT_Payload(&nbt, off);
			}
		}
		if (block)
		{
			ItemID_t itemId = itemGetByName(block, False);
			if (itemId > 0)
				return entityAddModel(entity->blockId = itemId | data, 0, NULL);
		}
	}
	else if (strcasecmp(id, "painting") == 0)
	{
		int off = NBT_FindNode(&nbt, 0, "Motive");
		if (off >= 0)
		{
			off = FindInList(paintings.names, NBT_Payload(&nbt, off), 0);
			if (off >= 0) return entityGetModelBank(ITEMID(ENTITY_PAINTINGS, off));
		}
	}
	else if (strcasecmp(id, "item_frame") == 0)
	{
		int item = NBT_FindNode(&nbt, 0, "Item");
		entity->special = ENTYPE_FRAME;
		if (item >= 0)
		{
			STRPTR   tech = NBT_Payload(&nbt, NBT_FindNode(&nbt, item, "id"));
			int      data = NBT_GetInt(&nbt, NBT_FindNode(&nbt, item, "Damage"), 0);
			ItemID_t blockId = itemGetByName(tech, True);

			/* note: don't compare <tech> with filled_map, older NBT contained numeric id */
			if (blockId == itemGetByName("filled_map", False))
			{
				/*
				 * filled map will use a slightly bigger model and need a cartograph entry to render the map bitmap
				 * note: data in this case refers to id in data/map_%d.dat, this number can be arbitrary large.
				 */
				entity->special = ENTYPE_FILLEDMAP;
				entity->blockId = ENTITY_ITEM | data;
				return entityGetModelBank(ITEMID(ENTITY_ITEMFRAME_FULL, 0));
			}
			else if (blockId > 0)
			{
				/* we will have to alloc another entity for the item in the frame */
				entity->special = ENTYPE_FRAMEITEM;
				entity->blockId = ENTITY_ITEM | blockId | data;
			}
		}
		/* item will be allocated later */
		return entityGetModelBank(ITEMID(ENTITY_ITEMFRAME, 0));
	}
	else if (strcasecmp(id, "item") == 0)
	{
		/* item laying in the world */
		int desc = NBT_FindNode(&nbt, 0, "Item");
//		int count = NBT_GetInt(&nbt, NBT_FindNode(&nbt, desc, "Count"), 1);
		int data = NBT_GetInt(&nbt, NBT_FindNode(&nbt, desc, "Damage"), 0);
		ItemID_t blockId = itemGetByName(NBT_Payload(&nbt, NBT_FindNode(&nbt, desc, "id")), False);

		if (blockId > 0)
		{
			entity->rotation[3] = 0.5; /* scale actually */
			if (isBlockId(blockId))
				entity->pos[VY] += 0.25f;
			return entityAddModel(entity->blockId = blockId | data, 0, NULL);
		}
	}
	return ENTITY_UNKNOWN;
}

void entityAddToCommandList(Entity entity)
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
	else bank->mdaiCount ++, bank->dirty = 1; /* redo the list from scratch */
}

void entityGetLight(Chunk c, vec4 pos, DATA32 light, Bool full, int debugLight)
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
			16,0,8,24, 16,0,8,24, 16,0,8,24, 16,0,8,24, 0,8,24,16, 16,0,8,24
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

	#if 0
	if (debugLight)
	{
		fprintf(stderr, "light entity at %g, %g, %g%s:\n", pos[0], pos[1], pos[2], full ? " (full)" : "");
		for (Y = 0; Y < LIGHT_SIZE/4; Y ++)
			fprintf(stderr, " - %08x\n", light[Y]);
	}
	#endif
}

EntityModel entityGetModelById(int modelBank)
{
	EntityBank bank;
	int i;

	for (i = modelBank, bank = HEAD(entities.banks); BANK_NUM(i); i --, NEXT(bank));

	return bank->models + (i >> 6);
}


/* change model used by an entity (update MDAI VBO) */
void entityResetModel(Entity entity)
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
		NBT_IterCompound(&iter, nbt->mem + offset);
		memset(pos, 0, sizeof pos); id = NULL;
		pos[10] = 1;
		while ((off = NBT_Iter(&iter)) >= 0)
		{
			off += offset;
			switch (FindInList("Motion,Pos,Rotation,id", iter.name, 0)) {
			case 0: NBT_GetFloat(nbt, off, pos,   3); break;
			case 1: NBT_GetFloat(nbt, off, pos+3, 3); break;
			case 2: NBT_GetFloat(nbt, off, pos+7, 2); break;
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
			entityGetLight(c, pos+3, entity->light, entity->fullLight = entity->blockId > 0, 0);
			entityAddToCommandList(entity);

			/* alloc an entity for the item in the frame */
			if (entity->blockId > 0)
				prev = worldItemAddItemFrame(entity, next+1);
		}
	}
}

Entity entityGetById(int id)
{
	EntityBuffer buffer;
	int i = id >> ENTITY_SHIFT;

	for (buffer = HEAD(entities.list); i > 0; i --, NEXT(buffer));

	return buffer->entities + (id & (ENTITY_BATCH-1));
}

Bool entityIter(int * entityId, vec4 pos)
{
	int id = *entityId;
	if (id != ENTITY_END)
	{
		Entity entity = entityGetById(id);
		if (entity)
		{
			memcpy(pos, entity->pos, 12);
			*entityId = entity->next;
			return True;
		}
	}
	return False;
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

	if ((id = entity->blockId) > 0)
	{
		if (isBlockId(id))
		{
			Block b = &blockIds[id>>4];
			if (b->orientHint == 0)
			{
				BlockState state = blockGetById(id);
				if (state > blockStates) name = state->name;
			}
			else name = b->name;
		}
		else /* item */
		{
			ItemDesc desc = itemGetById(id);
			if (desc) name = desc->name;
		}
	}
	else name = entity->name;

	if (name == NULL)
		name = "<unknown>";

	count = StrCat(buffer, max, count, name);
	if (id > 0 && isBlockId(id))
		count += sprintf(buffer + count, " <dim>(%d:%d)</dim>", id >> 4, id&15);

	if (fabsf(entity->rotation[0]) > EPSILON)
		count += sprintf(buffer + count, "\n<dim>Rotation Y:</dim> %g", (double) (entity->rotation[0] * RAD_TO_DEG));

	if (fabsf(entity->rotation[1]) > EPSILON)
		sprintf(buffer + count, "\n<dim>Rotation X:</dim> %g\n", (double) (entity->rotation[1] * RAD_TO_DEG));
}


/*
 * entity ray-picking
 */


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
			if (entities.selected->special == ENTYPE_FILLEDMAP)
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
			if (entity->special == ENTYPE_FILLEDMAP)
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
int entityRaypick(Chunk c, vec4 dir, vec4 camera, vec4 cur, vec4 ret_pos)
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
					    pointIsInRect(points /* rect */, inter /* point */))
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

/*
 * delete part or all of an entity
 */

/* remove any reference of this entity in all the data structure */
uint16_t entityClear(EntityBuffer buf, int index)
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

void entityDeleteSlot(int slot)
{
	EntityBuffer buffer;
	int i;
	for (buffer = HEAD(entities.list), i = slot >> ENTITY_SHIFT; i > 0 && buffer; NEXT(buffer), i --);
	entityClear(buffer, slot & (ENTITY_BATCH-1));
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
void entityMarkListAsModified(Map map, Chunk c)
{
	renderAddModif();
	mapAddToSaveList(map, c);
	if ((c->cflags & CFLAG_REBUILDETT) == 0)
		chunkUpdateEntities(c);
}

/* delete entity from memory and NBT */
void entityDeleteById(Map map, int entityId)
{
	EntityBuffer buffer;
	Entity entity;
	Chunk c;

	if (entityId == 0) return;
	entityId --;

	int i = entityId >> ENTITY_SHIFT;
	int slot = entityId & (ENTITY_BATCH-1);
	for (buffer = HEAD(entities.list); i > 0; i --, NEXT(buffer));
	entity = buffer->entities + slot;
	c = mapGetChunk(map, entity->pos);

	if (c)
	{
		DATA16 prev;
		/* mark the chunk as needing to be saved */
		mapAddToSaveList(map, c);
		if ((c->cflags & CFLAG_REBUILDETT) == 0)
			chunkUpdateEntities(c);

		/* unlink from chunk active entities */
		if (entity->ref || entity->special == ENTYPE_FILLEDMAP)
		{
			/* item in item frame: only delete this item */
			NBTFile_t nbt = {.mem = entity->tile};
			int item = NBT_FindNode(&nbt, 0, "Item");
			if (item >= 0)
			{
				/* unlink from chunk linked list */
				if (entity->special != ENTYPE_FILLEDMAP)
				{
					prev = entityGetPrev(c, entity, entityId);
					*prev = entity->next;
				}

				nbt.mem = NBT_Copy(entity->tile);
				nbt.usage = NBT_Size(nbt.mem)+4; /* XXX want TAG_EndCompound too */
				NBT_Delete(&nbt, item, -1);
				chunkDeleteTile(c, entity->tile);
				entity->name = NBT_Payload(&nbt, NBT_FindNode(&nbt, 0, "id"));
				/* item in frame is about to be deleted, modify item frame entity then */
				if (entity->ref)
					entity = entity->ref;
				entity->tile = nbt.mem;
				/* NBT compound from item and frame is the same */
				if (entity->special == ENTYPE_FILLEDMAP)
				{
					/* map removed from item frame: reset frame model to normal */
					entity->special = ENTYPE_FRAME;
					entity->VBObank = entityGetModelBank(ITEMID(ENTITY_ITEMFRAME, 0));
					entity->special = 0;
					entityResetModel(entity);
					cartoDelMap(entityId+1);
				}
				else entityClear(buffer, slot);
			}
		}
		else
		{
			prev = entityGetPrev(c, entity, entityId);
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
			entityClear(buffer, slot);
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
		/* entity->ref == item in item frame, which will be saved with item frame */
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

/* get the block id (if any) stored in this entity */
void entityGetItem(int entityId, Item ret)
{
	Entity entity = entityGetById(entityId-1);

	if (entity)
	{
		NBTFile_t nbt = {.mem = entity->tile};
		int offset = NBT_FindNode(&nbt, 0, "Item");
		if (offset > 0)
			ret->extra = NBT_Payload(&nbt, offset);

		ret->count = NBT_GetInt(&nbt, NBT_FindNode(&nbt, offset, "Count"), 1);
		if (ret->count < 0)
			ret->count = 1;
		if (entity->blockId > 0)
			ret->id = entity->blockId;
		else
			ret->id = itemGetByName(entity->name, True);
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
			int oldPos[3];
			for (j = 0; j < 3; j ++)
			{
				oldPos[j] = entity->pos[j];
				entity->pos[j] += (entity->motion[j] - entity->pos[j]) * (time - anim->prevTime) / remain;
			}
			anim->prevTime = time;
			/* update VBO */
			EntityBank bank;
			for (j = BANK_NUM(entity->VBObank), bank = HEAD(entities.banks); j > 0; j --, NEXT(bank));
			glBindBuffer(GL_ARRAY_BUFFER, bank->vboLoc);
			glBufferSubData(GL_ARRAY_BUFFER, entity->mdaiSlot * INFO_SIZE, 12, entity->pos);
			if (oldPos[0] != (int) entity->pos[0] ||
				oldPos[1] != (int) entity->pos[1] ||
				oldPos[2] != (int) entity->pos[2])
			{
				/* XXX check if chunk has changed */
				entityGetLight(mapGetChunk(globals.level, entity->pos), entity->pos, entity->light, entity->fullLight, 1);
				glBufferSubData(GL_ARRAY_BUFFER, entity->mdaiSlot * INFO_SIZE + INFO_SIZE-LIGHT_SIZE, LIGHT_SIZE, entity->light);
			}
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

	Block b = &blockIds[blockId>>4];
	memcpy(entity->pos, pos, 12);
	memcpy(entity->motion, dest, 12);
	entity->blockId = blockId;
	entity->tile = tile;
	entity->rotation[3] = 1;
	vecAddNum(entity->pos,    0.5f);
	vecAddNum(entity->motion, 0.5f);
	entity->VBObank = entityGetModelId(entity);
	entityGetLight(c, pos, entity->light, entity->fullLight = b->type != CUST || b->special == BLOCK_SOLIDOUTER, 1);
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
		anim->prevTime + ticks * 100 * (1000 / TICK_PER_SECOND);
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
			entityGetLight(c, entity->pos, light, entity->fullLight, 0);
		if (memcmp(light, entity->light, LIGHT_SIZE))
		{
			EntityBank bank;
			int j;
			memcpy(entity->light, light, LIGHT_SIZE);
			for (j = BANK_NUM(entity->VBObank), bank = HEAD(entities.banks); j > 0; j --, NEXT(bank));
			glBindBuffer(GL_ARRAY_BUFFER, bank->vboLoc);
			glBufferSubData(GL_ARRAY_BUFFER, entity->mdaiSlot * INFO_SIZE + INFO_SIZE-LIGHT_SIZE, LIGHT_SIZE, light);
			if (entity->special == ENTYPE_FILLEDMAP)
				cartoUpdateLight(id+1, entity->light);
		}
	}
}

void entityUpdateInfo(Entity entity)
{
	if (entity)
	{
		EntityBank bank;
		int j;
		for (j = BANK_NUM(entity->VBObank), bank = HEAD(entities.banks); j > 0; j --, NEXT(bank));
		glBindBuffer(GL_ARRAY_BUFFER, bank->vboLoc);
		glBufferSubData(GL_ARRAY_BUFFER, entity->mdaiSlot * INFO_SIZE, INFO_SIZE - LIGHT_SIZE, entity->pos);
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
			itemGetTechName(entity->blockId & 0xffff, name, sizeof name, True);
			fprintf(stderr, "item in frame: %s\n", name);
		}
		else
		{
			fprintf(stderr, "%s", entity->name);
			if (entity->special == ENTYPE_FILLEDMAP)
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
		// glEnable(GL_POLYGON_OFFSET_FILL);
		// glPolygonOffset(-1.0, -1.0);

		glEnable(GL_CULL_FACE);
		glCullFace(GL_BACK);
		glUseProgram(entities.shader);
		glBindVertexArray(bank->vao);
		glBindBuffer(GL_DRAW_INDIRECT_BUFFER, bank->vboMDAI);
		glMultiDrawArraysIndirect(GL_TRIANGLES, 0, bank->mdaiCount, 0);
		// glDisable(GL_POLYGON_OFFSET_FILL);
	}
}

/*
 * used to handle cloned entities in selection
 */
int entityGetModel(int entityId, int * vtxCount)
{
	EntityBank bank;
	Entity entity = entityGetById(entityId);
	int VBObank = entity->VBObank;
	int i;

	for (i = BANK_NUM(VBObank), bank = HEAD(entities.banks); i > 0; i --, NEXT(bank));

	*vtxCount = bank->models[VBObank >> 6].count;

	return VBObank;
}

APTR entityCopy(int vtxCount, vec4 origin, DATA16 entityIds, int maxEntities, DATA32 models, int maxModels)
{
	if (maxEntities == 0 || maxModels == 0)
		return NULL;

	EntityBank bank = calloc(sizeof *bank + sizeof (vec4) + maxEntities * 2, 1);
	EntityModel model;

	entityInitVAO(bank, vtxCount);
	bank->mdaiCount = maxEntities;
	bank->mdaiUsage = (DATA32) (bank+1) + 4;
	memcpy(bank + 1, origin, sizeof (vec4));
	memcpy(bank->mdaiUsage, entityIds, maxEntities * 2);

	/* create command buffer */
	glBindBuffer(GL_ARRAY_BUFFER, bank->vboLoc);
	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, bank->vboMDAI);

	glBufferData(GL_ARRAY_BUFFER, maxEntities * INFO_SIZE, NULL, GL_STATIC_DRAW);
	glBufferData(GL_DRAW_INDIRECT_BUFFER, maxEntities * 16, NULL, GL_STATIC_DRAW);

	MDAICmd mdai  = glMapBuffer(GL_DRAW_INDIRECT_BUFFER, GL_WRITE_ONLY);
	float * loc   = glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
	DATA16  first = alloca(maxModels * 2);
	DATA16  vtxdata;
	int     i, j;

	for (i = j = 0; i < maxModels; i ++)
	{
		model = entityGetModelById(models[i]);
		first[i] = j;
		j += model->count;
	}

	for (i = 0; i < maxEntities; i ++, loc += INFO_SIZE/4, mdai ++)
	{
		Entity entity = entityGetById(entityIds[i]);
		model = entityGetModelById(entity->VBObank);
		for (j = 0; j < maxModels && models[j] != entity->VBObank; j ++);

		mdai->baseInstance = i;
		mdai->count = model->count;
		mdai->first = first[j];
		mdai->instanceCount = 1;
		memcpy(loc, entity->pos, INFO_SIZE);
	}
	glUnmapBuffer(GL_DRAW_INDIRECT_BUFFER);
	glUnmapBuffer(GL_ARRAY_BUFFER);

	/* then model vbo */
	glBindBuffer(GL_ARRAY_BUFFER, bank->vboModel);
	vtxdata = glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);

	for (i = 0; i < maxModels; i ++)
	{
		EntityBank modelBank;
		for (j = models[i], modelBank = HEAD(entities.banks); BANK_NUM(j); j --, NEXT(modelBank));
		glBindBuffer(GL_DRAW_INDIRECT_BUFFER, modelBank->vboModel);
		model = modelBank->models + (j >> 6);
		glGetBufferSubData(GL_DRAW_INDIRECT_BUFFER, model->first * BYTES_PER_VERTEX, model->count * BYTES_PER_VERTEX, vtxdata);
		vtxdata += model->count * INT_PER_VERTEX;
	}

	glUnmapBuffer(GL_ARRAY_BUFFER);

	return bank;
}

#define bank ((EntityBank)duplicated)
/* render value returned by entityDuplicate() */
void entityCopyRender(APTR duplicated)
{
	if (duplicated == NULL) return;
	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);
	glUseProgram(entities.shader);
	glBindVertexArray(bank->vao);
	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, bank->vboMDAI);
	/* yeah, only a single draw call is necessary */
	glMultiDrawArraysIndirect(GL_TRIANGLES, 0, bank->mdaiCount, 0);
}

/* add entity and tile information to given map */
void entityCopyToMap(APTR duplicated, Map map)
{
	DATA16 entityIds;
	int i;

	/* worldItemDup() will rebind GL_ARRAY_BUFFER, so use a different binding point :-/ */
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, bank->vboLoc);
	vec loc = glMapBuffer(GL_ELEMENT_ARRAY_BUFFER, GL_READ_WRITE);
	for (i = bank->mdaiCount, entityIds = (DATA16) bank->mdaiUsage; i > 0; i --, entityIds ++, loc += INFO_SIZE/4)
		worldItemDup(map, loc, entityIds[0]);
	glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER);
}

/* update location of all duplicated entities */
void entityCopyRelocate(APTR duplicated, vec4 origin)
{
	if (duplicated == NULL) return;
	vec4 offset;
	vec  loc;
	int  i;
	vecSub(offset, origin, (vec) (bank+1));
	memcpy(bank+1, origin, 12);

	glBindBuffer(GL_ARRAY_BUFFER, bank->vboLoc);
	loc = glMapBuffer(GL_ARRAY_BUFFER, GL_READ_WRITE);
	for (i = bank->mdaiCount; i > 0; i --, loc += INFO_SIZE/4)
	{
		loc[VX] += offset[VX];
		loc[VY] += offset[VY];
		loc[VZ] += offset[VZ];
	}
	glUnmapBuffer(GL_ARRAY_BUFFER);
}

void entityCopyDelete(APTR duplicated)
{
	if (duplicated == NULL) return;
	glBindVertexArray(0);
	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);

	glDeleteBuffers(3, &bank->vboModel);
	glDeleteVertexArrays(1, &bank->vao);

	free(duplicated);
}
#undef bank
