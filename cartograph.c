/*
 * cartograph.c : handle in-game maps (the one you can display in a item frame). Use same techniques
 *                as signs: copy map in a texture bank (1024x1024) and render them in a quad as decals.
 *
 * Written by T.Pierron, oct 2021.
 */

#define CARTOGRAPH_IMPL
#include <glad.h>
#include <malloc.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "NBT2.h"
#include "maps.h"
#include "cartograph.h"
#include "globals.h"


struct CartoPrivate_t cartograph;

/* these are the 64 base colors used by maps */
uint8_t mapShading[] = {180, 220, 255, 135};
uint8_t mapLight[] = {2, 0, 3, 1, 3, 0};
uint8_t mapRGB[] = {
	255, 255, 255, 0x00, //  0: unexplored area
	127, 178, 56,  0xff, //  1: grass
	247, 233, 163, 0xff, //  2: sand
	199, 199, 199, 0xff, //  3: mushroom block
	255, 0,   0,   0xff, //  4: lava
	160, 160, 255, 0xff, //  5: ice
	167, 167, 167, 0xff, //  6: iron block
	0,   124, 0,   0xff, //  7: leaves
	255, 255, 255, 0xff, //  8: snow
	164, 168, 184, 0xff, //  9: clay
	151, 109, 77,  0xff, // 10: jungle wood
	112, 112, 112, 0xff, // 11: cobblestone/stone
	64,  64,  255, 0xff, // 12: water
	143, 119, 72,  0xff, // 13: oak wood
	255, 252, 245, 0xff, // 14: birch log
	216, 127, 51,  0xff, // 15: red sandstone/orange wool
	178, 76,  216, 0xff, // 16: purpur/magenta wool
	102, 153, 216, 0xff, // 17: light blue wool
	229, 229, 51,  0xff, // 18: hay bale/yellow wool
	127, 204, 25,  0xff, // 19: melon/lime wool
	242, 127, 165, 0xff, // 20: pink wool
	76,  76,  76,  0xff, // 21: gray wool
	153, 153, 153, 0xff, // 22: light gray
	76,  127, 153, 0xff, // 23: cyan wool
	127, 63,  178, 0xff, // 24: purple wool
	51,  76,  178, 0xff, // 25: blue wool
	102, 76,  51,  0xff, // 26: brown wool
	102, 127, 51,  0xff, // 27: green wool
	153, 51,  51,  0xff, // 28: red wool
	25,  25,  25,  0xff, // 29: black wool
	250, 238, 77,  0xff, // 30: gold block
	92,  219, 213, 0xff, // 31: diamond block
	74,  128, 255, 0xff, // 32: lapis block
	0,   217, 58,  0xff, // 33: emerald block
	129, 86,  49,  0xff, // 34: spruce wood
	112, 2,   0,   0xff, // 35: netherrack
	209, 177, 161, 0xff, // 36: white terracotta    -- 1.12 only
	159, 82,  36,  0xff, // 37: orange terracotta
	149, 87,  108, 0xff, // 38: magenta terracotta
	112, 108, 138, 0xff, // 39: light blue
	186, 133, 36,  0xff, // 40: yellow
	103, 117, 53,  0xff, // 41: lime
	160, 77,  78,  0xff, // 42: pink
	57,  41,  35,  0xff, // 43: gray
	135, 107, 98,  0xff, // 44: light gray
	87,  92,  92,  0xff, // 45: cyan
	122, 73,  88,  0xff, // 46: purple
	76,  62,  92,  0xff, // 47: blue
	76,  50,  35,  0xff, // 48: brown
	76,  82,  42,  0xff, // 49: green
	142, 60,  46,  0xff, // 50: red
	37,  22,  16,  0xff, // 51: black

	/* minecraft 1.13+ */
	189, 48,  49,  0xff, // 52
	148, 63,  97,  0xff, // 53
	92,  25,  29,  0xff, // 54
	22,  126, 134, 0xff, // 55
	58,  142, 140, 0xff, // 56
	86,  44,  62,  0xff, // 57
	20,  180, 133, 0xff, // 58
	100, 100, 100, 0xff, // 59
	216, 175, 147, 0xff, // 60
	127, 167, 150, 0xff, // 61

	/* slots 62~63: unused for now */
	0,   0,   0,   0x00,
	0,   0,   0,   0x00
};

