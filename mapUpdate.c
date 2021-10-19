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
#include "mapUpdate.h"
#include "blockupdate.h"
#include "blocks.h"
#include "render.h"
#include "sign.h"
#include "particles.h"
#include "redstone.h"
#include "entities.h"
#include "NBT2.h"

/* order is S, E, N, W, T, B ({xyz}off last slot is to get back to starting pos) */
int8_t xoff[] = {0,  1, -1, -1, 1,  0, 0};
int8_t zoff[] = {1, -1, -1,  1, 0,  0, 0};
int8_t yoff[] = {0,  0,  0,  0, 1, -2, 1};
int8_t relx[] = {0,  1,  0, -1, 0,  0};
int8_t rely[] = {0,  0,  0,  0, 1, -1};
int8_t relz[] = {1,  0, -1,  0, 0,  0};
int8_t opp[]  = {2,  3,  0,  1, 5,  4};


extern uint8_t  slotsXZ[]; /* from chunks.c */
extern uint8_t  slotsY[];
extern uint8_t  updateChunk[];
extern uint8_t  updateLength[];
extern uint16_t updateMore[];
extern uint8_t  updateChunks[];

/* track iteratively blocks that needs change for blocklight/skylight */
static struct MapUpdate_t track;

extern struct BlockSides_t blockSides;

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
	iter->cd   = cd = 0 <= layer && layer < CHUNK_LIMIT ? ref->layer[layer] : NULL;
	iter->y    = y & 15;
	iter->yabs = y;
	iter->x    = floorf(pos[0]) - ref->X;
	iter->z    = floorf(pos[2]) - ref->Z;
	iter->nbor = map->chunkOffsets;

	iter->alloc    = autoAlloc;
	iter->offset   = CHUNK_BLOCK_POS(iter->x, iter->z, y&15);
	iter->blockIds = cd ? cd->blockIds : NULL;

	if (cd == NULL)
	{
		if (autoAlloc && 0 <= layer && layer < CHUNK_LIMIT)
			iter->cd = cd = chunkCreateEmpty(ref, layer), renderResetFrustum();
		else /* iterator around a block in mapUpdate.c/blockUpdate.c might go one block above or below */
			iter->cd = cd = chunkAir;
		iter->blockIds = cd->blockIds;
	}
}

/* iterate from a ChunkData and offset */
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
	iter->nbor     = chunkNeighbor;
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
			ref += iter->nbor[ref->neighbor+8];
			pos += 16;
		} while (pos < 0);
	}
	else if (pos > 15)
	{
		do {
			ref += iter->nbor[ref->neighbor+2];
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
			ref += iter->nbor[ref->neighbor+4];
			pos += 16;
		} while (pos < 0);
	}
	else if (pos > 15)
	{
		do {
			ref += iter->nbor[ref->neighbor+1];
			pos -= 16;
		} while (pos > 15);
	}
	iter->z = pos;
	iter->yabs += dy;
	off += pos<<4;

	/* iterate over y axis */
	ChunkData cd = (iter->yabs>>4) < CHUNK_LIMIT ? ref->layer[iter->yabs>>4] : NULL;

	if (cd == NULL)
	{
		/* XXX need to check if above or below build limit */
		if (iter->alloc)
			cd = chunkCreateEmpty(ref, iter->yabs>>4), renderResetFrustum();
		else
			cd = chunkAir;
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
	if (table == SKYLIGHT_OFFSET || table == BLOCKLIGHT_OFFSET)
	{
		/* entity light in this chunk needs to be updated */
		iter->cd->cdFlags |= CDFLAG_UPDATENEARBY;
		iter->ref->cflags |= CFLAG_ETTLIGHT;
	}

//	if (table == DATA_OFFSET)
//		fprintf(stderr, "setting data %d, %d, %d to %d\n", iter->ref->X + iter->x, iter->yabs, iter->ref->Z + iter->z, val);

	/* track which side it is near to (we might have to update nearby chunk too) */
	cd->slot |= (slotsXZ[(iter->z<<4) | iter->x] | slotsY[iter->y]) << 1;
}

static uint8_t mapGetSky(BlockIter iter)
{
	int     off = iter->offset;
	uint8_t sky = iter->blockIds[SKYLIGHT_OFFSET + (off >> 1)];
	if (off & 1) return sky >> 4;
	else         return sky & 15;
}

static uint8_t mapGetLight(BlockIter iter)
{
	int     off   = iter->offset;
	uint8_t light = iter->blockIds[BLOCKLIGHT_OFFSET + (off >> 1)];
	if (off & 1) return light >> 4;
	else         return light & 15;
}

/* get both combined: 4 most significant bits = sky, 4 lowest = block light */
uint8_t mapGetSkyBlockLight(BlockIter iter)
{
	int     off   = iter->offset;
	uint8_t sky   = iter->blockIds[SKYLIGHT_OFFSET + (off >> 1)];
	uint8_t light = iter->blockIds[BLOCKLIGHT_OFFSET + (off >> 1)];

	if (off & 1) return (sky & 0xf0) | (light >> 4);
	else         return ((sky & 15) << 4) | (light & 15);
}

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

