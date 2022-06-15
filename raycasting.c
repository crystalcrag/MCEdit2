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

/* init opengl objects to do raycasting on GPU */
Bool raycastInitStatic(void)
{
	raycast.shader = createGLSLProgram("raycaster.vsh", "raycaster.fsh", NULL);

	if (! raycast.shader)
		return False;

	/* coordinates must be normalized between -1 and 1 for XY and [0 - 1] for Z */
	glGenBuffers(1, &raycast.vbo);
	glGenVertexArrays(1, &raycast.vao);
	glBindVertexArray(raycast.vao);
	glBindBuffer(GL_ARRAY_BUFFER, raycast.vbo);
	glBufferData(GL_ARRAY_BUFFER, 6 * 12 * 8, NULL, GL_STATIC_DRAW);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0);
	glEnableVertexAttribArray(0);

	glBindVertexArray(0);

	glGenTextures(1, &raycast.texMapId);
	glBindTexture(GL_TEXTURE_2D, raycast.texMapId);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

	raycast.uniformINVMVP = glGetUniformLocation(raycast.shader, "INVMVP");
	raycast.uniformChunk  = glGetUniformLocation(raycast.shader, "chunk");
	raycast.uniformSize   = glGetUniformLocation(raycast.shader, "size");

	return True;
}

static vec raycastGenQuad(vec buffer, int side, vec cube)
{
	uint8_t indices[6];
	DATA8   index = cubeIndices + side * 4;

	if (side < SIDE_TOP)
	{
		/* order them backward */
		indices[0] = index[3];
		indices[1] = index[2];
		indices[2] = index[1];
		indices[3] = index[0];
		indices[4] = index[3];
		indices[5] = index[1];
	}
	else
	{
		memcpy(indices, index, 4);
		indices[4] = index[0];
		indices[5] = index[2];
	}

	int i;
	for (i = 0; i < 6; i ++, buffer += 3)
	{
		index = cubeVertex + indices[i];
		buffer[VX] = index[VX] ? cube[VX+3] : cube[VX];
		buffer[VY] = index[VY] ? cube[VY+3] : cube[VY];
		buffer[VZ] = index[VZ] ? cube[VZ+3] : cube[VZ];
	}
	return buffer;
}


