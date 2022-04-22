/*
 * mobEntity.c: manage mob entities, although this module is not doing much right now.
 *
 * written by T.Pierron, apr 2022.
 */

#define ENTITY_IMPL
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <malloc.h>
#include "entities.h"
#include "render.h"
#include "globals.h"


static char mobIdList[] =
	"creeper,cow,pig,sheep,sheep_wool,chicken,squid,mooshroom,polar_bear,llama,"
	"slime,spider,zombie,skeleton,enderman,iron_golem,snow_golem,bat,wolf,ocelot,"
	"horse,villager,witch";

/* default sheep colormap if nothing is found (missing entries in blockTable.js) */
static uint8_t sheepWoolColors[] = {
	0xda, 0x7d, 0x3e, 0xff,   /* orange */
	0xb4, 0x50, 0xbc, 0xff,   /* magenta */
	0x6b, 0x8a, 0xc9, 0xff,   /* light blue */
	0xb1, 0xa5, 0x27, 0xff,   /* yellow */
	0x41, 0xae, 0x38, 0xff,   /* lime */
	0xd0, 0x84, 0x98, 0xff,   /* pink */
	0x40, 0x40, 0x40, 0xff,   /* gray */
	0x9a, 0xa1, 0xa1, 0xff,   /* light gray */
	0x2e, 0x6e, 0x89, 0xff,   /* cyan */
	0x7e, 0x3e, 0xb5, 0xff,   /* purple */
	0x2e, 0x38, 0x8d, 0xff,   /* blue */
	0x4f, 0x32, 0x1f, 0xff,   /* brown */
	0x35, 0x46, 0x1b, 0xff,   /* green */
	0x96, 0x34, 0x30, 0xff,   /* red */
	0x19, 0x16, 0x16, 0xff,   /* black */
};

#define TEX_WOOL_SHEEP_X      0
#define TEX_WOOL_SHEEP_Y      128
#define TEX_WOOL_SHEEP_W      64
#define TEX_WOOL_SHEEP_H      32
#define TEX_HORSE_W           128
#define TEX_HORSE_H           84
#define TEX_VILLAGER_W        64
#define ENTITY_FIRST_MOB      ENTITY_CREEPER

/* used to generate colored sheep model from base model (white) */
static struct
{
	/* variants will be derived from these */
	struct CustModel_t sheep, slime, llama, horse, villager;

}	mobModels;

