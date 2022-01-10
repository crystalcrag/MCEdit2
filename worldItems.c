/*
 * worldItem.c : handle item that can be added as entity in the world (dropped items, block entity,
 *               falling entities, ...)
 *
 * note: this module should only be accessed through entities.c
 *
 * written by T.Pierron, nov 2021
 */

#define ENTITY_IMPL
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <malloc.h>
#include "SIT.h"
#include "entities.h"
#include "MCEdit.h"   /* OVERLAY dialog for paintinga */
#include "redstone.h"
#include "blockUpdate.h"
#include "mapUpdate.h"
#include "cartograph.h"
#include "render.h"
#include "minecarts.h"
#include "undoredo.h"
#include "globals.h"

struct
{
	Entity   preview;
	uint16_t slot;
	uint8_t  createSide;
	float    previewOffVY;
}	worldItem;


/* item frame and paintings have a rectangular bbox: need to take rotation into account for collision check */
static int worldItemSwapAxis(Entity entity)
{
	int angle = roundf(entity->rotation[1] * (180 / M_PIf)); /* X axis acutally */
	entity->enflags |= ENFLAG_BBOXROTATED;
	if (angle == 90 || angle == 270 || angle == -90)
		return MODEL_SWAP_ZY;
	angle = roundf(entity->rotation[0] * (180 / M_PIf)); /* Y axis */
	if (angle == 90 || angle == 270 || angle == -90)
		return MODEL_SWAP_XZ;
	return MODEL_DONT_SWAP;
}

static int worldItemParseFallingBlock(NBTFile nbt, Entity entity)
{
	STRPTR block = NULL;
	int    data  = 0;
	int    off;

	NBTIter_t prop;
	//entity->pos[VY] += 0.5f;
	NBT_IterCompound(&prop, entity->tile);
	while ((off = NBT_Iter(&prop)) >= 0)
	{
		switch (FindInList("Data,Block", prop.name, 0)) {
		case 0: data  = NBT_GetInt(nbt, off, 0); break;
		case 1: block = NBT_Payload(nbt, off);
		}
	}
	if (block)
	{
		ItemID_t itemId = itemGetByName(block, False);
		if (itemId > 0)
			return entityAddModel(entity->blockId = itemId | data, 0, NULL, &entity->szx, MODEL_DONT_SWAP);
	}
	return 0;
}

static int worldItemParsePainting(NBTFile nbt, Entity entity)
{
	int off = NBT_FindNode(nbt, 0, "Motive");
	if (off >= 0)
	{
		off = FindInList(paintings.names, NBT_Payload(nbt, off), 0);
		if (off >= 0)
		{
			entity->enflags = ENFLAG_POPIFPUSHED;
			return entityAddModel(ITEMID(ENTITY_PAINTINGS, off), 0, NULL, &entity->szx, worldItemSwapAxis(entity));
		}
	}
	return 0;
}

static int worldItemParseItemFrame(NBTFile nbt, Entity entity)
{
	int item = NBT_FindNode(nbt, 0, "Item");
	entity->entype = ENTYPE_FRAME;
	entity->enflags = ENFLAG_POPIFPUSHED;
	if (item >= 0)
	{
		STRPTR   tech = NBT_Payload(nbt, NBT_FindNode(nbt, item, "id"));
		int      data = NBT_GetInt(nbt, NBT_FindNode(nbt, item, "Damage"), 0);
		ItemID_t blockId = itemGetByName(tech, True);

		/* note: don't compare <tech> with filled_map, older NBT contained numeric id */
		if (blockId == itemGetByName("filled_map", False))
		{
			/*
			 * filled map will use a slightly bigger model and need a cartograph entry to render the map bitmap
			 * note: data in this case refers to id in data/map_%d.dat, this number can be arbitrarily large.
			 */
			entity->entype  = ENTYPE_FILLEDMAP;
			entity->enflags = ENFLAG_POPIFPUSHED;
			entity->blockId = ENTITY_ITEM | data;
			return entityAddModel(ITEMID(ENTITY_ITEMFRAME_FULL, 0), 0, NULL, &entity->szx, worldItemSwapAxis(entity));
		}
		else if (blockId > 0)
		{
			/* we will have to alloc another entity for the item in the frame */
			entity->entype  = ENTYPE_FRAMEITEM;
			entity->enflags = ENFLAG_POPIFPUSHED;
			entity->blockId = ENTITY_ITEM | blockId | data;
		}
	}
	/* item in frame will be allocated later (worldItemAddItemInFrame) */
	return entityAddModel(ITEMID(ENTITY_ITEMFRAME, 0), 0, NULL, &entity->szx, worldItemSwapAxis(entity));
}

