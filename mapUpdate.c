/*
 * mapUpdate.c : keep various tables (Blocks, Data, HeightMap, SkyLight, BlockLight)
 *               up to date, according to user changes.
 *
 * written by T.Pierron, sep 2020
 */

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
#include "maps.h"
#include "blocks.h"
#include "render.h"
#include "sign.h"
#include "particles.h"
#include "redstone.h"
#include "blockupdate.h"
#include "NBT2.h"

/* order is S, E, N, W, T, B */
int8_t xoff[] = {0,  1, -1, -1, 1,  0};
int8_t zoff[] = {1, -1, -1,  1, 0,  0};
int8_t yoff[] = {0,  0,  0,  0, 1, -2};
int8_t relx[] = {0,  1,  0, -1, 0,  0};
int8_t rely[] = {0,  0,  0,  0, 1, -1};
int8_t relz[] = {1,  0, -1,  0, 0,  0};
int8_t opp[]  = {2<<5, 3<<5, 0, 1<<5, 5<<5, 4<<5};
uint8_t slots[] = {4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1};

static struct MapUpdate_t track;

/* return how many units of skylight a block will reduce incoming light */
static inline int blockGetSkyOpacity(int blockId, int min)
{
	uint8_t opac = blockIds[blockId].opacSky;
	return opac <= min ? min : opac;
}

static inline int blockGetLightOpacity(int blockId, int min)
{
	uint8_t opac = blockIds[blockId].opacLight;
	return opac <= min ? min : opac;
}

/*
 * block iterator over multiple chunks/sub-chunk
 */

/* get chunk from absolute coord <pos> */
Chunk mapGetChunk(Map map, vec4 pos)
{
	Chunk ref  = map->center;
	int   area = (map->maxDist >> 1)+1;
	int   offX = CPOS(pos[0] - ref->X);
	int   offZ = CPOS(pos[2] - ref->Z);

	if (abs(offX) > area || abs(offZ) > area)
		return NULL;

	area = map->mapArea;
	offX += map->mapX;
	offZ += map->mapZ;
	if (offX < 0)     offX += area; else
	if (offX >= area) offX -= area;
	if (offZ < 0)     offZ += area; else
	if (offZ >= area) offZ -= area;

	return map->chunks + offX + offZ * area;
}

/* start iterator at absolute <pos> */
void mapInitIter(Map map, BlockIter iter, vec4 pos, Bool autoAlloc)
{
	ChunkData cd;
	Chunk     ref = mapGetChunk(map, pos);
	int       y = (int) pos[1];
	int       layer = y >> 4;

	if (ref == NULL)
	{
		/* outside map */
		memset(iter, 0, sizeof *iter);
		iter->x = pos[0];
		iter->z = pos[2];
		iter->yabs = y;
		return;
	}
	iter->ref  = ref;
	iter->cd   = cd = layer < CHUNK_LIMIT ? ref->layer[layer] : NULL;
	iter->x    = (int) pos[0] - ref->X;
	iter->z    = (int) pos[2] - ref->Z;
	iter->y    = y & 15;
	iter->yabs = y;

	iter->alloc    = autoAlloc;
	iter->offset   = CHUNK_BLOCK_POS(iter->x, iter->z, y&15);
	iter->blockIds = cd ? cd->blockIds : NULL;

	if (autoAlloc && cd == NULL && layer <= CHUNK_LIMIT)
	{
		iter->cd = cd = chunkCreateEmpty(ref, layer);
		iter->blockIds = cd->blockIds;
	}
}

void mapInitIterOffset(BlockIter iter, ChunkData cd, int offset)
{
	iter->offset   = offset;
	iter->ref      = cd->chunk;
	iter->cd       = cd;
	iter->x        = offset & 15;   offset >>= 4;
	iter->z        = offset & 15;   offset >>= 4;
	iter->y        = offset;
	iter->yabs     = cd->Y + iter->y;
	iter->alloc    = False;
	iter->blockIds = cd ? cd->blockIds : NULL;
}

/* dx, dy, dz can be any value, but Chunk must remain within map */
void mapIter(BlockIter iter, int dx, int dy, int dz)
{
	Chunk ref = iter->ref;
	int   off;
	int   pos;
	/* iterate over x axis */
	pos = iter->x + dx;
	if (pos < 0)
	{
		do {
			ref += chunkNeighbor[ref->neighbor+8];
			pos += 16;
		} while (pos < 0);
	}
	else if (pos > 15)
	{
		do {
			ref += chunkNeighbor[ref->neighbor+2];
			pos -= 16;
		} while (pos > 15);
	}
	iter->x = pos;
	off = pos;

	/* iterate over z axis */
	pos = iter->z + dz;
	if (pos < 0)
	{
		do {
			ref += chunkNeighbor[ref->neighbor+4];
			pos += 16;
		} while (pos < 0);
	}
	else if (pos > 15)
	{
		do {
			ref += chunkNeighbor[ref->neighbor+1];
			pos -= 16;
		} while (pos > 15);
	}
	iter->z = pos;
	iter->yabs += dy;
	off += pos<<4;

	/* iterate over y axis */
	ChunkData cd = (iter->yabs>>4) < CHUNK_LIMIT ? ref->layer[iter->yabs>>4] : NULL;

	if (cd == NULL && iter->alloc)
	{
		/* XXX check if above or below build limit */
		cd = chunkCreateEmpty(ref, iter->yabs>>4);
	}

	iter->ref = ref;
	iter->y   = pos = (iter->y + dy) & 15;
	iter->cd  = cd;

	iter->offset   = (pos<<8) + off;
	iter->blockIds = cd ? cd->blockIds : NULL;
}

