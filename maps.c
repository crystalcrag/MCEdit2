/*
 * maps.c : anvil file format handling, load chunks according to player position, perform frustum and
 *          cave culling, also handle logic for ray-picking (ie: pointing at blocks).
 *
 * Written by T.Pierron, jan 2020
 */

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <malloc.h>
#include <math.h>
#include "meshBanks.h"
#include "render.h"
#include "blocks.h"
#include "NBT2.h"
#include "particles.h"
#include "entities.h"
#include "waypoints.h"
#include "globals.h"


//#define SLOW_CHUNK_LOAD   /* load 1 chunk (entire column) per second */

struct Frustum_t frustum = {
	.neighbors    = {0x0202a02c, 0x00809426, 0x002202a9, 0x00081263, 0x0101601c, 0x00404c16, 0x00110199, 0x00040953},
	.chunkOffsets = {0,1,2,4,8,16,32,3,9,17,33,6,18,34,12,20,36,24,40,19,35,25,41,22,38,28,44}
};

static uint8_t edgeCheck[] = {
	/* 6 one step away + 12 two steps away */
	19+0,   19+8, 19+16, 19+24, 19+32, 19+40,
	19+48, 19+50, 19+52, 19+54, 19+56, 19+58,
	19+60, 19+62, 19+64, 19+66, 19+68, 19+70,
	19+72,

	/* 1 step from center: S, E, N, W, T, B (4 lines to check) */
	2, 3, 3, 7, 6, 7, 6, 2,
	3, 1, 1, 5, 5, 7, 7, 3,
	1, 7, 7, 4, 4, 5, 5, 1,
	0, 2, 2, 6, 6, 4, 4, 0,
	4, 5, 5, 7, 7, 6, 6, 4,
	0, 1, 1, 3, 3, 2, 2, 0,

	/* 2 steps from center (1 line to check) */
	7, 3, 6, 2, 6, 7, 2, 3,
	5, 1, 7, 5, 3, 1, 0, 4,
	4, 5, 0, 1, 4, 6, 0, 2,

};

/* given a direction encodded as bitfield (S, E, N, W), return offset of where that chunk is */
int16_t   chunkNeighbor[16*9];
ChunkData chunkAir;

extern uint8_t openDoorDataToModel[];

int mapFirstFree(DATA32 usage, int count)
{
	DATA32 eof = usage + count;
	int    base;
	for (base = 0; usage < eof; usage ++, base += 32)
	{
		uint32_t bits = *usage ^ 0xffffffff;
		if (bits == 0) continue;
		/* count leading 0 */
		bits = ZEROBITS(bits);
		*usage |= 1 << bits;
		return base + bits;
	}
	return -1;
}

#ifdef DEBUG
void printCoord(BlockIter iter)
{
	fprintf(stderr, "%d, %d, %d\n", iter->ref->X + iter->x, iter->yabs, iter->ref->Z + iter->z);
}
#endif

int mapGetConnect(ChunkData cd, int offset, BlockState b)
{
	struct BlockIter_t iter;
	uint16_t neighbors[5];
	uint8_t  i, data;
	DATA16   n;

	mapInitIterOffset(&iter, cd, offset);

	for (i = 0, n = neighbors; i < 4; i ++, n ++)
	{
		mapIter(&iter, xoff[i], 0, zoff[i]);
		n[0] = iter.blockIds[iter.offset] << 4;
		data = iter.blockIds[DATA_OFFSET + (iter.offset >> 1)];
		n[0] |= (iter.offset & 1 ? data >>= 4 : data & 15);
	}
	neighbors[4] = 0;
	if (b->special == BLOCK_GLASS)
		return blockGetConnect4(neighbors, b->special);
	return blockGetConnect(b, neighbors);
}

/* get redstone wire connected flags */
static int mapGetConnectWire(ChunkData cd, int offset, BlockState b)
{
	static int8_t XYZoff[] = {
		0,-1,1,  1,0,-1,  -1,0,-1,  -1,0,1,  1,0,0,
		0, 1,1,  1,0,-1,  -1,0,-1,  -1,0,1,
		1, 1,1,  1,0,-1,  -1,0,-1,  -1,0,1,  1,0,0
	};
	struct BlockIter_t iter;
	uint16_t neighbors[14];
	uint8_t  i, data;
	DATA16   n;

	mapInitIterOffset(&iter, cd, offset);

	for (i = 0, n = neighbors; i < DIM(XYZoff); i += 3, n ++)
	{
		mapIter(&iter, XYZoff[i], XYZoff[i+1], XYZoff[i+2]);
		n[0] = iter.blockIds[iter.offset] << 4;
		data = iter.blockIds[DATA_OFFSET + (iter.offset >> 1)];
		n[0] |= (iter.offset & 1 ? data >>= 4 : data & 15);
	}
	int cnx = blockGetConnect(b, neighbors);
	if (cnx & 512)  cnx |= 5;
	if (cnx & 1024) cnx |= 10;
	return cnx & 15;
}

/*
 * Raycasting functions
 */

/* get blockId + metadata from block at pos (no new chunks will be loaded though) */
int mapGetBlockId(Map map, vec4 pos, MapExtraData extra)
{
	Chunk ref  = map->center;
	int   offX = CPOS(pos[0] - ref->X) + map->mapX;
	int   offZ = CPOS(pos[2] - ref->Z) + map->mapZ;
	int   absY = CPOS(pos[1]);
	int   area = map->mapArea;

	if (offX < 0)     offX += area; else
	if (offX >= area) offX -= area;
	if (offZ < 0)     offZ += area; else
	if (offZ >= area) offZ -= area;

	ref = map->chunks + offX + offZ * area;

	ChunkData cd;
	if (absY >= 0 && absY < ref->maxy && (cd = ref->layer[absY]))
	{
		int offset = ((int) pos[0] & 15) +
		             ((int) pos[2] & 15) * 16 +
		             ((int) pos[1] & 15) * 256;

		int blockId = cd->blockIds[offset] << 4;
		int data    = cd->blockIds[(offset>>1) + DATA_OFFSET];
		/* retracting piston head: ignore */
		if (blockId == ID(RSPISTONHEAD, 0) && chunkGetTileEntity(cd, offset))
			blockId = data = 0;

		if (offset & 1) data >>= 4;
		else            data &= 15;
		if (blockIds[blockId>>4].special != BLOCK_DOOR)
			blockId |= data;

		if (extra)
		{
			BlockState b = blockGetById(blockId);
			extra->cnxFlags = 0;
			extra->special  = b->special;
			extra->cd       = cd;
			extra->chunk    = ref;
			extra->offset   = offset;
			extra->blockId  = blockId;
			switch (SPECIALSTATE(b)) {
			case BLOCK_DOOR:
				if (data & 8)
				{
					/* top part */
					vec4 pos2;
					memcpy(pos2, pos, sizeof pos2);
					pos2[VY] --;
					mapGetBlockId(map, pos2, extra);
					extra->special = BLOCK_DOOR_TOP;
					blockId = extra->blockId;
				}
				else /* bottom part */
				{
					uint8_t data2;
					/* get data from top part */
					if (offset >= 256*15)
					{
						ChunkData top = ref->layer[absY+1];
						data2 = top ? top->blockIds[DATA_OFFSET + ((offset >> 1) & 127)] : 0;
					}
					else data2 = cd->blockIds[(offset>>1) + 128 + DATA_OFFSET];
					if (offset & 1) data2 >>= 4;
					else            data2 &= 15;

					data2 = (data&3) | ((data2&1) << 2);
					if (data & 4) data2 = openDoorDataToModel[data2];
					extra->blockId = blockId |= data2;
				}
				break;
			case BLOCK_RSWIRE:
				extra->cnxFlags = mapGetConnectWire(cd, offset, b);
				break;
			case BLOCK_WALL:
				/* don't care about center piece for bbox */
				extra->cnxFlags = mapGetConnect(cd, offset, b) & 15;
				break;
			case BLOCK_CHEST:
			case BLOCK_FENCE:
			case BLOCK_FENCE2:
			case BLOCK_GLASS:
				extra->cnxFlags = mapGetConnect(cd, offset, b);
			}
		}
		return blockId;
	}
	return 0;
}