static int worldItemParseItem(NBTFile nbt, Entity entity)
{
	/* item laying in the world */
	int desc = NBT_FindNode(nbt, 0, "Item");
//	int count = NBT_GetInt(nbt, NBT_FindNode(nbt, desc, "Count"), 1);
	int data = NBT_GetInt(nbt, NBT_FindNode(nbt, desc, "Damage"), 0);
	ItemID_t blockId = itemGetByName(NBT_Payload(nbt, NBT_FindNode(nbt, desc, "id")), False);

	if (blockId > 0)
	{
		entity->rotation[3] = 0.5; /* scale actually */
		//if (isBlockId(blockId))
		//	entity->pos[VY] += 0.25f;
		return entityAddModel(entity->blockId = blockId | data, 0, NULL, &entity->szx, MODEL_DONT_SWAP);
	}
	return 0;
}

void worldItemInit(void)
{
	entityRegisterType("falling_block", worldItemParseFallingBlock);
	entityRegisterType("painting",      worldItemParsePainting);
	entityRegisterType("item_frame",    worldItemParseItemFrame);
	entityRegisterType("item",          worldItemParseItem);
	entityRegisterType("minecart",      minecartParse);

	itemRegisterUse("minecart", minecartTryUsing);
}

static void worldItemRAD2MC(float rad[2], float mcangle[2])
{
	/*
	 * mcangle stores the following angles:
	 * mcangle[0]: yaw, clockwise, degrees, where 0 = south.
	 * mcangle[1]: pitch, degrees, +/- 90. negative = up, positive = down
	 */
	mcangle[0] = 360 - rad[0] * RAD_TO_DEG;
	mcangle[1] = - rad[1] * RAD_TO_DEG;
	/* not necessary, but looks nicer in debug output */
	if (mcangle[0] == -0.0f) mcangle[0] = 0; else
	if (mcangle[0] < 0)      mcangle[0] += 360; else
	if (mcangle[0] >= 360)   mcangle[0] -= 360;
	if (mcangle[1] == -0.0f) mcangle[1] = 0;
}

