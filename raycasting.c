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

	raycast.uniformRaster  = glGetUniformLocation(raycast.shader, "rasterArea");
	raycast.uniformDistant = glGetUniformLocation(raycast.shader, "distantArea");

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


/* generate the geometry that will be the bounding box of distant chunks */
static void raycastGenVertex(void)
{
	glBindBuffer(GL_ARRAY_BUFFER, raycast.vbo);
	vec buffer = glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);

	float cubeRaster[6] = {
		raycast.rasterArea[0], 0,
		raycast.rasterArea[1],
		raycast.rasterArea[2],
		raycast.chunkMaxHeight * 16,
		raycast.rasterArea[3],
	};

	/* vertical wall */
	buffer = raycastGenQuad(buffer, SIDE_SOUTH, cubeRaster);
	buffer = raycastGenQuad(buffer, SIDE_EAST,  cubeRaster);
	buffer = raycastGenQuad(buffer, SIDE_NORTH, cubeRaster);
	buffer = raycastGenQuad(buffer, SIDE_WEST,  cubeRaster);

	/* top cover */
	#define Ymax     cubeRaster[VY+3]
	#define Zmax     cubeRaster[VZ+3]
	float Xdist = raycast.distantArea[0] + raycast.distantArea[2];
	float Zdist = raycast.distantArea[1] + raycast.distantArea[2];
	buffer = raycastGenQuad(buffer, SIDE_TOP, (float [6]) {raycast.distantArea[0], 0, raycast.distantArea[1], Xdist, Ymax, cubeRaster[VZ]});
	buffer = raycastGenQuad(buffer, SIDE_TOP, (float [6]) {cubeRaster[VX+3], 0, cubeRaster[VZ], Xdist, Ymax, Zmax});
	buffer = raycastGenQuad(buffer, SIDE_TOP, (float [6]) {raycast.distantArea[0], 0, Zmax, Xdist, Ymax, Zdist});
	buffer = raycastGenQuad(buffer, SIDE_TOP, (float [6]) {raycast.distantArea[0], 0, cubeRaster[VZ], cubeRaster[VX], Ymax, Zmax});
	#undef Ymax
	#undef Zmax
	raycast.vboCount = 6 * 8;

	glUnmapBuffer(GL_ARRAY_BUFFER);
}


/* uniform for shaders */
static void raycastInitArea(Map map, Bool useDest)
{
	vec area = useDest ? raycast.rasterDest : raycast.rasterArea;

	area[0] = map->center->X - (raycast.rasterChunks >> 1) * 16;
	area[1] = map->center->Z - (raycast.rasterChunks >> 1) * 16;
	area[2] = area[0] + raycast.rasterChunks * 16;
	area[3] = area[1] + raycast.rasterChunks * 16;

	if (! useDest)
		memcpy(raycast.rasterDest, raycast.rasterArea, sizeof raycast.rasterDest);

	raycast.distantArea[0] = map->center->X - (raycast.distantChunks >> 1) * 16;
	raycast.distantArea[1] = map->center->Z - (raycast.distantChunks >> 1) * 16;
	raycast.distantArea[2] = raycast.distantChunks * 16;
}