/* generate the geometry that will be the bounding of distant chunks */
static void raycastGenVertex(void)
{
	glBindBuffer(GL_ARRAY_BUFFER, raycast.vbo);
	vec buffer = glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);

	float cubeRaster[6] = {
		raycast.chunkLoc[2], 0,
		raycast.chunkLoc[3],
		raycast.chunkLoc[2] + raycast.rasterChunks * 16,
		raycast.chunkMaxHeight * 16,
		raycast.chunkLoc[3] + raycast.rasterChunks * 16,
	};

	/* vertical wall */
	buffer = raycastGenQuad(buffer, SIDE_SOUTH, cubeRaster);
	buffer = raycastGenQuad(buffer, SIDE_EAST,  cubeRaster);
	buffer = raycastGenQuad(buffer, SIDE_NORTH, cubeRaster);
	buffer = raycastGenQuad(buffer, SIDE_WEST,  cubeRaster);

	/* top cover */
	#define Ymax     cubeRaster[VY+3]
	#define Zmax     cubeRaster[VZ+3]
	float Xdist = raycast.chunkLoc[0] + raycast.distantChunks * 16;
	float Zdist = raycast.chunkLoc[1] + raycast.distantChunks * 16;
	buffer = raycastGenQuad(buffer, SIDE_TOP, (float [6]) {raycast.chunkLoc[0], 0, raycast.chunkLoc[1], Xdist, Ymax, raycast.chunkLoc[3]});
	buffer = raycastGenQuad(buffer, SIDE_TOP, (float [6]) {cubeRaster[VX+3], 0, raycast.chunkLoc[3], Xdist, Ymax, Zmax});
	buffer = raycastGenQuad(buffer, SIDE_TOP, (float [6]) {raycast.chunkLoc[0], 0, Zmax, Xdist, Ymax, Zdist});
	buffer = raycastGenQuad(buffer, SIDE_TOP, (float [6]) {raycast.chunkLoc[0], 0, raycast.chunkLoc[3], raycast.chunkLoc[2], Ymax, Zmax});
	#undef Ymax
	#undef Zmax
	raycast.vboCount = 6 * 8;

	glUnmapBuffer(GL_ARRAY_BUFFER);
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

	/* intel cards DO NOT LIKE NPOT textures: only 2 days lost debugging that crap :-/ */
	int width = distant;
	width --;
	width |= width >> 1;
	width |= width >> 2;
	width |= width >> 4;
	width |= width >> 8;
	width |= width >> 16;
	width ++;

	int bitmap = (distant * distant + 7) >> 3;
	int size = (width * distant * CHUNK_LIMIT + (distant * distant - maxDist * maxDist)) * sizeof *raycast.texMap;

	raycast.texMapWidth = width;
	raycast.texMap      = malloc(size + bitmap);
	raycast.priorityMap = raycast.texMap + width * distant * CHUNK_LIMIT;
	raycast.priorityMax = distant * distant - maxDist * maxDist;
	raycast.bitmapMap   = (DATA8) raycast.texMap + size;
	raycast.bitmapMax   = bitmap;

	raycast.chunkLoc[0] = raycast.Xdist;
	raycast.chunkLoc[1] = raycast.Zdist;
	raycast.chunkLoc[2] = raycast.Xmin;
	raycast.chunkLoc[3] = raycast.Zmin;

	raycast.chunkSize[0] = raycast.distantChunks;
	raycast.chunkSize[2] = raycast.rasterChunks;

	memset(raycast.texMap, 0xff, size);


	/* texture for retrieving chunk location in main texture banks */
	glActiveTexture(GL_TEXTURE7);
	glBindTexture(GL_TEXTURE_2D, raycast.texMapId);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RG, width, distant * CHUNK_LIMIT, 0, GL_RG, GL_UNSIGNED_BYTE, raycast.texMap);
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
	if (raycast.vboCount > 0)
	{
		glDepthMask(GL_FALSE);
		glUseProgram(raycast.shader);
		glProgramUniformMatrix4fv(raycast.shader, raycast.uniformINVMVP, 1, GL_FALSE, globals.matInvMVP);
		glProgramUniform4fv(raycast.shader, raycast.uniformChunk, 1, raycast.chunkLoc);
		glProgramUniform4fv(raycast.shader, raycast.uniformSize,  1, raycast.chunkSize);
		glBindVertexArray(raycast.vao);
//		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
		glDrawArrays(GL_TRIANGLES, 0, raycast.vboCount);
		glBindVertexArray(0);
		glDepthMask(GL_TRUE);
//		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	}
}

/* texture from a ChunkData has been processed: store it in the texture banks */
void raycastFlushChunk(DATA8 rgbaTex, int XZ, int Y, int maxy)
{
	ChunkTexture tex;
	int slot = 0;
	int addId;

	/* find a free slot */
	for (tex = HEAD(raycast.texBanks), addId = 0; tex; NEXT(tex), addId ++)
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
	fprintf(stderr, "creating texture id %d\n", addId);
	glActiveTexture(GL_TEXTURE8 + addId);
	glBindTexture(GL_TEXTURE_2D, tex->textureId);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

	/* texture will be 4096x1024 "px": one ChunkData per scanline */
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 4096, TEXTURE_SLOTS, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

	glActiveTexture(GL_TEXTURE0);
	slot = 0;

	found:

	glActiveTexture(GL_TEXTURE8 + addId);
	glBindTexture(GL_TEXTURE_2D, tex->textureId);

	/* each ChunkData will be stored in a single scanline of the texture */
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, slot, 4096, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgbaTex);
	glActiveTexture(GL_TEXTURE0);
	tex->total ++;

	/* will stop raycasting early for rays that point toward the sky */
	if (raycast.chunkMaxHeight < maxy)
	{
		raycast.chunkMaxHeight = raycast.chunkSize[1] = maxy;
		raycastGenVertex();
	}

	/* texMap is what will link voxel space to texture banks */
	int stride = raycast.texMapWidth * raycast.distantChunks;

	/* shader will read the texture as individual unsigned bytes */
	DATA8 texmap = (DATA8) (&raycast.texMap[XZ + Y * stride]);

	slot += (addId << 10);
	texmap[0] = slot >> 8;
	texmap[1] = slot & 0xff;

	if (raycast.maxSlot < slot)
		raycast.maxSlot = slot;

	texmap = (DATA8) (&raycast.texMap[XZ + maxy * stride]);

	/* uneven column: store distance from nearest valid ChunkData */
	for (slot = 0; maxy < CHUNK_LIMIT; slot ++, maxy ++, texmap += stride * 2)
	{
		texmap[0] = 0xff;
		texmap[1] = slot;
	}

	/* note: texMap texture will be updated after all staging chunks have been processed */

	//static int total;
	//fprintf(stderr, "loaded = %d / %d\n", ++ total, raycast.chunkMaxHeight * raycast.priorityMax);
}