/* add the pre-defined fields of a world item in the <nbt> fragment */
void worldItemCreateGeneric(NBTFile nbt, Entity entity, STRPTR name)
{
	EntityUUID_t uuid;
	double       pos64[3];
	float        rotation[2];
	STRPTR       id;
	DATA8        p, e;

	pos64[VX] = entity->pos[VX];
	pos64[VY] = entity->pos[VY];
	pos64[VZ] = entity->pos[VZ];
	worldItemRAD2MC(entity->rotation, rotation);
	if (strncmp(id = name, "minecraft:", 10))
		sprintf(id = alloca(strlen(name) + 11), "minecraft:%s", name);

	for (p = uuid.uuid8, e = p + sizeof uuid.uuid8; p < e; *p++ = rand());
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

void worldItemDup(Map map, vec info, int entityId)
{
	uint16_t slot;
	Entity entity = entityGetById(entityId);
	Entity dup    = entityAlloc(&slot);
	Chunk  chunk  = mapGetChunk(map, info);

	memcpy(dup->pos, info, INFO_SIZE);
	dup->VBObank   = entity->VBObank;
	dup->entype    = entity->entype;
	dup->enflags   = entity->enflags;
	dup->blockId   = entity->blockId;
	dup->tile      = NBT_Copy(entity->tile);
	quadTreeInsertItem(dup);

	NBTFile_t nbt = {.mem = dup->tile};
	NBTIter_t iter;
	int       offset;
	float     rotation[2];
	DATA8     p, e;

	/* update NBT record too */
	worldItemRAD2MC(dup->rotation, rotation);
	NBT_IterCompound(&iter, nbt.mem);
	EntityUUID_t uuid;
	for (p = uuid.uuid8, e = p + sizeof uuid.uuid8; p < e; *p++ = rand());
	while ((offset = NBT_Iter(&iter)) >= 0)
	{
		switch (FindInList("UUIDLeast,UUIDMost,Rotation,Pos,Motion", iter.name, 0)) {
		case 0: memcpy(NBT_Payload(&nbt, offset), uuid.uuid8,   8); break;
		case 1: memcpy(NBT_Payload(&nbt, offset), uuid.uuid8+8, 8); break;
		case 2: NBT_SetFloat(&nbt, offset, rotation, 2); break;
		case 3: NBT_SetFloat(&nbt, offset, dup->pos, 3); break;
		case 4: NBT_SetFloat(&nbt, offset, dup->motion, 3);
		}
	}

	dup->next = chunk->entityList;
	dup->name = NBT_Payload(&nbt, NBT_FindNode(&nbt, 0, "id"));
	chunk->entityList = slot;
	entityGetLight(chunk, dup->pos, dup->light, dup->enflags & ENFLAG_FULLLIGHT);
	entityAddToCommandList(dup);
	mapAddToSaveList(map, chunk);
	if ((chunk->cflags & CFLAG_REBUILDETT) == 0)
		chunkUpdateEntities(chunk);
}

/* model for full frame is oriented in the XY plane: grab coord of south face and apply entity transformation */
static void worldItemGetFrameCoord(Entity entity, float vertex[12])
{
	entityGetBoundsForFace(entity, SIDE_SOUTH | 8, vertex, vertex+3);
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

	vec3Add(vertex,   entity->pos);
	vec3Add(vertex+3, entity->pos);
	vec3Add(vertex+6, entity->pos);

	/* 4th point: simple geometry from previous 3 points */
	vertex[VX+9] = vertex[VX] + (vertex[VX+3] - vertex[VX+6]);
	vertex[VY+9] = vertex[VY] + (vertex[VY+3] - vertex[VY+6]);
	vertex[VZ+9] = vertex[VZ] + (vertex[VZ+3] - vertex[VZ+6]);
}

/* add the item within the frame in the list of entities to render */
Entity worldItemAddItemInFrame(Chunk chunk, Entity frame, int entityId)
{
	if (frame->entype == ENTYPE_FRAMEITEM)
	{
		uint16_t next;
		/* normal item in frame */
		Entity item = entityAlloc(&next);
		frame->next = next;
		item->ref = frame;
		item->next = ENTITY_END;
		item->blockId = frame->blockId & ~ENTITY_ITEM;
		item->tile = frame->tile;
		item->chunkRef = chunk;
		frame->blockId = 0;
		memcpy(item->motion, frame->motion, INFO_SIZE + 12);
		item->pos[VT] = 0; /* selection */
		item->rotation[3] = 0.4; /* scaling */
		if (! isBlockId(item->blockId))
			/* items are rendered in XZ plane, item frame are oriented in XY or ZY plane */
			item->rotation[1] = M_PI_2f - frame->rotation[1];
		item->VBObank = entityGetModelId(item);
		entityAddToCommandList(item);
		return item;
	}
	else if (frame->entype == ENTYPE_FILLEDMAP)
	{
		float coord[12];
		int   VBObank = entityGetModelBank(ITEMID(ENTITY_ITEMFRAME_FULL, 0));
		if (frame->VBObank != VBObank)
		{
			/* map was placed in item frame: need to change frame model */
			frame->VBObank = VBObank;
			entityResetModel(frame);
		}
		worldItemGetFrameCoord(frame, coord);
		/* 65536 possible maps ought to be enough(TM) */
		cartoAddMap(entityId, coord, ITEMMETA(frame->blockId), frame->light);
	}
	return frame;
}


static void worldItemFillPos(vec4 dest, vec4 src, int side, int orientX, vec size)
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

Bool worldItemCreatePainting(Map map, int paintingId, vec4 pos)
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

	/* .location contains 4 bytes of each painting where they are located in terrain.png (in tile coord) */
	loc = paintings.location + paintingId * 4;
	for (name = paintings.names; paintingId > 0; name = strchr(name, ',') + 1, paintingId --);
	buffer = name; name = strchr(name, ','); if (name) *name = 0;
	buffer = STRDUPA(buffer); if (name) *name = ',';
	size[VX] = loc[2] - loc[0];
	size[VY] = loc[3] - loc[1];
	size[VZ] = 1/16.;

	worldItemFillPos(posAndRot, pos, worldItem.createSide, 0, size);
	c = mapGetChunk(map, posAndRot);
	if (c == NULL) return False; /* outside map? */

	entity = entityAlloc(&slot);
	memcpy(entity->pos, posAndRot, sizeof posAndRot);
	quadTreeInsertItem(entity);
	worldItemCreateGeneric(&nbt, entity, "painting");
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
	entity->enflags &= ~ENFLAG_FULLLIGHT;
	entity->chunkRef = c;
	entityGetLight(c, entity->pos, entity->light, False);
	entityAddToCommandList(entity);

	/* flag chunk for saving later */
	entityMarkListAsModified(map, c);
	undoLog(LOG_ENTITY_ADDED, slot);
	renderAddModif();
	return True;
}