void cartoInitStatic(int shader, int * mdaCount)
{
	/* same as signs */
	cartograph.shader = shader;
	cartograph.mdaCount = mdaCount;
	cartograph.lastIdCount = -1;
}

/* save a NBT chunk in the map folder of current level */
int cartoSaveMap(DATA8 mem, int size)
{
	STRPTR levelDat = globals.level->path;
	STRPTR path = alloca(strlen(levelDat) + 32);
	int    lastId = cartograph.lastIdCount;
	int    len;
	NBTFile_t nbt = {.page = 127};

	strcpy(path, levelDat);
	ParentDir(path);
	len = strlen(path);
	if (lastId < 0)
	{
		/* this file will contain the last committed map id used */
		AddPart(path, "data/idcounts.dat", 1e6);
		if (NBT_Parse(&nbt, path))
		{
			lastId = NBT_ToInt(&nbt, NBT_FindNode(&nbt, 0, "map"), 0);
			NBT_Free(&nbt);
		}
		else /* hmm, missing or can't read it: let's be cautious */
		{
			ScanDirData args;
			ParentDir(path);
			lastId = 0;
			if (ScanDirInit(&args, path))
			{
				do {
					int mapId;
					if (strncasecmp(args.name, "map_", 4) == 0 && sscanf(args.name + 4, "%d", &mapId) > 0 && mapId > lastId)
						lastId = mapId;
				}
				while (ScanDirNext(&args));
			}
		}
		cartograph.lastIdCount = lastId;
		cartograph.lastMapId = lastId;
		path[len] = 0;
	}

	AddPart(path, "data/map_", 1e6);
	len += strlen(path+len);
	sprintf(path+len, "%d.dat", ++ cartograph.lastMapId);

	nbt.mem = mem;
	nbt.usage = size;

	NBT_Save(&nbt, path, NULL, NULL);

	return cartograph.lastMapId;
}

static FILE * fopen_base(STRPTR base, STRPTR path, STRPTR open)
{
	STRPTR buffer = alloca(strlen(base) + strlen(path) + 2);
	strcpy(buffer, base);
	AddPart(buffer, path, 1e6);
	return fopen(buffer, open);
}

/* mark all temporary maps as permanent */
void cartoCommitNewMaps(void)
{
	if (cartograph.lastMapId > 0 && cartograph.lastMapId > cartograph.lastIdCount)
	{
		/* idcounts.dat is an uncompressed NBT file: hardcode content */
		static uint8_t buffer[] = {0x0A, 0, 0, 0x02, 0, 0x03, 'm', 'a', 'p', 0, 0, 0};

		/* update idcounts.dat */
		FILE * out = fopen_base(globals.level->path, "../data/idcounts.dat", "wb");
		int    i   = cartograph.lastMapId;
		buffer[sizeof buffer-3] = i >> 8;
		buffer[sizeof buffer-2] = i & 255;
		if (out)
		{
			fwrite(buffer, 1, sizeof buffer, out);
			fclose(out);

			/* clear temp flag */
			Cartograph map;
			for (i = cartograph.count, map = cartograph.maps; i > 0; map->temp = 0, i --, map ++);
		}
	}
}

/* convert map from NBT to GL texture */
void cartoGenBitmap(Cartograph map, int texId)
{
	STRPTR levelDat = globals.level->path;
	STRPTR path = alloca(strlen(levelDat) + 32);
	TEXT   mapPath[32];

	strcpy(path, levelDat);
	sprintf(mapPath, "../data/map_%d.dat", map->mapId);
	AddPart(path, mapPath, 1e6);

	NBTFile_t nbt;
	if (NBT_Parse(&nbt, path))
	{
		int cmap = NBT_FindNode(&nbt, 0, "colors");
		NBTHdr hdr = NBT_Hdr(&nbt, cmap);

		/* this map only exist in memory for now: will have to delete it if changes are not saved */
		if (cartograph.lastIdCount >= 0 && map->mapId > cartograph.lastIdCount)
			map->temp = True;

		if (cmap >= 0 && hdr->type == TAG_Byte_Array && hdr->count <= CARTO_WIDTH * CARTO_HEIGHT)
		{
			DATA8 tex = malloc(4 * CARTO_WIDTH * CARTO_HEIGHT);
			DATA8 src = NBT_Payload(&nbt, cmap);
			DATA8 dst;
			int   i, j;

			for (j = 0, dst = tex; j < CARTO_HEIGHT; j ++)
			{
				for (i = 0; i < CARTO_WIDTH; i ++, dst += 4, src ++)
				{
					#if 1
					uint8_t s = *src;
					#else /* used to debug lighting: map will be white (snow) */
					uint8_t s = (8<<2)|2;
					#endif
					if (s >= DIM(mapRGB)) s = 0; /* black */
					DATA8 rgb = mapRGB + (s & ~3);
					uint8_t shader = mapShading[s & 3];
					dst[0] = rgb[0] * shader / 255;
					dst[1] = rgb[1] * shader / 255;
					dst[2] = rgb[2] * shader / 255;
					dst[3] = rgb[3];
				}
			}
			i = map->bank >> 10;
			glBindTexture(GL_TEXTURE_2D, texId);
			glTexSubImage2D(GL_TEXTURE_2D, 0, (i & 7) * CARTO_WIDTH, (i >> 3) * CARTO_HEIGHT, CARTO_WIDTH, CARTO_HEIGHT,
				GL_RGBA, GL_UNSIGNED_BYTE, tex);
			glBindTexture(GL_TEXTURE_2D, 0);
			free(tex);
		}
		NBT_Free(&nbt);
	}
}