void raycastUpdateTexMap(void)
{
	/* push the entire texture */
	glBindTexture(GL_TEXTURE_2D, raycast.texMapId);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, raycast.texMapWidth, raycast.distantChunks * CHUNK_LIMIT, GL_RG, GL_UNSIGNED_BYTE, raycast.texMap);
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
		static int8_t xoff8dir[] = {0,  1, -1, -1, 2,  0, -2,  0};
		static int8_t zoff8dir[] = {1, -1, -1,  1, 1, -2,  2, -2};
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
			for (i = 0, flags = 0; i < DIM(xoff8dir); i ++)
			{
				mapX += xoff8dir[i];
				mapZ += zoff8dir[i];

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
	int    dist = raycast.texMapWidth;
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
			XZId[2] = X + Z * dist;
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
static int     iteration;
static uint8_t color[4];
static vec4    camera;
static float   chunk[4];
static float   size[4];

static float normals[] = { /* S, E, N, W, T, B */
	 0,  0,  1, 1,
	 1,  0,  0, 1,
	 0,  0, -1, 1,
	-1,  0,  0, 1,
	 0,  1,  0, 1,
	 0, -1,  0, 1,
};

void voxelGetBoundsForFace(DATA8 tex, int face, vec4 V0, vec4 V1, vec4 posOffset)
{
	vec4 pt1, pt2;

	#define vec3(dst, x,y,z)   dst[0]=x,dst[1]=y,dst[2]=z
	switch (tex[3]) {
	case 0x80: // void space inside a ChunkData
		// encoding is done in raycast.c:chunkConvertToRGBA()
		vec3(pt1,
			floor(posOffset[VX] * 0.0625f) * 16 + (tex[0] & 15),
			floor(posOffset[VY] * 0.0625f) * 16 + (tex[1] & 15),
			floor(posOffset[VZ] * 0.0625f) * 16 + (tex[0] >> 4)
		);

		vec3(pt2,
			pt1[0] + (tex[2] & 15) + 1,
			pt1[1] + (tex[1] >> 4) + 1,
			pt1[2] + (tex[2] >> 4) + 1
		);
		break;

	case 0x81: // void space inside Chunk
		vec3(pt1,
			floor(posOffset[VX] * 0.0625f) * 16,
			tex[0] << 4,
			floor(posOffset[VZ] * 0.0625f) * 16
		);
		vec3(pt2, pt1[0] + 16, tex[1] << 4, pt1[2] + 16);
		break;

	case 0x82: // raster chunks area
		vec3(pt1, chunk[VZ], 0, chunk[VT]);
		vec3(pt2, pt1[0] + size[VZ] * 16, 256, pt1[2] + size[VZ] * 16);
		break;

	case 0x83: // area above distant chunks
		vec3(pt1, chunk[VX], 0, chunk[VY]);
		vec3(pt2, pt1[VX] + size[VX] * 16, pt1[VY] + size[VY] * 16, pt1[VZ] + size[VX] * 16);
		break;

	default: return;
	}

	switch (face) {
	case 0: // south
		vec3(V0, pt1[VX], pt1[VY], pt2[VZ]);
		memcpy(V1, pt2, 12);
		break;
	case 1: // east
		vec3(V0, pt2[VX], pt1[VY], pt1[VZ]);
		memcpy(V1, pt2, 12);
		break;
	case 2: // north
		memcpy(V0, pt1, 12);
		vec3(V1, pt2[VX], pt2[VY], pt1[VZ]);
		break;
	case 3: // west
		memcpy(V0, pt1, 12);
		vec3(V1, pt1[VX], pt2[VY], pt2[VZ]);
		break;
	case 4: // top
		vec3(V0, pt1[VX], pt2[VY], pt1[VZ]);
		memcpy(V1, pt2, 12);
		break;
	case 5: // bottom
		memcpy(V0, pt1, 12);
		vec3(V1, pt2[VX], pt1[VY], pt2[VZ]);
	}
	#undef vec4
}

static void texelFetch(vec4 ret, int texId, int X, int Y, int lod)
{
	DATA8 src;
	if (texId == 0)
	{
		src = (DATA8) (&raycast.texMap[X + Y * (int) size[VX]]);
		ret[0] = src[0] / 255.0f;
		ret[1] = src[1] / 255.0f;
		ret[2] = ret[3] = 1.0f;
	}
	else
	{
		ChunkTexture bank;
		for (bank = HEAD(raycast.texBanks); bank && texId > 1; NEXT(bank), texId --);

		if (! bank->data)
		{
			/* need to retrieve the entire tex data :-/ */
			bank->data = malloc(4096 * 1024 * 4); // 16 mb
			if (bank->data == NULL)
				return;
			glBindTexture(GL_TEXTURE_2D, bank->textureId);
			glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, bank->data);
		}
		src = bank->data + X * 4 + Y * 16*1024;
		ret[0] = src[0] / 255.0f;
		ret[1] = src[1] / 255.0f;
		ret[2] = src[2] / 255.0f;
		ret[3] = src[3] / 255.0f;
	}
}

