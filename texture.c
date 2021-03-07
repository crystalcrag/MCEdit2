/*
 * Simple helper function to load textures into an OpenGL texture id
 *
 * Written by T.Pierron, Dec 2019
 */

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBIW_CRC32(x, y)    crc32(0, x, y);
#include <glad.h>
#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include <math.h>
#include <zlib.h>
#include "utils.h"
#include "nanovg.h" /* contains stb_image */
#include "stb_image_write.h"

static void textureGenMipmap(DATA8 data, int w, int h, int bpp)
{
	glGenerateMipmap(GL_TEXTURE_2D);
	#if 0
	if (bpp == 4)
	{
		char  trans[256];
		DATA8 s1, s2, d;
		int   i,  j, mipmap, step;
		int   stride = w * bpp;

		glGenerateMipmap(GL_TEXTURE_2D);
		memset(trans+1, 0, 254);
		trans[0] = trans[255] = 1;
		step = w / (32 * 4);

		/* generate mipmap until texture is 4x4px */
		for (mipmap = 1; (1<<mipmap) <= step; mipmap ++)
		{
			for (j = 0, d = data, s1 = data, s2 = data + stride; j < h; j += 2, s1 += stride, s2 += stride)
			{
				for (i = 0; i < w; i += 2, s2 += 8, s1 += 8, d += 4)
				{
					char isTrans = trans[s1[3]] && trans[s1[7]] && trans[s2[3]] && trans[s2[7]];
					d[0] = (s1[0] + s1[4] + s2[0] + s2[4]) >> 2;
					d[1] = (s1[1] + s1[5] + s2[1] + s2[5]) >> 2;
					d[2] = (s1[2] + s1[6] + s2[2] + s2[6]) >> 2;
					d[3] = (s1[3] + s1[7] + s2[3] + s2[7]) >> 2;
					if (isTrans)
					{
						/* no alpha in texture, also avoid alpha in mipmap */
						if (d[3] >= 127)
							d[3] = 255;
						else
							d[0] = d[1] = d[2] = d[3] = 0;
					}
				}
			}
			w /= 2;
			h /= 2;
			stride /= 2;
			glTexImage2D(GL_TEXTURE_2D, mipmap, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
		}
	}
	#endif
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

	if (data == NULL)
	{
		fprintf(stderr, "fail to load image: %s\n", dir);
		return 0;
	}

	if (process)
		process(&data, &w, &h, bpp)/*, textureSaveSTB("dump.png", w, h, bpp, data, w*bpp)*/;

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
		// glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexImage2D(GL_TEXTURE_2D, 0, format, w, h, 0, cspace, GL_UNSIGNED_BYTE, data);
		checkOpenGLError("glTexImage2D");
		textureGenMipmap(data, w, h, bpp);
		free(data);
		return texId;
	}
	return 0;
}

/* XX in the name will be replaced by texture part (need power of 2 on intel) */
int textureLoadCubeMap(const char * basename, int single)
{
	static char * ext[] = {"yp", "yn", "xn", "xp", "zn", "zp"};
	static int    type[] = {
		GL_TEXTURE_CUBE_MAP_POSITIVE_Y,
		GL_TEXTURE_CUBE_MAP_NEGATIVE_Y,
		GL_TEXTURE_CUBE_MAP_NEGATIVE_X,
		GL_TEXTURE_CUBE_MAP_POSITIVE_X,
		GL_TEXTURE_CUBE_MAP_NEGATIVE_Z,
		GL_TEXTURE_CUBE_MAP_POSITIVE_Z
	};
	int texId, i;
	int w, h, bpp, format, cspace;

	if (single)
	{
		int   coord[6];
		int   tx, ty, stride;
		DATA8 data = stbi_load(basename, &w, &h, &bpp, 0);
		if (data == NULL) return 0;
		switch (bpp) {
		case 1: format = GL_LUMINANCE8; cspace = 0; break;
		case 2: format = GL_LUMINANCE8_ALPHA8; cspace = 0; break;
		case 3: format = GL_RGB8;  cspace = GL_RGB; break;
		case 4: format = GL_RGBA8; cspace = GL_RGBA;
		default: return 0; /* should not happen */
		}
		tx = w/4*bpp;
		ty = h/3;
		stride = w * bpp;
		coord[0] = tx;
		coord[1] = coord[0] + ty * stride * 2;
		coord[2] = ty * stride;
		coord[3] = coord[2] + 2 * tx;
		coord[4] = coord[3] + tx;
		coord[5] = coord[2] + tx;
		glGenTextures(1, &texId);
		glBindTexture(GL_TEXTURE_CUBE_MAP, texId);
		for (i = 0; i < 6; i ++)
		{
			DATA8 s, d;
			int   j;
			for (s = data + coord[i], d = data, j = ty; j > 0; j --, memcpy(d, s, tx), d += tx, s += stride);
//			textureDump(data, w/4, ty, 3);
			glTexImage2D(type[i], 0, format, tx/bpp, ty, 0, cspace, GL_UNSIGNED_BYTE, data);
		}
		free(data);
	}
	else
	{
		/* load as separate texture */
		STRPTR name = strcpy(alloca(strlen(basename) + 5), basename);
		STRPTR sep  = strstr(name, "XX");

		if (sep == NULL)
			return 0;

		glGenTextures(1, &texId);
		glBindTexture(GL_TEXTURE_CUBE_MAP, texId);

		for (i = 0; i < 6; i ++)
		{
			memcpy(sep, ext[i], 2);
			DATA8 data = stbi_load(name, &w, &h, &bpp, 0);
			if (! data)
			{
				glDeleteTextures(1, &texId);
				return 0;
			}
			switch (bpp) {
			case 1: format = GL_LUMINANCE8; cspace = 0; break;
			case 2: format = GL_LUMINANCE8_ALPHA8; cspace = 0; break;
			case 3: format = GL_RGB8;  cspace = GL_RGB; break;
			case 4: format = GL_RGBA8; cspace = GL_RGBA;
			default: return 0; /* should not happen */
			}
			glTexImage2D(type[i], 0, format, w, h, 0, cspace, GL_UNSIGNED_BYTE, data);
			checkOpenGLError("glTexImage2D");
			free(data);
			// textureSetAniso();
		}
	}
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	checkOpenGLError("loadTextureCubeMap");
	return texId;
}

/* generate a checkboard texture, useful for debug */
int textureCheckboard(int w, int h, int cellsz, DATA8 color1, DATA8 color2)
{
	DATA8 bitmap, d, s, s2;
	int   i, j, k, stride;

	bitmap = malloc(w * h * 3);
	if (! bitmap) return 0;

	for (j = 0, stride = w * 3, d = bitmap, s = color1; j < h; j ++)
	{
		for (s2 = s, i = 0, k = cellsz; i < w; i ++, d += 3)
		{
			memcpy(d, s2, 3);
			k --; if (k == 0) s2 = (s2 == color1 ? color2 : color1), k = cellsz;
		}
		k = j + cellsz - 1;
		if (k >= h) k = h - 1;
		for (; j < k; d += stride, j ++)
			memcpy(d, d - stride, stride);
		s = (s == color1 ? color2 : color1);
	}

	GLuint texId;
	glGenTextures(1, &texId);
	glBindTexture(GL_TEXTURE_2D, texId);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, bitmap);
	free(bitmap);
	glGenerateMipmap(GL_TEXTURE_2D);
	return texId;
}

