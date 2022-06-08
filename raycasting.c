/*
 * raycasting.c: use raycasting and special chunk rendering to draw distant voxels.
 *
 * written by T.Pierron, may 2022.
 */

#define RAYCASTING_IMPL
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

	glGenTextures(1, &raycast.texMapId);
	glBindTexture(GL_TEXTURE_2D, raycast.texMapId);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

	return True;
}

/* map is being opened */
void raycastInitMap(Map map)
{
	int maxDist = map->maxDist;
	int distant = maxDist + globals.extraDist * 2;
	raycast.distantChunks = distant;
	raycast.rasterChunks  = maxDist;

	raycast.map   = map;
	raycast.Xmin  = map->center->X - (maxDist >> 1) * 16;
	raycast.Zmin  = map->center->Z - (maxDist >> 1) * 16;
	raycast.Xdist = map->center->X - (distant >> 1) * 16;
	raycast.Zdist = map->center->Z - (distant >> 1) * 16;

	int bitmap = (distant * distant + 7) >> 3;
	int size = (distant * distant * CHUNK_LIMIT + (distant * distant - maxDist * maxDist)) * sizeof *raycast.texMap;

	raycast.texMap      = malloc(size + bitmap);
	raycast.priorityMap = raycast.texMap + distant * distant * CHUNK_LIMIT;
	raycast.priorityMax = distant * distant - maxDist * maxDist;
	raycast.bitmapMap   = (DATA8) raycast.texMap + size;
	raycast.bitmapMax   = bitmap;

	memset(raycast.texMap, 0xff, size);

	/* texture for retrieving chunk location in main texture banks */
	glActiveTexture(GL_TEXTURE7);
	glBindTexture(GL_TEXTURE_2D, raycast.texMapId);
	glTexImage2D(GL_TEXTURE_2D, 0,  GL_LUMINANCE_ALPHA, distant, distant * CHUNK_LIMIT, 0, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, raycast.texMap);
	glActiveTexture(GL_TEXTURE0);

	memset(raycast.chunkVisible, 0, sizeof raycast.chunkVisible);

	/* now raycasting thread can start its work */
	fprintf(stderr, "start processing chunks from thread...\n");
	SemAdd(map->waitChanges, 1);
}

/* map being closed */
void raycastFreeAll(void)
{
	ChunkTexture tex, next;

	for (tex = next = HEAD(raycast.texBanks); tex; tex = next)
	{
		NEXT(next);
		glDeleteTextures(1, &tex->textureId);
		free(tex);
	}
	ListNew(&raycast.texBanks);

	if (raycast.texMapId)
	{
		glDeleteTextures(1, &raycast.texMapId);
		raycast.texMapId = 0;
	}
	free(raycast.texMap);
	raycast.texMap = NULL;
	raycast.map = NULL;
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

/* texture from a ChunkData has been processed: store it in the texture banks */
void raycastFlushChunk(DATA8 rgbaTex, int XZ, int Y, int maxy)
{
	static int total;
	ChunkTexture tex;
	int slot = 0;
	int addId;

	/* find a free slot */
	for (tex = HEAD(raycast.texBanks), addId = 0; tex; NEXT(tex), addId += TEXTURE_SLOTS)
	{
		if (tex->total < TEXTURE_SLOTS)
		{
			slot = mapFirstFree(tex->usage, DIM(tex->usage));
			goto found;
		}
	}

	/* alloc texture on the fly */
	tex = calloc(sizeof *tex, 1);
	ListAddTail(&raycast.texBanks, &tex->node);

	glGenTextures(1, &tex->textureId);

	/*
	 * banks will be associated to TEXTURE8 to TEXTURE24
	 * each texture can hold 1024 ChunkData. Worst case scenario = 65x65x16 render distance = 16384 ChunkData
	 */
//	glActiveTexture(GL_TEXTURE8 + addId);
	glBindTexture(GL_TEXTURE_2D, tex->textureId);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
//	glActiveTexture(GL_TEXTURE0);


	/* texture will be 4096x1024 "px": one ChunkData per scanline */
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 4096, TEXTURE_SLOTS, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	slot = 0;

	found:

	glBindTexture(GL_TEXTURE_2D, tex->textureId);

	/* each ChunkData will be stored in a single scanline of the texture */
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, slot, 4096, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgbaTex);
	tex->total ++;

	/* will stop raycasting early for rays that point toward the sky */
	if (raycast.chunkMaxHeight < maxy)
		raycast.chunkMaxHeight = maxy;

	/* texMap is what will link voxel space to texture banks */
	int stride = raycast.distantChunks * raycast.distantChunks;
	raycast.texMap[XZ + Y * stride] = slot + addId;

	/* uneven column: store distance from nearest valid ChunkData */
	for (slot = 0; maxy < CHUNK_LIMIT; slot ++, maxy ++)
	{
		raycast.texMap[XZ + maxy * stride] = 0xff00 + slot;
	}

	/* note: texMap texture will be updated after all staging chunks have been processed */

	fprintf(stderr, "loaded = %d / %d\n", ++ total, raycast.chunkMaxHeight * raycast.priorityMax);
}