/* simpler version of previous function */
int getBlockId(BlockIter iter)
{
	uint8_t data = iter->blockIds[DATA_OFFSET + (iter->offset >> 1)];
	return (iter->blockIds[iter->offset] << 4) | (iter->offset & 1 ? data >> 4 : data & 15);
}

/* get bounding box from block pointed by iter */
VTXBBox mapGetBBox(BlockIter iterator, int * count, int * cnxFlags)
{
	*count = 0;
	if (iterator->blockIds == NULL)
		return NULL;

	int id = getBlockId(iterator);
	Block block = blockIds + (id >> 4);

	/* opened fence gates mostly */
	if (block->bboxPlayer == BBOX_NONE || (id & block->bboxIgnoreBit))
		return NULL;

	if (block->id == RSPISTONHEAD && chunkGetTileEntity(iterator->cd, iterator->offset))
		return NULL;

	*cnxFlags = 0xffff;
	switch (block->special & 31) {
	case BLOCK_DOOR:
		{
			struct BlockIter_t iter = *iterator;
			int top;
			if (id & 8)
			{
				/* top part: get bottom */
				top = id;
				mapIter(&iter, 0, -1, 0);
				id = getBlockId(&iter);
			}
			else
			{
				mapIter(&iter, 0, 1, 0);
				top = getBlockId(&iter);
			}
			top = (id&3) | ((top&1) << 2);
			if (id & 4) top = openDoorDataToModel[top];
			id = (id & ~15) | top;
		}
		break;
	case BLOCK_STAIRS:
		{
			static struct VTXBBox_t bboxes[4];
			struct BlockIter_t iter = *iterator;
			DATA16  neighbors = alloca(14);  /* this trick will circumvent -Warray-bound :-/ */
			uint8_t i;
			neighbors[3] = getBlockId(&iter);
			for (i = 0; i < 4; i ++)
			{
				static uint8_t offset[] = {6, 4, 0, 2};
				mapIter(&iter, xoff[i], 0, zoff[i]);
				neighbors[offset[i]] = getBlockId(&iter);
			}
			/* first 10 items are not accessed */
			halfBlockGetBBox(neighbors - 10, bboxes, DIM(bboxes));
			*count = bboxes->cont;
			return bboxes;
		}
		break;
	case BLOCK_WALL:
		/* don't care about center piece for bbox */
		*cnxFlags = mapGetConnect(iterator->cd, iterator->offset, blockGetById(id)) & 15;
		break;
	case BLOCK_FENCE:
	case BLOCK_FENCE2:
	case BLOCK_GLASS:
		*cnxFlags = mapGetConnect(iterator->cd, iterator->offset, blockGetById(id));
	}

	VTXBBox box = blockGetBBox(blockGetById(id));
	if (box)
	{
		*count = block->special == BLOCK_CHEST ? 1 : box->cont;
		return box;
	}
	return NULL;
}



/*
 * from https://www.geomalgorithms.com/a05-_intersect-1.html
 * intersectRayPlane(): find the 3D intersection of a segment and a plane
 *    Input:  S = segment = {from P0, following direction u}, and Pn = a plane = {Point V0;  Vector n;}
 *    Output: I = the intersect point (when it exists)
 *    Return: 0 = disjoint (no intersection)
 *            1 = intersection in the unique point I
 */
int intersectRayPlane(vec4 P0, vec4 u, vec4 V0, vec norm, vec4 I)
{
	vec4 w = {P0[0]-V0[0], P0[1]-V0[1], P0[2]-V0[2], 1};

	float D =  vecDotProduct(norm, u);
	float N = -vecDotProduct(norm, w);

	if (fabsf(D) < EPSILON) /* segment is parallel to plane */
		return 0;

	/* they are not parallel: compute intersect param */
	float sI = N / D;
	/* we will check later if intersection is within plane */
	// if (sI < 0) return 0;

	I[0] = P0[0] + sI * u[0];
	I[1] = P0[1] + sI * u[1];
	I[2] = P0[2] + sI * u[2];

    return 1;
}

static Bool mapBlockIsFaceVisible(Map map, vec4 pos, int blockId, int8_t * offset)
{
	BlockState b = blockGetById(blockId);

	if (b->type == INVIS) return False;
	if (b->type == SOLID || b->type == TRANS)
	{
		vec4 neighbor = {pos[0] + offset[0], pos[1] + offset[1], pos[2] + offset[2], 1};
		BlockState n = blockGetById(mapGetBlockId(map, neighbor, NULL));
		if (n->type == b->type && n->type == TRANS) return False;
		return n->type != SOLID || n->special == BLOCK_HALF;
	}
	return True;
}