/* note: val must be between 0 and 15 (included) */
void mapUpdateTable(BlockIter iter, int val, int table)
{
	int     off  = iter->offset;
	DATA8   data = iter->blockIds + table + (off >> 1);
	uint8_t cur  = *data;

	if (off & 1) *data = (cur & 0x0f) | (val << 4);
	else         *data = (cur & 0xf0) | val;

	/* track chunk that have been modified */
	ChunkData cd = iter->cd;
	if (! cd->slot)
	{
		cd->slot = 1;
		*track.list = cd;
		track.list = &cd->update;
	}
	/* track which side it is near to (we might have to update nearby chunk too) */
	cd->slot |= (slots[iter->z] << 1) | (slots[iter->x] << 2) | (slots[iter->y] << 5);
}

static uint8_t mapGetSky(BlockIter iter)
{
	int   off = iter->offset;
	DATA8 sky = iter->blockIds + SKYLIGHT_OFFSET + (off >> 1);
	if (off & 1) return *sky >> 4;
	else         return *sky & 15;
}

static uint8_t mapGetLight(BlockIter iter)
{
	int   off   = iter->offset;
	DATA8 light = iter->blockIds + BLOCKLIGHT_OFFSET + (off >> 1);
	if (off & 1) return *light >> 4;
	else         return *light & 15;
}

#if 0
static inline uint8_t mapGetData(BlockIter iter)
{
	int   off  = iter->offset;
	DATA8 data = iter->blockIds + DATA_OFFSET + (off >> 1);
	if (off & 1) return *data >> 4;
	else         return *data & 15;
}
#endif

#define STEP     126   /* need to be multiple of 3 */

#define mapUpdateInitTrack(track)    \
	memset(&track.pos, 0, sizeof track - offsetof(struct MapUpdate_t, pos));

/* coordinates that will need further investigation for skylight/blocklight */
static void trackAdd(int x, int y, int z)
{
	int8_t * buffer;
	/* this is an expanding ring buffer */
	if (track.usage == track.max)
	{
		/* not enough space left */
		track.max += STEP;
		buffer = realloc(track.coord, track.max);
		if (! buffer) return;
		track.coord = buffer;
		if (track.last > 0)
		{
			int nb = track.last;
			if (nb > STEP) nb = STEP;
			memcpy(track.coord + track.usage, track.coord, nb);
			if (nb < track.last) memmove(track.coord, track.coord + nb, track.last - nb);
		}
		track.last -= STEP;
		if (track.last < 0)
			track.last += track.max;
	}
	if (track.unique)
	{
		int i;
		for (i = track.usage, buffer = track.coord + track.last; i > 0; i -= 3)
		{
			if (buffer == track.coord) buffer += track.max;
			buffer -= 3;
			if ((buffer[0]&31) == (x&31) && buffer[1] == y && buffer[2] == z)
				return;
		}
	}
	buffer = track.coord + track.last;
	buffer[0] = x;
	buffer[1] = y;
	buffer[2] = z;
	track.last += 3;
	track.usage += 3;
	if (track.last == track.max)
		track.last = 0;
	if (track.maxUsage < track.usage)
		track.maxUsage = track.usage;
}

/* will prevent use of recursion */
static void trackAddUpdate(BlockIter iter, int blockId)
{
	int max = (track.updateCount+127) & ~127;
	if (max == track.updateCount)
	{
		max += 128;
		BlockUpdate list = realloc(track.updates, sizeof *list * max + 4 * (max >> 5));
		if (list == NULL) return;
		/* move usage table */
		memmove(track.updateUsage = (DATA32) (list + max), list + track.updateCount, (track.updateCount >> 5) * 4);
		memset(track.updateUsage + (track.updateCount >> 5), 0, 16);
		track.updates = list;
	}

	BlockUpdate update = track.updates + mapFirstFree(track.updateUsage, max);

	track.updateCount ++;
	update->cd = iter->cd;
	update->offset = iter->offset;
	update->blockId = blockId;
}

/*
 * SkyLight/HeightMap update: these functions use an iterative approach, to update only
 * what's mostly necessary. See SkyLight in the utility folder for a simplified version of
 * this algorithm (limited to 2D space).
 */