/* map is being opened */
void raycastInitMap(Map map)
{
	int maxDist = map->maxDist;
	int distant = maxDist + globals.extraDist * 2;
	raycast.distantChunks = distant;
	raycast.rasterChunks  = maxDist;

	int bitmap = (distant * distant + 7) >> 3;
	int size = (distant * distant * CHUNK_LIMIT + (distant * distant - maxDist * maxDist)) * sizeof *raycast.texMap;

	raycast.texMap      = malloc(size + bitmap);
	raycast.priorityMap = raycast.texMap + distant * distant * CHUNK_LIMIT;
	raycast.priorityMax = distant * distant - maxDist * maxDist;
	raycast.bitmapMap   = (DATA8) raycast.texMap + size;
	raycast.bitmapMax   = bitmap;
	raycast.map         = map;

	raycastInitArea(map, False);
	memset(raycast.texMap, 0xff, size);


	/* texture for retrieving chunk location in main texture banks */
	glActiveTexture(GL_TEXTURE7);
	glBindTexture(GL_TEXTURE_2D, raycast.texMapId);
	/* texMap is an arbitrary sized texture, without any padding on the row */
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	/* would be easier if this tex was declared as GL_UNSIGNED_SHORT, but I can't manage to make it work :-/ */
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RG, distant, distant * CHUNK_LIMIT, 0, GL_RG, GL_UNSIGNED_BYTE, raycast.texMap);
	glActiveTexture(GL_TEXTURE0);

	memset(raycast.chunkVisible, 0, sizeof raycast.chunkVisible);

	/* now raycasting thread can start its work */
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
		/* glDepthRangef() will make sure that raycasting chunks are painting behind raster chunks no matter what */
		glDepthRangef(1, 1);
		glUseProgram(raycast.shader);
		glProgramUniform4fv(raycast.shader, raycast.uniformRaster,  1, raycast.rasterArea);
		glProgramUniform4fv(raycast.shader, raycast.uniformDistant, 1, raycast.distantArea);
		glBindVertexArray(raycast.vao);
		glDrawArrays(GL_TRIANGLES, 0, raycast.vboCount);
		glBindVertexArray(0);
		glDepthMask(GL_TRUE);
		glDepthRangef(0, 1);
	}
}

/* texture from a ChunkData has been processed: store it the texture banks */
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

	/* no free slot: alloc texture on the fly */
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
	tex->usage[0] = 1;
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
		raycast.distantArea[3] = raycast.chunkMaxHeight = maxy;
		raycastGenVertex();
	}

	/* texMap is what will link voxel space to texture banks */
	int stride = raycast.distantChunks * raycast.distantChunks;

	/* shader will read the texture as individual unsigned bytes */
	DATA8 texmap = (DATA8) (&raycast.texMap[XZ + Y * stride]);

	if (texmap[0] < 0xff)
		puts("not good");

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
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, raycast.distantChunks, raycast.distantChunks * CHUNK_LIMIT, GL_RG, GL_UNSIGNED_BYTE, raycast.texMap);

	#ifdef DEBUG
	ChunkTexture bank;
	int total;
	for (bank = HEAD(raycast.texBanks), total = 0; bank; total += bank->total, NEXT(bank));
	fprintf(stderr, "maxSlot = %d, total = %d\n", raycast.maxSlot, total);
	#endif
}

#ifdef DEBUG
/* print bottom layer of raycast.texMap */
void printtex(void)
{
	DATA8 tex;
	int   X, Y, Z, stride, total;

	stride = raycast.distantChunks;
	for (tex = (DATA8) raycast.texMap, Z = 0; Z < stride; Z ++)
	{
		for (X = 0; X < stride; X ++, tex += 2)
		{
			int sep = X+1 == globals.extraDist || X+1 == globals.extraDist + raycast.rasterChunks ? '\xba' : '|';
			int count;
			DATA8 texgl;
			for (Y = 0, texgl = tex, count = 0; Y < CHUNK_LIMIT; Y ++, texgl += stride*stride*2)
				if (texgl[0] < 0xff) count ++;
			if (count == 0)
				fprintf(stderr, "    %c", sep);
			else
				fprintf(stderr, " %2d %c", count, sep);
			total += count;
		}
		fputc('\n', stderr);
		for (X = 0; X < stride; X ++)
		{
			if (Z+1 == globals.extraDist || Z+1 == globals.extraDist + raycast.rasterChunks)
				fprintf(stderr, "====+");
			else
				fprintf(stderr, "----+");
		}
		fputc('\n', stderr);
	}
	fprintf(stderr, "tex area %dx%d: %d rc chunks\n", stride, stride, total);
}

/* dump raycast.texMap into a NetPBM image, map to R, G channel */
void texmap(void)
{
	FILE * out = fopen("texmap.ppm", "wb");

	if (out)
	{
		DATA8 src;
		int i = raycast.distantChunks;
		fprintf(out, "P6\n%d %d 255\n", i, i * CHUNK_LIMIT);
		for (src = (DATA8) raycast.texMap, i = i*i*CHUNK_LIMIT; i > 0; i --, src += 2)
		{
			uint8_t rgb[] = {src[0], src[1], 0};
			fwrite(rgb, 1, 3, out);
		}
		fclose(out);

		fprintf(stderr, "texmap dump in texmap.ppm\n");
	}
}
#endif