/* find the object (block, entity or waypoint) pointed by tracing a ray using direction <dir> */
Bool mapPointToObject(Map map, vec4 camera, vec4 dir, vec4 ret, MapExtraData data)
{
	static float normals[] = { /* S, E, N, W, T, B */
		 0,  0,  1, 1,
		 1,  0,  0, 1,
		 0,  0, -1, 1,
		-1,  0,  0, 1,
		 0,  1,  0, 1,
		 0, -1,  0, 1,
	};
	static int8_t next[] = {
		 0,  0,  1, 0,
		 1,  0,  0, 0,
		 0,  0, -1, 0,
		-1,  0,  0, 0,
		 0,  1,  0, 0,
		 0, -1,  0, 0
	};
	static uint8_t opposite[] = {2, 3, 0, 1, 5, 4};

	/* too annoying to make a global function */
	void mapCheckOtherObjects(vec4 inter)
	{
		data->entity = entityRaypick(map->center, dir, camera, inter, ret);
		if (data->entity == 0)
		{
			data->entity = wayPointRaypick(dir, camera, inter, ret);
			if (data->entity > 0)
				data->side = SIDE_WAYPOINT;
		}
		else data->side = SIDE_ENTITY;
	}


	vec4 pos, u;
	memcpy(u, dir, sizeof u);
	vec4 plane = {floorf(camera[0]), floorf(camera[1]), floorf(camera[2]), 1};
	int  flags = (u[0] < 0 ? 8 : 2) | (u[1] < 0 ? 32 : 16) | (u[2] < 0 ? 4 : 1);
	int  check = 0;
	int  block, cnx;

	memcpy(pos, camera, sizeof pos);

	block = mapGetBlockId(map, plane, data);
	cnx   = data->cnxFlags;
	data->entity = 0;

	while (vecDistSquare(pos, camera) < MAX_PICKUP*MAX_PICKUP)
	{
		int8_t * offset;
		float *  norm;
		VTXBBox  box;
		uint8_t  i, j, nb;
		uint8_t  faces = check ? flags^63 : flags;

		/* check intersection with all planes */
		for (i = 0, norm = normals, box = blockGetBBoxForVertex(blockGetById(block)); i < 6; i ++, norm += 4)
		{
			vec4 inter, V0, V1;
			/* we can already eliminate some planes based on the ray direction */
			if ((faces & (1 << i)) == 0) continue;
			for (nb = box && box->cont ? box->cont : 1, j = 0; j < nb; j ++)
			{
				if (! blockGetBoundsForFace(box + j, i, V0, V1, plane, cnx))
					continue;

				if (intersectRayPlane(pos, u, V0, norm, inter) == 1)
				{
					/* need to check that intersection point remains within box */
					if (norm[0] == 0 && ! (V0[0] <= inter[0] && inter[0] <= V1[0])) continue;
					if (norm[1] == 0 && ! (V0[1] <= inter[1] && inter[1] <= V1[1])) continue;
					if (norm[2] == 0 && ! (V0[2] <= inter[2] && inter[2] <= V1[2])) continue;
					if (check)
					{
						memcpy(data->inter, inter, sizeof data->inter);
						data->side = i;
						mapCheckOtherObjects(inter);
						return True;
					}

					memcpy(pos, inter, sizeof pos);

					/* we have an intersection: move to block */
					offset = next + i * 4;
					plane[0] += offset[0];
					plane[1] += offset[1];
					plane[2] += offset[2];
					block = mapGetBlockId(map, plane, data);
					cnx   = data->cnxFlags;
					if (block > 0 && mapBlockIsFaceVisible(map, plane, block, next + opposite[i] * 4))
					{
						BlockState b = blockGetById(block);
						memcpy(ret, plane, 3*sizeof *ret);
						memcpy(data->inter, inter, sizeof data->inter);
						data->side = opposite[i];
						data->topHalf = data->side < 4 && inter[1] - (int) inter[1] >= 0.5f;
						ret[3] = 1;
						if (b->bboxId > 1)
						{
							/* the block we just checked was Air, now we need to check if the ray is intersecting a custom bounding box */
							check = 1;
							goto break_all;
						}
						mapCheckOtherObjects(inter);
						return True;
					}
					goto break_all;
				}
			}
		}
		/* custom model bounding boxes did not intersect with our ray, continue casting then */
		if (check)
		{
			block = 0;
			check = 0;
			continue;
		}
		break_all:
		/* hmm, if somehow we didn't find any intersection, this will prevent an infinite loop */
		if (i == 6)
			break;
	}
	/* no intersection with voxels, check for other objects */
	mapCheckOtherObjects(NULL);
	return data->entity > 0;
}

/*
 * dynamic chunk loading depending on player position
 */

void mapShowChunks(Map map)
{
	Chunk c;
	int   i, size;
	fprintf(stderr, "=== map chunk loaded ===\n");
	for (i = 0, c = map->chunks, size = map->mapArea * map->mapArea; i < size; i ++, c ++)
	{
		uint8_t flags = c->cflags;
		int     discard = 0;
		int     hasflags = 0;
		if (flags & CFLAG_HASMESH)
		{
			int j;
			for (j = 0; j < c->maxy; j ++)
			{
				ChunkData cd = c->layer[j];
				if (cd && cd->glBank && cd->frame == map->frame && cd->glDiscard > 0)
				{
					discard ++;
					if (cd->cdFlags & CDFLAG_DISCARDABLE) hasflags ++;
				}
			}
		}
		if (i % map->mapArea == 0)
			fprintf(stderr, "%2d:", i / map->mapArea);
		uint8_t bank =
			c == map->center ? '#' :
			(flags & (CFLAG_HASMESH|CFLAG_GOTDATA)) == (CFLAG_HASMESH|CFLAG_GOTDATA) ? (bank ? 'M' : '!') :
			flags & CFLAG_GOTDATA ? 'L' : ' ';
		if (c->chunkFrame != map->frame)
			bank = '.';

		fputc(bank, stderr);
		fputc(hasflags && discard != hasflags ? 'X' : (hasflags ? 'D' : ' '), stderr);

		if (i % map->mapArea == map->mapArea-1) fputs("|\n", stderr);
	}
}

/* check within entire map, if there are chunks that need meshing */
static int mapRedoGenList(Map map)
{
	int8_t * spiral;
	int      XC   = CPOS(map->cx) << 4;
	int      ZC   = CPOS(map->cz) << 4;
	int      n    = map->maxDist * map->maxDist;
	int      area = map->mapArea;
	int      ret  = 0;

	meshStopThreads(map, THREAD_EXIT_LOOP);
	ListNew(&map->genList);

	/* they have to be scanned from closest to farthest from center */
	for (spiral = frustum.spiral; n > 0; n --, spiral += 2)
	{
		Chunk c = &map->chunks[(map->mapX + spiral[0] + area) % area + (map->mapZ + spiral[1] + area) % area * area];
		if (c->cflags & CFLAG_GOTDATA)
		{
			int X = XC + (spiral[0] << 4);
			int Z = ZC + (spiral[1] << 4);
			if (c->X != X || c->Z != Z)
			{
				map->GPUchunk -= chunkFree(map, c, True);
			}
		}
		else c->entityList = ENTITY_END;
		c->cflags &= ~(CFLAG_STAGING | CFLAG_PROCESSING);

		if ((c->cflags & CFLAG_HASMESH) == 0)
		{
			c->X = XC + (spiral[0] << 4);
			c->Z = ZC + (spiral[1] << 4);
			ListAddTail(&map->genList, &c->next);
			ret ++;
		}
	}
	/* return number of chunk needed to be read/meshed/trandfered to GPU */
	return ret;
}

/* unload entities from lazy chunk (not MT safe!) */
static void mapMarkLazyChunk(Map map)
{
	int8_t * ptr;
	int8_t * end;
	int      area = map->mapArea;

	for (ptr = frustum.lazy, end = ptr + frustum.lazyCount; ptr < end; ptr += 3)
	{
		uint8_t dir = ptr[2];
		Chunk chunk = &map->chunks[(map->mapX + ptr[0] + area) % area + (map->mapZ + ptr[1] + area) % area * area];
		if (chunk->maxy == 0) continue;

		if (chunk->cflags & CFLAG_HASENTITY)
		{
			if (chunk->entityList != ENTITY_END)
				entityUnload(chunk);
			chunk->cflags &= ~CFLAG_HASENTITY;
		}

		/* also free lazy chunks that are not at their place */
		Chunk neighbor = chunk + map->chunkOffsets[chunk->neighbor + dir];

		int X = chunk->X;
		int Z = chunk->Z;
		if (dir & 1) Z += 16;
		if (dir & 2) X += 16;
		if (dir & 4) Z -= 16;
		if (dir & 8) X -= 16;

		if (X != neighbor->X || Z != neighbor->Z)
			map->GPUchunk -= chunkFree(map, chunk, True);
	}
}