/* generate vertex for map quad: very similar to sign, except we will use 4 light values instead of 1 */
static void cartoGenVertex(Cartograph map, CartoBank bank, float points[12])
{
	#define COLOR_DECAL   (1<<8)
	/* generate one quad per map: need 6 vec4 */
	static uint8_t mapVtx[] = {6,0,3, 9,3,0};
	static float addMeta[] = {
		COLOR_DECAL, COLOR_DECAL + (1<<15), COLOR_DECAL + (1<<10), COLOR_DECAL + ((1<<15)|(1<<10)),
		COLOR_DECAL + (1<<10), COLOR_DECAL + (2<<14)
	};
	float vertices[4*6], meta;
	uint8_t slot;

	/* using 4 light values will provide a smooth transition between corners if there is a light source near the item frames */
	slot = CBANK_SLOT(map->bank);
	meta = ((slot / CBANK_WIDTH) << 15) | ((slot & (CBANK_WIDTH-1)) << 10);

	int i;
	vec vtx;
	for (i = 0, vtx = vertices; i < 6; i ++, vtx += 4)
	{
		memcpy(vtx, points + mapVtx[i], 12);
		vtx[3] = meta + addMeta[i] + map->light[mapLight[i]];
	}
	i = slot * 6;
	glBindBuffer(GL_ARRAY_BUFFER, bank->vbo);
	glBufferSubData(GL_ARRAY_BUFFER, i * 16, sizeof vertices, vertices);
	bank->mdaFirst[slot] = i;
	if (bank->inMDA <= slot)
		bank->inMDA = slot+1;
	cartograph.toRender ++;
}

void cartoUpdateLight(int entityId, DATA32 light)
{
	Cartograph map;
	int i;
	for (i = 0, map = cartograph.maps; i < cartograph.count && map->entityId != entityId; map ++, i ++);
	if (i < cartograph.count)
	{
		uint32_t face = light[map->normal];
		for (i = 0; i < 4; i ++, face >>= 8)
			map->light[i] = face & 0xff;

		/* need to update VBO, but thankfully there are only 6 vertices */
		CartoBank bank = cartograph.banks + CBANK_NUM(map->bank);

		glBindBuffer(GL_ARRAY_BUFFER, bank->vbo);
		vec array = glMapBuffer(GL_ARRAY_BUFFER, GL_READ_WRITE);
		array += CBANK_SLOT(map->bank) * 4 * 6 + 3;

		for (i = 0; i < 6; i ++, array += 4)
			array[0] = ((int) array[0] & ~0xff) | map->light[mapLight[i]];

		glUnmapBuffer(GL_ARRAY_BUFFER);
	}
}