/* a solid block has been placed */
static void mapUpdateSkyLightBlock(BlockIter iterator)
{
	struct BlockIter_t iter = *iterator;
	int8_t max, level, old, sky;
	int    i;

	mapUpdateInitTrack(track);
	track.unique = 1;

	i = CHUNK_BLOCK_POS(iter.x, iter.z, 0);
	if (iter.ref->heightMap[i] < iter.yabs+1)
	{
		int j, max;
		sky = mapGetSky(&iter) - blockGetSkyOpacity(iter.blockIds[iter.offset], 0);
		/* block is not blocking sky (ie: glass) */
		if (sky == MAXSKY) return;
		if (sky < 0) sky = 0;
		mapUpdateTable(&iter, sky, SKYLIGHT_OFFSET);
		/* block is set at a higher position */
		for (j = iter.yabs - (sky == 0), max = iter.ref->heightMap[i]; j >= max; j --)
		{
			trackAdd(MAXSKY | (4<<5), j - iter.yabs, 0);
		}
		iter.ref->heightMap[i] = iter.yabs + 1;
	}
	else
	{
		mapIter(&iter, 0, -1, 0);
		if (blockGetSkyOpacity(iter.blockIds[iter.offset], 0) < MAXSKY)
			trackAdd(MAXSKY | (4<<5), -1, 0);

		mapIter(&iter, 0, 2, 0);
		if (blockGetSkyOpacity(iter.blockIds[iter.offset], 0) < MAXSKY)
			trackAdd(MAXSKY | (5<<5), 1, 0);

		mapIter(&iter, 0, -1, 0);
	}
	iter.alloc = 0;
	for (i = 0; i < 4; i ++)
	{
		mapIter(&iter, xoff[i], yoff[i], zoff[i]);
		if (iter.ref->heightMap[CHUNK_BLOCK_POS(iter.x, iter.z, 0)] > iter.yabs &&
			blockGetSkyOpacity(iter.blockIds[iter.offset], 0) < MAXSKY)
		{
			trackAdd(MAXSKY + relx[i] + opp[i], 0, relz[i]);
		}
	}
	iter.alloc = 1;
	/* get iterator back to original pos */
	mapIter(&iter, 0, 0, 1);

	while (track.usage > 0)
	{
		struct BlockIter_t initial;
		struct BlockIter_t neighbor = iter;
		int8_t * XYZ = track.coord + track.pos;
		int8_t   dir = (XYZ[0] >> 5) & 7;

		mapIter(&neighbor, (XYZ[0] & 31) - MAXSKY, XYZ[1], XYZ[2]);
		initial = neighbor;
		sky = mapGetSky(&neighbor);

		/* is it a local maximum? */
		for (i = max = 0; i < DIM(xoff); i ++)
		{
			mapIter(&neighbor, xoff[i], yoff[i], zoff[i]);

			level = mapGetSky(&neighbor);

			if (level - (i>=4) >= sky && max < level)
			{
				max = level;
				if (max == MAXSKY) break;
			}
		}
		neighbor = initial;
		if (max > 0)
		{
			/* not a local maximum */
			old = max - blockGetSkyOpacity(neighbor.blockIds[neighbor.offset], 1);
			if (old <= 0) old = 0;
			mapUpdateTable(&neighbor, old, SKYLIGHT_OFFSET);

			if (old > 0)
			/* check if surrounding need increase in sky level */
			for (i = 0; i < DIM(xoff); i ++)
			{
				mapIter(&neighbor, xoff[i], yoff[i], zoff[i]);
				int8_t min = old - blockGetSkyOpacity(neighbor.blockIds[neighbor.offset], 1);
				level = mapGetSky(&neighbor);

				if (level < min)
				{
					mapUpdateTable(&neighbor, min, SKYLIGHT_OFFSET);
					trackAdd((XYZ[0]&31) + relx[i] + opp[i], XYZ[1] + rely[i], XYZ[2] + relz[i]);
				}
			}
			if (sky == old)
				goto skip;
		}
		else /* it is a local maximum */
		{
			mapIter(&neighbor, relx[dir], rely[dir], relz[dir]);
			level = mapGetSky(&neighbor) - blockGetSkyOpacity(initial.blockIds[initial.offset], 1);
			mapUpdateTable(&initial, level <= 0 ? 0 : level, SKYLIGHT_OFFSET);
		}
		/* check if neighbors depend on light level of cell we just changed */
		for (i = 0, neighbor = initial; i < DIM(xoff); i ++)
		{
			mapIter(&neighbor, xoff[i], yoff[i], zoff[i]);
			if (i == dir) continue;
			level = blockGetSkyOpacity(neighbor.blockIds[neighbor.offset], 1);
			if (level == MAXSKY) continue;
			old = sky - level;
			level = mapGetSky(&neighbor);
			if (level > 0 && (level == old || (XYZ[1] == 0 && i >= 4 && level == sky)))
			{
				/* incorrect light level here */
				trackAdd((XYZ[0]&31) + relx[i] + opp[i], XYZ[1] + rely[i], XYZ[2] + relz[i]);
			}
		}
		skip:
		track.pos += 3;
		track.usage -= 3;
		if (track.pos == track.max) track.pos = 0;
	}
}

/* a solid block has been removed */
static void mapUpdateSkyLightUnblock(BlockIter iterator)
{
	struct BlockIter_t iter = *iterator;

	mapUpdateInitTrack(track);
	track.unique = 0;

	int i = CHUNK_BLOCK_POS(iter.x, iter.z, 0);
	if (iter.yabs+1 >= iter.ref->heightMap[i])
	{
		/* highest block removed: compute new height */
		int startY = iter.yabs;
		/* check if a transparent block has been removed */
		if (mapGetSky(&iter) == MAXSKY)
			return;
		while (iter.yabs >= 0 && blockGetSkyOpacity(iter.blockIds[iter.offset], 0) == 0)
		{
			mapUpdateTable(&iter, MAXSKY, SKYLIGHT_OFFSET);
			trackAdd(0, iter.yabs - startY, 0);
			mapIter(&iter, 0, -1, 0);
		}
		iter.ref->heightMap[i] = iter.yabs+1;
		iter = *iterator;
	}
	else
	{
		uint8_t max;
		/* look around for the highest skylight value */
		for (i = max = 0; i < DIM(xoff); i ++)
		{
			mapIter(&iter, xoff[i], yoff[i], zoff[i]);

			if (iter.blockIds)
			{
				uint8_t sky = mapGetSky(&iter);
				if (sky > max) max = sky;
			}
		}
		/* get iterator back to original pos */
		mapIter(&iter, 0, 1, 0);
		if (max > 0)
		{
			mapUpdateTable(&iter, max-blockGetSkyOpacity(iter.blockIds[iter.offset], 1), SKYLIGHT_OFFSET);
			trackAdd(0, 0, 0);
		}
	}

	while (track.usage > 0)
	{
		struct BlockIter_t neighbor = iter;
		int8_t * XYZ = track.coord + track.pos;
		uint8_t  sky;

		mapIter(&neighbor, XYZ[0], XYZ[1], XYZ[2]);
		sky = mapGetSky(&neighbor);

		for (i = 0; i < DIM(xoff); i ++)
		{
			mapIter(&neighbor, xoff[i], yoff[i], zoff[i]);
			if (! neighbor.blockIds) continue;

			int8_t col = sky - blockGetSkyOpacity(neighbor.blockIds[neighbor.offset], i < 4 || sky < MAXSKY);
			if (col < 0) col = 0;
			if (mapGetSky(&neighbor) < col)
			{
				mapUpdateTable(&neighbor, col, SKYLIGHT_OFFSET);
				trackAdd(XYZ[0] + relx[i], XYZ[1] + rely[i], XYZ[2] + relz[i]);
			}
		}

		track.pos += 3;
		track.usage -= 3;
		if (track.pos == track.max) track.pos = 0;
	}
}