/* chunks are stored in a 2D circular array (circular horizontally and vertically) */
Bool mapMoveCenter(Map map, vec4 old, vec4 pos)
{
	int area = map->mapArea;
	int dx   = CPOS(pos[VX]) - CPOS(old[VX]);
	int dz   = CPOS(pos[VZ]) - CPOS(old[VZ]);

	/* current pos: needed to track center chunk coord */
	memcpy(&map->cx, pos, sizeof (float) * 3);

	if (dx || dz)
	{
		if (dx >= area || dz >= area)
		{
			/* reset map center */
			map->mapX = map->mapZ = area / 2;
		}
		else /* some chunks will still be useful */
		{
			map->mapX = (map->mapX + dx + area) % area;
			map->mapZ = (map->mapZ + dz + area) % area;
		}
		int count = mapRedoGenList(map);
		map->center = map->chunks + (map->mapX + map->mapZ * area);
		mapMarkLazyChunk(map);
		meshAddToProcess(map, count);
		//mapShowChunks(map);

		//fprintf(stderr, "new map center: %d, %d (%d,%d)\n", map->mapX, map->mapZ, (int) pos[VX], (int) pos[VZ]);
		return True;
	}
	return False;
}

static int sortByDist(const void * item1, const void * item2)
{
	int8_t * c1 = (int8_t *) item1;
	int8_t * c2 = (int8_t *) item2;

	return c1[0] * c1[0] + c1[1] * c1[1] - (c2[0] * c2[0] + c2[1] * c2[1]);
}

Chunk mapAllocArea(int area)
{
	Chunk chunks = calloc(sizeof *chunks, area * area);
	Chunk c;
	int   i, j, n, dist = area - 3;

	if (chunks)
	{
		/* should be property of a map... */
		int8_t * ptr = realloc(frustum.spiral, dist * dist * 2 + (dist * 4 + 4) * 3);

		if (ptr)
		{
			/* vertical wrap (mask value from chunkInitStatic) */
			for (c = chunks, i = area-1, n = area * (area-1), c->neighbor = 1 * 16, c[n].neighbor = 6 * 16, c ++;
				 i > 1; i --, c->neighbor = 2 * 16, c[n].neighbor = 7 * 16, c ++);
			c[0].neighbor = 3 * 16;
			c[n].neighbor = 8 * 16;
			/* horizontal wrap */
			for (n = area, c = chunks + n, i = n-2; i > 0; i --, c[0].neighbor = 4 * 16, c[n-1].neighbor = 5 * 16, c += n);

			/* to priority load chunks closest to the player */
			for (j = 0, frustum.spiral = ptr; j < dist; j ++)
			{
				for (i = 0; i < dist; i ++, ptr += 2)
				{
					ptr[0] = i - (dist >> 1);
					ptr[1] = j - (dist >> 1);
				}
			}
			i = dist * dist;
			qsort(frustum.spiral, i, 2, sortByDist);
			frustum.lazy = frustum.spiral + i * 2;

			/* to quickly enumerate all lazy chunks (need when map center has changed) */
			for (ptr = frustum.lazy, j = 0, dist += 2, i = dist >> 1; j < dist; j ++, ptr += 6)
			{
				/* note: 3rd value is direction of the nearest chunk within render distance (from lazy chunk POV) */
				ptr[0] = ptr[3] = j - i;
				ptr[2] = 1 << SIDE_SOUTH;
				ptr[1] = - i;
				ptr[4] =   i;
				ptr[5] = 1 << SIDE_NORTH;
			}

			/* corner */
			ptr[-1] |= 1 << SIDE_WEST;
			ptr[-4] |= 1 << SIDE_WEST;

			for (j = 0, dist -= 2; j < dist; j ++, ptr += 6)
			{
				ptr[1] = ptr[4] = j - (dist >> 1);
				ptr[0] = - i;
				ptr[3] =   i;
				ptr[2] = 1 << SIDE_EAST;
				ptr[5] = 1 << SIDE_WEST;
			}

			/* corner */
			frustum.lazy[2] |= 1 << SIDE_EAST;
			frustum.lazy[5] |= 1 << SIDE_EAST;

			frustum.lazyCount = ptr - frustum.lazy;

			/* reset chunkNeighbor table: it depends on map size */
			static uint8_t wrap[] = {0, 12, 4, 6, 8, 2, 9, 1, 3}; /* bitfield: &1:+Z, &2:+X, &4:-Z, &8:-X, ie: SENW */

			for (j = 0, dist = area, n = area*area; j < DIM(wrap); j ++)
			{
				int16_t * p;
				uint8_t   w = wrap[j];
				for (i = 0, p = chunkNeighbor + j * 16; i < 16; i ++, p ++)
				{
					int pos = 0;
					if (i & 1) pos += w & 1 ? dist-n : dist;
					if (i & 2) pos += w & 2 ? 1-dist : 1;
					if (i & 4) pos -= w & 4 ? dist-n : dist;
					if (i & 8) pos -= w & 8 ? 1-dist : 1;
					if (popcount(i) > 2) pos = 0;
					p[0] = pos;
				}
			}

			return chunks;
		}
		else free(chunks);
	}
	return NULL;
}

/* change render distance dynamicly */
Bool mapSetRenderDist(Map map, int maxDist)
{
	int area = (maxDist * 2) + 4;

	if (area == map->mapArea) return True;
	if (maxDist < 2 || maxDist > 31) return False;

	Chunk chunks = mapAllocArea(area);

	fprintf(stderr, "setting map size to %d (from %d)\n", area, map->mapArea);

	if (chunks)
	{
		/* we have all the memory we need: can't fail from this point */
		int oldArea  = map->mapArea;
		int size     = ((oldArea < area ? oldArea : area) - 2) >> 1;
		int XZmid    = (area >> 1) - 1;
		int freeMesh = 0;
		int loaded   = 0;
		int i, j, k;

		meshStopThreads(map, THREAD_EXIT_LOOP);
		maxDist ++;

		/* copy chunk information (including lazy chunks) */
		for (j = -size; j <= size; j ++)
		{
			if (abs(j) == maxDist) freeMesh |= 1;
			else freeMesh &= ~1;

			for (i = -size; i <= size; i ++)
			{
				int XC = map->mapX + i;
				int ZC = map->mapZ + j;

				if (XC < 0)        XC += oldArea; else
				if (XC >= oldArea) XC -= oldArea;
				if (ZC < 0)        ZC += oldArea; else
				if (ZC >= oldArea) ZC -= oldArea;

				Chunk source = map->chunks + XC + ZC * oldArea;
				Chunk dest   = chunks + (XZmid+i) + (XZmid+j) * area;
				char  nbor   = dest->neighbor;
				memcpy(&dest->save, &source->save, sizeof *dest - offsetp(Chunk, save));
				source->cflags = 0;
				dest->neighbor = nbor;
				if (abs(i) == maxDist) freeMesh |= 2;
				else freeMesh &= ~2;

				/* ChunkData ref needs to be readjusted */
				for (k = dest->maxy-1; k >= 0; k --)
				{
					ChunkData cd = dest->layer[k];
					if (cd)
					{
						cd->chunk = dest;
						if (freeMesh && cd->glSlot)
						{
							dest->cflags &= ~CFLAG_HASMESH;
							meshFreeGPU(cd);
						}
						loaded += cd->glBank != NULL;
					}
					else fprintf(stderr, "chunk %d, %d missing layer %d?\n", dest->X, dest->Z, k);
				}
			}
		}

		if (oldArea > area)
		{
			Chunk old;
			/* need to free chunk outside new render dist */
			for (i = 0, j = oldArea * oldArea, old = map->chunks; i < j; old ++, i ++)
			{
				if (old->cflags & (CFLAG_HASMESH|CFLAG_GOTDATA))
					map->GPUchunk -= chunkFree(map, old, True);
			}
		}
		/* need to point to the new chunk array, otherwise it will point to some free()'ed memory */
		Chunk * prev;
		Chunk   list;
		i = map->center->X;
		j = map->center->Z;
		for (list = map->needSave, prev = &map->needSave; list; list = list->save)
		{
			int DX = (list->X - i) >> 4;
			int DZ = (list->Z - j) >> 4;
			if (abs(DX) < maxDist && abs(DZ) < maxDist)
			{
				*prev = chunks + (XZmid + DX) + (XZmid + DZ) * area;
				prev = &(*prev)->save;
			}
			else fprintf(stderr, "discarding modified chunk %d, %d out of render distance\n", list->X, list->Z);
		}
		*prev = NULL;

		free(map->chunks);
		map->maxDist  = area - 3;
		map->mapArea  = area;
		map->mapZ     = map->mapX = XZmid;
		map->genLast  = NULL;
		map->chunks   = chunks;
		map->GPUchunk = loaded;
		map->center   = map->chunks + map->mapX + map->mapZ * area;
		meshAddToProcess(map, mapRedoGenList(map));
		return True;
	}

	return False;
}