#define TEXID(tex)   ((tex[0] << 8) | (tex)[1])

static void raycastFreeTex(DATA8 tex)
{
	ChunkTexture bank;
	int texId = TEXID(tex);
	for (bank = HEAD(raycast.texBanks); bank && texId >= TEXTURE_SLOTS; texId -= TEXTURE_SLOTS, NEXT(bank));
	bank->usage[texId >> 5] ^= 1 << (texId & 31);
	bank->total --;
	((DATA16)tex)[0] = 0xffff;
}

/* main thread want to cancel raycasting chunk generation: delete partial chunk column */
void raycastCancelColumn(int XZ)
{
	DATA8 texgl = (DATA8) (raycast.texMap + XZ);
	if (texgl[0] == 0xff)
	{
		/* incomplete column */
		((DATA16)texgl)[0] = 0xffff;
		int stride = raycast.distantChunks * raycast.distantChunks * 2, i;
		for (i = 1, texgl += stride; i < CHUNK_LIMIT; i ++, texgl += stride)
		{
			if (texgl[0] < 0xff)
				raycastFreeTex(texgl);
		}
	}
}

static Bool raycastHasRaster(Map map, int X, int Z)
{
	int area = map->mapArea;
	int cx = ((X - map->center->X) >> 4) + map->mapX;
	int cz = ((Z - map->center->Z) >> 4) + map->mapZ;

	if (cx < 0)    cx += area; else
	if (cx > area) cx -= area;
	if (cz < 0)    cz += area; else
	if (cz > area) cz -= area;

	Chunk c = map->chunks + cx + cz * area;
	return (c->cflags & CFLAG_HASMESH) && c->X == X && c->Z == Z;
}

