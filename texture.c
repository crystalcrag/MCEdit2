/*
 * Simple helper function to load textures into an OpenGL texture id
 *
 * Written by T.Pierron, Dec 2019
 */

#define PNGWRITE_IMPL
#define STBIW_CRC32(x, y)    crc32(0, x, y)
#include <glad.h>
#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include <math.h>
#include <zlib.h>
#include "utils.h"
#include "nanovg.h" /* contains stb_image */
#include "PNGWrite.h"

static void textureGenMipmap(DATA8 data, int w, int h, int bpp)
{
	glGenerateMipmap(GL_TEXTURE_2D);
	if (bpp == 4)
	{
		/*
		 * default filtering done by OpenGL is problematic: texture tile that does not contain any
		 * translucent fragment (1 <= alpha <= 254) at level 0, might contain some at level 1 and above
		 * due to linear filtering: that means any texture that isn't fully opaque should go to the alpha
		 * rendering pass, with potential triangle sorting: YUCK!
		 *
		 * there are way too many triangles that are impacted by this, we need to disable alpha for
		 * these textures, therefore applying selective filtering.
		 */
		DATA8 s1, s2, d;
		int   i,  j, mipmap;
		int   stride = w * bpp;

		/*
		 * the terrain texture is 32x64 tiles, therefore texture mapping MUST not use a mipmap level
		 * below 32px x 64px, otherwise nearby tile texture will merge and produce complete garbage.
		 */
		for (mipmap = 1; w > 32; mipmap ++)
		{
			for (j = 0, d = data, s1 = data, s2 = data + stride; j < h; j += 2, s1 += stride, s2 += stride)
			{
				for (i = 0; i < w; i += 2, s2 += 8, s1 += 8, d += 4)
				{
					if (s1[3] == 0 || s1[7] == 0 || s2[3] == 0 || s2[7] == 0)
					{
						int nb = 0;
						int amount[4] = {0};
						if (s1[3]) amount[0] += s1[0], amount[1] += s1[1], amount[2] += s1[2], nb ++;
						if (s1[7]) amount[0] += s1[4], amount[1] += s1[5], amount[2] += s1[6], nb ++;
						if (s2[3]) amount[0] += s2[0], amount[1] += s2[1], amount[2] += s2[2], nb ++;
						if (s2[7]) amount[0] += s2[4], amount[1] += s2[5], amount[2] += s2[6], nb ++;
						/* no alpha in texture, also avoid alpha in mipmap */
						if (nb > 1)
						{
							d[0] = amount[0] / nb;
							d[1] = amount[1] / nb;
							d[2] = amount[2] / nb;
							d[3] = 255;
						}
						else
							d[0] = d[1] = d[2] = d[3] = 0;
					}
					else
					{
						d[0] = (s1[0] + s1[4] + s2[0] + s2[4]) >> 2;
						d[1] = (s1[1] + s1[5] + s2[1] + s2[5]) >> 2;
						d[2] = (s1[2] + s1[6] + s2[2] + s2[6]) >> 2;
						d[3] = (s1[3] + s1[7] + s2[3] + s2[7]) >> 2;
					}
				}
			}
			w /= 2;
			h /= 2;
			stride /= 2;
			glTexSubImage2D(GL_TEXTURE_2D, mipmap, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, data);
		}
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, mipmap-1);
	}
}

void textureDump(int glTex, int w, int h)
{
	DATA8 data = malloc(w * h * 3);
	glBindTexture(GL_TEXTURE_2D, glTex);
	glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
	FILE * out = fopen("dump.ppm", "wb");
	if (out)
	{
		fprintf(out, "P6\n%d %d 255\n", w, h);
		fwrite(data, w * 3, h, out);
		fclose(out);
	}
	free(data);
}

int textureLoad(const char * dir, const char * name, int clamp, PostProcess_t process)
{
	int texId, w, h, bpp, format, cspace;
	DATA8 data;
	w = strlen(dir) + strlen(name) + 2;
	dir = strcpy(alloca(w), dir);
	AddPart((STRPTR) dir, (STRPTR) name, w);

	data = stbi_load(dir, &w, &h, &bpp, 0);

	/* default value for opengl is 4: stb_image return data without padding */
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glPixelStorei(GL_PACK_ALIGNMENT,   1);

	if (data == NULL)
	{
		fprintf(stderr, "fail to load image: %s\n", dir);
		return 0;
	}

	/* post-processing can be chained that way */
	PostProcess_t post;
	for (post = process; post; post = post(&data, &w, &h, bpp));

	switch (bpp) {
	case 1: format = GL_RED; cspace = GL_RED; break;
	case 2: format = GL_LUMINANCE8_ALPHA8; cspace = 0; break;
	case 3: format = GL_RGB8;  cspace = GL_RGB; break;
	case 4: format = GL_RGBA8; cspace = GL_RGBA; break;
	default: return 0; /* should not happen */
	}

	if (data)
	{
		glGenTextures(1, &texId);
		glBindTexture(GL_TEXTURE_2D, texId);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, clamp ? GL_CLAMP : GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, clamp ? GL_CLAMP : GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		/* XXX looks way worse if mipmap is enabled and looking at a shallow angle :-/ */
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
		//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexImage2D(GL_TEXTURE_2D, 0, format, w, h, 0, cspace, GL_UNSIGNED_BYTE, data);
		checkOpenGLError("glTexImage2D");
		if (process)
			textureGenMipmap(data, w, h, bpp)/*, textureSaveSTB("dump.png", w, h, bpp, data, w*bpp)*/;
		else
			glGenerateMipmap(GL_TEXTURE_2D);
		free(data);
		return texId;
	}
	return 0;
}