/*
 * Reading/saving chunks from disk
 */

/* before world is loaded, check that the map has a few chunks in it */
Map mapInitFromPath(STRPTR path, int renderDist)
{
	Map map = calloc(sizeof *map + sizeof *chunkAir + MIN_SECTION_MEM, 1);

	if (map)
	{
		ChunkData air = chunkAir = (ChunkData) (map + 1);
		map->maxDist = renderDist * 2 + 1;
		map->mapArea = renderDist * 2 + 4;
		map->mapZ    = map->mapX = renderDist + 1;

		/* all tables but skyLight will be 0 */
		air->blockIds = (DATA8) (air+1);
		air->cdFlags = CDFLAG_CHUNKAIR;
		air->glLightId = LIGHT_SKY15_BLOCK0;
		/* fully lit */
		memset(air->blockIds + SKYLIGHT_OFFSET, 255, 2048);

		map->chunks = mapAllocArea(map->mapArea);
		map->center = map->chunks + (map->mapX + map->mapZ * map->mapArea);
		map->chunkOffsets = chunkNeighbor;
		map->GPUMaxChunk = 20 * 1024 * 1024;

		if (! map->chunks)
		{
			free(map);
			return NULL;
		}
	}
	else return NULL;
	ExpandEnvVarBuf(path, map->path, MAX_PATHLEN);

	if (IsDir(map->path))
	{
		AddPart(map->path, "level.dat", MAX_PATHLEN);
	}

	NBTFile_t nbt;
	if (NBT_Parse(&nbt, map->path))
	{
		float xyz[3];

		if (NBT_GetFloat(&nbt, NBT_FindNode(&nbt, 0, "pos"), xyz, 3))
		{
			map->cx = xyz[0];
			map->cy = xyz[1];
			map->cz = xyz[2];
		}
		map->levelDat = nbt;

		//NBT_Dump(&nbt, 0, 0, stdout);

		ParentDir(map->path);
		AddPart(map->path, "region", MAX_PATHLEN);

		/* init genList already */

		#if NUM_THREADS > 0
		map->genLock = MutexCreate();
		map->genCount = SemInit(0);
		meshAddToProcess(map, mapRedoGenList(map));
		meshInitThreads(map);
		#else
		mapRedoGenList(map);
		#endif

		#ifdef DEBUG
		fprintf(stderr, "center = %d, %d\n", map->center->X, map->center->Z);
		#endif

		quadTreeInit(xyz[VX] - 1, xyz[VZ] - 1, map->maxDist * 16);

		return map;
	}
	free(map);
	return NULL;
}

/* destructor */
void mapFreeAll(Map map)
{
	Chunk chunk;
	int   nb;
	for (nb = map->mapArea * map->mapArea, chunk = map->chunks; nb > 0; nb --, chunk ++)
		chunkFree(map, chunk, False);

	ChunkFake fake, next;
	for (fake = map->cdPool; fake; next = fake->next, free(fake), fake = next);

	LightingTex light;
	while ((light = (LightingTex) ListRemHead(&map->lightingTex)))
	{
		meshDeleteTex(light);
		free(light);
	}

	NBT_Free(&map->levelDat);
	MutexDestroy(map->genLock);
	SemClose(map->genCount);
	free(map->chunks);
	free(map);
	chunkAir = NULL;
}

/* save any changes made to levelDat */
Bool mapSaveLevelDat(Map map)
{
	int    len  = strlen(map->path) + 16;
	STRPTR path = alloca(len * 2);
	STRPTR copy = path + len;
	/* map->path point to region dir */
	CopyString(path, map->path, len);
	ParentDir(path);
	strcpy(copy, path);
	AddPart(path, "level.dat", len);
	AddPart(copy, "level.dat_old", len);

	/* make a copy of level.dat first */
	if (FileCopy(path, copy, True) && NBT_Save(&map->levelDat, path, NULL, 0) > 0)
	{
		/* remove modif mark */
		NBTHdr hdr = (NBTHdr) map->levelDat.mem;
		hdr->count = 0;
		return True;
	}
	return False;
}

/* add the chunk to the pending list of chunks to be saved */
void mapAddToSaveList(Map map, Chunk chunk)
{
	if ((chunk->cflags & CFLAG_NEEDSAVE) == 0)
	{
		/* must not be added twice */
		chunk->cflags |= CFLAG_NEEDSAVE;
		chunk->save = map->needSave;
		map->needSave = chunk;
	}
}

void cartoCommitNewMaps(void);

/* save chunks that have been mark as modified */
Bool mapSaveAll(Map map)
{
	Chunk * prev;
	Chunk   chunk;
	Bool    ret = True;
	for (prev = &map->needSave, chunk = *prev; chunk; chunk = chunk->save)
	{
		if ((chunk->cflags & CFLAG_NEEDSAVE) && ! chunkSave(chunk, map->path) /* will remove NEEDSAVE flag */)
		{
			/* fail to save a chunk: try to save the remaining still */
			*prev = chunk;
			prev = &chunk->save;
			ret = False;
		}
	}
	cartoCommitNewMaps();
	*prev = NULL;
	map->needSave = chunk;
	return ret;
}


/* check if there is a chest connected to another one */
int mapConnectChest(Map map, MapExtraData sel, MapExtraData ret)
{
	struct BlockIter_t iter;

	Chunk chunk = sel->chunk;
	int   offset = sel->offset;
	int   block = sel->blockId >> 4;
	vec4  pos = {chunk->X, sel->cd->Y, chunk->Z};

	pos[0] += offset & 15; offset >>= 4;
	pos[2] += offset & 15; offset >>= 4;
	pos[1] += offset;

	mapInitIter(map, &iter, pos, False);

	offset = 0;
	switch (sel->blockId & 15) {
	case 0: case 2: case 3: /* chest oriented north/south */
		mapIter(&iter, -1, 0, 0);
		if (iter.blockIds[iter.offset] != block)
		{
			/* no block with same type, check west */
			mapIter(&iter, 2, 0, 0);
			if (iter.blockIds[iter.offset] != block)
				return 0;
			offset = 1;
		}
		else offset = 2;
		break;
	case 4: case 5: /* oriented east/west */
		mapIter(&iter, 0, 0, -1);
		if (iter.blockIds[iter.offset] != block)
		{
			mapIter(&iter, 0, 0, 2);
			if (iter.blockIds[iter.offset] != block)
				return 0;
			offset = 1;
		}
		else offset = 2;
		break;
	default: return 0;
	}

	ret->offset  = iter.offset;
	ret->blockId = getBlockId(&iter);
	ret->chunk   = iter.ref;
	ret->cd      = iter.cd;

	return offset;
}