/* will prevent use of recursion (mapUpdate) */
static void trackAddUpdate(BlockIter iter, int blockId, DATA8 tile)
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

	/* redstone need a 2 pass system */
	BlockUpdate update = track.updates + mapFirstFree(track.updateUsage, max);

	track.updateCount ++;
	update->cd = iter->cd;
	update->offset = iter->offset;
	update->blockId = blockId;
	update->tile = tile;

	//fprintf(stderr, "adding update at %d,%d,%d\n", iter->ref->X + iter->x, iter->yabs, iter->ref->Z + iter->z);
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
	int    i, height;

	sky = mapGetSky(iterator) - blockGetSkyOpacity(iter.blockIds[iter.offset], 0);
	mapUpdateInitTrack(track);
	mapUpdateTable(iterator, sky < 0 ? 0 : sky, SKYLIGHT_OFFSET);
	track.unique = 1;

	i = CHUNK_BLOCK_POS(iter.x, iter.z, 0);
	height = iter.ref->heightMap[i];
	if (height < iter.yabs+1)
	{
		/* block is higher than heightmap (or at the same level) */
		int j;
		sky = mapGetSky(&iter) - blockGetSkyOpacity(iter.blockIds[iter.offset], 0);
		/* block is not blocking sky (ie: glass) */
		if (sky == MAXSKY) return;
		if (sky < 0) sky = 0;
		mapUpdateTable(&iter, sky, SKYLIGHT_OFFSET);
		/* block is set at a higher position */
		for (j = iter.yabs - (sky == 0); j >= height; j --)
		{
			trackAdd(MAXSKY | (4<<5), j - iter.yabs, 0);
		}
		iter.ref->heightMap[i] = iter.yabs + 1;
	}
	else /* block is lower or equal to heightmap */
	{
		mapIter(&iter, 0, -1, 0);
		if (blockGetSkyOpacity(iter.blockIds[iter.offset], 0) < MAXSKY)
			trackAdd(MAXSKY | (4<<5), -1, 0);

		mapIter(&iter, 0, 2, 0);
		if (iter.yabs < height && blockGetSkyOpacity(iter.blockIds[iter.offset], 0) < MAXSKY)
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
			trackAdd(MAXSKY + relx[i] + (opp[i] << 5), 0, relz[i]);
		}
	}
	iter.alloc = 1;
	/* get iterator back to original pos */
	mapIter(&iter, 1, 0, 0);

	while (track.usage > 0)
	{
		struct BlockIter_t initial;
		struct BlockIter_t neighbor = iter;
		int8_t XYZ[3], dir;

		memcpy(XYZ, track.coord + track.pos, 3);
		dir = (XYZ[0] >> 5) & 7;

		mapIter(&neighbor, (XYZ[0] & 31) - MAXSKY, XYZ[1], XYZ[2]);
		initial = neighbor;
		sky = mapGetSky(&neighbor);

		/* is it a local maximum? */
		for (i = max = 0; i < 6; i ++)
		{
			mapIter(&neighbor, xoff[i], yoff[i], zoff[i]);

			level = mapGetSky(&neighbor);
			if (i >= 4) level --;

			if (level >= sky && max < level)
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
			for (i = 0; i < 6; i ++)
			{
				mapIter(&neighbor, xoff[i], yoff[i], zoff[i]);
				int8_t min = old - blockGetSkyOpacity(neighbor.blockIds[neighbor.offset], 1);
				level = mapGetSky(&neighbor);

				if (level < min)
				{
					mapUpdateTable(&neighbor, min, SKYLIGHT_OFFSET);
					trackAdd((XYZ[0]&31) + relx[i] + (opp[i] << 5), XYZ[1] + rely[i], XYZ[2] + relz[i]);
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
		for (i = 0, neighbor = initial; i < 6; i ++)
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
				trackAdd((XYZ[0]&31) + relx[i] + (opp[i] << 5), XYZ[1] + rely[i], XYZ[2] + relz[i]);
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
		int transp = mapGetSky(&iter) == MAXSKY;
		while (iter.yabs >= 0 && blockGetSkyOpacity(iter.blockIds[iter.offset], 0) == 0)
		{
			mapUpdateTable(&iter, MAXSKY, SKYLIGHT_OFFSET);
			trackAdd(0, iter.yabs - startY, 0);
			mapIter(&iter, 0, -1, 0);
		}
		iter.ref->heightMap[i] = iter.yabs+1;
		/* check if a transparent block has been removed */
		if (transp) return;
		iter = *iterator;
	}
	else
	{
		uint8_t max;
		/* look around for the highest skylight value */
		for (i = max = 0; i < 6; i ++)
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
		int8_t XYZ[3];
		uint8_t sky;
		memcpy(XYZ, track.coord + track.pos, 3);

		mapIter(&neighbor, XYZ[0], XYZ[1], XYZ[2]);
		sky = mapGetSky(&neighbor);

		for (i = 0; i < 6; i ++)
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

/* only restore the sky light for the block pointed by <iter> */
static void mapUpdateRestoreSky(BlockIter iterator)
{
	struct BlockIter_t iter = *iterator;

	mapIter(&iter, 0, 1, 0);
	uint8_t sky = mapGetSky(&iter);
	if (0 < sky && sky < MAXSKY) sky --;
	mapUpdateTable(iterator, sky, SKYLIGHT_OFFSET);
	DATA32 height = &iter.ref->heightMap[CHUNK_BLOCK_POS(iter.x, iter.z, 0)];
	if (sky == MAXSKY && height[0] == iter.yabs)
		height[0] --;
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
		int8_t XYZ[3], level, i, dim;

		memcpy(XYZ, track.coord + track.pos, 3);
		mapIter(&neighbor, XYZ[0], XYZ[1], XYZ[2]);
		level = mapGetLight(&neighbor);

		for (i = 0; i < 6; i ++)
		{
			mapIter(&neighbor, xoff[i], yoff[i], zoff[i]);

			dim = blockGetLightOpacity(neighbor.blockIds[neighbor.offset], 1);
			if (dim < MAXLIGHT && mapGetLight(&neighbor) < level - dim)
			{
				if (level > 1)
					trackAdd(XYZ[0] + relx[i], XYZ[1] + rely[i], XYZ[2] + relz[i]);

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

		int8_t XYZ[3], level, max, i, dir, equal;

		memcpy(XYZ, track.coord + track.pos, 3);
		mapIter(&neighbor, XYZ[0], XYZ[1], XYZ[2]);
		level = mapGetLight(&neighbor);

		for (i = max = equal = 0; i < 6; i ++)
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
		for (i = 0; i < 6; i ++)
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

static void mapUpdateObstructLight(struct BlockIter_t iter)
{
	int8_t light;

	mapUpdateInitTrack(track);
	trackAdd(0, 0, 0);
	light = mapGetLight(&iter);
	if (light <= 1) return;
	light -= blockGetLightOpacity(iter.blockIds[iter.offset], 0);
	mapUpdateTable(&iter, light < 0 ? 0 : light, BLOCKLIGHT_OFFSET);

	while (track.usage > 0)
	{
		struct BlockIter_t neighbor = iter;
		int8_t XYZ[3], i, dim, max, k;

		memcpy(XYZ, track.coord + track.pos, 3);
		mapIter(&neighbor, XYZ[0], XYZ[1], XYZ[2]);

		/* check surrounding blocks if light levels are correct */
		for (i = 0; i < 6; i ++)
		{
			mapIter(&neighbor, xoff[i], yoff[i], zoff[i]);

			/* check if there is a light with a higher level than the one being deleted */
			int block = neighbor.blockIds[neighbor.offset];
			light = mapGetLight(&neighbor);
			dim   = blockIds[block].emitLight;
			if (dim > 0 && dim <= light) continue;
			dim   = blockGetLightOpacity(block, 1);
			if (dim < MAXLIGHT)
			{
				struct BlockIter_t depend = neighbor;
				/* check if there is a block light == light + dim surrounding this pos */
				for (k = max = 0; k < 6; k ++)
				{
					/* this means that deleted light was not a local maximum */
					mapIter(&depend, xoff[k], yoff[k], zoff[k]);
					if (blockGetLightOpacity(depend.blockIds[depend.offset], 0) == MAXLIGHT) continue;
					uint8_t light2 = mapGetLight(&depend);
					if (light2 == light + dim) break;
					if (max < light2) max = light2;
				}
				/* nope, must readjust light levels around here */
				if (k == 6)
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

static void mapUpdateRestoreLight(struct BlockIter_t iter)
{
	int i, max = mapGetLight(&iter);
	for (i = max = 0; i < 6; i ++)
	{
		mapIter(&iter, xoff[i], yoff[i], zoff[i]);
		uint8_t light = mapGetLight(&iter);
		if (max < light)
			max = light;
	}
	/* back to origin */
	mapIter(&iter, 0, 1, 0);
	if (max > 0)
		mapUpdateAddLight(&iter, max-blockGetLightOpacity(iter.blockIds[iter.offset], 1));
	else
		mapUpdateTable(&iter, 0, BLOCKLIGHT_OFFSET);
}

static Bool mapUpdateIsLocalMax(struct BlockIter_t iter, int light)
{
	int i;
	for (i = 0; i < 6; i ++)
	{
		mapIter(&iter, xoff[i], yoff[i], zoff[i]);
		if (mapGetLight(&iter) > light) return False;
	}
	return True;
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
		uint8_t opac = blockIds[newId>>4].opacLight;
		uint8_t light = mapGetLight(iter);
		if (opac == blockIds[oldId>>4].opacLight)
		{
			/* block replace with same light emittance and same opacity: nothing is changed */
			return;
		}
		else if (light == MAXLIGHT || (light > 0 && mapUpdateIsLocalMax(*iter, light)))
		{
			/* a light has been removed */
			mapUpdateRemLight(iter);
		}
		else if (light > 0)
		{
			mapUpdateObstructLight(*iter);
		}
		else mapUpdateRestoreLight(*iter);
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
	Block b = &blockIds[iter.blockIds[iter.offset]];
	if (b->rsupdate & RSUPDATE_RECV)
		trackAddUpdate(&iter, 0xffff, NULL);

	/* only check the where the update is, if power is weak */
	if (cnx->pow != POW_WEAK && cnx->signal == RSUPDATE)
	{
		for (i = 0; i < 6; i ++)
		{
			/* we cannot perform the update yet, we need the signal to be updated all the way :-/ */
			mapIter(&iter, xoff[i], yoff[i], zoff[i]);
			int id = getBlockId(&iter);
			Block neighbor = &blockIds[id>>4];
			switch (neighbor->orientHint) {
			case ORIENT_TORCH:
				if (blockSides.torch[id&7] != opp[i]) continue;
				break;
			case ORIENT_SWNE: /* repeater */
				if (i > 4 || blockSides.repeater[id&3] != opp[i]) continue;
			}
			if (neighbor->rsupdate & RSUPDATE_RECV)
				trackAddUpdate(&iter, 0xffff, NULL);
		}
	}
}

/*
 * redstone propagation is very similar to blockLight, but with a lot more rules
 * to check which block to connect to.
 */
static void mapUpdatePropagateSignal(BlockIter iterator)
{
	struct RSWire_t connectTo[RSMAXUPDATE];
	int count, i, signal;

	mapUpdateInitTrack(track);
	track.unique = False;
	signal = redstoneSignalStrength(iterator, True);
	count = redstoneConnectTo(*iterator, connectTo);
	if (iterator->blockIds[iterator->offset] == RSWIRE)
		mapUpdateTable(iterator, signal, DATA_OFFSET);

	for (i = 0; i < count; i ++)
	{
		RSWire cnx = connectTo + i;
		if (cnx->signal == RSUPDATE || cnx->blockId != RSWIRE)
		{
			mapUpdateAddRSUpdate(iterator, cnx);
		}
		else if (cnx->signal < signal - 1)
		{
			struct BlockIter_t iter = *iterator;
			mapIter(&iter, cnx->dx, cnx->dy, cnx->dz);
			mapUpdateTable(&iter, signal-1, DATA_OFFSET);
			trackAdd(cnx->dx, cnx->dy, cnx->dz);
		}
	}

	while (track.usage > 0)
	{
		struct BlockIter_t neighbor = *iterator;
		int8_t XYZ[3];

		memcpy(XYZ, track.coord + track.pos, 3);
		mapIter(&neighbor, XYZ[0], XYZ[1], XYZ[2]);

		signal = redstoneSignalStrength(&neighbor, False);
		count = redstoneConnectTo(neighbor, connectTo);
		for (i = 0; i < count; i ++)
		{
			RSWire cnx = connectTo + i;
			if (cnx->signal == RSUPDATE || cnx->blockId != RSWIRE)
			{
				mapUpdateAddRSUpdate(&neighbor, cnx);
			}
			else if (cnx->signal < signal - 1)
			{
				struct BlockIter_t iter = neighbor;
				mapIter(&iter, cnx->dx, cnx->dy, cnx->dz);
				mapUpdateTable(&iter, signal-1, DATA_OFFSET);
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
	struct RSWire_t connectTo[RSMAXUPDATE];
	int i, count, signal, block;

	/* block must not be deleted at this point */
	mapUpdateInitTrack(track);
	track.unique = True;

	if (blockId >= 0)
	{
		count = redstoneConnectTo(*iterator, connectTo);

		for (i = 0; i < count; i ++)
		{
			RSWire cnx = connectTo + i;
			if (cnx->signal == RSUPDATE || cnx->blockId != RSWIRE)
				mapUpdateAddRSUpdate(iterator, cnx);
			else
				trackAdd(cnx->dx, cnx->dy, cnx->dz);
		}
		/* now we can delete the block */
		iterator->blockIds[iterator->offset] = blockId >> 4;
		mapUpdateTable(iterator, blockId & 15, DATA_OFFSET);
	}
	else trackAdd(0, 0, 0);

	while (track.usage > 0)
	{
		struct BlockIter_t neighbor = *iterator;
		int8_t XYZ[3], max, dir, equal, level;

		memcpy(XYZ, track.coord + track.pos, 3);
		mapIter(&neighbor, XYZ[0], XYZ[1], XYZ[2]);
		track.pos += 3;
		track.usage -= 3;
		if (track.pos == track.max) track.pos = 0;

		level = redstoneSignalStrength(&neighbor, False);
		count = redstoneConnectTo(neighbor, connectTo);
		block = neighbor.blockIds[neighbor.offset];

		for (i = max = equal = 0; i < count; i ++)
		{
			RSWire cnx = connectTo + i;

			signal = cnx->signal;

			if (signal == RSUPDATE || cnx->blockId != RSWIRE)
			{
				mapUpdateAddRSUpdate(&neighbor, cnx);
				continue;
			}
			if (level < signal && max < signal)
				max = signal;
			if (level <= signal && equal <= signal)
				equal = signal;
		}
		if (level == MAXSIGNAL)
		{
			/* can be powered by a nearby block */
			for (i = 0; i < 6; i ++)
			{
				if (redstoneIsPowered(neighbor, i, POW_STRONG))
				{
					max = MAXSIGNAL+1;
					break;
				}
			}
		}

		dir = 0;
		if (max > 0)
		{
			/* not a local maximum */
			int sig = max - 1;
			if (sig < 0) sig = 0;
			level = sig;
			if (block == RSWIRE)
				mapUpdateTable(&neighbor, sig, DATA_OFFSET);

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
				mapUpdateTable(&neighbor, equal, DATA_OFFSET);

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

static int mapUpdateIfPowered(Map map, BlockIter iterator, int oldId, int blockId, Bool init, DATA8 * tile);

/* redstone power level has been updated in a block, check nearby what updates it will trigger */
static void mapUpdateChangeRedstone(Map map, BlockIter iterator, int side, RSWire dir)
{
	struct BlockIter_t iter = *iterator;

	if (dir)
		mapIter(&iter, dir->dx, dir->dy, dir->dz);
	else if (side != RSSAMEBLOCK)
		mapIter(&iter, relx[side], rely[side], relz[side]);

	Block b = &blockIds[iter.blockIds[iter.offset]];
	int   i, count, flags;

	if (b->type == SOLID || b->id == 0)
	{
		flags = 127 ^ (side == RSSAMEBLOCK ? 1 : 1 << (opp[side]+1));
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
			mapIter(&iter, xoff[i], yoff[i], zoff[i]);
		if ((flags & 1) == 0) continue;

		int blockId = getBlockId(&iter);
		if (blockIds[blockId>>4].rsupdate)
		{
			int newId = mapUpdateIfPowered(map, &iter, blockId, blockId, False, NULL);
			if (newId != blockId)
			{
				vec4 pos = {iter.ref->X + iter.x, iter.yabs, iter.ref->Z + iter.z};
				mapUpdate(map, pos, newId, NULL, False);
			}
		}
	}
}

/* blockId is about to be deleted or replaced, check if we need to update redstone signal before */
static void mapUpdateDeleteRedstone(Map map, BlockIter iterator, int blockId)
{
	Block b;
	switch (blockId >> 4) {
	case RSWIRE:
		if ((blockId & 15) == 0) return;
		// no break;
	case RSTORCH_ON:
	case RSBLOCK:
		mapUpdateDeleteSignal(iterator, 0);
		mapUpdateChangeRedstone(map, iterator, RSSAMEBLOCK, NULL);
		break;
	case RSREPEATER_ON:
		iterator->blockIds[iterator->offset] = 0;
		mapUpdateChangeRedstone(map, iterator, blockSides.repeater[blockId & 3] ^ 2, NULL);
		break;
	case RSPOWERRAILS:
		if (blockId & 8)
		{
			/* powered and about to be deleted */
			mapUpdateDeleteRails(map, iterator, blockId);
		}
		break;
	default:
		/* this code will be executed whenever a solid block is deleted :-/ */
		b = &blockIds[blockId >> 4];
		if (b->type == SOLID)
		{
			/* check for a wire nearby (any other block will be deleted through block update) */
			struct BlockIter_t iter = *iterator;
			uint8_t i;
			/* first, quickly check if it can trigger updates */
			for (i = 0; i < 6; i ++)
			{
				mapIter(&iter, xoff[i], yoff[i], zoff[i]);
				if (iter.blockIds[iter.offset] == RSWIRE) break;
			}
			/* extremely likely there are no wire nearby (ie: i == 6) */
			if (i < 6 && redstoneIsPowered(*iterator, RSSAMEBLOCK, POW_STRONG))
			{
				iterator->blockIds[iterator->offset] = 0;
				/* this block was powered */
				for ( ; i < 6; i ++, mapIter(&iter, xoff[i], yoff[i], zoff[i]))
				{
					if (i != 4 && iter.blockIds[iter.offset] == RSWIRE) /* block on top, will be deleted with block update */
						mapUpdateDeleteSignal(&iter, -1);
				}
			}
		}
		else if (b->orientHint == ORIENT_LEVER && (blockId & 8))
		{
			iterator->blockIds[iterator->offset] = 0;
			mapUpdateChangeRedstone(map, iterator, blockSides.lever[blockId&7], NULL);
		}
	}
}

static void mapUpdateConnected(Map map, BlockIter iterator, int blockId)
{
	struct RSWire_t connect[RSMAXUPDATE];
	int i, count;
	iterator->blockIds[iterator->offset] = blockId >> 4;
	count = redstoneConnectTo(*iterator, connect);
	mapUpdateTable(iterator, blockId & 15, DATA_OFFSET);
	for (i = 0; i < count; i ++)
		mapUpdateChangeRedstone(map, iterator, 0, connect + i);
}

/* check if a block is powered nearby, change current block to active state then */
static int mapUpdateIfPowered(Map map, BlockIter iterator, int oldId, int blockId, Bool init, DATA8 * tile)
{
	Block b = &blockIds[blockId >> 4];

	switch (b->id) {
	case RSNOTEBLOCK:
		/* no states to track power level change? */
		break;
	case RSSTICKYPISTON:
	case RSPISTON:
		return mapUpdatePiston(map, iterator, blockId, init, tile);
	case RSDISPENSER:
	case RSDROPPER:
		/* very similar to gence gate actually */
		return mapUpdateGate(iterator, blockId, init);
	case RSPOWERRAILS:
		return mapUpdatePowerRails(map, blockId, iterator);
	case RSRAILS:
		return init ? blockId : mapUpdateRails(map, blockId, iterator);
	case RSLAMP:
		if (redstoneIsPowered(*iterator, RSSAMEBLOCK, POW_NORMAL))
			return ID(RSLAMP+1, 0);
		break;
	case  RSLAMP+1:
		if (! redstoneIsPowered(*iterator, RSSAMEBLOCK, POW_NORMAL))
			return ID(RSLAMP, 0);
		break;
	case RSTORCH_OFF:
		if (! redstoneIsPowered(*iterator, blockSides.torch[blockId & 7], POW_NORMAL))
		{
			if (! init)
			{
				updateAdd(iterator, ID(RSTORCH_ON, blockId & 15), 1);
				return blockId;
			}
			/* will prevent unnecessary block update */
			return ID(RSTORCH_ON, blockId & 15);
		}
		else if ((oldId >> 4) == RSTORCH_ON)
		{
			/* torch went off: adjust power levels to blocks connected */
			mapUpdateConnected(map, iterator, blockId);
		}
		break;
	case RSTORCH_ON:
		if (redstoneIsPowered(*iterator, blockSides.torch[blockId & 7], POW_NORMAL))
		{
			if (! init)
			{
				updateAdd(iterator, ID(RSTORCH_OFF, blockId & 15), 1);
				return blockId;
			}
			return ID(RSTORCH_OFF, blockId & 15);
		}
		else if ((oldId >> 4) == RSTORCH_OFF)
		{
			/* torch went on */
			mapUpdateConnected(map, iterator, blockId);
		}
		break;
	case RSREPEATER_OFF:
		if (redstoneIsPowered(*iterator, blockSides.repeater[blockId & 3], POW_NORMAL))
		{
			/* need to be updated later */
			updateAdd(iterator, ID(RSREPEATER_ON, blockId & 15), redstoneRepeaterDelay(blockId));
			return blockId;
		}
		else if ((oldId >> 4) == RSREPEATER_ON)
		{
			/* not yet updated */
			iterator->blockIds[iterator->offset] = blockId >> 4;
			/* repeater went off */
			mapUpdateChangeRedstone(map, iterator, blockSides.repeater[blockId & 3] ^ 2, NULL);
		}
		break;
	case RSREPEATER_ON:
		if (! redstoneIsPowered(*iterator, blockSides.repeater[blockId & 3], POW_NORMAL))
		{
			updateAdd(iterator, ID(RSREPEATER_OFF, blockId & 15), redstoneRepeaterDelay(blockId));
			return blockId;
		}
		else if ((oldId >> 4) == RSREPEATER_OFF)
		{
			iterator->blockIds[iterator->offset] = blockId >> 4;
			/* repeater went on */
			mapUpdateChangeRedstone(map, iterator, blockSides.repeater[blockId & 3] ^ 2, NULL);
		}
		break;
	case RSWIRE:
		if (init)
			iterator->blockIds[iterator->offset] = blockId >> 4;
		return ID(RSWIRE, redstoneSignalStrength(iterator, True));
	default:
		if (b->orientHint == ORIENT_LEVER)
		{
			/* buttons & lever */
			if ((oldId >> 4) == (blockId >> 4))
				mapUpdateConnected(map, iterator, blockId);
		}
		else switch (b->special) {
		case BLOCK_DOOR:
			return mapUpdateDoor(iterator, blockId, init);
		case BLOCK_TRAPDOOR:
		case BLOCK_FENCEGATE:
			if ((oldId >> 4) == (blockId >> 4))
				return mapUpdateGate(iterator, blockId, init);
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
		Chunk c = cd->chunk;
		*first = cd;
		first = &cd->update;
		next = *first;
		if ((c->cflags & CFLAG_NEEDSAVE) == 0)
		{
			*save = c;
			save = &c->save;
			c->cflags |= CFLAG_NEEDSAVE;
		}
		if (c->cflags & CFLAG_ETTLIGHT)
		{
			if (c->entityList != ENTITY_END)
				entityUpdateLight(c);
			c->cflags &= ~CFLAG_ETTLIGHT;
		}

		if (cd->cdFlags & CDFLAG_UPDATENEARBY)
		{
			ChunkData nbor;
			int       slots = cd->slot >> 1;
			uint8_t   pos   = updateChunk[slots];
			uint8_t   len   = updateLength[slots];
			uint16_t  more  = 0;
			int       layer = cd->Y >> 4;

			cd->cdFlags &= ~CDFLAG_UPDATENEARBY;
			if (len > 31)
				more = updateMore[len >> 5], len &= 31;

			while (len > 0)
			{
				int   sides = updateChunks[pos];
				Chunk chunk = c + chunkNeighbor[c->neighbor + (sides & 15)];
				if (sides & 16) /* top */
				{
					nbor = layer + 1 < chunk->maxy ? chunk->layer[layer+1] : NULL;
				}
				else if (sides & 32) /* bottom */
				{
					nbor = layer > 0 ? chunk->layer[layer-1] : NULL;
				}
				else nbor = chunk->layer[layer];
				if (nbor && nbor->slot == 0)
				{
					*first = nbor;
					first = &nbor->update;
					nbor->slot = 1;
				}
				len --; pos ++;
				if (len == 0)
					/* sequence was broken in 2 */
					len = more >> 8, pos += more & 255, more = 0;
			}
		}
	}

	*first = NULL;
	*save = NULL;
	track.modif = NULL;
	track.list  = &track.modif;
}

/*
 * flood-fill for getting face connection, used by cave culling.
 * should belongs to chunks.c, but we need the resizable ring-buffer for this.
 */
int mapUpdateGetCnxGraph(ChunkData cd, int start, DATA8 visited)
{
	extern uint16_t faceCnx[]; /* from chunks.c */

	mapUpdateInitTrack(track);
	trackAdd(start & 15, start >> 8, (start >> 4) & 15);
	DATA8 blocks = cd->blockIds;
	int init = slotsXZ[start&0xff] | slotsY[start>>8];
	int cnx = faceCnx[init];

	while (track.usage > 0)
	{
		int8_t XYZ[3];
		uint8_t i, x, y, z;

		memcpy(XYZ, track.coord + track.pos, 3);
		track.pos += 3;
		track.usage -= 3;
		if (track.pos == track.max) track.pos = 0;

		for (i = 0; i < 6; i ++)
		{
			x = XYZ[0] + relx[i];
			y = XYZ[1] + rely[i];
			z = XYZ[2] + relz[i];

			/* clipping (not 100% portable, but who cares?) */
			if (x >= 16 || y >= 16 || z >= 16) continue;
			int   pos = CHUNK_BLOCK_POS(x, z, y);
			Block b = blockIds + blocks[pos];
			/* only fully opaque blocks will stop flood: we could be more precise, but not worth the time spent */
			if (! blockIsFullySolid(b) &&
				(visited[pos>>3] & mask8bit[pos&7]) == 0)
			{
				trackAdd(x, y, z);
				visited[pos>>3] |= mask8bit[pos&7];
				init |= slotsXZ[pos&0xff] | slotsY[pos>>8];
				cnx |= faceCnx[init];
			}
		}
	}

	return cnx;
}

/* extended selection: select all similar blocks within a 16x16x16 area */
void mapUpdateFloodFill(Map map, vec4 pos, uint8_t visited[4096], int8_t minMax[8])
{
	struct BlockIter_t iter;
	int8_t min[4] = {0, 0, 0};
	int8_t max[4] = {0, 0, 0};
	int    block;
	mapUpdateInitTrack(track);
	mapInitIter(map, &iter, pos, False);
	trackAdd(0, 0, 0);
	visited[0] |= mask8bit[0];
	block = getBlockId(&iter);

	while (track.usage > 0)
	{
		int8_t XYZ[3], i;

		memcpy(XYZ, track.coord + track.pos, 3);
		track.pos += 3;
		track.usage -= 3;
		if (track.pos == track.max) track.pos = 0;

		/* no more than 16x16x16 */
		for (i = 0; i < 3; i ++)
		{
			int8_t x = XYZ[i];
			if (x < min[i] || x > max[i])
			{
				if (max[i] - min[i] + 1 >= 32) continue;
				if (x < min[i]) min[i] = x;
				else            max[i] = x;
			}
		}

		struct BlockIter_t neighbor = iter;
		mapIter(&neighbor, XYZ[0], XYZ[1], XYZ[2]);
		for (i = 0; i < 6; i ++)
		{
			mapIter(&neighbor, xoff[i], yoff[i], zoff[i]);
			if (getBlockId(&neighbor) == block)
			{
				int8_t x = XYZ[0] + relx[i];
				int8_t y = XYZ[1] + rely[i];
				int8_t z = XYZ[2] + relz[i];
				/* that's why it is limited to 16x16x16: <visited> can only hold 4096 bits */
				int xzy = (x & 31) + (z & 31) * 32 + (y & 15) * 1024;
				if ((visited[xzy>>3] & mask8bit[xzy&7]) == 0)
				{
					visited[xzy>>3] |= mask8bit[xzy&7];
					trackAdd(x, y, z);
				}
			}
		}
	}

	memcpy(minMax,   min, 4);
	memcpy(minMax+4, max, 4);
}

/*
 * generic block update function: dispatch to various other functions of this module.
 */

void mapUpdateMesh(Map map)
{
	ChunkData cd, next;

	/* list of sub-chunks that we need to update their mesh */
	mapUpdateListChunk(map);
	#ifdef DEBUG
	int count = 0;
	#endif

	for (cd = map->dirty; cd; cd = next)
	{
		cd->slot = 0;
		next = cd->update;
		//fprintf(stderr, "updating chunk %d, %d, %d%s\n", cd->chunk->X, cd->Y, cd->chunk->Z, cd->cdFlags & CDFLAG_UPDATENEARBY ? " [NEARBY]" : "");
		chunkUpdate(cd->chunk, chunkAir, map->chunkOffsets, cd->Y >> 4);
		renderFinishMesh(map, True);
		particlesChunkUpdate(map, cd);
		if (cd->cdFlags == CDFLAG_PENDINGDEL)
			/* link within chunk has already been removed in chunkUpdate() */
			free(cd), renderResetFrustum();
		else
			cd->update = NULL;
		#ifdef DEBUG
		count ++;
		#endif
	}
	map->dirty = NULL;
	#ifdef DEBUG
	fprintf(stderr, "%d chunk updated, max: %d, usage: %d\n", count, track.max, track.maxUsage);
	#endif
}

/* async update: NBT tables need to be up to date before we can apply these changes */
void mapUpdateFlush(Map map)
{
	BlockUpdate update;
	int         i, j;
	for (i = track.updateCount, update = track.updates, j = 0; i > 0; j ++, update ++)
	{
		if (track.updateUsage[j>>5] & (1 << (j & 31)))
		{
			int   offset = update->offset;
			Chunk c = update->cd->chunk;
			vec4  pos;
			track.updateUsage[j>>5] ^= 1 << (j & 31);
			track.updateCount --;
			pos[0] = c->X + (offset & 15); offset >>= 4;
			pos[2] = c->Z + (offset & 15);
			pos[1] = update->cd->Y + (offset >> 4);
			if (update->blockId == 0xffff)
			{
				struct BlockIter_t iter;
				int newId;
				mapInitIterOffset(&iter, update->cd, update->offset);
				offset = getBlockId(&iter);
				newId  = mapUpdateIfPowered(map, &iter, offset, offset, False, NULL);
				if (offset != newId)
				{
					mapUpdate(map, pos, newId, NULL, False);
				}
			}
			else mapUpdate(map, pos, update->blockId, update->tile, UPDATE_SILENT);
			i --;
		}
	}
}

/* blocks moved by piston update can't be updated directly, they need to be done at once */
void mapUpdatePush(Map map, vec4 pos, int blockId, DATA8 tile)
{
	/* otherwise update order will depend on piston push direction: way too annoying */
	struct BlockIter_t iter;
	mapInitIter(map, &iter, pos, False);
	switch (blockId >> 4) {
	case RSPISTON:
	case RSSTICKYPISTON:
		/* XXX already set meta data to prevent calling mapUpdatePiston() again */
		mapUpdateTable(&iter, blockId & 15, DATA_OFFSET);
	}

	/* update must not overwrite each other */
	BlockUpdate update;
	int         i, j;
	for (i = track.updateCount, update = track.updates, j = 0; i > 0; j ++, update ++)
	{
		if ((track.updateUsage[j>>5] & (1 << (j & 31))) == 0) continue;
		if (update->offset == iter.offset && update->cd == iter.cd)
		{
			/* air blocks have lower priority */
			if (blockId > 0)
			{
				update->blockId = blockId;
				update->tile = tile;
			}
			//fprintf(stderr, "reusing block update %p:%d\n", iter.cd, iter.offset);
			return;
		}
		i --;
	}
	trackAddUpdate(&iter, blockId, tile);
}

/*
 * group multiple updates
 */
void mapUpdateInit(BlockIter iter)
{
	track.modif = NULL;
	track.list  = &track.modif;
	track.iter  = iter;
}

void mapUpdateEnd(Map map)
{
	if (track.updateCount > 0)
		mapUpdateFlush(map);

	/* update mesh */
	mapUpdateMesh(map);
	renderPointToBlock(-1, -1);
}

/*
 * main entry point for altering voxel tables and keep them consistent.
 */
void mapUpdate(Map map, vec4 pos, int blockId, DATA8 tile, int blockUpdate)
{
	struct BlockIter_t iter;
	uint8_t silent = blockUpdate & UPDATE_SILENT; /* no particles */
	uint8_t doLight = (blockUpdate & UPDATE_KEEPLIGHT) == 0;

	if (pos == NULL)
	{
		/* selection update: this will potentially modify a large number of blocks */
		iter = *track.iter;
	}
	else mapInitIter(map, &iter, pos, blockId > 0);

	blockUpdate &= 15;
	if (blockUpdate)
	{
		track.modif = NULL;
		track.list  = &track.modif;
	}

	if (iter.blockIds == NULL)
	{
		/* try to build above or below build limit */
		/* if (blockId == 0) */ return;
	}

	int oldId = iter.blockIds[iter.offset] << 4;
	int XYZ[] = {iter.x, iter.yabs, iter.z};
	DATA8 data = iter.blockIds + DATA_OFFSET + (iter.offset >> 1);
	Block b = &blockIds[blockId >> 4];

	if (b->updateNearby || (oldId > 0 && blockIds[oldId >> 4].updateNearby))
		iter.cd->cdFlags |= CDFLAG_UPDATENEARBY;

	if (iter.offset & 1) oldId |= *data >> 4;
	else                 oldId |= *data & 15;

	if (oldId == blockId)
		// XXX tile entity might differ
		return;

	/* this needs to be done before tables are updated */
	if (b->type != blockIds[oldId>>4].type)
		mapUpdateDeleteRedstone(map, &iter, oldId);

	if (b->rsupdate)
	{
		/* change block based on power level: will prevent unnecessary updates */
		blockId = mapUpdateIfPowered(map, &iter, oldId, blockId, True, &tile);
	}

	/* update blockId/metaData tables */
	//fprintf(stderr, "setting block %g, %g, %g to %d:%d\n", pos[0], pos[1], pos[2], blockId >> 4, blockId & 15);
	iter.blockIds[iter.offset] = blockId >> 4;
	if (iter.offset & 1) *data = (*data & 0x0f) | ((blockId & 0xf) << 4);
	else                 *data = (*data & 0xf0) | (blockId & 0xf);

	if (doLight)
	{
		/* update skyLight */
		uint8_t opac   = blockGetSkyOpacity(blockId>>4, 0);
		uint8_t oldSky = mapGetSky(&iter);
		uint8_t newSky = oldSky > opac ? oldSky - opac : 0;
		if (newSky != oldSky || opac != blockGetSkyOpacity(oldId>>4, 0))
		{
			if (blockGetSkyOpacity(blockId>>4, 0) > 0)
				mapUpdateSkyLightBlock(&iter);
			else
				mapUpdateSkyLightUnblock(&iter);
		}
		/* update blockLight */
		mapUpdateBlockLight(map, &iter, oldId, blockId);
	}
	else
	{
		mapUpdateRestoreSky(&iter);
		mapUpdateRestoreLight(iter);
	}

	if (iter.cd->slot == 0 && blockId != oldId)
	{
		/* not in update list: add it now */
		iter.cd->slot = 1;
		if (blockIds[(blockId == 0 ? oldId : blockId) >> 4].type != QUAD)
			iter.cd->slot |= (slotsXZ[(iter.z<<4)|iter.x] | slotsY[iter.y]) << 1;
		*track.list = iter.cd;
		track.list = &iter.cd->update;
	}

	/* redstone signal if any */
	if ((b->rsupdate & RSUPDATE_SEND) && (blockId>>4) != RSREPEATER_ON)
	{
		if (blockIds[blockId>>4].orientHint == ORIENT_LEVER)
		{
			/* buttons & lever */
			mapUpdateChangeRedstone(map, &iter, blockSides.lever[blockId&7], NULL);
		}
		else if ((blockId>>4) == RSWIRE && (blockId & 15) < (oldId & 15))
		{
			mapUpdateDeleteSignal(&iter, blockId);
		}
		else
		{
			mapUpdatePropagateSignal(&iter);
			mapUpdateChangeRedstone(map, &iter, RSSAMEBLOCK, NULL);
		}
	}

	/* block replaced: check if there is a tile entity to delete */
	DATA8 oldTile = chunkDeleteTileEntity(iter.ref, XYZ, False);
	if (oldTile)
	{
		if (blockIds[oldId >> 4].special == BLOCK_SIGN)
			/* remove off-screen bitmap */
			signDel(oldTile);

		if ((iter.ref->cflags & CFLAG_REBUILDTE) == 0)
			chunkMarkForUpdate(iter.ref);
	}

	if (tile)
	{
		/* update position */
		chunkUpdateTilePosition(iter.ref, XYZ, tile);
		chunkAddTileEntity(iter.ref, XYZ, tile);
		if ((iter.ref->cflags & CFLAG_REBUILDTE) == 0)
			chunkMarkForUpdate(iter.ref);
	}

	/* trigger a block update, it might call mapUpdate recursively */
	if (blockUpdate)
	{
		/* update nearby block if needed */
		mapUpdateBlock(map, pos, blockId, oldId, tile);

		/* updates triggered by previous block updates */
		if (track.updateCount > 0)
			mapUpdateFlush(map);

		/* update mesh */
		mapUpdateMesh(map);
		renderPointToBlock(-1, -1);
	}
	/* transfer modified chunk in a list, but don't re-generate mesh yet */
	// else mapUpdateListChunk(map);

	if (blockId == 0)
	{
		updateRemove(iter.cd, iter.offset, True);
		if (! silent)
			particlesExplode(map, 4, oldId, pos);
	}
}

/* high level function: dispatch to specialized module */
Bool mapActivate(Map map, vec4 pos)
{
	struct BlockIter_t iter;

	track.modif = NULL;
	track.list  = &track.modif;

	mapInitIter(map, &iter, pos, False);
	if (! iter.blockIds) return False;

	int block = iter.blockIds[iter.offset] << 4;
	int data  = iter.blockIds[DATA_OFFSET + (iter.offset >> 1)];

	if (iter.offset & 1) block |= data >> 4;
	else                 block |= data & 15;

	block = mapActivateBlock(&iter, pos, block);

	if (block > 0)
	{
		mapUpdate(map, pos, block, NULL, True);
		return True;
	}
	return False;
}