/*
 * BlockLight table update functions: see LightMap example for a simplied version
 * of this algorithm (using 2D space).
 */
static void mapUpdateAddLight(BlockIter iterator, int intensity /* max: 15 */)
{
	mapUpdateInitTrack(track);
	track.unique = 0;
	if (mapGetLight(iterator) >= intensity)
		return;
	trackAdd(0, 0, 0);
	mapUpdateTable(iterator, intensity, BLOCKLIGHT_OFFSET);

	while (track.usage > 0)
	{
		struct BlockIter_t neighbor = *iterator;
		int8_t * XYZ = track.coord + track.pos;
		int8_t   level, i, dim;

		mapIter(&neighbor, XYZ[0], XYZ[1], XYZ[2]);
		level = mapGetLight(&neighbor);

		for (i = 0; i < DIM(xoff); i ++)
		{
			mapIter(&neighbor, xoff[i], yoff[i], zoff[i]);

			dim = blockGetLightOpacity(neighbor.blockIds[neighbor.offset], 1);
			if (dim < MAXLIGHT && mapGetLight(&neighbor) < level - dim)
			{
				if (level > 1)
				{
					trackAdd(XYZ[0] + relx[i], XYZ[1] + rely[i], XYZ[2] + relz[i]);
					XYZ = track.coord + track.pos;
				}

				mapUpdateTable(&neighbor, level - dim, BLOCKLIGHT_OFFSET);
			}
		}
		track.pos += 3;
		track.usage -= 3;
		if (track.pos == track.max) track.pos = 0;
	}
}

static void mapUpdateRemLight(BlockIter iterator)
{
	mapUpdateInitTrack(track);
	track.unique = 1;
	trackAdd(0, 0, 0);

	while (track.usage > 0)
	{
		struct BlockIter_t neighbor = *iterator;

		int8_t * XYZ = track.coord + track.pos;
		int8_t   level, max, i, dir, equal;

		mapIter(&neighbor, XYZ[0], XYZ[1], XYZ[2]);
		level = mapGetLight(&neighbor);

		for (i = max = equal = 0; i < DIM(xoff); i ++)
		{
			mapIter(&neighbor, xoff[i], yoff[i], zoff[i]);
			if (neighbor.cd == NULL) continue;

			int8_t light = mapGetLight(&neighbor);

			if (level < light && max < light)
				max = light;
			if (level <= light && equal <= light)
				equal = light;
		}
		/* iterator back to start */
		mapIter(&neighbor, 0, 1, 0);

		dir = 0;
		if (max > 0)
		{
			/* not a local maximum */
			int light = max - blockGetLightOpacity(neighbor.blockIds[neighbor.offset], 1);
			if (light < 0) light = 0;
			mapUpdateTable(&neighbor, level = light, BLOCKLIGHT_OFFSET);
			/* light needs to increase around here */
			dir = 1;
		}
		else if (level != 0) /* local maximum */
		{
			/* light need to decrease around here */
			if (equal > 0)
			{
				equal -= blockGetLightOpacity(neighbor.blockIds[neighbor.offset], 1);
				if (equal < 0) equal = 0;
			}
			mapUpdateTable(&neighbor, equal, BLOCKLIGHT_OFFSET);
			dir = equal > 0 ? 1 : -1;
		}

		/* check if neighbors need adjustment */
		if (dir != 0)
		for (i = 0; i < DIM(xoff); i ++)
		{
			mapIter(&neighbor, xoff[i], yoff[i], zoff[i]);
			if (neighbor.cd == NULL) continue;

			int opac = blockGetLightOpacity(neighbor.blockIds[neighbor.offset], 1);
			if (opac == MAXLIGHT) continue;
			int8_t light = mapGetLight(&neighbor);
			if (dir < 0)
			{
				if (light != level-opac || level-opac <= 0) continue;
			} else {
				if (light >= level) continue;
			}

			trackAdd(XYZ[0] + relx[i], XYZ[1] + rely[i], XYZ[2] + relz[i]);
		}
		track.pos += 3;
		track.usage -= 3;
		if (track.pos == track.max) track.pos = 0;
	}
}