// extract voxel color at position <pos>
Bool voxelFindClosest(vec4 pos, DATA8 tex, float upward)
{
	int X = (int) (pos[VX] - chunk[VX]) >> 4;
	int Z = (int) (pos[VZ] - chunk[VY]) >> 4;
	int Y = (int) pos[VY] >> 4;

	if (Y >= size[VY])
	{
		// above raycasted chunks and going upward: no way we can reach a distant chunk
		if (upward >= 0)
			return False;

		// maybe we can
		memcpy(tex, "\0\0\0\x83", 4);
	}
	else if (X < 0 || Z < 0 || X >= size[VX] || Z >= size[VX] || Y < 0)
	{
		return False;
	}

	vec4 texel;
	texelFetch(texel, 0, X, Z + Y * size[VX], 0);
	int texId = roundf(texel[0] * 65280.0f + texel[1] * 255.0f);

	if (texId < 0xff00 && texId > raycast.maxSlot)
		puts("here");

	if (texId == 0xffff)
	{
		/* missing ChunkData: assume empty then */
		memcpy(tex, "\0\xf0\ff\x80", 4);
		return True;
	}

	if (texId >= 0xff00)
	{
		/* empty space above chunk */
		tex[0] = Y - (texId - 0xff00);
		tex[1] = size[VY];
		tex[2] = 0;
		tex[3] = 0x81;
		return True;
	}

	int coord[2] = {
		((int) floor(pos[VX]) & 15) +
		((int) floor(pos[VZ]) & 15) * 16 +
		((int) floor(pos[VY]) & 15) * 256, texId & 1023
	};

	// should consider using bindless texture one day (not supported on intel though :-/).
	vec4 voxel;
	switch (texId >> 10) {
	// 4096 ChunkData is not much actually  :-/ should increase texture size...
	case 0: texelFetch(voxel, 1, coord[0], coord[1], 0); break;
	case 1: texelFetch(voxel, 2, coord[0], coord[1], 0); break;
	case 2: texelFetch(voxel, 3, coord[0], coord[1], 0); break;
	case 3: texelFetch(voxel, 4, coord[0], coord[1], 0); break;
	default: return False;
	}

	tex[0] = voxel[0] * 255;
	tex[1] = voxel[1] * 255;
	tex[2] = voxel[2] * 255;
	tex[3] = voxel[3] * 255;

	return True;
}