/* player moved into another chunk */
void raycastMoveCenter(Map map, vec4 old, vec4 pos)
{
	int dx = CPOS(pos[VX]) - CPOS(old[VX]);
	int dz = CPOS(pos[VZ]) - CPOS(old[VZ]);

	raycast.chunkMaxHeight = 0;
	if (abs(dx) <= globals.extraDist && abs(dz) <= globals.extraDist)
	{
		/* we can keep some chunks around: first discard chunks that are going out of the map */
		DATA16 tex, from;
		int    sz, x, y, z, remain;
		int    stride = raycast.distantChunks;
		int    start = globals.extraDist * stride + globals.extraDist;

		/* in the vast majority of case, either |dx| == 1 or |dz| == 1 */
		if (dx < 0) sz = - dx, tex = raycast.texMap + stride - sz;
		else sz = dx, tex = raycast.texMap;

		raycastInitArea(map, True);
		if (dx < 0) raycast.rasterArea[2] += dx*16;
		else        raycast.rasterArea[0] += dx*16;

		if (dz < 0) raycast.rasterArea[3] += dz*16;
		else        raycast.rasterArea[1] += dz*16;

		/* free vertical strip */
		if (sz > 0)
		{
			for (y = 0, from = raycast.texMap, remain = stride - sz; y < CHUNK_LIMIT; y ++)
			{
				for (z = 0; z < stride; z ++, tex += stride, from += stride)
				{
					/* opengl needs byte access :-/ */
					DATA8 texgl = (DATA8) tex;
					for (x = 0; x < sz; x ++, texgl += 2)
					{
						if (texgl[0] < 0xff)
							/* a chunk have been allocated: free it */
							raycastFreeTex(texgl);
					}
					/* now we can move the remaining texture ids */
					if (dx < 0) memmove(from - dx, from, remain * 2), memset(from, 0xff, sz * 2);
					else        memmove(from, from + dx, remain * 2), memset(from + remain, 0xff, sz * 2);
				}
			}
			/* chunk might have already been loaded/meshed because of lazy chunks */
			if (dx > 0)
			{
				from = raycast.texMap + start + raycast.rasterChunks - dx;
				x = raycast.rasterArea[2];
			}
			else
			{
				from = raycast.texMap + start + sz - 1;
				x = raycast.rasterArea[0] - 16;
			}
			for (; sz > 0; sz --)
			{
				int free;
				for (tex = from, z = raycast.rasterDest[1], remain = raycast.rasterChunks, free = 0; remain > 0; tex += stride, z += 16, remain --)
				{
					if (raycastHasRaster(map, x, z))
					{
						/* raycasting chunk here, but chunk has already a raster version */
						DATA8 texgl;
						for (y = 0, texgl = (DATA8) tex; y < CHUNK_LIMIT; y ++, texgl += stride * stride << 1)
							if (texgl[0] < 0xff) raycastFreeTex(texgl);
						free ++;
					}
				}
				if (dx > 0)
				{
					if (free == raycast.rasterChunks && x == raycast.rasterArea[2])
						raycast.rasterArea[2] += 16, fprintf(stderr, "chunk already meshed, moving eastern border\n");
					x += 16, from ++;
				}
				else
				{
					if (free == raycast.rasterChunks && x+16 == raycast.rasterArea[0])
						raycast.rasterArea[0] -= 16, fprintf(stderr, "chunk already meshed, moving western border\n");
					x -= 16, from --;
				}
			}
		}

		/* free horizontal strip */
		if (dz < 0) sz = - dz, tex = raycast.texMap + (stride - sz) * stride;
		else sz = dz, tex = raycast.texMap;
		if (sz > 0)
		{
			for (y = 0, from = raycast.texMap, remain = stride - sz; y < CHUNK_LIMIT; y ++, tex += stride * remain, from += stride * stride)
			{
				for (z = 0; z < sz; z ++, tex += stride)
				{
					DATA8 texgl = (DATA8) tex;
					for (x = 0; x < stride; x ++, texgl += 2)
					{
						if (texgl[0] < 0xff)
							raycastFreeTex(texgl);
					}
				}

				/* shift the texture ids */
				if (dz < 0)
				{
					memmove(from + stride * sz, from, remain * stride * 2);
					memset(from, 0xff, stride * sz * 2);
				}
				else
				{
					memmove(from, from + stride * sz, remain * stride * 2);
					memset(from + stride * remain, 0xff, stride * sz * 2);
				}
			}
			/* chunk might have already been loaded/meshed because of lazy chunks */
			if (dz > 0)
			{
				from = raycast.texMap + start + (raycast.rasterChunks - dz) * stride;
				z = raycast.rasterArea[3];
			}
			else
			{
				from = raycast.texMap + start + (sz - 1) * stride;
				z = raycast.rasterArea[1] - 16;
			}
			for (; sz > 0; sz --)
			{
				int free;
				for (tex = from, x = raycast.rasterDest[0], remain = raycast.rasterChunks, free = 0; remain > 0; tex ++, x += 16, remain --)
				{
					if (raycastHasRaster(map, x, z))
					{
						/* raycasting chunk here, but chunk has already a raster version */
						DATA8 texgl;
						for (y = 0, texgl = (DATA8) tex; y < CHUNK_LIMIT; y ++, texgl += stride * stride << 1)
							if (texgl[0] < 0xff) raycastFreeTex(texgl);
						free ++;
					}
				}
				if (dz > 0)
				{
					if (free == raycast.rasterChunks && z == raycast.rasterArea[3])
						raycast.rasterArea[3] += 16, fprintf(stderr, "chunk already meshed, moving southern border\n");
					z += 16, from += stride;
				}
				else
				{
					if (free == raycast.rasterChunks && z+16 == raycast.rasterArea[1])
						raycast.rasterArea[1] -= 16, fprintf(stderr, "chunk already meshed, moving northern border\n");
					z -= 16, from -= stride;
				}
			}
		}
		/* recompute chunkMaxHeight */
		for (tex = raycast.texMap + stride * stride * (CHUNK_LIMIT - 1), sz = stride * stride; sz > 0; sz --, tex ++)
		{
			y = CHUNK_LIMIT - ((DATA8)tex)[1];
			if (raycast.chunkMaxHeight < y)
				raycast.chunkMaxHeight = y;
		}
	}
	else /* teleported too far away: discard all distant chunks */
	{
		ChunkTexture bank;
		for (bank = HEAD(raycast.texBanks); bank; NEXT(bank))
		{
			memset(bank->usage, 0, sizeof bank->usage);
			bank->total = 0;
		}
		memset(raycast.texMap, 0xff, raycast.distantChunks * raycast.distantChunks * CHUNK_LIMIT * 2);
		raycastInitArea(map, False);
	}

	raycast.priorityIndex = 0;
	raycastRebuildPriority(map);
	raycastUpdateTexMap();
	raycastGenVertex();

	SemAdd(map->waitChanges, 1);
}

