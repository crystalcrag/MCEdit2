/*
 * raycasting.c: use raycasting and special chunk rendering to draw distant voxels.
 *
 * written by T.Pierron, may 2022.
 */

#include <glad.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <math.h>
#include "maps.h"
#include "NBT2.h"
#include "raycasting.h"
#include "globals.h"

static struct RaycastPrivate_t raycast = {
	.shading = {230, 204, 230, 204, 255, 179}
};

#define SCR_WIDTH         400
#define SCR_HEIGHT        400

/* init opengl objects to do raycasting on GPU */
Bool raycastInitStatic(void)
{
	raycast.shader = createGLSLProgram("raycaster.vsh", "raycaster.fsh", NULL);

	if (! raycast.shader)
		return False;

	/* coordinates must be normalized between -1 and 1 for XY and [0 - 1] for Z */
	#define ZVAL  1
	static float vertices[] = {
		1.0,  1.0, ZVAL,  -1.0, 1.0, ZVAL,   1.0, -1.0, ZVAL,
		1.0, -1.0, ZVAL,  -1.0, 1.0, ZVAL,  -1.0, -1.0, ZVAL
	};
	#undef ZVAL

	glGenBuffers(1, &raycast.vbo);
	glGenVertexArrays(1, &raycast.vao);
	glBindVertexArray(raycast.vao);
	glBindBuffer(GL_ARRAY_BUFFER, raycast.vbo);
	glBufferData(GL_ARRAY_BUFFER, 6 * 12, vertices, GL_STATIC_DRAW);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0);
	glEnableVertexAttribArray(0);

	glBindVertexArray(0);
//	raycast.texRadius = globals.extraDist;
//	raycast.texHole   = map->maxDist;
	return True;
}

void raycastRender(void)
{
	glDepthMask(GL_FALSE);
	glUseProgram(raycast.shader);
	glBindVertexArray(raycast.vao);
	glDrawArrays(GL_TRIANGLES, 0, 6);
	glBindVertexArray(0);
	glDepthMask(GL_TRUE);
}

/* taken from https://algotree.org/algorithms/stack_based/largest_rectangle_in_histogram */
static void maxAreaHistogram(DATA8 histogram, DATA8 res)
{
	uint8_t stack[16];
	uint8_t position[16];
	int     stackSize, i, maxArea, topStack;

	res[0] = res[1] = res[2] = res[3] = 0;
	maxArea = 0;

	for (stackSize = topStack = 0, i = 0; i < 16; i ++)
	{
		int h = histogram[i];
		if (topStack <= h)
		{
			stack[stackSize] = topStack = h;
			position[stackSize] = i;
			stackSize ++;
			if (i == 15)
			{
				h = 0;
				i ++;
				goto pop_all;
			}
		}
		else
		{
			int pos, area;
			do {
				pop_all:
				stackSize --;
				pos = position[stackSize];
				area = (i - pos) * topStack;
				if (area > maxArea)
				{
					res[0] = i - pos;
					res[1] = topStack;
					res[2] = pos;
					maxArea = area;
				}
				topStack = stackSize > 0 ? stack[stackSize-1] : 0;
			}
			while (topStack > h);
			stack[stackSize] = topStack = h;
			position[stackSize] = pos;
			stackSize ++;
		}
	}
}

#ifdef DEBUG
void printLayer(DATA8 rgba, int y)
{
	int i, j;
	fprintf(stderr, "layer %d:\n", y);
	for (j = 0; j < 16; j ++)
	{
		for (i = 0; i < 16; i ++, rgba += 4)
		{
			fputc(rgba[3] == 0 ? '1' : '.', stderr);
		}
		fputc('\n', stderr);
	}
}
#endif

/* taken from https://www.algotree.org/algorithms/stack_based/maximum_size_rectangle_in_a_binary_matrix */
static void maxAreaMatrix(DATA8 rgba, DATA8 res, DATA8 maxRegion)
{
	uint8_t histogram[16];
	int i, j, maxArea, maxZ, maxX;

	res[0] = res[1] = res[2] = res[3] = 0;
	maxArea = 0;

	maxX = maxRegion[0] == 0 ? 16 : maxRegion[0];
	maxZ = maxRegion[1] == 0 ? 16 : maxRegion[1];
	memset(histogram, 0, sizeof histogram);
	rgba += maxRegion[3] * 16 * 4 + 3;
	for (j = maxRegion[3]; j < maxZ; j ++, rgba += 16*4)
	{
		for (i = maxRegion[2]; i < maxX; i ++)
		{
			/* if alpha of voxel is exactly 0 == consider this an air block */
			if (rgba[i<<2] == 0) histogram[i] ++;
			else histogram[i] = 0;
		}

		uint8_t area[4];
		maxAreaHistogram(histogram, area);
		if (area[0] * area[1] > maxArea)
		{
			maxArea = area[0] * area[1];
			memcpy(res, area, 4);
			res[3] = j - (area[1] - 1);
		}
	}
	res[0] += res[2];
	res[1] += res[3];
}