static int worldItemCreateItemFrame(Map map, vec4 pos, int side)
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

	worldItemFillPos(posAndRot, pos, side, side == SIDE_TOP ? -90 : side == SIDE_BOTTOM ? 90 : 0, size);
	c = mapGetChunk(map, posAndRot);
	if (c == NULL) return 0; /* outside map? */

	/* create first a dummy entity to check if there is space to place it here */
	struct Entity_t dummy;
	memset(&dummy, 0, sizeof dummy);
	memcpy(dummy.pos, posAndRot, sizeof posAndRot);
	dummy.rotation[3] = 1;
	dummy.name = "item_frame";
	dummy.VBObank = entityGetModelId(&dummy);
	dummy.enflags |= ENFLAG_FULLLIGHT;

	/* check if there are other entities in this space */
	size[VX] = ENTITY_SCALE(&dummy);
	memcpy(posAndRot+3, posAndRot, 12);
	posAndRot[VX] -= size[VX];   posAndRot[VX+3] += size[VX];
	posAndRot[VY] -= size[VY];   posAndRot[VY+3] += size[VY];
	posAndRot[VZ] -= size[VZ];   posAndRot[VZ+3] += size[VZ];
	int count = 0;
	quadTreeIntersect(posAndRot, &count, 0);
	if (count > 0)
	{
		/* does not fit: cancel creation */
		fprintf(stderr, "can't fit item frame in %g, %g, %g\n", (double) posAndRot[VX], (double) posAndRot[VY], (double) posAndRot[VZ]);
		return 0;
	}

	entity = entityAlloc(&slot);
	memcpy(entity, &dummy, sizeof dummy);
	entity->mdaiSlot = MDAI_INVALID_SLOT;
	worldItemCreateGeneric(&nbt, entity, dummy.name);
	NBT_Add(&nbt, TAG_Compound_End);

	entity->next = c->entityList;
	entity->name = NBT_Payload(&nbt, NBT_FindNode(&nbt, 0, "id"));
	entity->chunkRef = c;
	c->entityList = slot;

	entity->tile = nbt.mem;
	entityGetLight(c, entity->pos, entity->light, True);
	entityAddToCommandList(entity);
	entityMarkListAsModified(map, c);
	quadTreeInsertItem(entity);
	undoLog(LOG_ENTITY_ADDED, slot);
	renderAddModif();
	return slot + 1;
}