/* group maps into bank for easier rendering later */
static void cartoAddToBank(Cartograph map, float points[12])
{
	CartoBank bank;
	int       i, slot;

	/*
	 * we could check if map->mapId has already been processed: in practice it is way TOO MUCH
	 * boilerplate work, for something that should not happen very often.
	 */
	for (i = 0, slot = -1, bank = cartograph.banks; i < cartograph.maxBank; i ++, bank ++)
	{
		if (bank->inBank < CBANK_MAX)
		{
			slot = mapFirstFree(bank->usage, DIM(bank->usage));
			if (slot >= 0) break;
		}
	}

	if (slot < 0)
	{
		cartograph.maxBank ++;
		bank = realloc(cartograph.banks, cartograph.maxBank * sizeof *cartograph.banks);
		if (bank == NULL) return;
		cartograph.banks = bank;
		bank += cartograph.maxBank - 1;
		memset(bank, 0, sizeof *bank);
		/* array is of fixed size, but don't want to be included in struct: too many bytes to relocate */
		bank->mdaFirst = calloc(4, CBANK_MAX);
		bank->usage[0] = 1;
		slot = 0;
	}

	if (bank->vbo == 0)
	{
		glGenBuffers(1, &bank->vbo);
		glGenVertexArrays(1, &bank->vao);

		glBindVertexArray(bank->vao);
		glBindBuffer(GL_ARRAY_BUFFER, bank->vbo);
		glBufferData(GL_ARRAY_BUFFER, 16 * 6 * (CBANK_MAX + 1), NULL, GL_STATIC_DRAW);
		glVertexAttribPointer(0, 4, GL_FLOAT, 0, 0, 0);
		glEnableVertexAttribArray(0);

		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindVertexArray(0);
	}
	if (bank->glTex == 0)
	{
		int texId;
		/* that's the main difference with signs: we are using a simple GL texture for maps, no renderbuffer */
		glGenTextures(1, &texId);
		glBindTexture(GL_TEXTURE_2D, texId);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, CBANK_WIDTH * CARTO_WIDTH, CBANK_HEIGHT * CARTO_HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		glBindTexture(GL_TEXTURE_2D, 0);
		bank->glTex = texId;
	}

	bank->update = 1;
	bank->inBank ++;
	map->bank = (slot << 10) | i;

	/* update bitmap */
	cartoGenBitmap(map, bank->glTex);
	cartoGenVertex(map, bank, points);
}

void cartoAddMap(int entityId, float coord[12], int mapId, DATA32 light)
{
	struct Cartograph_t map = {.entityId = entityId, .bank = -1, .mapId = mapId};

	if (cartograph.count == cartograph.max)
	{
		int old = cartograph.max;
		int max = old + 32;
		cartograph.maps = realloc(cartograph.maps, max * sizeof map + (max >> 5) * 4);

		/* move some buffers to keep them contiguous */
		cartograph.usage = (DATA32) (cartograph.maps + max);
		cartograph.max = max;
		if (old > 0)
		{
			/* usage buffers (in signs.list) */
			memmove(cartograph.maps + old, cartograph.usage, 4 * (old >> 5));
		}
		cartograph.usage[old>>5] = 0;
	}
	/* try to find which side coord[] is closest to */
	if (fabsf(coord[VX] - coord[VX+3]) < EPSILON)
	{
		/* VX same: must be east or west */
		map.normal = coord[VX] - (int) coord[VX] < 0.5f ? SIDE_EAST : SIDE_WEST;
	}
	else if (fabsf(coord[VZ] - coord[VZ+3]) < EPSILON)
	{
		/* VZ same: either south or north */
		map.normal = coord[VZ] - (int) coord[VZ] < 0.5f ? SIDE_SOUTH : SIDE_NORTH;
	}
	else map.normal = coord[VY] - (int) coord[VY] < 0.5f ? SIDE_TOP : SIDE_BOTTOM;

	#if 1
	int i;
	uint32_t face = light[map.normal];
	for (i = 0; i < 4; i ++, face >>= 8)
		map.light[i] = face & 0xff;
	#else
	int i, min, max;
	uint32_t face = light[map.normal];
	for (i = 0, min = 15, max = 0; i < 4; i ++, face >>= 8)
	{
		uint8_t val = face & 0xff;
		map.light[i] = val;
		if (min > val) min = val;
		if (max < val) max = val;
	}
	if (min < max)
		for (i = 0, max -= min; i < 4; i ++)
			map.light[i] = (map.light[i] - min) * 15 / max;
	#endif

	// fprintf(stderr, "%d. light (%d) = %02x, %02x, %02x, %02x\n", entityId, map.normal, map.light[0], map.light[1], map.light[2], map.light[3]);

	/* check for a free place */
	i = mapFirstFree(cartograph.usage, cartograph.max >> 5);

	cartograph.maps[i] = map;
	cartograph.count ++;
	cartoAddToBank(cartograph.maps + i, coord);
}