static void mapUpdateObstructLight(BlockIter iterator)
{
	struct BlockIter_t iter = *iterator;
	int8_t light;

	mapUpdateInitTrack(track);
	trackAdd(0, 0, 0);
	light = mapGetLight(&iter);
	if (light <= 1) return;
	light -= blockGetLightOpacity(iter.blockIds[iter.offset], 0);
	mapUpdateTable(&iter, light < 0 ? 0 : light, BLOCKLIGHT_OFFSET);

	while (track.usage > 0)
	{
		struct BlockIter_t neighbor = *iterator;

		int8_t * XYZ = track.coord + track.pos;
		int8_t   i, dim, max, k;

		mapIter(&neighbor, XYZ[0], XYZ[1], XYZ[2]);

		/* check surrounding blocks if light levels are correct */
		for (i = 0; i < DIM(xoff); i ++)
		{
			mapIter(&neighbor, xoff[i], yoff[i], zoff[i]);

			int block = neighbor.blockIds[neighbor.offset];
			light = mapGetLight(&neighbor);
			dim   = blockIds[block].emitLight;
			if (dim > 0 && dim <= light) continue;
			dim   = blockGetLightOpacity(block, 1);
			if (dim < MAXLIGHT)
			{
				struct BlockIter_t iter = neighbor;
				/* check if there is a block light == light + dim surrounding this pos */
				for (k = max = 0; k < DIM(xoff); k ++)
				{
					mapIter(&iter, xoff[k], yoff[k], zoff[k]);
					if (blockGetLightOpacity(iter.blockIds[iter.offset], 0) == MAXLIGHT) continue;
					uint8_t light2 = mapGetLight(&iter);
					if (light2 == light + dim) break;
					if (max < light2) max = light2;
				}
				/* nope, must readjust light levels around here */
				if (k == DIM(xoff))
				{
					/* incorrect light level */
					if (max >= dim)
					{
						mapUpdateTable(&neighbor, max - dim, BLOCKLIGHT_OFFSET);
						trackAdd(XYZ[0] + relx[i], XYZ[1] + rely[i], XYZ[2] + relz[i]);
					}
					else mapUpdateTable(&neighbor, 0, BLOCKLIGHT_OFFSET);
				}
			}
		}
		track.pos += 3;
		track.usage -= 3;
		if (track.pos == track.max) track.pos = 0;
	}
}

static void mapUpdateRestoreLight(Map map, BlockIter iterator)
{
	struct BlockIter_t neighbor = *iterator;
	int i, max;
	for (i = max = 0; i < DIM(xoff); i ++)
	{
		mapIter(&neighbor, xoff[i], yoff[i], zoff[i]);

		uint8_t light = mapGetLight(&neighbor);
		if (max < light)
			max = light;
	}
	if (max > 0)
		mapUpdateAddLight(iterator, max-blockGetLightOpacity(iterator->blockIds[iterator->offset], 1));
	else
		mapUpdateTable(iterator, 0, BLOCKLIGHT_OFFSET);
}

/* dispatch changes to block light update functions */
static void mapUpdateBlockLight(Map map, BlockIter iter, int oldId, int newId)
{
	uint8_t oldLight = blockIds[oldId>>4].emitLight;
	uint8_t newLight = blockIds[newId>>4].emitLight;

	if (oldLight != newLight)
	{
		if (oldLight > newLight)
			mapUpdateRemLight(iter);
		else
			mapUpdateAddLight(iter, newLight);
	}
	else
	{
		uint8_t opac = blockIds[newId>>4].opacLight, i;
		uint8_t light = mapGetLight(iter);
		if (opac == 0 || light == 0)
		{
			struct BlockIter_t neighbor = *iter;
			neighbor.alloc = False;
			for (i = 0; i < DIM(xoff); i ++)
			{
				mapIter(&neighbor, xoff[i], yoff[i], zoff[i]);
				if (neighbor.cd && mapGetLight(&neighbor) > 1)
				{
					mapUpdateRestoreLight(map, iter);
					break;
				}
			}
		}
		else if (newLight != light)
		{
			mapUpdateObstructLight(iter);
		}
	}
}

/*
 * redstone propagation
 */

/* queue block that needs updating in the vicinity of <iterator> */
static void mapUpdateAddRSUpdate(BlockIter iterator, RSWire cnx)
{
	struct BlockIter_t iter = *iterator;
	int i;
	mapIter(&iter, cnx->dx, cnx->dy, cnx->dz);
//	printCoord("checking update at", &iter);
	if (cnx->pow == POW_VERYWEAK)
	{
		/* only check the where the update is */
		Block b = &blockIds[iter.blockIds[iter.offset]];
		if (b->rsupdate)
			trackAddUpdate(&iter, 0xffff);
	}
	else for (i = 0; i < 6; i ++)
	{
		/* we cannot perform the update yet, we need the signal to be updated all the way :-/ */
		mapIter(&iter, xoff[i], yoff[i], zoff[i]);
		Block b = &blockIds[iter.blockIds[iter.offset]];
		if (b->rsupdate)
			trackAddUpdate(&iter, 0xffff);
	}
}

/*
 * redstone propagation is very similar to blockLight, but with a lot more rules
 * to check which block to connect to.
 */
static void mapUpdatePropagateSignal(BlockIter iterator)
{
	struct RSWire_t connectTo[MAXUPDATE];
	int count, i, signal;

	mapUpdateInitTrack(track);
	track.unique = False;
	signal = redstoneSignalStrength(*iterator, True);
	count = redstoneConnectTo(*iterator, connectTo);
	if (signal <= MAXSIGNAL)
		mapUpdateTable(iterator, signal, DATA_OFFSET);
	for (i = 0; i < count; i ++)
	{
		RSWire cnx = connectTo + i;
		if (cnx->signal == RSUPDATE)
		{
			mapUpdateAddRSUpdate(iterator, cnx);
		}
		else if (cnx->signal < signal - 1)
		{
			if (cnx->blockId == RSWIRE)
			{
				struct BlockIter_t iter = *iterator;
				mapIter(&iter, cnx->dx, cnx->dy, cnx->dz);
				mapUpdateTable(&iter, signal-1, DATA_OFFSET);
			}
			trackAdd(cnx->dx, cnx->dy, cnx->dz);
		}
	}

	while (track.usage > 0)
	{
		struct BlockIter_t neighbor = *iterator;
		int8_t * XYZ = track.coord + track.pos;

		mapIter(&neighbor, XYZ[0], XYZ[1], XYZ[2]);

		signal = redstoneSignalStrength(neighbor, False);
		count = redstoneConnectTo(neighbor, connectTo);
		for (i = 0; i < count; i ++)
		{
			RSWire cnx = connectTo + i;
			if (cnx->signal == RSUPDATE)
			{
				mapUpdateAddRSUpdate(&neighbor, cnx);
			}
			else if (cnx->signal < signal - 1)
			{
				if (cnx->blockId == RSWIRE)
				{
					struct BlockIter_t iter = neighbor;
					mapIter(&iter, cnx->dx, cnx->dy, cnx->dz);
					mapUpdateTable(&iter, signal-1, DATA_OFFSET);
				}
				trackAdd(XYZ[0] + cnx->dx, XYZ[1] + cnx->dy, XYZ[2] + cnx->dz);
			}
		}
		track.pos += 3;
		track.usage -= 3;
		if (track.pos == track.max) track.pos = 0;
	}
}

