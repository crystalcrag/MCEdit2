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
#include "globals.h"
#include "entities.h"
#include "worldItems.h"

struct
{
	Entity   preview;
	uint16_t slot;
	uint8_t  createSide;
	vec4     createPos;        /* paintings are created asynchronously */
	float    previewOffVY;
}	worldItem;


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
static void worldItemCreateGeneric(NBTFile nbt, Entity entity, STRPTR name)
{
	EntityUUID_t uuid;
	TEXT         id[64];
	double       pos64[3];
	float        rotation[2];
	DATA8        p, e;

	pos64[VX] = entity->pos[VX];
	pos64[VY] = entity->pos[VY];
	pos64[VZ] = entity->pos[VZ];
	worldItemRAD2MC(entity->rotation, rotation);
	sprintf(id, "minecraft:%s", name);
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
	dup->special   = entity->special;
	dup->fullLight = entity->fullLight;
	dup->blockId   = entity->blockId;
	dup->tile      = NBT_Copy(entity->tile);

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
	entityGetLight(chunk, dup->pos, dup->light, dup->fullLight, 0);
	entityAddToCommandList(dup);
	mapAddToSaveList(map, chunk);
	if ((chunk->cflags & CFLAG_REBUILDETT) == 0)
		chunkUpdateEntities(chunk);
}

/* model for full frame is oriented in the XY plane: grab coord of south face and apply entity transformation */
static void worldItemGetFrameCoord(Entity entity, float vertex[12])
{
	EntityModel model = entityGetModelById(entity->VBObank);

	blockGetBoundsForFace(model->bbox, SIDE_SOUTH, vertex, vertex+3, (vec4){0,0,0,0}, 0);
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

/* add the item within the frame in the list of entities to render */
Entity worldItemAddItemFrame(Entity frame, int entityId)
{
	if (frame->special == ENTYPE_FRAMEITEM)
	{
		uint16_t next;
		/* normal item in frame */
		Entity item = entityAlloc(&next);
		frame->next = next;
		item->ref = frame;
		item->next = ENTITY_END;
		item->blockId = frame->blockId & ~ENTITY_ITEM;
		item->tile = frame->tile;
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
	else if (frame->special == ENTYPE_FILLEDMAP)
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

/* get exact coordinates of entity model in world coordinates */
static void worldItemGetCoord(float outCoord[6], float posAndRot[6], VTXBBox bbox)
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
static Bool worldItemFitIn(int entityId, float posAndRot[8], VTXBBox bbox)
{
	float coord[6];
	float diff[3];

	worldItemGetCoord(coord, posAndRot, bbox);
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
			worldItemGetCoord(coord2, entity->pos, size);
			if (coord[VX] < coord2[VX+3] && coord[VX+3] > coord2[VX] &&
				coord[VY] < coord2[VY+3] && coord[VY+3] > coord2[VY] &&
				coord[VZ] < coord2[VZ+3] && coord[VZ+3] > coord2[VZ])
				return False;
		}
		entityId = entity->next;
	}
	return True;
}



void worldItemCreatePainting(Map map, int paintingId)
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

	worldItemFillPos(posAndRot, worldItem.createPos, worldItem.createSide, 0, size);
	c = mapGetChunk(map, posAndRot);
	if (c == NULL) return; /* outside map? */
	if (! worldItemFitIn(c->entityList, posAndRot, entityGetModelById(entityGetModelBank(ITEMID(ENTITY_ITEMFRAME, 0)))->bbox))
	{
		/* does not fit: cancel creation */
		fprintf(stderr, "can't fit painting in %g, %g, %g\n", (double) posAndRot[VX], (double) posAndRot[VY], (double) posAndRot[VZ]);
		return;
	}

	entity = entityAlloc(&slot);
	memcpy(entity->pos, posAndRot, sizeof posAndRot);
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
	entityGetLight(c, entity->pos, entity->light, entity->fullLight = False, 0);
	entityAddToCommandList(entity);

	/* flag chunk for saving later */
	entityMarkListAsModified(map, c);
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

	if (! worldItemFitIn(c->entityList, posAndRot, entityGetModelById(entityGetModelBank(ITEMID(ENTITY_ITEMFRAME, 0)))->bbox))
	{
		/* does not fit: cancel creation */
		fprintf(stderr, "can't fit item frame in %g, %g, %g\n", (double) posAndRot[VX], (double) posAndRot[VY], (double) posAndRot[VZ]);
		return 0;
	}

	entity = entityAlloc(&slot);
	memcpy(entity->pos, posAndRot, sizeof posAndRot);
	if (c == NULL) return 0; /* outside map? */
	worldItemCreateGeneric(&nbt, entity, "item_frame");
	NBT_Add(&nbt, TAG_Compound_End);

	entity->next = c->entityList;
	entity->name = NBT_Payload(&nbt, NBT_FindNode(&nbt, 0, "id"));
	c->entityList = slot;

	entity->tile = nbt.mem;
	entity->rotation[3] = 1;
	entity->VBObank = entityGetModelId(entity);
	entityGetLight(c, entity->pos, entity->light, entity->fullLight = True, 0);
	entityAddToCommandList(entity);
	entityMarkListAsModified(map, c);
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
		memcpy(worldItem.createPos, pos, 12);
		worldItem.createSide = side;
		mceditUIOverlay(MCUI_OVERLAY_PAINTING);
	}
	return 0;
}

/* action on entity */
void worldItemUseItemOn(Map map, int entityId, ItemID_t itemId, vec4 pos)
{
	Entity entity = entityGetById(entityId - 1);

	if (entity && entity->special == ENTYPE_FRAME)
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
			chunkDeleteTile(chunk, entity->tile);
			entity->name = NBT_Payload(&tile, NBT_FindNode(&tile, 0, "id"));
			entity->tile = tile.mem;
			entity->blockId = itemId | ENTITY_ITEM;
			entity->special = strcmp(buffer, "minecraft:filled_map") == 0 ? ENTYPE_FILLEDMAP : ENTYPE_FRAMEITEM;
			uint16_t next = entity->next;
			entity = worldItemAddItemFrame(entity, entityId);
			entity->next = next;
			entityMarkListAsModified(map, chunk);
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
		preview->VBObank = entityAddModel(preview->blockId = itemId, 0, NULL);
		EntityModel model = entityGetModelById(preview->VBObank);

		if (model)
		{
			VTXBBox bbox = model->bbox;
			worldItem.previewOffVY = (bbox->pt2[VY] - bbox->pt1[VY]) / (4.0f*BASEVTX);
		}
		else worldItem.previewOffVY = 0.25f;
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
		memcpy(preview->pos, pos, 12);
		preview->pos[VY] += worldItem.previewOffVY;
		float angle = atan2f(pos[VX] - camera[VX], pos[VZ] - camera[VZ]);
		if (isBlockId(preview->blockId) && blockIds[preview->blockId>>4].special == BLOCK_STAIRS)
			/* viewed from front instead of side */
			angle += M_PI_2f;
		else
			angle += M_PIf;
		preview->rotation[0] = normAngle(angle);
		entityUpdateInfo(preview);
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
			chunk->entityList = worldItem.slot;

			preview->tile = nbt.mem;
			entityGetLight(chunk, preview->pos, preview->light, preview->fullLight = False, 0);
			entityMarkListAsModified(map, chunk);
			worldItem.preview = NULL;
			worldItem.slot = 0;
		}
	}
}