/*
 * if underwater (or inside lava) we'll have to change how fog is applied
 */
int mapIsPositionInLiquid(Map map, vec4 pos)
{
	Chunk chunk = mapGetChunk(map, pos);

	int Y = CPOS(pos[VY]);

	if (0 <= Y && Y < chunk->maxy)
	{
		ChunkData cur = chunk->layer[Y];

		int offset =
			 (int) (pos[VX] - chunk->X) +
			((int) (pos[VZ] - chunk->Z) << 4) +
			((int) (pos[VY] - cur->Y) << 8);

		Block b = blockIds + cur->blockIds[offset];

		if (b->special == BLOCK_LIQUID)
		{
			/* skylight rapidly change when underwater: smooth transition a little bit */
			if (b->emitLight == 0)
			{
				/* underwater */
				uint8_t sky = cur->blockIds[SKYLIGHT_OFFSET + (offset >> 1)];
				uint8_t sky2;
				/* get skylight just above this block */
				if (offset >= 4096-256)
				{
					Y ++;
					sky2 = Y < chunk->maxy ? chunk->layer[Y]->blockIds[SKYLIGHT_OFFSET + ((offset & 255) >> 1)] : 15;
				}
				else sky2 = cur->blockIds[SKYLIGHT_OFFSET + 128 + (offset >> 1)];

				float diff = ceilf(pos[VY]) - pos[VY];

				return 1 | (lroundf(sky * diff + sky2 * (1-diff)) << 8);
			}
			else return 2 | (255 << 8); /* inside lava */
		}
	}
	return 0;
}

/*
 * lighting tex management (transfer to GPU will be done in meshBanks.c)
 */
int mapAllocLightingTex(Map map)
{
	LightingTex lightTex;
	int lightingId = 0;

	for (lightTex = HEAD(map->lightingTex); lightTex && lightTex->usage == 512; NEXT(lightTex), lightingId ++);

	if (lightTex == NULL)
	{
		lightTex = calloc(sizeof *lightTex, 1);
		ListAddTail(&map->lightingTex, &lightTex->node);
		fprintf(stderr, "allocating a lighting texture\n");
	}
	lightTex->usage ++;

	/* this id will be used by terrain fragment shader */
	return lightingId | (mapFirstFree(lightTex->slots, DIM(lightTex->slots)) << 7);
}

void mapFreeLightingSlot(Map map, int lightId)
{
	LightingTex lightTex;

	for (lightTex = HEAD(map->lightingTex); lightTex && (lightId & 127) > 0; NEXT(lightTex), lightId ++);

	if (lightTex)
	{
		/* no need to clear texture data */
		int slot = lightId >> 7;
		lightTex->usage --;
		lightTex->slots[slot >> 5] &= ~(1 << (slot & 31));
	}
}


/*
 * Frustum culling: the goal of these functions is to create a linked list of chunks
 * representing all the ones that are visible in the current view matrix (MVP)
 * check doc/internals.html for explanation on how this part works.
 */

#define FAKE_CHUNK_SIZE     (offsetof(struct ChunkData_t, blockIds)) /* fields after blockIds are completely useless for frustum */
#define UNVISITED           0x40
#define VISIBLE             0x80
#define UNCERTAIN           0x80
//#define FRUSTUM_DEBUG

static ChunkData mapAllocFakeChunk(Map map)
{
	ChunkFake * prev;
	ChunkFake   cf;
	ChunkData   cd;
	int         slot;

	for (prev = &map->cdPool, cf = *prev; cf && cf->usage == 0xffffffff; prev = &cf->next, cf = cf->next);

	if (cf == NULL)
	{
		cf = calloc(sizeof *cf + FAKE_CHUNK_SIZE * 32, 1);
		if (cf == NULL) return NULL;
		*prev = cf;
	}

	slot = mapFirstFree(&cf->usage, 1);
	cd = (ChunkData) (cf->buffer + FAKE_CHUNK_SIZE * slot);
	memset(cd, 0, FAKE_CHUNK_SIZE);
	cd->slot = slot+1;
	cf->usage |= 1<<slot;
	cd->cnxGraph = 0xffff;
	map->fakeMax ++;

	return cd;
}

static void mapFreeFakeChunk(ChunkData cd)
{
	/* as ugly as it looks, it is portable however... */
	int       slot = cd->slot-1;
	ChunkFake cf = (ChunkFake) ((DATA8) cd - FAKE_CHUNK_SIZE * slot - offsetof(struct ChunkFake_t, buffer));
	Chunk     c  = cd->chunk;
	cf->usage &= ~(1 << slot);
	c->layer[cd->Y>>4] = NULL;

	//fprintf(stderr, "free fake chunk at %d, %d, %d: %d\n", c->X, cd->Y, c->Z, cd->slot);
}

static int mapGetOutFlags(Map map, ChunkData cur, DATA8 outflags)
{
	uint8_t out, i, sector;
	Chunk   chunk = cur->chunk;
	int     layer = cur->Y >> 4;
	int     neighbors = 0;
	for (i = out = neighbors = 0; i < 8; i ++)
	{
		static uint8_t dir[] = {0, 2, 1, 3, 16, 16+2, 16+1, 16+3};
		Chunk neighbor = chunk + chunkNeighbor[chunk->neighbor + (dir[i] & 15)];
		if (neighbor->chunkFrame != map->frame)
		{
			memset(neighbor->outflags, UNVISITED, sizeof neighbor->outflags);
			neighbor->chunkFrame = map->frame;
		}
		int Y = layer + (dir[i]>>4);
		if ((sector = neighbor->outflags[Y]) & UNVISITED)
		{
			vec4 point = {neighbor->X, Y << 4, neighbor->Z, 1};

			sector &= ~0x7f;
			/* XXX global var for MVP, should be a function param */
			matMultByVec(point, globals.matMVP, point);
			if (point[0] <= -point[3]) sector |= 1;  /* to the left of left plane */
			if (point[0] >=  point[3]) sector |= 2;  /* to the right of right plane */
			if (point[1] <= -point[3]) sector |= 4;  /* below the bottom plane */
			if (point[1] >=  point[3]) sector |= 8;  /* above top plane */
			if (point[2] <= -point[3]) sector |= 16; /* behind near plane */
			if (point[2] >=  point[3]) sector |= 32; /* after far plane */
			sector = (neighbor->outflags[Y] = sector) & 63;
		}
		else sector &= 63;
		if (sector == 0)
			/* point of the chunk is entirely included in frustum: add all connected chunks to the list */
			neighbors |= frustum.neighbors[i];
		else
			out ++;
		outflags[i] = sector;
	}
	outflags[i] = out;
	return neighbors;
}