/* add some pre-defined entity in the world map */
int worldItemCreate(Map map, int itemId, vec4 pos, int side)
{
	ItemDesc desc = itemGetById(itemId);
	if (desc == NULL) return 0;
	switch (FindInList("painting,item_frame", desc->tech, 0)) {
	case 1: /* empty item frame */
		return worldItemCreateItemFrame(map, pos, side);
	case 0: /* ask for a painting first */
		if (side >= SIDE_TOP) break;
		worldItem.createSide = side;
		mceditUIOverlay(MCUI_OVERLAY_PAINTING);
	}
	return 0;
}

/* action on entity */
void worldItemUseItemOn(Map map, int entityId, ItemID_t itemId, vec4 pos)
{
	Entity entity = entityGetById(entityId - 1);

	if (entity && entity->entype == ENTYPE_FRAME)
	{
		NBTFile_t tile  = {.mem = entity->tile};
		int       item  = NBT_FindNode(&tile, 0, "Item");
		Chunk     chunk = mapGetChunk(map, entity->pos);
		/* check if there is already an item in it */
		if (chunk && item < 0)
		{
			/* no: add it to the item frame */
			TEXT buffer[64];
			int  meta;
			tile.mem = NULL;
			tile.page = 127;
			itemGetTechName(itemId, buffer, sizeof buffer, True);
			STRPTR sep = strrchr(buffer, ':');
			if (sep && '0' <= sep[1] && sep[1] <= '9')
				meta = atoi(sep+1), sep[0] = 0;
			else
				meta = 0;
			NBT_Add(&tile,
				TAG_Raw_Data, NBT_Size(entity->tile), entity->tile,
				TAG_Compound, "Item",
					TAG_String, "id",     buffer,
					TAG_Byte,   "Count",  1,
					TAG_Short,  "Damage", meta,
					TAG_Compound_End
			);
			NBT_Add(&tile, TAG_Compound_End); /* end of whole entity */
			undoLog(LOG_ENTITY_CHANGED, entity->pos, entity->tile, entityId-1);
			chunkDeleteTile(chunk, entity->tile);
			entity->name = NBT_Payload(&tile, NBT_FindNode(&tile, 0, "id"));
			entity->tile = tile.mem;
			entity->blockId = itemId | ENTITY_ITEM;
			entity->entype = strcmp(buffer, "minecraft:filled_map") == 0 ? ENTYPE_FILLEDMAP : ENTYPE_FRAMEITEM;
			uint16_t next = entity->next;
			entity = worldItemAddItemInFrame(chunk, entity, entityId);
			entity->next = next;
			entityMarkListAsModified(map, chunk);
			renderAddModif();
		}
	}
}

/* entity->entype == ENTITY_FRAME */
void worldItemDelete(Entity entity)
{
	/* item in item frame: only delete this item */
	NBTFile_t nbt = {.mem = entity->tile};
	int item = NBT_FindNode(&nbt, 0, "Item");
	if (item >= 0)
	{
		nbt.mem = NBT_Copy(entity->tile);
		nbt.usage = NBT_Size(nbt.mem)+4; /* want TAG_EndCompound too */
		NBT_Delete(&nbt, item, -1);
		entity->name = NBT_Payload(&nbt, NBT_FindNode(&nbt, 0, "id"));
		/* item in frame is about to be deleted, modify item frame entity then */
		if (entity->ref)
			entity = entity->ref;
		entity->tile = nbt.mem;
		/* NBT compound from item and frame is the same */
		if (entity->entype == ENTYPE_FILLEDMAP)
		{
			/* map removed from item frame: reset frame model to normal */
			entity->entype  = ENTYPE_FRAME;
			entity->VBObank = entityAddModel(ITEMID(ENTITY_ITEMFRAME, 0), 0, NULL, &entity->szx, worldItemSwapAxis(entity));
			entity->entype  = 0;
			entityResetModel(entity);
		}
	}
}