void raycastUpdateTexMap(void)
{
	/* push the entire texture */
	glBindTexture(GL_TEXTURE_2D, raycast.texMapId);

	int distant = raycast.distantChunks;
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, distant, distant * CHUNK_LIMIT, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, raycast.texMap);
}


/*
 * try hard to load chunks that are visible first, it is somewhat a "frustum culling" for raycasting,
 * with some simplifications:
 * - we operate at the chunk level (not ChunkData).
 * - we don't have to check frustum visibility: the test has to be good enough, not perfect (because
 *   all chunks from distant region will eventually be loaded).
 */
void raycastRebuiltPriority(Map map)
{
	/* check at the edge of the map which chunks are visible */
	int dir  = raycast.rasterChunks >> 1;
	int mapX = map->mapX - dir;
	int mapZ = map->mapZ - dir;
	int row;

	dir = map->mapArea;
	if (mapX < 0) mapX += dir;
	if (mapZ < 0) mapZ += dir;
	if (mapX >= dir) mapX -= dir;
	if (mapZ >= dir) mapZ -= dir;

	DATA16 priority = alloca((raycast.rasterChunks - 1) * 8), store;
	Chunk  edges    = map->chunks + mapX + mapZ * dir;

	mapX = mapZ = 0;
	dir = (1 << SIDE_EAST) | (1 << (SIDE_SOUTH+4)) | (1 << (SIDE_WEST+8)) | (1 << (SIDE_NORTH+12));
	memset(priority, 0xff, 4);

	for (store = priority, row = map->maxDist - 1; dir > 0; )
	{
		if (edges->chunkFrame == map->frame)
		{
			/* chunk is visible */
			*store ++ = mapX | (mapZ << 8);
		}
		Chunk next = edges + map->chunkOffsets[edges->neighbor + (dir & 15)];
		mapX += (next->X - edges->X) >> 4;
		mapZ += (next->Z - edges->Z) >> 4;
		edges = next;
		row --;
		if (row == 0)
		{
			row = map->maxDist - 1;
			if (dir < 15) row --;
			dir >>= 4;
		}
	}

	dir = store[store == priority ? 0 : -1];
	/* highly likely it is the same as before */
	if (raycast.chunkVisible[0] != priority[0] ||
	    raycast.chunkVisible[1] != dir)
	{
		/* like xoff/zoff: but scan all 8 surrounding block instead of just 4 */
		static int8_t xoff8[] = {0,  1, -1, -1, 2,  0, -2,  0};
		static int8_t zoff8[] = {1, -1, -1,  1, 1, -2,  2, -2};
		static uint8_t corner[] = {1, 2, 4, 8, 3<<4, 6<<4, 9<<4, 12<<4};

		raycast.chunkVisible[0] = priority[0];
		raycast.chunkVisible[1] = dir;
		raycast.priorityIndex   = 0;

		/* list has changed: first add chunks that are near the visible raster chunks */
		if (store == priority)
		{
			/* no chunks visible at edge of map: use an arbitrary starting point */
			store[0] = 0;
			store ++;
		}
		memset(raycast.bitmapMap, 0, raycast.bitmapMax);

		DATA16 src, dst, eof;
		int    center = raycast.rasterChunks >> 1, i;
		for (dst = raycast.priorityMap, src = priority, row = raycast.distantChunks; src < store; src ++)
		{
			uint8_t flags;
			mapX = src[0] & 255;
			mapZ = src[0] >> 8;
			for (i = 0, flags = 0; i < DIM(xoff8); i ++)
			{
				mapX += xoff8[i];
				mapZ += zoff8[i];

				if (i >= 4 && flags != (corner[i] >> 4))
					continue;

				if (abs(mapX - center) > center || abs(mapZ - center) > center)
				{
					int coord = (mapX + globals.extraDist) + (mapZ + globals.extraDist) * row;
					/* each chunk must only be added once */
					if ((raycast.bitmapMap[coord>>3] & mask8bit[coord&7]) == 0)
					{
						raycast.bitmapMap[coord>>3] |= mask8bit[coord&7];
						*dst++ = (mapX + globals.extraDist) | ((mapZ + globals.extraDist) << 8);
					}
					flags |= corner[i] & 15;
				}
			}
		}

		/* look-up table to check if chunk coord is within distant area (too annoying to do analytically) */
		DATA8 valid = alloca(row);
		memset(valid, 1, row);
		memset(valid + globals.extraDist, 0, raycast.rasterChunks);

		/* load the rest of the chunks by proximity */
		for (src = raycast.priorityMap, eof = src + raycast.priorityMax; dst < eof; src ++)
		{
			mapX = src[0] & 255;
			mapZ = src[0] >> 8;
			for (i = 0; i < 4; i ++)
			{
				mapX += xoff[i];
				mapZ += zoff[i];
				if ((unsigned) mapX < row && (unsigned) mapZ < row && (valid[mapX] || valid[mapZ]))
				{
					int coord = mapX + mapZ * row;
					/* each chunk must only be added once */
					if ((raycast.bitmapMap[coord>>3] & mask8bit[coord&7]) == 0)
					{
						raycast.bitmapMap[coord>>3] |= mask8bit[coord&7];
						*dst++ = mapX | (mapZ << 8);
					}
				}
			}
		}
	}

	/* debug priorityMap */
	#if 0
	TEXT line[80];
	fprintf(stderr, "priority map (%d x %d)\n", raycast.distantChunks, raycast.distantChunks);
	for (mapZ = 0; mapZ < raycast.distantChunks; mapZ ++)
	{
		int i;
		memset(line, ' ', sizeof line);
		for (i = 0; i < raycast.distantChunks; line[i*4+3] = '|', i ++);
		for (i = 0; i < raycast.priorityMax; i ++)
		{
			int coord = raycast.priorityMap[i];
			if ((coord >> 8) == mapZ)
			{
				TEXT order[16];
				sprintf(order, "%3d|", i);
				memcpy(line + (coord & 255) * 4, order, 4);
			}
		}
		fwrite(line, raycast.distantChunks, 4, stderr);
		fputc('\n', stderr);
	}
	#endif
}