static ChunkData mapAddToVisibleList(Map map, Chunk from, int direction, int layer, DATA8 outFlagsFrom, int frame)
{
	static int8_t dir[] = {0, 1, -1};
	uint8_t dirFlags = frustum.chunkOffsets[direction];
	Chunk c = from + chunkNeighbor[from->neighbor + (dirFlags & 15)];
	Chunk center = map->center;
	ChunkData cd;

	int X = c->X - center->X;
	int Z = c->Z - center->Z;
	int Y = layer + dir[dirFlags >> 4];
	int half = (map->maxDist >> 1) << 4;

	/* outside of render distance */
	if (X < -half || X > half || Z < -half || Z > half || Y < 0 || Y >= CHUNK_LIMIT)
		return NULL;

	if (c->chunkFrame != frame)
	{
		memset(c->outflags, UNVISITED, sizeof c->outflags);
		c->chunkFrame = frame;
	}
	if ((c->cflags & CFLAG_HASMESH) == 0)
	{
		/* move to front of list of chunks waiting to be generated */
		if ((Chunk) c->next.ln_Prev != map->genLast && map->genLast != c)
		{
			if ((c->cflags & CFLAG_PROCESSING) == 0)
			{
				MutexEnter(map->genLock);
				ListRemove(&map->genList, &c->next);
				ListInsert(&map->genList, &c->next, &map->genLast->next);
				map->genLast = c;
				MutexLeave(map->genLock);
			}
		}
		return NULL;
	}

	/*
	 * special case: there might be chunks below but they are currently outside of the frustum.
	 * As we get farther from the camera, we might eventually intersect with these, that's why we have
	 * to add those fake chunks along the bottom plane of the frustum.
	 */
	cd = c->layer[Y];
	if (cd == NULL)
	{
		if (Y >= CHUNK_LIMIT || direction >= 19 || (c->outflags[Y] & VISIBLE))
			return NULL;

		/* check if faces/edges cross bottom plane */
		DATA8 edges = edgeCheck + edgeCheck[direction - 1];
		DATA8 end   = edgeCheck + edgeCheck[direction];
		while (edges < end)
		{
			if ((outFlagsFrom[edges[0]] ^ outFlagsFrom[edges[1]]) & 4)
			{
				cd = mapAllocFakeChunk(map);
				cd->Y = Y << 4;
				cd->chunk = c;
				c->layer[Y] = cd;
				//fprintf(stderr, "allocating fake chunk at %d, %d, %d: %d\n", c->X, cd->Y, c->Z, cd->slot);
				goto break_all;
			}
			edges += 2;
		}
		return NULL;
	}

	break_all:
	if (cd && c->outflags[Y] < VISIBLE)
	{
		/* not visited yet */
		c->outflags[Y] |= VISIBLE;
		cd->comingFrom = 0;
		cd->visible = NULL;
		return cd;
	}
	return NULL;
}

/* cave culling based on visibility graph traversal (returns True if <cur> should be culled) */
static void mapCullCave(ChunkData cur, vec4 camera)
{
	uint8_t side, i, oppSide;
	Chunk   chunk = cur->chunk;
	int     X = chunk->X;
	int     Z = chunk->Z;
	int     frame = chunk->chunkFrame;

//	if (chunk->X == -48 && cur->Y == 64 && chunk->Z == -560)
//		puts("here");

	/* try to get back to a known location from <cur> */
	for (i = 0; i < 3; i ++)
	{
		extern uint16_t hasCnx[]; /* from chunks.c */
		static int8_t TB[] = {0, 0, 0, 0, -1, 1};
		ChunkData neighbor;

		/* check which face is visible based on dot product between face normal and camera */
		switch (i) {
		case 0: /* N/S */
			/* side is a flag, oppSide is enumeration */
			if (Z + 16 - camera[VZ] < 0) side = 1, oppSide = 2;
			else if (camera[VZ] - Z < 0) side = 4, oppSide = 0;
			else continue;
			break;
		case 1: /* E/W */
			if (X + 16 - camera[VX] < 0) side = 2, oppSide = 3;
			else if (camera[VX] - X < 0) side = 8, oppSide = 1;
			else continue;
			break;
		case 2: /* T/B */
			if (cur->Y + 16 - camera[VY] < 0) side = 0, oppSide = 5;
			else if (camera[VY] - cur->Y < 0) side = 0, oppSide = 4;
			else continue;
		}

		chunk    = cur->chunk + chunkNeighbor[cur->chunk->neighbor + side];
		neighbor = chunk->layer[(cur->Y >> 4) + TB[oppSide]];
		side     = 1 << opp[oppSide];

		if (neighbor == NULL || neighbor->slot > 0)
		{
			/* high column without neighbor: consider this chunk visible */
			cur->comingFrom = side;
			break;
		}
		if (neighbor->frame != frame)
		{
			if (neighbor->cnxGraph & hasCnx[(1 << oppSide) | neighbor->comingFrom])
				cur->comingFrom = UNCERTAIN | side;
		}
		else if (neighbor->comingFrom > 0 /* can be visited */)
		{
			if (neighbor->comingFrom == 127)
			{
				/* starting pos: multiple paths possible */
				static int canGoTo[] = { /* S, E, N, W, T, B */
					1+2+4+8+16+(1<<15), 1+32+64+128+256+(1<<16), 2+32+512+1024+2048+(1<<17), 4+64+512+4096+8192+(1<<18),
					8+128+1024+4096+16384+(1<<19), 16+256+2048+8192+16384+(1<<20)
				};
				if ((neighbor->cnxGraph | ((neighbor->cdFlags & CDFLAG_HOLE) << 6)) & canGoTo[oppSide])
				{
					cur->comingFrom = side;
					break;
				}
			}
			else if (neighbor->cnxGraph & hasCnx[(1 << oppSide) | neighbor->comingFrom])
			{
				cur->comingFrom = side;
				break;
			}
		}
	}
}