void chunkConvertToRGBA(ChunkData cd)
{
	uint8_t layerArea[16*4];
	DATA8   rgba = calloc(4096, 4);
	int     y, boxes, layerDone;

	/* pre-process blockIds first */
	{
		struct BlockIter_t iter = {.cd = cd, .blockIds = cd->blockIds};
		for (y = 0; y < 4096; y ++)
		{
			static uint8_t air[] = {0,0,0,0};
			iter.offset = y;
			BlockState state = blockGetById(getBlockId(&iter));
			DATA8 tex = &state->pyU;
			if (tex[0] == 30 && tex[1] == 0)
				/* undefined tex */
				tex = &state->nzU;

			/* only cares about texture of top face */
			memcpy(rgba + (y << 2),
				state == blockGetById(0) || state->type == QUAD ? air :
					raycast.palette + tex[1] * raycast.paletteStride + (tex[0] << 2), 4);
//			if ((y & 255) == 255)
//				printLayer(rgba + (y - 255) * 4, y >> 8);
		}
	}

	for (boxes = layerDone = 0; layerDone != 0xffff; )
	{
		DATA8 area;
		uint8_t curArea[6];
		/* scan region from top to bottom (above ground chunks have lots of air at the top) */
		for (y = 15, area = layerArea + 15*4, memset(curArea, 0, 4); y >= 0; y --, area -= 4)
		{
			if (layerDone & (1 << y))
			{
				if (curArea[0] > 0) break;
				else continue;
			}
			/* find maximum volume of air in this chunk */
			// printLayer(rgba + (y << 10), y)
			maxAreaMatrix(rgba + (y << 10), area, curArea);
			if (area[0] == 0)
			{
				if (curArea[0] - curArea[2] == 16 &&
					curArea[1] - curArea[3] == 16)
					layerDone |= 1 << y;
				if (curArea[0] > 0) break;
				else continue;
			}
			else if (curArea[0] > 0)
			{
				/* check if intersecting regions on XZ plane, will produce a wider volume */
				uint8_t tmpArea[4];
				tmpArea[0] = curArea[0] > area[0] ? area[0] : curArea[0];
				tmpArea[1] = curArea[1] > area[1] ? area[1] : curArea[1];
				tmpArea[2] = curArea[2] < area[2] ? area[2] : curArea[2];
				tmpArea[3] = curArea[3] < area[3] ? area[3] : curArea[3];
				if ((tmpArea[0] - tmpArea[2]) * (tmpArea[1] - tmpArea[3]) * (curArea[5]+1) >=
				    (curArea[0] - curArea[2]) * (curArea[1] - curArea[3]) * curArea[5])
				{
					memcpy(curArea, tmpArea, 4);
					curArea[5] ++;
				}
				else break;
			}
			else memcpy(curArea, area, 4), curArea[4] = y, curArea[5] = 1;
		}

		if (curArea[0] > 0)
		{
			/* if there is only one voxel, it can be encoded the normal way */
			uint8_t szx = curArea[0] - curArea[2];
			uint8_t szz = curArea[1] - curArea[3];
			uint8_t szy = curArea[5];
			uint8_t voxel[4] = {
				curArea[2] | (curArea[3] << 4), /* XZ pos */
				(curArea[4] - curArea[5] + 1) | ((szy - 1) << 4), /* Y pos/size */
				(szx - 1) | ((szz - 1) << 4), /* XZ size */
				0x80
			};

			y = voxel[1] & 15;
			if (szz == 16 && szx == 16)
				layerDone |= 1 << y;
			int   offset = curArea[2] + (curArea[3] << 4) + (y << 8);
			DATA8 cmap   = rgba + offset * 4;
			DATA8 color  = cmap;
			int   n;

			/* copy in x direction */
			for (n = 0; n < szx; memcpy(cmap + (n << 2), voxel, 4), n ++);
			/* copy in z direction */
			for (n = szz, cmap += 16*4; n > 1; memcpy(cmap, color, szx << 2), cmap += 4*16, n --);
			/* copy in y direction */
			for (cmap = color + 256*4; szy > 1; szy --, cmap += 256*4)
			{
				DATA8 cmap2;
				for (n = szz, cmap2 = cmap; n > 0; memcpy(cmap2, color, szx << 2), cmap2 += 4*16, n --);
				y ++;
				if (szz == 16 && szx == 16)
					layerDone |= 1 << y;
			}
			boxes ++;
		}
		else break;
	}

	cd->rgbaTex = rgba;
}