/* find the first unprocessed chunk */
Bool raycastNextChunk(int XZId[3])
{
	/* scan priority list, until we find something */
	DATA16 priority, eof;
	int    dist = raycast.distantChunks;
	int    frame = raycast.map->frame;

	/* mesh chunks have not started processing yet */
	if (frame == 0)
		frame = 1;

	if (raycast.priorityFrame != frame)
	{
		fprintf(stderr, "rebuilt priority list: %d\n", frame);
		raycastRebuiltPriority(raycast.map);
		raycast.priorityFrame = frame;
	}

	for (priority = raycast.priorityMap + raycast.priorityIndex, eof = raycast.priorityMap + raycast.priorityMax; priority < eof; priority ++)
	{
		int X = priority[0] & 0xff;
		int Z = priority[0] >> 8;

		/* check if already processed */
		if (raycast.texMap[X + Z * dist] == 0xffff)
		{
			XZId[0] = X * 16 + raycast.Xdist;
			XZId[1] = Z * 16 + raycast.Zdist;
			XZId[2] = X + Z * raycast.distantChunks;
			raycast.priorityIndex = priority - raycast.priorityMap + 1;
			return True;
		}
	}
	return False;
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

void chunkConvertToRGBA(ChunkData cd, DATA8 rgba)
{
	uint8_t layerArea[16*4];
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
}

/*
 * the following functions mimic the raycasting done on the GPU, they are used mostly for debug, to
 * have a reference implementation to fallback to, in case of problem.
 */
#ifdef DEBUG
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

	switch (texture[3]) {
	case 0x80:
		/* void space inside a ChunkData */
		pt[0] = (CPOS(posOffset[VX]) << 4)  + (texture[0] & 15);
		pt[1] = ((int) posOffset[VY] & ~15) + (texture[1] & 15);
		pt[2] = (CPOS(posOffset[VZ]) << 4)  + (texture[0] >> 4);

		pt[3] = pt[0] + (texture[2] & 15) + 1;
		pt[4] = pt[1] + (texture[1] >> 4) + 1;
		pt[5] = pt[2] + (texture[2] >> 4) + 1;
		break;

	case 0x81: /* void space inside Chunk */
		pt[0] = CPOS(posOffset[VX]) << 4;
		pt[2] = CPOS(posOffset[VZ]) << 4;
		pt[1] = texture[0] << 4;
		pt[4] = texture[1] << 4;
		pt[3] = pt[0] + 16;
		pt[5] = pt[2] + 16;
		break;

	case 0x82: /* raster chunks area */
		pt[0] = raycast.Xmin;   pt[3] = pt[0] + raycast.rasterChunks * 16;
		pt[1] = 0;              pt[4] = 256;
		pt[2] = raycast.Zmin;   pt[5] = pt[2] + raycast.rasterChunks * 16;
		break;

	default: return;
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
	int X = (int) (pos[VX] - raycast.Xdist) >> 4;
	int Z = (int) (pos[VZ] - raycast.Zdist) >> 4;
	int Y = CPOS(pos[1]);

	if (X < 0 || Z < 0 || X >= raycast.distantChunks || Z >= raycast.distantChunks || Y >= raycast.chunkMaxHeight || Y < 0)
		return NULL;

	uint16_t texId = raycast.texMap[X + (Z + Y * raycast.distantChunks) * raycast.distantChunks];

	static uint8_t tex[4];

	if (texId == 0xffff)
	{
		/* missing ChunkData: assume empty then */
		tex[0] = 0;
		tex[1] = 0xf0;
		tex[2] = 0xff;
		tex[3] = 0x80;
		return tex;
	}

	if (texId >= 0xff00)
	{
		/* empty space above chunk */
		tex[0] = Y - (texId - 0xff00);
		tex[1] = map->maxHeight;
		tex[2] = 0;
		tex[3] = 0x81;
		return tex;
	}

	ChunkTexture bank;
	for (bank = HEAD(raycast.texBanks); bank && texId >= TEXTURE_SLOTS; NEXT(bank), texId -= TEXTURE_SLOTS);

	if (! bank->data)
	{
		/* need to retrieve the entire tex data :-/ */
		bank->data = malloc(4096 * 1024 * 4); // 16 mb
		if (bank->data == NULL)
			return NULL;
		glBindTexture(GL_TEXTURE_2D, bank->textureId);
		glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, bank->data);
	}

	int index =
		((int) floorf(pos[0]) & 15) +
		((int) floorf(pos[2]) & 15) * 16 +
		((int) floorf(pos[1]) & 15) * 256;

	return bank->data + texId * (16*1024) + index * 4;
}

/* similar to mapPointToObject(), but only check voxel and consider them full */
static Bool mapPointToVoxel(Map map, vec4 camera, vec4 dir, DATA8 color)
{
	vec4 pos, u, V0, V1;
	memcpy(u, dir, sizeof u);
	vec4 plane = {floorf(camera[0]), floorf(camera[1]), floorf(camera[2]), 1};
	int  flags = (u[0] < 0 ? 8 : 2) | (u[1] < 0 ? 32 : 16) | (u[2] < 0 ? 4 : 1);
	int  side  = 4;

	memcpy(pos, camera, sizeof pos);

	DATA8 tex = "\0\0\0\x82";

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
				vec4 inter;
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

	ChunkTexture bank;
	for (bank = HEAD(raycast.texBanks); bank; NEXT(bank))
	{
		free(bank->data);
		bank->data = NULL;
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
#endif

/* textureLoad() callback to convert terrain.png into a color map */
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