void mapViewFrustum(Map map, vec4 camera)
{
	ChunkData * prev;
	ChunkData   cur, last;
	Chunk       chunk;
	int         frame;
	int         center[3];

	chunk = mapGetChunk(map, camera);

	center[VY] = CPOS(camera[1]);
	center[VX] = chunk->X;
	center[VZ] = chunk->Z;

	map->firstVisible = NULL;
	map->fakeMax = 0;
	meshClearBank(map);

	#if 0
	/*
	 * DEBUG: all chunks that have a mesh will be sent to GPU. If frustum culling function is FUBAR
	 * you can reactivate this code to see the world again.
	 */
	int8_t * spiral;
	int area = map->mapArea, n = map->maxDist * map->maxDist, i;
	for (spiral = frustum.spiral, prev = &map->firstVisible; n > 0; n --, spiral += 2)
	{
		Chunk c = &map->chunks[(map->mapX + spiral[0] + area) % area + (map->mapZ + spiral[1] + area) % area * area];

		if (c->cflags & CFLAG_HASMESH)
		{
			for (i = 0; i < c->maxy; i ++)
			{
				ChunkData cd = c->layer[i];
				if (cd) *prev = cd, prev = &cd->visible, meshAddToBank(cd);
			}
		}
	}
	*prev = NULL;
	meshAllocCmdBuffer(map);
	return;
	#endif

	// fprintf(stderr, "frame = %d, start = %d, %d, %d\n", map->frame+1, chunk->X, center[1]<<4, chunk->Z);

	if (center[1] < 0)
	{
		/* don't care: you are not supposed to be here anyway */
		return;
	}
	else if (center[1] >= chunk->maxy)
	{
		if (center[1] >= CHUNK_LIMIT)
		{
			/* higher than build limit: we need to get below build limit using geometry */
			vec4 dir = {0, -1, 0, 1};

			center[1] = CHUNK_LIMIT-1;

			matMultByVec(dir, globals.matInvMVP, dir);

			dir[VX] = dir[VX] / dir[VT] - camera[VX];
			dir[VY] = dir[VY] / dir[VT] - camera[VY];
			dir[VZ] = dir[VZ] / dir[VT] - camera[VZ];

			/* dir is now the vector coplanar with bottom plane (anglev - FOV/2 doesn't seem to work: slightly off :-/) */
			if (dir[1] >= 0)
			{
				/* camera is pointing up above build limit: no chunks can be visible in this configuration */
				return;
			}

			float DYSlope = (CHUNK_LIMIT*16 - camera[VY]) / dir[VY];
			int   chunkX  = CPOS(camera[VX] + dir[VX] * DYSlope) - (chunk->X>>4);
			int   chunkZ  = CPOS(camera[VZ] + dir[VZ] * DYSlope) - (chunk->Z>>4);
			int   area    = map->mapArea;
			int   half    = map->maxDist >> 1;

			if (chunkX < -half || chunkX > half ||
			    chunkZ < -half || chunkZ > half)
			    return;

			chunkX += map->mapX;
			chunkZ += map->mapZ;

			if (chunkX < 0)     chunkX += area; else
			if (chunkX >= area) chunkX -= area;
			if (chunkZ < 0)     chunkZ += area; else
			if (chunkZ >= area) chunkZ -= area;

			chunk = map->chunks + chunkX + chunkZ * area;
			center[0] = chunk->X;
			center[2] = chunk->Z;

			cur = chunk->layer[center[1]];
			if (cur) goto found_start;
		}
		cur = mapAllocFakeChunk(map);
		cur->Y = center[1] * 16;
		cur->chunk = chunk;
		chunk->layer[center[1]] = cur;
		// fprintf(stderr, "init fake chunk at %d, %d: %d\n", chunk->X, chunk->Z, center[1] << 4);
	}
	else cur = chunk->layer[center[1]];

	if (! cur) return;
	found_start:
	map->firstVisible = cur;
	map->chunkCulled  = 0;
	prev              = &map->firstVisible;
	cur->visible      = NULL;
	cur->comingFrom   = 127;
	frame             = ++ map->frame;
	chunk->chunkFrame = cur->frame = frame;
	map->genLast      = NULL;

	memset(chunk->outflags, UNVISITED, sizeof chunk->outflags);
	chunk->outflags[cur->Y>>4] |= VISIBLE;

	for (last = cur; cur; cur = *prev)
	{
		uint8_t outflags[9];
		int     i, neighbors;

		/* 1st pass: check if chunk corners are in frustum */
		chunk     = cur->chunk;
		center[1] = cur->Y >> 4;
		neighbors = mapGetOutFlags(map, cur, outflags);

		#if 0
		fprintf(stderr, "chunk %d, %d, %d: outflags = %d,%d,%d,%d,%d,%d,%d,%d\n", chunk->X, chunk->Z, cur->Y,
			outflags[0], outflags[1], outflags[2], outflags[3], outflags[4], outflags[5], outflags[6], outflags[7]);
		#endif

		/* up to 26 neighbor chunks can be added for the 8 corners */
		for (i = 1; neighbors; i ++, neighbors >>= 1)
		{
			if ((neighbors & 1) == 0) continue;
			ChunkData cd = mapAddToVisibleList(map, chunk, i, center[1], outflags, frame);
			if (cd)
			{
				/* by culling chunk early, it will prune huge branch of chunks from frustum */
				mapCullCave(cd, camera);
				//cd->comingFrom = 1;
				/* fake chunks (slot > 0) must not be culled (yet) */
				if (cd->comingFrom > 0 || cd->slot > 0)
				{
					last->visible = cd;
					last = cd;
					cd->frame = frame;
				}
				else /* ignore this chunk */
				{
					map->chunkCulled ++;
				}
			}
		}

		/* 2nd pass: try harder for chunks that have at least 2 corners out of frustum */
		if (outflags[8] >= 2)
		{
			static uint8_t faces[] = {
				/* numbers reference boxPts, order is S, E, N, W, T, B */
				3, 2, 7, 6,
				1, 3, 5, 7,
				0, 1, 4, 5,
				2, 0, 6, 4,
				4, 5, 6, 7,
				0, 1, 2, 3,
			};
			DATA8 p;
			for (i = 0, p = faces; i < sizeof faces/4; i ++, p += 4)
			{
				/* check if an entire face crosses a plane */
				uint8_t sector1 = outflags[p[0]];
				uint8_t sector2 = outflags[p[1]];
				uint8_t sector3 = outflags[p[2]];
				uint8_t sector4 = outflags[p[3]];

				if ((sector1*sector2*sector3*sector4) != 0 && /* all points of face must be outside frustum */
				    (sector1&sector2&sector3&sector4) == 0 && /* but not all outside the same plane */
				    (popcount(sector1 ^ sector2) >= 2 ||      /* segment is crossing 2 or more planes */
				     popcount(sector2 ^ sector4) >= 2 ||
				     popcount(sector3 ^ sector4) >= 2 ||
				     popcount(sector1 ^ sector3) >= 2))
				{
					/* face crosses a plane: add chunk connected to it to the visible list */
					ChunkData cd = mapAddToVisibleList(map, chunk, i+1, center[1], outflags, frame);
					if (cd)
					{
						mapCullCave(cd, camera);
						/* fake chunks (slot > 0) must not be culled (yet) */
						if (cd->comingFrom > 0 || cd->slot > 0)
						{
							#ifdef FRUSTUM_DEBUG
							fprintf(stderr, "extra chunk added: %d, %d [%d]\n", chunk->X, chunk->Z, cd->Y);
							#endif
							last->visible = cd;
							last = cd;
							cd->frame = frame;
						}
						else map->chunkCulled ++;
					}
				}
			}
		}

		if (cur->comingFrom & UNCERTAIN)
		{
			/* should not happend very often */
			static int8_t TB[] = {0,1,-1};
			int side = cur->comingFrom;
			Chunk nbor = cur->chunk + chunkNeighbor[cur->chunk->neighbor + (side & 15)];
			ChunkData neighbor = nbor->layer[(cur->Y >> 4) + TB[side >> 4]];
			if (neighbor && neighbor->frame == frame)
				cur->comingFrom &= UNCERTAIN;
			else
				goto skip;
		}

		if (cur->slot > 0 || cur->glBank == NULL)
		{
			/* fake or empty chunk: remove from list */
			skip:
			*prev = cur->visible;
			if (cur->slot > 0)
				mapFreeFakeChunk(cur);
		}
		else
		{
			meshWillBeRendered(cur);
//			fprintf(stderr, "adding chunk %d, %d, %d from %d to visible list\n",
//				chunk->X, cur->Y, chunk->Z, cur->comingFrom);
			prev = &cur->visible;
			cur->cdFlags &= ~ CDFLAG_DISCARDABLE;
			if (cur->glDiscard > 0)
			{
				float dx = camera[VX] - chunk->X;
				float dz = camera[VZ] - chunk->Z;
				if (dx * dx + dz * dz >= 100 * 100)
					cur->cdFlags |= CDFLAG_DISCARDABLE;
			}
		}
	}
	meshAllocCmdBuffer(map);
}