static int iteration;

static float faceNormals[] = { /* S, E, N, W, T, B */
	 0,  0,  0.5, 1,
	 0.5,  0,  0, 1,
	 0,  0, -0.5, 1,
	-0.5,  0,  0, 1,
	 0,  0.5,  0, 1,
	 0, -0.5,  0, 1,
};


static void voxelGetBoundsForFace(DATA8 texture, int face, vec4 V0, vec4 V1, vec4 posOffset)
{
	static uint8_t offsets[] = { /* S, E, N, W, T, B */
		0, 1, 2, 1,
		1, 2, 0, 1,
		0, 1, 2, 0,
		1, 2, 0, 0,
		0, 2, 1, 1,
		0, 2, 1, 0
	};

	DATA8 dir = offsets + face * 4;
	uint8_t x = dir[0];
	uint8_t y = dir[1];
	uint8_t z = dir[2];
	uint8_t t = z;

	float pt[6];

	if (texture[3] == 0x80)
	{
		/* void space inside a ChunkData */
		pt[0] = (CPOS(posOffset[VX]) << 4)  + (texture[0] & 15);
		pt[1] = ((int) posOffset[VY] & ~15) + (texture[1] & 15);
		pt[2] = (CPOS(posOffset[VZ]) << 4)  + (texture[0] >> 4);

		pt[3] = pt[0] + (texture[2] & 15) + 1;
		pt[4] = pt[1] + (texture[1] >> 4) + 1;
		pt[5] = pt[2] + (texture[2] >> 4) + 1;
	}
	else /* void space inside Chunk */
	{
		pt[0] = CPOS(posOffset[VX]) << 4;
		pt[2] = CPOS(posOffset[VZ]) << 4;
		pt[1] = texture[0] << 4;
		pt[4] = texture[1] << 4;
		pt[3] = pt[0] + 16;
		pt[5] = pt[2] + 16;
	}

	if (dir[3]) t += 3;
	V0[x] = pt[x];
	V0[y] = pt[y];
	V0[z] = pt[t];

	V1[x] = pt[x+3];
	V1[y] = pt[y+3];
	V1[z] = pt[t];
}

static DATA8 voxelFindClosest(Map map, vec4 pos)
{
	ChunkData cd;
	Chunk c = mapGetChunk(map, pos);
	int absY = CPOS(pos[1]);

	if (c)
	{
		static uint8_t tex[4];
		if ((unsigned) absY < c->maxy)
		{
			if ((cd = c->layer[absY]))
			{
				if (! cd->rgbaTex)
					chunkConvertToRGBA(cd);
				int index =
					((int) floorf(pos[0]) & 15) +
					((int) floorf(pos[2]) & 15) * 16 +
					((int) floorf(pos[1]) & 15) * 256;
				return cd->rgbaTex + 4 * index;
			}
			else /* missing ChunkData: assume empty then :-/ */
			{
				tex[0] = 0;
				tex[1] = 0xf0;
				tex[2] = 0xff;
				tex[3] = 0x80;
				return tex;
			}
		}
		else if ((unsigned) absY < map->maxHeight)
		{
			tex[0] = c->maxy;
			tex[1] = map->maxHeight;
			tex[2] = 0;
			tex[3] = 0x81;
			return tex;
		}
	}
	return NULL;
}