static int mobEntityCreate(NBTFile nbt, Entity entity, STRPTR id)
{
	if (nbt == NULL)
	{
		/* initial creation actually */
		CustModel src = (CustModel) entity;
		CustModel cust;
		switch (ITEMNUM((int)id)) {
		default: return 0;
		case ENTITY_SHEEPWOOL: cust = &mobModels.sheep; break;
		case ENTITY_SLIME:     cust = &mobModels.slime; break;
		case ENTITY_LLAMA:     cust = &mobModels.llama; break;
		case ENTITY_HORSE:     cust = &mobModels.horse; break;
		case ENTITY_VILLAGER:  cust = &mobModels.villager; break;
		}

		cust->model = malloc(sizeof (float) * src->vertex);
		cust->vertex = src->vertex;
		cust->texId = 1;
		memcpy(cust->model, src->model, sizeof (float) * src->vertex);
		return 0;
	}

	entity->enflags |= ENFLAG_TEXENTITES;
	entity->entype = ENTYPE_MOB;

	EntityType entype = entityFindType(id);
	if (! entype) return 0;

	int entityId = entype->entityId;
	int data = 0;

	switch (entityId) {
	case ENTITY_SHEEP:
		/* check if it has been sheared */
		if (NBT_GetInt(nbt, NBT_FindNode(nbt, 0, "/Sheared"), 0) == 0)
		{
			entityId = ENTITY_SHEEPWOOL;
			data = NBT_GetInt(nbt, NBT_FindNode(nbt, 0, "/Color"), 0);

			/* only white sheep is initially added, add the other on the fly */
			if (data > 0 && entityGetModelBank(ITEMID(ENTITY_SHEEPWOOL, data)) == 0)
			{
				/* only change texture wool coating of model id 1 */
				mobModels.sheep.faceId = 1;
				mobModels.sheep.U = (data &  7) * TEX_WOOL_SHEEP_W;
				mobModels.sheep.V = (data >> 3) * TEX_WOOL_SHEEP_H;
				entityAddModel(ITEMID(ENTITY_SHEEPWOOL, data), 0, &mobModels.sheep, NULL, False);
			}
		}
		break;
	case ENTITY_LLAMA:
		data = NBT_GetInt(nbt, NBT_FindNode(nbt, 0, "/Variant"), 0);
		/* only creamy variant is generated at start */
		if (data > 0 && entityGetModelBank(ITEMID(ENTITY_LLAMA, data)) == 0)
		{
			/* add the other on the fly */
			mobModels.llama.U = 64 * data;
			entityAddModel(ITEMID(ENTITY_LLAMA, data), 0, &mobModels.llama, NULL, False);
		}
		break;
	case ENTITY_HORSE:
		/* will ignore markings (stored in bits >= 8) */
		data = NBT_GetInt(nbt, NBT_FindNode(nbt, 0, "/Variant"), 0);
		if (data > 5) data = 1;
		if (data > 0 && entityGetModelBank(ITEMID(ENTITY_HORSE, data)) == 0)
		{
			mobModels.horse.U = TEX_HORSE_W * (data & 3);
			mobModels.horse.V = TEX_HORSE_H * (data >> 2);
			mobModels.horse.faceId = 0xff;
			entityAddModel(ITEMID(ENTITY_HORSE, data), 0, &mobModels.horse, NULL, False);
		}
		break;
	case ENTITY_VILLAGER:
		/* XXX probably used for their head orient */
		entity->rotation[1] = 0;
		data = NBT_GetInt(nbt, NBT_FindNode(nbt, 0, "/Profession"), 0);
		if (data > 5) data = 5;
		if (data > 0 && entityGetModelBank(ITEMID(ENTITY_VILLAGER, data)) == 0)
		{
			/* only farmer model initially added */
			mobModels.villager.U = TEX_VILLAGER_W * data;
			mobModels.villager.faceId = 0xff;
			entityAddModel(ITEMID(ENTITY_VILLAGER, data), 0, &mobModels.villager, NULL, False);
		}
		break;
	case ENTITY_SLIME:
		data = NBT_GetInt(nbt, NBT_FindNode(nbt, 0, "/Size"), 0);
		if (data > 0)
		{
			/* only smallest size is generated initially: generate the others on the fly */
			if (data > 3) data = 3;
			if (entityGetModelBank(ITEMID(ENTITY_SLIME, data)) == 0)
			{
				struct CustModel_t slime = mobModels.slime;
				slime.model = alloca(sizeof *slime.model * slime.vertex);
				/* change size of model */
				memcpy(slime.model, mobModels.slime.model, sizeof *slime.model * slime.vertex);
				vec dst, eof;
				int arg;
				for (data ++, dst = slime.model, eof = dst + slime.vertex; dst < eof; dst += arg+1)
				{
					extern uint8_t modelTagArgs[];
					arg = dst[0];
					switch (arg & 0xff) {
					case BHDR_SIZE:
						dst[1] *= data;
						dst[2] *= data;
						dst[3] *= data;
						// no break;
					default:
						arg = modelTagArgs[arg];
						break;
					case BHDR_TEX:
						arg >>= 8;
					}
				}
				entityAddModel(ITEMID(ENTITY_SLIME, data), 0, &slime, NULL, False);
			}
		}
	}

	int vboBank = entityAddModel(ITEMID(entityId, data), 0, NULL, &entity->szx, MODEL_DONT_SWAP);

	/* position of entity in NBT is at feet level, position for display is at center */
	entity->pos[VY] += entity->szy * (0.5f/BASEVTX) + 0.01f;

	return vboBank;
}

void mobEntityInit(void)
{
	STRPTR mob, next;
	int    entityId = ENTITY_FIRST_MOB;
	for (mob = mobIdList; *mob; mob = next, entityId ++)
	{
		for (next = mob; *next && *next != ','; next ++);
		if (*next) *next++ = 0;
		entityRegisterType(mob, mobEntityCreate, entityId);
	}
}

void mobEntityProcessTex(DATA8 * data, int * width, int * height, int bpp)
{
	#if 0
	int size[2], level, texId;
	DATA8 cmap;
	renderGetTerrain(size, &texId);
	for (level = 0; size[0] > 32; size[0] >>= 1, size[1] >>= 1, level ++);
	cmap = malloc(size[0] * size[1] * 4);
	/* the last mipmap actually contains the colormap of terrain texture */
	glBindTexture(GL_TEXTURE_2D, texId);
	glGetTexImage(GL_TEXTURE_2D, level, GL_RGBA, GL_UNSIGNED_BYTE, cmap);

	int itemId = itemGetByName("wool", False);
	#endif

	DATA8 colors, end, bitmap;
	int   i, j, stride = bpp * *width;
	bitmap = *data + TEX_WOOL_SHEEP_X * bpp + TEX_WOOL_SHEEP_Y * stride;
	for (colors = sheepWoolColors, end = EOT(sheepWoolColors), i = 1; colors < end; colors += 4, i ++)
	{
		DATA8 dst = bitmap + (i & 7) * TEX_WOOL_SHEEP_W * bpp + (i >> 3) * stride * TEX_WOOL_SHEEP_H;
		DATA8 src;

		for (src = bitmap, j = 0; j < TEX_WOOL_SHEEP_H; j ++, src += stride, dst += stride)
		{
			DATA8 s, d;
			int   k;
			for (k = 0, s = src, d = dst; k < TEX_WOOL_SHEEP_W; k ++, s += bpp, d += bpp)
			{
				d[0] = s[0] * colors[0] / 255;
				d[1] = s[1] * colors[1] / 255;
				d[2] = s[2] * colors[2] / 255;
				d[3] = s[3];
			}
		}
	}
}