#define FIRE_WIDTH                  32
#define FIRE_HEIGHT                 32
#define	MIN_VAL                     0
#define	MAX_COL                     256
#define LAVA_TILE_X                 13
#define LAVA_TILE_Y                 14
#define FIRE_TILE_X                 20
#define FIRE_TILE_Y                 7

struct FireEffect_t
{
	uint8_t decay;                  /* cooling particle speed (higher number = lower flame) */
	uint8_t smooth;                 /* how chaotic flames will look (higher number = less chaotic) */
	uint8_t spreadRate;             /* spread to nearby cells (higher number = more turbulence) */
	uint8_t distribution;           /* another turbulence parameter */
	uint8_t chaos;
	uint8_t init;
	int     flammability;           /* how intense foyer will be (higher number = more heat) */
	int     maxHeat;
	uint8_t palette[256*4];
	uint8_t foyer[FIRE_WIDTH];
	uint8_t bitmap[FIRE_WIDTH * (FIRE_HEIGHT + 1)];
	DATA8   temp;
};

void textureAnimate(void)
{
	static float L_soupHeat[256];
	static float L_potHeat[256];
	static float L_flameHeat[256];
	static uint8_t bitmap[16*16*4];

	float soupHeat, potHeat, col;
	int8_t x, y;
	int i;
	DATA8 p;

	/* from https://github.com/UnknownShadow200/ClassiCube/blob/master/src/Animations.c */
	for (y = 0, i = 0, p = bitmap; y < 16; y++)
	{
		for (x = 0; x < 16; x++, p += 4)
		{
			/* Lookup table for (int)(1.2 * sin([ANGLE] * 22.5 * MATH_DEG2RAD)); */
			/* [ANGLE] is integer x/y, so repeats every 16 intervals */
			static int8_t sin_adj_table[16] = { 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, -1, -1, -1, 0, 0 };
			int xx = x + sin_adj_table[y & 0xF], yy = y + sin_adj_table[x & 0xF];

			#define mask  15
			#define shift 4
			soupHeat =
				L_soupHeat[((yy - 1) & mask) << shift | ((xx - 1) & mask)] +
				L_soupHeat[((yy - 1) & mask) << shift | (xx       & mask)] +
				L_soupHeat[((yy - 1) & mask) << shift | ((xx + 1) & mask)] +

				L_soupHeat[(yy & mask) << shift | ((xx - 1) & mask)] +
				L_soupHeat[(yy & mask) << shift | (xx       & mask)] +
				L_soupHeat[(yy & mask) << shift | ((xx + 1) & mask)] +

				L_soupHeat[((yy + 1) & mask) << shift | ((xx - 1) & mask)] +
				L_soupHeat[((yy + 1) & mask) << shift | (xx       & mask)] +
				L_soupHeat[((yy + 1) & mask) << shift | ((xx + 1) & mask)];

			potHeat =
				L_potHeat[i] +                                          /* x    , y     */
				L_potHeat[y << shift | ((x + 1) & mask)] +              /* x + 1, y     */
				L_potHeat[((y + 1) & mask) << shift | x] +              /* x    , y + 1 */
				L_potHeat[((y + 1) & mask) << shift | ((x + 1) & mask)];/* x + 1, y + 1 */
			#undef shift
			#undef mask

			L_soupHeat[i] = soupHeat * 0.1f + potHeat * 0.2f;

			L_potHeat[i] += L_flameHeat[i];
			if (L_potHeat[i] < 0.0f) L_potHeat[i] = 0.0f;

			L_flameHeat[i] -= 0.06f * 0.01f;
			if (RandRange(0, 1) <= 0.005f) L_flameHeat[i] = 1.5f * 0.01f;

			/* Output the pixel */
			col = 2.0f * L_soupHeat[i];
			if (col < 0) col = 0;
			if (col > 1) col = 1;

			p[0] = col * 100.0f + 155.0f;
			p[1] = col * col * 255.0f;
			p[2] = col * col * col * col * 128.0f;
			p[3] = 255;
			i++;
		}
	}

	/* terrain texture must be bound on GL_TEXTURE_2D */
	glTexSubImage2D(GL_TEXTURE_2D, 0, LAVA_TILE_X * 16, LAVA_TILE_Y * 16, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, bitmap);


	/*
	 * animated fire texture: render it in a 32x32 area, because it is too pixelated in a 16x16 tile
	 */

	static struct FireEffect_t fire = {
		.decay = 12,
		.smooth = 3,
		.flammability = 399,
		.maxHeat = 256,
		.chaos = 100,
		.spreadRate = 40,
		.distribution = 1
	};

	if (! fire.init)
	{
		/* setup some lookup tables */
		DATA8 pal;
		int r = 256+256+255-48;
		int g = 256+255-48;
		int b = 255-48, nb;
		fire.init = True;

		for (pal = fire.palette + 255*4, nb = 255; nb >= 0; nb --, pal -= 4)
		{
			pal[0] = (r > 255 ? 255 : r);
			pal[1] = (g > 255 ? 255 : g);
			pal[2] = (b > 255 ? 255 : b);
			pal[3] = nb >= 48 ? 255 : 0; /* avoid using alpha other than 0 or 255 */
			r -= 3; if (r < 0) r = 0;
			g -= 3; if (g < 0) g = 0;
			b -= 3; if (b < 0) b = 0;
		}

		memset(fire.bitmap, MIN_VAL, sizeof fire.bitmap);
		memset(fire.foyer,  MIN_VAL, sizeof fire.foyer);
		fire.temp = malloc(FIRE_WIDTH * FIRE_HEIGHT * 4);
	}

	DATA8 pRow, pNextRow;

	/* compute heat of foyer (first line): next ones will be derived from previous row */
	memcpy(fire.bitmap, fire.foyer, FIRE_WIDTH);
	memset(fire.bitmap, 5, fire.distribution);
	memset(fire.bitmap + FIRE_WIDTH - fire.distribution - 1, 5, fire.distribution);

	/* distribution fire particles (main effect is here) */
	for (y = FIRE_HEIGHT, pRow = fire.bitmap + y * FIRE_WIDTH; y >= 0; y--, pRow -= FIRE_WIDTH)
	{
		pNextRow = pRow - FIRE_WIDTH;
		for (x = 0; x < FIRE_WIDTH; x++)
		{
			if (pNextRow[x] == MIN_VAL) continue;
			int off = rand() % (fire.distribution + 1);
			int val = pNextRow[x] - (rand() % (fire.decay+1));
			int pos = x + (rand() & 1 ? off : -off);

			if (0 <= pos && pos < FIRE_WIDTH)
				pRow[pos] = val < MIN_VAL ? MIN_VAL : val;
		}
	}

	/* add "heat" into the foyer */
	if (rand() % (400 - fire.flammability) == 0)
		memset(fire.foyer + rand() % (FIRE_WIDTH - 15), 128 /* added heat */, 15);

	/* extend flams according to fire.spreadRate and fire.maxHeat */
	for (x = 0; x < FIRE_WIDTH; x++)
	{
		if (fire.foyer[x] < fire.maxHeat)
		{
			int val = rand() % fire.chaos+1;
			val -= fire.chaos / 2;
			val += fire.spreadRate;
			val += fire.foyer[x];

			if (val > fire.maxHeat)
				fire.foyer[x] = fire.maxHeat;
			else if (val < MIN_VAL)
				fire.foyer[x] = MIN_VAL;
			else
				fire.foyer[x] = val;
		}
		else fire.foyer[x] = fire.maxHeat;
	}

	/* smooth values a bit if needed */
	if (fire.smooth > 0)
	{
		for (x = fire.smooth; x < FIRE_WIDTH-fire.smooth; x ++)
		{
			int val = 0;
			for (y = x - fire.smooth; y < x + 1 + fire.smooth; y++)
				val += fire.foyer[y];

			fire.foyer[x] = val / (2*fire.smooth+1);
		}
	}

	/* transfer bitmap to GPU */
	DATA32 src, dst;
	for (y = 0, pRow = fire.bitmap, src = (DATA32) fire.palette, dst = (DATA32) fire.temp + (FIRE_HEIGHT - 1) * FIRE_WIDTH; y < FIRE_HEIGHT;
	     y ++, dst -= FIRE_WIDTH)
	{
		for (x = 0; x < FIRE_WIDTH; dst[x] = src[*pRow], x ++, pRow ++);
	}
	glTexSubImage2D(GL_TEXTURE_2D, 0, FIRE_TILE_X * 16, FIRE_TILE_Y * 16, FIRE_WIDTH, FIRE_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, fire.temp);
}