Bool mapPointToVoxel(vec4 dir)
{
	vec4 pos, V0, V1;
	vec4 plane = {floor(camera[VX]), floor(camera[VY]), floor(camera[VZ]), 1};
	int  flags = (dir[VX] < 0 ? 8 : 2) | (dir[VY] < 0 ? 32 : 16) | (dir[VZ] < 0 ? 4 : 1);
	int  side  = 4;

	memcpy(pos, camera, 16);

	uint8_t tex[4] = {0, 0, 0, 0x82};

	for (;;)
	{
		if (tex[3] < 0x80)
		{
			color[0] = tex[0] * raycast.shading[side] >> 8;
			color[1] = tex[1] * raycast.shading[side] >> 8;
			color[2] = tex[2] * raycast.shading[side] >> 8;
			return True;
		}

		// empty space in voxel: skip this part as quickly as possible
		int i;
		for (i = 0; i < 6; i ++)
		{
			vec4 inter;
			vec  norm;

			if ((flags & (1 << i)) == 0) continue;

			iteration ++;
			voxelGetBoundsForFace(tex, i, V0, V1, plane);
			norm = normals + i * 4;

			if (intersectRayPlane(pos, dir, V0, norm, inter))
			{
				// need to check that intersection point remains within box
				if (norm[VX] == 0 && ! (V0[VX] <= inter[VX] && inter[VX] <= V1[VX])) continue;
				if (norm[VY] == 0 && ! (V0[VY] <= inter[VY] && inter[VY] <= V1[VY])) continue;
				if (norm[VZ] == 0 && ! (V0[VZ] <= inter[VZ] && inter[VZ] <= V1[VZ])) continue;

				memcpy(plane, inter, 12);
				memcpy(pos, inter, 12);

				if (norm[VX] == 0)
				{
					if (inter[VX] == V0[VX] || inter[VX] == V1[VX])
						plane[VX] += dir[VX];
				}
				else plane[VX] += norm[VX] * 0.5f;
				if (norm[VY] == 0)
				{
					if (inter[VY] == V0[VY] || inter[VY] == V1[VY])
						plane[VY] += dir[VY];
				}
				else plane[VY] += norm[VY] * 0.5f;
				if (norm[VZ] == 0)
				{
					if (inter[VZ] == V0[VZ] || inter[VZ] == V1[VZ])
						plane[VZ] += dir[VZ];
				}
				else plane[VZ] += norm[VZ] * 0.5f;

				if (! voxelFindClosest(plane, tex, dir[VY]))
					return False;
				side = opp[i];
				break;
			}
		}
		if (i == 6)
			return False;
	}
}

#define SCR_WIDTH         400
#define SCR_HEIGHT        400

/* raycasting on CPU, mostly used for debugging */
void raycastWorld(Map map, mat4 invMVP, vec4 pos)
{
	static uint8_t skyColor[] = {0x72, 0xae, 0xf1};
	DATA8 bitmap, px;
	int   i, j;

	bitmap = malloc(SCR_WIDTH * SCR_HEIGHT * 3);
	camera[VX] = pos[VX];
	camera[VZ] = pos[VZ];
	camera[VY] = pos[VY] + 1.6f;
	camera[VT] = 1;
	iteration = 0;

	memcpy(chunk, raycast.chunkLoc, sizeof chunk);
	memcpy(size,  raycast.chunkSize, sizeof size);

	for (j = 0, px = bitmap; j < SCR_HEIGHT; j ++)
	{
		for (i = 0; i < SCR_WIDTH; i ++, px += 3)
		{
			vec4 clip = {i * 2. / SCR_WIDTH - 1, 1 - j * 2. / SCR_HEIGHT, 0, 1};
			vec4 dir;

			matMultByVec(dir, invMVP, clip);

			/* ray direction according to position on screen and player's view vector */
			dir[VX] = dir[VX] / dir[VT] - camera[VX];
			dir[VY] = dir[VY] / dir[VT] - camera[VY];
			dir[VZ] = dir[VZ] / dir[VT] - camera[VZ];

			if (mapPointToVoxel(dir))
			{
				px[0] = color[0];
				px[1] = color[1];
				px[2] = color[2];
			}
			else
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

