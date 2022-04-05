/*
 * mobEntity.c: manage mob entities, although this module not doing much right now.
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


char mobIdList[] = "minecart,creeper,cow,pig,sheep,sheep_wool,chicken,mooshroom,horse,squid,bat,zombie,skeleton,enderman";

/* default sheep colormap if nothing is found (broken blockTable.js) */
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

/* used to generated colored sheep model from base model (white) */
static struct CustModel_t sheepModel;

static int mobEntityCreate(NBTFile nbt, Entity entity, STRPTR id)
{
	entity->enflags |= ENFLAG_TEXENTITES;
	entity->entype = ENTYPE_MOB;

	int entityId = FindInList(mobIdList, id, 0) + ENTITY_MINECART;
	int data = 0;
	if (entityId < ENTITY_MINECART) return 0;
	if (entityId == ENTITY_MINECART)
		entity->enflags |= ENFLAG_HASBBOX;

	if (entityId == ENTITY_SHEEP)
	{
		/* check if it has been sheared */
		if (NBT_GetInt(nbt, NBT_FindNode(nbt, 0, "/Sheared"), 0) == 0)
		{
			entityId = ENTITY_SHEEPWOOL;
			data = NBT_GetInt(nbt, NBT_FindNode(nbt, 0, "/Color"), 0);

			/* only white sheep is initially added, add the other on the fly */
			if (data > 0 && entityGetModelBank(ITEMID(ENTITY_SHEEPWOOL, data)) == 0)
			{
				/* only change wool coating of model */
				sheepModel.faceId = 1;
				sheepModel.U = (data & 7) * TEX_WOOL_SHEEP_W;
				sheepModel.V = (data >> 3) * TEX_WOOL_SHEEP_H;
				entityAddModel(ITEMID(ENTITY_SHEEPWOOL, data), 0, &sheepModel, NULL, False);
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
	entityRegisterType("creeper", mobEntityCreate);
	entityRegisterType("cow",     mobEntityCreate);
	entityRegisterType("pig",     mobEntityCreate);
	entityRegisterType("sheep",   mobEntityCreate);
	entityRegisterType("chicken", mobEntityCreate);
	entityRegisterType("squid",   mobEntityCreate);
}

void mobEntityProcess(int entityId, float * model, int count)
{
	if (entityId == ITEMID(ENTITY_SHEEPWOOL, 0))
	{
		sheepModel.model = malloc(sizeof *model * count);
		sheepModel.vertex = count;
		sheepModel.texId = 1;
		memcpy(sheepModel.model, model, sizeof *model * count);
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
	#if 0
	FILE * out = fopen("dump.ppm", "wb");
	if (out)
	{
		fprintf(out, "P6\n%d %d 255\n", *width, *height);
		fwrite(*data, *width * bpp, *height, out);
		fclose(out);
	}
	#endif
}