/* a redstone element has been deleted: update signal */
void mapUpdateDeleteSignal(BlockIter iterator, int blockId)
{
	struct RSWire_t connectTo[MAXUPDATE];
	int i, count, signal, block;

	/* block must not be deleted at this point */
	mapUpdateInitTrack(track);
	track.unique = True;
	count = redstoneConnectTo(*iterator, connectTo);

	for (i = 0; i < count; i ++)
	{
		RSWire cnx = connectTo + i;
		if (cnx->signal == RSUPDATE)
			mapUpdateAddRSUpdate(iterator, cnx);
		else
			trackAdd(cnx->dx, cnx->dy, cnx->dz);
	}
	/* now we can delete the block */
	iterator->blockIds[iterator->offset] = blockId >> 4;
	mapUpdateTable(iterator, blockId & 15, DATA_OFFSET);

	while (track.usage > 0)
	{
		struct BlockIter_t neighbor = *iterator;
		int8_t * XYZ = track.coord + track.pos;
		int8_t   max, dir, equal, level;

		mapIter(&neighbor, XYZ[0], XYZ[1], XYZ[2]);
		track.pos += 3;
		track.usage -= 3;
		if (track.pos == track.max) track.pos = 0;

		level = redstoneSignalStrength(neighbor, False);
		count = redstoneConnectTo(neighbor, connectTo);
		block = neighbor.blockIds[neighbor.offset];

		//printCoord("delete signal at", &neighbor);

		for (i = max = equal = 0; i < count; i ++)
		{
			struct BlockIter_t iter = neighbor;
			RSWire cnx = connectTo + i;

			signal = cnx->signal;

			if (signal == RSUPDATE)
			{
				mapUpdateAddRSUpdate(&iter, cnx);
				continue;
			}
			mapIter(&iter, cnx->dx, cnx->dy, cnx->dz);
			if (level < signal && max < signal)
				max = signal;
			if (level <= signal && equal <= signal)
				equal = signal;
		}

		dir = 0;
		if (max > 0)
		{
			/* not a local maximum */
			int sig = max - 1;
			if (sig < 0) sig = 0;
			if (block == RSWIRE)
			{
				//fprintf(stderr, "setting wire %d, %d, %d to %d\n", neighbor.ref->X + neighbor.x, neighbor.yabs, neighbor.ref->Z + neighbor.z, sig);
				mapUpdateTable(&neighbor, sig, DATA_OFFSET);
			}
			/* signal needs to increase around here */
			dir = 1;
		}
		else if (level != 0) /* local maximum */
		{
			/* signal need to decrease around here */
			if (equal > 0)
			{
				equal -= 1;
				if (equal < 0) equal = 0;
			}
			if (block == RSWIRE)
			{
				//fprintf(stderr, "setting wire %d, %d, %d to %d\n", neighbor.ref->X + neighbor.x, neighbor.yabs, neighbor.ref->Z + neighbor.z, equal);
				mapUpdateTable(&neighbor, equal, DATA_OFFSET);
			}
			dir = equal > 0 ? 1 : -1;
		}

		/* check if neighbors need adjustment */
		if (dir != 0)
		for (i = 0; i < count; i ++)
		{
			RSWire cnx = connectTo + i;
			if (cnx->blockId != RSWIRE) continue;
			if (dir < 0)
			{
				if (cnx->signal != level-1 || level-1 == 0) continue;
			} else {
				if (cnx->signal >= level) continue;
			}
			trackAdd(cnx->dx + XYZ[0], cnx->dy + XYZ[1], cnx->dz + XYZ[2]);
		}
	}
}

/* redstone power level has been updated in a block, check nearby what updates it will trigger */
static void mapUpdateRedstone(Map map, BlockIter iter, int power, int side)
{
	Block b = &blockIds[iter->blockIds[iter->offset]];
	int   i, count, flags;

	if (b->type == SOLID)
	{
		/* check all block surrounding this one */
		if (power < POW_STRONG)
		{
			/* need to make sure that the block is not powered from elsewhere */
			int pow = redstoneIsPowered(*iter, 0);
			if (pow > power) power = pow;
		}
		flags = 127 ^ (1 << (side+1));
		count = 6;
	}
	else
	{
		flags = 1;
		count = 0;
	}
	for (i = -1; i < count; flags >>= 1, i ++)
	{
		if (i >= 0)
			mapIter(iter, xoff[i], yoff[i], zoff[i]);
		if ((flags & 1) == 0) continue;

		int blockId = getBlockId(iter);
		int newId   = redstonePowerAdjust(blockId, i < 0 ? side : i, power);

		if (blockId != (newId & 0xffff))
		{
			int tick = newId >> 16;
			if (tick > 0)
			{
				/* need to be updated later */
				updateAdd(iter, newId & 0xffff, tick & 15);
			}
			else if ((newId >> 4) == RSWIRE)
			{
				if ((newId & 15) < (blockId  & 15))
					mapUpdateDeleteSignal(iter, newId);
				else
					mapUpdatePropagateSignal(iter);
			}
			else /* immediate update */
			{
				vec4 pos = {iter->ref->X + iter->x, iter->yabs, iter->ref->Z + iter->z};
				mapUpdate(map, pos, newId, NULL, False);
			}
		}
	}
}