/* check if a raycasting chunk overlap a raster chunk: delete raycasting if yes */
Bool raycastChunkReady(Map map, Chunk ready)
{
	if (! raycast.texMap) return False;

	/* raster chunks have priority over raycasting */
	int offset = (map->maxDist >> 1) << 4;
	int offX = (ready->X - map->center->X + offset) >> 4;
	int offZ = (ready->Z - map->center->Z + offset) >> 4;
	int stride = raycast.distantChunks;
	int start = globals.extraDist * stride + globals.extraDist;
	DATA8 raster = (DATA8) (raycast.texMap + start + offZ * stride + offX);

	if (raster[0] < 0xff)
	{
		/* there was a raycasting chunk in place of this raster chunk */
		int i;
		/* free the entire column */
		for (i = ready->maxy; i > 0; i --, raster += stride * stride * 2)
		{
			if (raster[0] < 0xff)
				raycastFreeTex(raster);
		}

		/* check if we can push raster boundaries */
		int X, Z;
		/* check east-west */
		if (raycast.rasterArea[2] < raycast.rasterDest[2])
		{
			for (X = ready->X; raycast.rasterArea[2] < raycast.rasterDest[2] && X == raycast.rasterArea[2]; X += 16)
			{
				/* check if we can push east */
				offX = (X - map->center->X + offset) >> 4;
				raster = (DATA8) (raycast.texMap + start + offX);
				for (Z = raycast.rasterChunks; Z > 0 && raster[0] == 0xff; Z --, raster += stride << 1);
				if (Z == 0)
				{
					/* entire column is free: move rasterArea */
					raycast.rasterArea[2] += 16;
					fprintf(stderr, "moving raster 16 blocks to the east\n");
				}
			}
		}
		else if (raycast.rasterArea[0] > raycast.rasterDest[0])
		{
			for (X = ready->X; raycast.rasterArea[0] > raycast.rasterDest[0] && X+16 == raycast.rasterArea[0]; X -= 16)
			{
				/* check if we can push west */
				offX = (X - map->center->X + offset) >> 4;
				raster = (DATA8) (raycast.texMap + start + offX);
				for (Z = raycast.rasterChunks; Z > 0 && raster[0] == 0xff; Z --, raster += stride << 1);
				if (Z == 0)
				{
					raycast.rasterArea[0] -= 16;
					fprintf(stderr, "moving raster 16 blocks to the west\n");
				}
			}
		}

		/* check south-north */
		if (raycast.rasterArea[3] < raycast.rasterDest[3])
		{
			for (Z = ready->Z; raycast.rasterArea[3] < raycast.rasterDest[3] && Z == raycast.rasterArea[3]; Z += 16)
			{
				/* check if we can push south */
				offZ = (Z - map->center->Z + offset) >> 4;
				raster = (DATA8) (raycast.texMap + start + offZ * stride);
				for (X = raycast.rasterChunks; X > 0 && raster[0] == 0xff; X --, raster += 2);
				if (X == 0)
				{
					raycast.rasterArea[3] += 16;
					fprintf(stderr, "moving raster 16 blocks to the south\n");
				}
			}
		}
		else if (raycast.rasterArea[1] > raycast.rasterDest[1])
		{
			for (Z = ready->Z; raycast.rasterArea[1] > raycast.rasterDest[1] && Z == raycast.rasterArea[1]; Z -= 16)
			{
				/* check if we can push north */
				offZ = (Z - map->center->Z + offset) >> 4;
				raster = (DATA8) (raycast.texMap + start + offZ * stride);
				for (X = raycast.rasterChunks; X > 0 && raster[0] == 0xff; X --, raster += 2);
				if (X == 0)
				{
					/* entire column is free: move rasterArea */
					raycast.rasterArea[1] -= 16;
					fprintf(stderr, "moving raster 16 blocks to the north\n");
				}
			}
		}
		return True;
	}
	return False;
}

/*
 * try hard to load chunks that are visible first, it is somewhat a "frustum culling" for raycasting,
 * with some simplifications:
 * - we operate at the chunk level (not ChunkData).
 * - we don't have to check frustum visibility: the test has to be good enough, not perfect (because
 *   all chunks from distant region will eventually be loaded).
 */