/* similar to mapPointToObject(), but only check voxel and consider them full */
static Bool mapPointToVoxel(Map map, vec4 camera, vec4 dir, DATA8 color)
{
	vec4 pos, u;
	memcpy(u, dir, sizeof u);
	vec4 plane = {floorf(camera[0]), floorf(camera[1]), floorf(camera[2]), 1};
	int  flags = (u[0] < 0 ? 8 : 2) | (u[1] < 0 ? 32 : 16) | (u[2] < 0 ? 4 : 1);
	int  side  = 4;

	memcpy(pos, camera, sizeof pos);

	DATA8 tex = voxelFindClosest(map, plane);

	next: for (;;)
	{
		if (tex)
		{
			vec norm;
			int i;
			if ((tex[3] & 0x80) == 0)
			{
				color[0] = tex[0] * raycast.shading[side] >> 8;
				color[1] = tex[1] * raycast.shading[side] >> 8;
				color[2] = tex[2] * raycast.shading[side] >> 8;
				return True;
			}

			/* merged blank space */
			for (i = 0, norm = faceNormals; i < 6; i ++, norm += 4)
			{
				vec4 inter, V0, V1;
				/* we can already eliminate some planes based on the ray direction */
				if ((flags & (1 << i)) == 0) continue;

				iteration ++;
				voxelGetBoundsForFace(tex, i, V0, V1, plane);

				if (intersectRayPlane(pos, u, V0, norm, inter) == 1)
				{
					/* need to check that intersection point remains within box */
					if (norm[0] == 0 && ! (V0[0] <= inter[0] && inter[0] <= V1[0])) continue;
					if (norm[1] == 0 && ! (V0[1] <= inter[1] && inter[1] <= V1[1])) continue;
					if (norm[2] == 0 && ! (V0[2] <= inter[2] && inter[2] <= V1[2])) continue;

					/* we have an intersection: move to block */
					memcpy(pos, inter, 12);
					memcpy(plane, inter, 12);
					if (norm[0] == 0)
					{
						if (inter[VX] == V0[VX] || inter[VX] == V1[VX])
							plane[VX] += u[VX];
					}
					else plane[VX] += norm[0];
					if (norm[1] == 0)
					{
						if (inter[VY] == V0[VY] || inter[VY] == V1[VY])
							plane[VY] += u[VY];
					}
					else plane[VY] += norm[1];
					if (norm[2] == 0)
					{
						if (inter[VZ] == V0[VZ] || inter[VZ] == V1[VZ])
							plane[VZ] += u[VZ];
					}
					else plane[VZ] += norm[2];

					tex = voxelFindClosest(map, plane);
					side = opp[i];
					goto next;
				}
			}
			if (i == 6)
				return False;
		}
		else return False;
	}
}

/* raycasting on CPU, mostly used for debugging */
void raycastWorld(Map map, mat4 invMVP, vec4 pos)
{
	static uint8_t skyColor[] = {0x72, 0xae, 0xf1};
	DATA8 bitmap, px;
	vec4  player;
	int   i, j;

	bitmap = malloc(SCR_WIDTH * SCR_HEIGHT * 3);
	player[VX] = pos[VX];
	player[VZ] = pos[VZ];
	player[VY] = pos[VY] + 1.6f;
	player[VT] = 1;
	iteration = 0;

	for (j = 0, px = bitmap; j < SCR_HEIGHT; j ++)
	{
		for (i = 0; i < SCR_WIDTH; i ++, px += 3)
		{
			vec4 clip = {i * 2. / SCR_WIDTH - 1, 1 - j * 2. / SCR_HEIGHT, 0, 1};
			vec4 dir;

			matMultByVec(dir, invMVP, clip);

			/* ray direction according to position on screen and player's view vector */
			dir[VX] = dir[VX] / dir[VT] - player[VX];
			dir[VY] = dir[VY] / dir[VT] - player[VY];
			dir[VZ] = dir[VZ] / dir[VT] - player[VZ];

			if (! mapPointToVoxel(map, player, dir, px))
			{
				/* no intersection with voxel terrain: use sky color then */
				px[0] = skyColor[0];
				px[1] = skyColor[1];
				px[2] = skyColor[2];
			}
		}
	}

	FILE * out = fopen("dump.ppm", "wb");

	if (out)
	{
		fprintf(out, "P6\n%d %d 255\n", SCR_WIDTH, SCR_HEIGHT);
		fwrite(bitmap, SCR_WIDTH * SCR_HEIGHT, 3, out);
		fclose(out);

		fprintf(stderr, "image dumped in dump.ppm, iteration avg: %.1f\n", iteration / (double) (SCR_WIDTH * SCR_HEIGHT));
	}
	free(bitmap);
}


APTR raycastConvertToCMap(DATA8 * data, int * width, int * height, int bpp)
{
	int   h, i, j, k, stride, res;
	int * sum;
	DATA8 s, d;

	h = *height * 32 / *width;
	res = *width / 32;
	raycast.paletteStride = 32 * 4;
	raycast.palette = d = malloc(raycast.paletteStride * h);
	sum = calloc(stride = *width * bpp, sizeof *sum);

	for (j = *height, s = *data, k = 0; j > 0; j --)
	{
		for (i = 0; i < stride; s ++, i ++)
			sum[i] += *s;

		k ++;
		if (k == res)
		{
			int rgba[4] = {0,0,0,0};
			for (k = i = 0; i < stride; i ++)
			{
				rgba[i&3] += sum[i];
				k ++;
				if ((k>>2) == res)
				{
					/* note: alpha will only use 7bits, the 8th is reserved for special voxels (air, water) */
					d[0] = rgba[0] / (res*res);
					d[1] = rgba[1] / (res*res);
					d[2] = rgba[2] / (res*res);
					d[3] = rgba[3] / (res*res) >> 1; d += 4;
					memset(rgba, 0, sizeof rgba);
					k = 0;
				}
			}
			k = 0;

			memset(sum, 0, stride * sizeof *sum);
		}
	}
	return NULL;
}