/* check if a block is powered nearby, change current block to active state then */
static int mapUpdateIfPowered(Map map, BlockIter iterator, int oldId, int blockId)
{
	static uint8_t repeaterFrom[] = {0,3,2,1}; /* data orient to iter orient ({x,y,z}off[]) */
	Block b = &blockIds[blockId >> 4];

	switch (b->id) {
	case RSDISPENSER:
	case RSNOTEBLOCK:
	case RSPOWRAILS:
	case RSSTICKYPISTON:
	case RSPISTON:
	case RSLAMP:
	case RSDROPPER:
		// TODO
		break;
	case RSREPEATER_OFF:
		if (redstoneIsPowered(*iterator, repeaterFrom[blockId & 3]))
		{
			/* need to be updated later */
			updateAdd(iterator, ID(RSREPEATER_ON, blockId & 15), redstoneRepeaterDelay(blockId));
			return blockId;
		}
		else if ((oldId >> 4) == RSREPEATER_ON)
		{
			struct BlockIter_t iter = *iterator;
			uint8_t dir = repeaterFrom[blockId & 3] ^ 2;
			/* not yet updated */
			iter.blockIds[iter.offset] = blockId >> 4;
			mapIter(&iter, relx[dir], 0, relz[dir]);
			mapUpdateRedstone(map, &iter, POW_NONE, dir);
		}
		break;
	case RSREPEATER_ON:
		if (! redstoneIsPowered(*iterator, repeaterFrom[blockId & 3]))
		{
			updateAdd(iterator, ID(RSREPEATER_OFF, blockId & 15), redstoneRepeaterDelay(blockId));
			return blockId;
		}
		else if ((oldId >> 4) == RSREPEATER_OFF)
		{
			struct BlockIter_t iter = *iterator;
			uint8_t dir = repeaterFrom[blockId & 3] ^ 2;
			/* not yet updated */
			iter.blockIds[iter.offset] = blockId >> 4;
			mapIter(&iter, relx[dir], 0, relz[dir]);
			mapUpdateRedstone(map, &iter, POW_STRONG, dir);
		}
		break;
	default:
		switch (b->special) {
		case BLOCK_DOOR:
		case BLOCK_TRAPDOOR:
		case BLOCK_FENCE: /* gate */
			// TODO
			break;
		}
	}
	return blockId;
}

/*
 * link chunks that have been modified in the update
 */
static void mapUpdateListChunk(Map map)
{
	ChunkData * first = &map->dirty;
	Chunk *     save  = &map->needSave;
	ChunkData   cd, next;
	Chunk       list;

	*track.list = NULL;

	for (cd = *first; cd; first = &cd->update, cd = *first);
	for (list = *save; list; save = &list->save, list = *save);

	for (cd = track.modif; cd; cd = next)
	{
		uint8_t slots = cd->slot >> 1, i;
		int     layer = cd->Y >> 4;
		Chunk   c     = cd->chunk;
		*first = cd;
		first = &cd->update;
		next = *first;

		if ((c->cflags & CFLAG_NEEDSAVE) == 0)
		{
			*save = c;
			save = &c->save;
			c->cflags |= CFLAG_NEEDSAVE;
		}

		/* check for nearby chunks (S, E, N, W) */
		for (i = 1, slots = (cd->slot >> 1) & 15; slots; i <<= 1, slots >>= 1)
		{
			if ((slots & 1) == 0) continue;
			Chunk     chunk = c + chunkNeighbor[c->neighbor + i];
			ChunkData nbor  = chunk->layer[layer];
			if (nbor && nbor->slot == 0)
			{
				*first = nbor;
				first = &nbor->update;
				nbor->slot = 1;
			}
		}
		/* top */
		slots = cd->slot;
		if ((slots & 32) && layer < CHUNK_LIMIT)
		{
			ChunkData top = c->layer[layer+1];
			if (top && top->slot == 0)
			{
				*first = top;
				first = &top->update;
				top->slot = 1;
			}
		}
		/* bottom */
		if ((slots & 128) && layer > 0)
		{
			ChunkData bottom = c->layer[layer-1];
			if (bottom && bottom->slot == 0)
			{
				*first = bottom;
				first = &bottom->update;
				bottom->slot = 1;
			}
		}
	}
	*first = NULL;
	*save = NULL;
	track.modif = NULL;
}

/*
 * generic block update function: dispatch to various other functions of this module.
 */

static void mapUpdateMesh(Map map)
{
	ChunkData cd, next;
	track.pos = 0;
	for (cd = map->dirty; cd; cd = next)
	{
		cd->slot = 0;
		next = cd->update;
		//fprintf(stderr, "updating chunk %d, %d, %d\n", cd->chunk->X, cd->Y, cd->chunk->Z);
		renderInitBuffer(cd);
		chunkUpdate(cd->chunk, map->air, cd->Y >> 4, renderFlush);
		renderFinishMesh();
		particlesChunkUpdate(map, cd);
		if (cd->pendingDel)
			/* link within chunk has already been removed in chunkUpdate() */
			free(cd);
		else
			cd->update = NULL;
		track.pos ++;
	}
	map->dirty = NULL;
	fprintf(stderr, "%d chunk updated, max: %d, usage: %d\n", track.pos, track.max, track.maxUsage);
}