void raycastRebuildPriority(Map map)
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
		/* like xoff/zoff from mapUpdate, but scan all 8 surrounding block instead of just 4 */
		static int8_t xoff8dir[] = {0,  1, -1, -1, 2,  0, -2,  0};
		static int8_t zoff8dir[] = {1, -1, -1,  1, 1, -2,  2, -2};
		static uint8_t corner[] = {1, 2, 4, 8, 3<<4, 6<<4, 9<<4, 12<<4};

		fprintf(stderr, "rebuilt priority list: %d\n", raycast.priorityFrame);

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
	int    stride = raycast.distantChunks;
	int    frame = raycast.map->frame;

	/* mesh chunks have not started processing yet */
	if (frame == 0)
		frame = 1;

	if (raycast.priorityFrame != frame)
	{
		raycastRebuildPriority(raycast.map);
		raycast.priorityFrame = frame;
	}

	for (priority = raycast.priorityMap + raycast.priorityIndex, eof = raycast.priorityMap + raycast.priorityMax; priority < eof; priority ++)
	{
		int X = priority[0] & 0xff;
		int Z = priority[0] >> 8;

		/* check if already processed */
		if (raycast.texMap[X + Z * stride] == 0xffff)
		{
			XZId[0] = X * 16 + (int) raycast.distantArea[0];
			XZId[1] = Z * 16 + (int) raycast.distantArea[1];
			XZId[2] = X + Z * stride;
			/* be sure we don't regen this chunk */
			((DATA8)(raycast.texMap + XZId[2]))[1] = 0xfe;
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

			Block b = blockIds + iter.blockIds[iter.offset];
			if (b->orientHint == ORIENT_LOG)
				/* use side texture instead of top */
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
static vec4    rasterArea;
static vec4    distantArea;

static float normals[] = { /* S, E, N, W, T, B */
	 0,  0,  1, 1,
	 1,  0,  0, 1,
	 0,  0, -1, 1,
	-1,  0,  0, 1,
	 0,  1,  0, 1,
	 0, -1,  0, 1,
};

#define vec3(dst, x,y,z)   dst[0]=x,dst[1]=y,dst[2]=z

static void texelFetch(vec4 ret, int texId, int X, int Y, int lod)
{
	DATA8 src;
	if (texId == 0)
	{
		src = (DATA8) (&raycast.texMap[X + Y * raycast.distantChunks]);
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
int voxelFindClosest(vec4 pos, vec4 V0, vec4 V1, float upward, int side)
{
	int X = (int) (pos[VX] - distantArea[0]) >> 4;
	int Z = (int) (pos[VZ] - distantArea[1]) >> 4;
	int Y = (int) pos[VY] >> 4;
	int size = (int) (distantArea[2] * 0.0625f);

	if (Y >= distantArea[3])
	{
		// above raycasted chunks and going upward: no way we can reach a distant chunk
		if (upward >= 0)
			return 0;

		// maybe we can
		vec3(V0, distantArea[0], distantArea[3] * 16, distantArea[1]);
		vec3(V1, V0[VX] + distantArea[2], V0[VY], V0[VZ] + distantArea[2]);
		return 2;
	}
	else if (X < 0 || Z < 0 || X >= size || Z >= size || pos[VY] < 0)
	{
		return 0;
	}

	vec4 texel;
	texelFetch(texel, 0, X, Z + Y * size, 0);
	int texId = roundf(texel[0] * 65280.0f + texel[1] * 255.0f);

	if (texId < 0xff00 && texId > raycast.maxSlot)
		/* this is not good :-/ */
		return 0;

	if (texId == 0xffff)
	{
		/* missing ChunkData: assume empty then */
		vec3(V0,
			floorf(pos[VX] * 0.0625f) * 16,
			floorf(pos[VY] * 0.0625f) * 16,
			floorf(pos[VZ] * 0.0625f) * 16
		);
		vec3(V1, V0[VX] + 16, V0[VY] + 16, V0[VZ] + 16);
		return 2;
	}

	if (texId >= 0xff00)
	{
		/* empty space above chunk */
		vec3(V0,
			floorf(pos[VX] * 0.0625f) * 16,
			floorf(pos[VY] * 0.0625f) * 16,
			floorf(pos[VZ] * 0.0625f) * 16
		);
		V0[VY] -= (texId - 0xff00) * 16;
		vec3(V1, V0[VX] + 16, distantArea[3] * 16, V0[VZ] + 16);
		return 2;
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
	default: return 0;
	}

	if (voxel[3] >= 0.5f)
	{
		int tex[3] = {voxel[0] * 255, voxel[1] * 255, voxel[2] * 255};
		vec3(V0,
			floorf(pos[VX] * 0.0625f) * 16 + (tex[0] & 15),
			floorf(pos[VY] * 0.0625f) * 16 + (tex[1] & 15),
			floorf(pos[VZ] * 0.0625f) * 16 + (tex[0] >> 4)
		);

		vec3(V1,
			V0[0] + (tex[2] & 15) + 1,
			V0[1] + (tex[1] >> 4) + 1,
			V0[2] + (tex[2] >> 4) + 1
		);
		return 2;
	}
	color[0] = (int) (voxel[0] * 255) * raycast.shading[side] >> 8;
	color[1] = (int) (voxel[1] * 255) * raycast.shading[side] >> 8;
	color[2] = (int) (voxel[2] * 255) * raycast.shading[side] >> 8;
	color[3] = 1;
	return 1;
}


Bool mapPointToVoxel(vec4 dir)
{
	vec4 pos, pt1, pt2;
	vec4 plane    = {floor(camera[VX]), floor(camera[VY]), floor(camera[VZ])};
	vec4 offset   = {dir[VX] < 0 ? -0.1 : 0.1, dir[VY] < 0 ? -0.1 : 0.1, dir[VZ] < 0 ? -0.1 : 0.1};
	int  sides[3] = {dir[VX] < 0 ? 3 : 1, dir[VZ] < 0 ? 2 : 0, dir[VY] < 0 ? 5 : 4};

	memcpy(pos, camera, 16);

	vec3(pt1, rasterArea[0], 0,   rasterArea[1]);
	vec3(pt2, rasterArea[2], 256, rasterArea[3]);

	for (;;)
	{
		// empty space in voxel: skip this part as quickly as possible
		int i;
		for (i = 0; i < 3; i ++)
		{
			vec4 inter, V0, V1;
			vec  norm;

			iteration ++;
			switch (sides[i]) {
			case 0:  vec3(V0, pt1[VX], pt1[VY], pt2[VZ]); memcpy(V1, pt2, 12); break; // south
			case 1:  vec3(V0, pt2[VX], pt1[VY], pt1[VZ]); memcpy(V1, pt2, 12); break; // east
			case 2:  vec3(V1, pt2[VX], pt2[VY], pt1[VZ]); memcpy(V0, pt1, 12); break; // north
			case 3:  vec3(V1, pt1[VX], pt2[VY], pt2[VZ]); memcpy(V0, pt1, 12); break; // west
			case 4:  vec3(V0, pt1[VX], pt2[VY], pt1[VZ]); memcpy(V1, pt2, 12); break; // top
			default: vec3(V1, pt2[VX], pt1[VY], pt2[VZ]); memcpy(V0, pt1, 12); // bottom
			}
			norm = normals + sides[i] * 4;

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
						plane[VX] += offset[VX];
				}
				else plane[VX] += norm[VX] * 0.5f;
				if (norm[VY] == 0)
				{
					if (inter[VY] == V0[VY] || inter[VY] == V1[VY])
						plane[VY] += offset[VY];
				}
				else plane[VY] += norm[VY] * 0.5f;
				if (norm[VZ] == 0)
				{
					if (inter[VZ] == V0[VZ] || inter[VZ] == V1[VZ])
						plane[VZ] += offset[VZ];
				}
				else plane[VZ] += norm[VZ] * 0.5f;

				switch (voxelFindClosest(plane, pt1, pt2, dir[VY], opp[sides[i]])) {
				case 0: return False;
				case 1: return True;
				}
				break;
			}
		}
		if (i == 3)
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

	memcpy(rasterArea,  raycast.rasterArea, sizeof rasterArea);
	memcpy(distantArea, raycast.distantArea, sizeof distantArea);

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
	free(sum);
	return NULL;
}