/* show a preview of the item that will be placed if left-clicked */
void worldItemPreview(vec4 camera, vec4 pos, ItemID_t itemId)
{
	Entity preview = worldItem.preview;

	if (preview == NULL)
	{
		preview = entityAlloc(&worldItem.slot);

		memcpy(preview->pos, pos, 12);
		preview->pos[3] = 1;

		preview->next = ENTITY_END;
		float angle = atan2f(pos[VX] - camera[VX], pos[VZ] - camera[VZ]);
		preview->rotation[3] = 0.5; /* scale actually */
		if (isBlockId(itemId) && blockIds[itemId>>4].special == BLOCK_STAIRS)
			/* viewed from front instead of side */
			angle += M_PI_2f;
		else
			angle += M_PIf;
		/* shader wants a positive angle */
		preview->rotation[0] = normAngle(angle);
		preview->VBObank = entityAddModel(preview->blockId = itemId, 0, NULL, &preview->szx, MODEL_DONT_SWAP);

		worldItem.previewOffVY = preview->szy * preview->rotation[3] * 0.25f / BASEVTX;
		preview->pos[VY] += worldItem.previewOffVY;

		/* fully bright, to somewhat highlight that this item has not been placed yet */
		memset(preview->light, 0xf0, sizeof preview->light);
		entityAddToCommandList(preview);
		worldItem.preview = preview;
	}
}

void worldItemUpdatePreviewPos(vec4 camera, vec4 pos)
{
	Entity preview = worldItem.preview;

	if (preview)
	{
		float oldPos[3];
		memcpy(oldPos, preview->pos, 12);
		memcpy(preview->pos, pos, 12);
		preview->pos[VY] += worldItem.previewOffVY;
		float angle = atan2f(pos[VX] - camera[VX], pos[VZ] - camera[VZ]);
		if (isBlockId(preview->blockId) && blockIds[preview->blockId>>4].special == BLOCK_STAIRS)
			/* viewed from front instead of side */
			angle += M_PI_2f;
		else
			angle += M_PIf;
		preview->rotation[0] = normAngle(angle);
		/* note: this entity is not linked to any chunk, no need to update entityList */
		entityUpdateInfo(preview, oldPos);
	}
}

void worldItemDeletePreview(void)
{
	Entity preview = worldItem.preview;

	if (preview)
	{
		entityDeleteSlot(worldItem.slot);
		worldItem.preview = NULL;
		worldItem.slot = 0;
	}
}

/* place current preview item in the world */
void worldItemAdd(Map map)
{
	Entity preview = worldItem.preview;

	if (preview)
	{
		Chunk chunk = mapGetChunk(map, preview->pos);

		if (chunk)
		{
			TEXT itemName[64];
			NBTFile_t nbt = {.page = 511};

			itemGetTechName(preview->blockId, itemName, sizeof itemName, False);
			worldItemCreateGeneric(&nbt, preview, "item");
			NBT_Add(&nbt,
				TAG_Compound, "Item",
					TAG_String, "id", itemName,
					TAG_Byte,   "Count", 1,
					TAG_Short,  "Damage", 0,
				TAG_Compound_End
			);
			NBT_Add(&nbt, TAG_Compound_End);

			preview->next = chunk->entityList;
			preview->name = NBT_Payload(&nbt, NBT_FindNode(&nbt, 0, "id"));
			quadTreeInsertItem(preview);
			chunk->entityList = worldItem.slot;
			undoLog(LOG_ENTITY_ADDED, worldItem.slot);

			preview->tile = nbt.mem;
			preview->enflags &= ~ENFLAG_FULLLIGHT;
			preview->chunkRef = chunk;
			entityGetLight(chunk, preview->pos, preview->light, False);
			entityMarkListAsModified(map, chunk);
			worldItem.preview = NULL;
			worldItem.slot = 0;
			renderAddModif();
		}
	}
}