/* async update: NBT tables need to be updates before we can apply these */
static void mapUpdateFlush(Map map)
{
	BlockUpdate update;
	int         i, j;
	for (i = track.updateCount, update = track.updates, j = 0; i > 0; j ++, update ++)
	{
		if (track.updateUsage[j>>5] & (1 << (j & 31)))
		{
			int   offset = update->offset;
			Chunk c = update->cd->chunk;
			Block b = &blockIds[update->cd->blockIds[offset]];
			vec4  pos;
			track.updateUsage[j>>5] ^= 1 << (j & 31);
			track.updateCount --;
			pos[0] = c->X + (offset & 15); offset >>= 4;
			pos[2] = c->Z + (offset & 15);
			pos[1] = update->cd->Y + (offset >> 4);
			if (update->blockId == 0xffff)
			{
				struct BlockIter_t iter;
				mapInitIterOffset(&iter, update->cd, update->offset);
				offset = getBlockId(&iter);
				mapUpdateIfPowered(map, &iter, offset, offset);
				fprintf(stderr, "updating block %s at %g,%g,%g\n", b->name, pos[0], pos[1], pos[2]);
			}
			else
			{
				mapUpdate(map, pos, update->blockId, NULL, False);
			}
			i --;
		}
	}
}

void mapUpdate(Map map, vec4 pos, int blockId, DATA8 tile, Bool blockUpdate)
{
	struct BlockIter_t iter;

	mapInitIter(map, &iter, pos, blockId > 0);
	track.modif = NULL;
	track.list  = &track.modif;

	int     oldId = iter.blockIds[iter.offset] << 4;
	int     coord = DATA_OFFSET + (iter.offset >> 1);
	uint8_t meta  = iter.blockIds[coord];
	int     XYZ[] = {iter.x, iter.yabs, iter.z};

	/* this needs to be done before tables are updated */
	if (blockId != oldId && redstonePropagate(oldId))
	{
		mapUpdateDeleteSignal(&iter, blockId);
	}

	if (blockIds[blockId >> 4].rsupdate)
	{
		blockId = mapUpdateIfPowered(map, &iter, oldId, blockId);
	}

	/* update blockId/metaData tables */
	iter.blockIds[iter.offset] = blockId >> 4;
	if (iter.offset & 1) oldId |= meta >> 4, meta &= 0x0f, meta |= (blockId & 0xf) << 4;
	else                 oldId |= meta & 15, meta &= 0xf0, meta |= (blockId & 0xf);
	iter.blockIds[coord] = meta;

	/* update skyLight */
	{
		uint8_t newSkyOpac = blockGetSkyOpacity(blockId>>4, 0);
		uint8_t oldSkyOpac = blockGetSkyOpacity(oldId>>4, 0);
		if (newSkyOpac != oldSkyOpac)
		{
			if (newSkyOpac > 0)
				mapUpdateSkyLightBlock(&iter);
			else
				mapUpdateSkyLightUnblock(&iter);
		}
	}

	/* update blockLight */
	mapUpdateBlockLight(map, &iter, oldId, blockId);

	if (iter.cd->slot == 0 && blockId != oldId)
	{
		/* not in update list: add it now */
		iter.cd->slot = 1;
		if (blockIds[(blockId == 0 ? oldId : blockId) >> 4].type != QUAD)
			iter.cd->slot |= (slots[iter.z] << 1) | (slots[iter.x] << 2) | (slots[iter.y] << 5);
		*track.list = iter.cd;
		track.list = &iter.cd->update;
	}

	/* redstone signal if any */
	if (redstonePropagate(blockId))
	{
		mapUpdatePropagateSignal(&iter);
	}

	/* list of sub-chunks that we need to update their mesh */
	mapUpdateListChunk(map);

	/* block replaced: check if there is a tile entity to delete */
	DATA8 oldTile = chunkDeleteTileEntity(iter.ref, XYZ);
	if (oldTile)
	{
		if (blockIds[oldId >> 4].special == BLOCK_SIGN)
			signDel(oldTile);

		if ((iter.ref->cflags & CFLAG_MARKMODIF) == 0)
			chunkMarkForUpdate(iter.ref);
	}

	if (tile)
	{
		/* update position */
		chunkUpdateTilePosition(iter.ref, XYZ, tile);
		chunkAddTileEntity(iter.ref, XYZ, tile);
	}

	/* trigger a block update, it might call mapUpdate recursively */
	if (blockUpdate)
	{
		/* update nearby block if needed */
		mapUpdateBlock(map, pos, blockId, oldId, tile);

		/* updates triggered by previous block updates */
		if (track.updateCount > 0)
		{
			mapUpdateFlush(map);
		}
		/* update mesh */
		mapUpdateMesh(map);
		renderPointToBlock(-1, -1);
	}

	if (blockId == 0)
		particlesExplode(map, 4, oldId, pos);
}

/* high level function: dispatch to specialized module */
void mapActivate(Map map, vec4 pos)
{
	struct BlockIter_t iter;

	track.modif = NULL;
	track.list  = &track.modif;

	mapInitIter(map, &iter, pos, False);

	int block = iter.blockIds[iter.offset] << 4;
	int data  = iter.blockIds[DATA_OFFSET + (iter.offset >> 1)];

	if (iter.offset & 1) block |= data >> 4;
	else                 block |= data & 15;

	if (mapActivateBlock(map, pos, block, &iter))
	{
		*track.list = iter.cd;
		track.list = &iter.cd->update;
		mapUpdateListChunk(map);
		mapUpdateMesh(map);
		renderPointToBlock(-1, -1);
	}
}