/* item frame deleted: remove map texture */
void cartoDelMap(int entityId)
{
	Cartograph map;
	int        i;
	for (i = 0, map = cartograph.maps; i < cartograph.count && map->entityId != entityId; map ++, i += map->bank >= 0);
	// fprintf(stderr, "deleting map %d: %d / %d\n", entityId, i, cartograph.count);
	if (i < cartograph.count)
	{
		int slot = map->bank;
		cartograph.usage[i >> 5] ^= 1 << (i & 31);
		cartograph.count --;
		cartograph.toRender --;
		map->bank = -1;
		if (map->temp)
		{
			/* delete file too */
			STRPTR base = globals.level->path;
			STRPTR buffer = alloca(strlen(base) + 32);
			TEXT   mapPath[32];
			strcpy(buffer, base);
			sprintf(mapPath, "../data/map_%d.dat", map->mapId);
			AddPart(buffer, mapPath, 1e6);
			DeleteDOS(buffer);
		}

		CartoBank bank = cartograph.banks + CBANK_NUM(slot);

		/* free slot from bank */
		slot = CBANK_SLOT(slot);
		bank->usage[slot >> 5] ^= 1 << (slot & 31);
		bank->inBank --;
		if (bank->inBank == 0)
		{
			/* only delete what's expensive */
			glDeleteTextures(1, &bank->glTex);
			bank->inMDA = 0;
			bank->glTex = 0;
			return;
		}

		#define DELETED_SLOT         CBANK_MAX
		/* 64th slot have all vertices set to 0, will be discarded by vertex shader */
		bank->mdaFirst[slot] = DELETED_SLOT;
		if (slot == bank->inMDA-1)
		{
			/* last map being deleted: we can reduce the number of instance count */
			for (i = slot - 1; i > 0 && bank->mdaFirst[i] == DELETED_SLOT; i --);
			bank->inMDA = i + 1;
		}
		/* else relocating VBO not worth the cost: there are only 6 vertices per model */
	}
}

/* toggle selection flag in vertex buffer */
void cartoSetSelect(int entityId, Bool set)
{
	Cartograph map;
	int        i;
	/* not a performance critical path */
	for (i = 0, map = cartograph.maps; i < cartograph.count && map->entityId != entityId; map ++, i += map->bank >= 0);
	if (i < cartograph.count)
	{
		CartoBank bank = cartograph.banks + CBANK_NUM(map->bank);

		glBindBuffer(GL_ARRAY_BUFFER, bank->vbo);
		vec array = glMapBuffer(GL_ARRAY_BUFFER, GL_READ_WRITE);
		array += CBANK_SLOT(map->bank) * 4 * 6 + 3;
		if (set)
		{
			for (i = 0; i < 6; i ++, array += 4)
				array[0] = (int) array[0] | (1 << 9);
		}
		else /* clear selection flag */
		{
			for (i = 0; i < 6; i ++, array += 4)
				array[0] = (int) array[0] & ~(1 << 9);
		}
		glUnmapBuffer(GL_ARRAY_BUFFER);
	}
}


/* very similar to sign */
void cartoRender(void)
{
	if (cartograph.toRender == 0)
		return;

	glCullFace(GL_BACK);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_DEPTH_TEST);
//	glEnable(GL_POLYGON_OFFSET_FILL);
	glDepthFunc(GL_LEQUAL);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_STENCIL_TEST);
	glFrontFace(GL_CCW);
	glActiveTexture(GL_TEXTURE0);
	/*
	 * maps shouldn't have any geometry behind, therefore depth buffer shouldn't interfere
	 * but let's be safe, and apply the same offset than sign rendering.
	 */
//	glPolygonOffset(-5.0, -5.0);

	/* will use the same shader than signs actually (decals.vsh) */
	glUseProgram(cartograph.shader);

	CartoBank bank;
	int       count;
	for (bank = cartograph.banks, count = cartograph.maxBank; count > 0; count --, bank ++)
	{
		if (bank->inMDA == 0) continue;
		glBindVertexArray(bank->vao);
		glBindTexture(GL_TEXTURE_2D, bank->glTex);
		if (bank->update)
		{
			glGenerateMipmap(GL_TEXTURE_2D);
			bank->update = 0;
		}
		glMultiDrawArrays(GL_TRIANGLES, bank->mdaFirst, cartograph.mdaCount, bank->inMDA);
		glBindVertexArray(0);
	}
	glDisable(GL_POLYGON_OFFSET_FILL);
}

