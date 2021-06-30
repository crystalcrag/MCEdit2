/*
 * maps.c : anvil file format handling, load chunks according to player position
 *
 * Written by T.Pierron, jan 2020
 */

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
#include "maps.h"
#include "render.h"
#include "blocks.h"
#include "NBT2.h"
#include "particles.h"
#include "entities.h"


//#define SLOW_CHUNK_LOAD   /* load 1 chunk (entire column) per second */

static struct Frustum_t frustum;

/* given a direction encodded as bitfield (S, E, N, W), return offset of where that chunk is */
int16_t chunkNeighbor[16*9];

extern uint8_t openDoorDataToModel[];


/*
 * frustum culling static tables : this part is explained in a separate document: doc/internals.html
 */
void mapInitStatic(void)
{
	static uint8_t boxPts[] = {
		/* coords X, Z, Y */
		0, 0, 0,
		1, 0, 0,
		0, 1, 0,
		1, 1, 0,
		0, 0, 1,
		1, 0, 1,
		0, 1, 1,
		1, 1, 1,
	};
	int   i, j, k;
	DATA8 ptr;

	/* 8 corners of the box */
	for (i = 0, ptr = boxPts; i < 8; i ++, ptr += 3)
	{
		/* 7 boxes sharing that vertex (excluding the one we are in: 0,0,0) */
		for (j = 1; j < 8; j ++)
		{
			int8_t xoff = (j&1);
			int8_t zoff = (j&2)>>1;
			int8_t yoff = (j&4)>>2;

			if (ptr[0] == 0) xoff = -xoff;
			if (ptr[2] == 0) yoff = -yoff;
			if (ptr[1] == 0) zoff = -zoff;

			/* offset of neighbor chunk (-1, 0 or 1) */
			frustum.neighbors[i] |= 1 << ((xoff+1) + (zoff+1)*3 + (yoff+1)*9);
		}
	}

	for (i = 0; i < 27; i ++)
	{
		if (i == 13) continue; /* center, don't care */
		static uint8_t dirs[] = {8, 0, 2, 4, 0, 1, 32, 0, 16};
		int8_t x = i%3;
		int8_t z = (i/3)%3;
		int8_t y = i/9;
		frustum.chunkOffsets[i] = dirs[x] | dirs[z+3] | dirs[y+6];
	}

	/* firstFree[] table will tell for a given integer [0,255] which rightmost bit is set to 0 */
	for (i = 0; i < 256; i ++)
	{
		for (j = i, k = 0; j&1; k ++, j >>= 1);
		frustum.firstFree[i] = k;
	}
}

int mapFirstFree(DATA32 usage, int count)
{
	int base, i;
	for (i = count, base = 0; i > 0; i --, usage ++, base += 32)
	{
		uint32_t bits = *usage;
		int slot = frustum.firstFree[bits & 0xff];
		if (slot == 8)
		{
			bits >>= 8;
			slot += frustum.firstFree[bits & 0xff];
			if (slot == 16)
			{
				bits >>= 8;
				slot += frustum.firstFree[bits & 0xff];
				if (slot == 24)
				{
					slot += frustum.firstFree[bits >> 8];
					if (slot == 32) continue;
				}
			}
		}
		*usage |= 1 << slot;
		return base + slot;
	}
	return -1;
}

/* debug */
void printCoord(STRPTR hdr, BlockIter iter)
{
	int y = iter->offset;
	int x = y & 15; y >>= 4;
	int z = y & 15;
	if (hdr == NULL)
		fprintf(stderr, "%d, %d, %d\n", iter->ref->X + x, iter->yabs, iter->ref->Z + z);
	else if (iter->ref == NULL)
		fprintf(stderr, "%s NO CHUNK: %d, %d, %d\n", hdr, x, y, z);
	else
		fprintf(stderr, "%s: %d, %d, %d\n", hdr, iter->ref->X + x, iter->yabs, iter->ref->Z + z);
}

static int mapGetConnect(ChunkData cd, int offset, BlockState b)
{
	static int8_t XZoff[] = {
		0, 1, 1, -1, -1, -1, -1, 1
	};
	struct BlockIter_t iter;
	uint8_t neighbors[10];
	DATA8   n;
	int     i;

	mapInitIterOffset(&iter, cd, offset);

	for (i = 0, n = neighbors; i < DIM(XZoff); i += 2, n += 2)
	{
		mapIter(&iter, XZoff[i], 0, XZoff[i+1]);
		n[0] = iter.blockIds[iter.offset];
		n[1] = iter.blockIds[DATA_OFFSET + (iter.offset >> 1)];
		if (iter.offset & 1) n[1] >>= 4;
		else n[1] &= 15;
	}
	neighbors[8] = neighbors[9] = 0;
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
		1, 1,1,  1,0,-1,  -1,0,-1,  -1,0,1,
	};
	struct BlockIter_t iter;
	uint8_t neighbors[26];
	DATA8   n;
	int     i;

	mapInitIterOffset(&iter, cd, offset);

	for (i = 0, n = neighbors; i < DIM(XYZoff); i += 3, n += 2)
	{
		mapIter(&iter, XYZoff[i], XYZoff[i+1], XYZoff[i+2]);
		n[0] = iter.blockIds[iter.offset];
		n[1] = iter.blockIds[DATA_OFFSET + (iter.offset >> 1)];
		if (iter.offset & 1) n[1] >>= 4;
		else n[1] &= 15;
	}
	i = blockGetConnect(b, neighbors);
	if (i & 512)  i |= 5;
	if (i & 1024) i |= 10;
	return i & 15;
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
		if (blockId == ID(RSPISTONHEAD, 0) && chunkGetTileEntity(ref, (int[3]) {offset & 15, pos[1], (offset >> 4) & 15}))
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

	if (block->bboxPlayer == BBOX_NONE)
		return NULL;

	*cnxFlags = 0xffff;
	switch (block->special) {
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
			uint16_t neighbors[7];
			uint8_t  i;
			neighbors[3] = getBlockId(&iter);
			for (i = 0; i < 4; i ++)
			{
				static uint8_t offset[] = {6, 4, 0, 2};
				mapIter(&iter, xoff[i], 0, zoff[i]);
				neighbors[offset[i]] = getBlockId(&iter);
			}
			halfBlockGetBBox(neighbors - 10, bboxes, DIM(bboxes));
			*count = bboxes->cont;
			return bboxes;
		}
		break;
	case BLOCK_CHEST:
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
 *            2 = the segment lies in the plane
 */
int intersectRayPlane(vec4 P0, vec4 u, vec4 V0, vec norm, vec4 I)
{
	vec4 w = {P0[0]-V0[0], P0[1]-V0[1], P0[2]-V0[2], 1};

	float D =  vecDotProduct(norm, u);
	float N = -vecDotProduct(norm, w);

	if (fabs(D) < EPSILON) /* segment is parallel to plane */
		return 0;

	/* they are not parallel: compute intersect param */
	float sI = N / D;
	/* XXX we will check later if intersection is within plane */
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
		return n->type != SOLID || n->special == BLOCK_HALF;
	}
	return True;
}

/* find the block pointed by tracing a ray using direction <dir> or <yawPitch> */
Bool mapPointToBlock(Map map, vec4 camera, float * yawPitch, vec4 dir, vec4 ret, MapExtraData data)
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

	vec4 pos, u;
	if (dir == NULL)
	{
		/* use yaw-pitch to get our direction vector */
		float cv = cosf(yawPitch[1]);
		u[VX] = cosf(yawPitch[0]) * cv;
		u[VY] = sinf(yawPitch[1]);
		u[VZ] = sinf(yawPitch[0]) * cv;
		u[VT] = 1;
	}
	else memcpy(u, dir, sizeof u);
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
		for (i = 0, norm = normals, box = blockGetBBox(blockGetById(block)); i < 6; i ++, norm += 4)
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
						data->side = i;
						data->entity = entityRaycast(map->center, dir, camera, inter, ret);
						return data->entity == 0;
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
						data->topHalf = data->side < 4 && inter[1] - (int) inter[1] >= 0.5;
						ret[3] = 1;
						if (b->bboxId > 1)
						{
							/* the block we just checked was Air, now we need to check if the ray is intersecting a custom bounding box */
							check = 1;
							goto break_all;
						}
						data->entity = entityRaycast(map->center, dir, camera, inter, ret);
						return data->entity == 0;
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
	data->entity = entityRaycast(map->center, dir, camera, NULL, ret);
	return data->entity == 0;
}

/* DEBUG */
#if 0
static void mapShowChunks(Map map)
{
	Chunk c;
	int   i, size;
	fprintf(stderr, "=== map chunk loaded ===\n");
	for (i = 0, c = map->chunks, size = map->mapSize; i < size; i ++, c ++)
	{
		uint8_t flags = c->cflags;
		uint8_t bank = 0;
		if (flags & CFLAG_HASMESH)
		{
			int j;
			for (j = 0; j < c->maxy; j ++)
			{
				ChunkData cd = c->layer[j];
				if (cd && cd->glBank) bank = 1;
			}
		}
		fprintf(stderr, "%c",
			c == map->center ? '#' :
			(flags & (CFLAG_HASMESH|CFLAG_GOTDATA)) == (CFLAG_HASMESH|CFLAG_GOTDATA) ? (bank ? 'A' : '!') :
			flags & CFLAG_HASMESH ? 'C' :
			flags & CFLAG_GOTDATA ? 'L' : ' '
		);
		if (i % map->mapArea == map->mapArea-1) fputs("|\n", stderr);
	}
}
#else
#define mapShowChunks(x)
#endif

/*
 * dynamic chunk loading depending on player position
 */

static void mapRedoGenList(Map map)
{
	int8_t * spiral;
	int      XC   = CPOS(map->cx) << 4;
	int      ZC   = CPOS(map->cz) << 4;
	int      n    = map->maxDist * map->maxDist;
	int      area = map->mapArea;

	ListNew(&map->genList);

	for (spiral = frustum.spiral; n > 0; n --, spiral += 2)
	{
		Chunk c = &map->chunks[(map->mapX + spiral[0] + area) % area + (map->mapZ + spiral[1] + area) % area * area];
		if ((c->cflags & CFLAG_HASMESH) == 0)
		{
			c->X = XC + (spiral[0] << 4);
			c->Z = ZC + (spiral[1] << 4);
			ListAddTail(&map->genList, &c->next);
		}
		/* push entities into active list */
		else if ((c->cflags & CFLAG_HASENTITY) == 0)
		{
			//fprintf(stderr, "loading entities from %d, %d\n", c->X, c->Z);
			chunkExpandEntities(c);
		}
	}
}

/* unload entities from lazy chunk XXX should be kept in a special state */
static void mapMarkLazyChunk(Map map)
{
	int8_t * ptr;
	int      i, area = map->mapArea;

	for (i = frustum.lazyCount, ptr = frustum.lazy; i > 0; ptr += 2, i --)
	{
		Chunk c = &map->chunks[(map->mapX + ptr[0] + area) % area + (map->mapZ + ptr[1] + area) % area * area];

		if (c->cflags & CFLAG_HASENTITY)
		{
			if (c->entityList != ENTITY_END)
				entityUnload(c);
			c->cflags &= ~CFLAG_HASENTITY;
		}
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
			/* tp to a completely different location: clear everything */
			Chunk chunk;
			int   i;
			for (chunk = map->chunks, i = MAP_SIZE; i > 0; chunk ++, i --)
			{
				map->GPUchunk -= chunkFree(chunk);
			}
			/* reset map center */
			map->mapX = map->mapZ = area / 2;
		}
		else /* some chunks will still be useful */
		{
			int mapX = (map->mapX + dx + area) % area;
			int mapZ = (map->mapZ + dz + area) % area;

			if (dx)
			{
				int s = dx < 0 ? -1 : 1;
				int x = map->mapX - s * (area/2), i;
				for (; dx; dx -= s, x += s)
				{
					Chunk row = map->chunks + (x + area) % area;
					for (i = 0; i < area; i ++, row += area)
					{
						map->GPUchunk -= chunkFree(row);
					}
				}
			}
			if (dz)
			{
				int s = dz < 0 ? -1 : 1;
				int z = map->mapZ - s * (area/2), i;
				for (; dz; dz -= s, z += s)
				{
					Chunk row = map->chunks + ((z + area) % area) * area;
					for (i = 0; i < area; i ++, row ++)
					{
						map->GPUchunk -= chunkFree(row);
					}
				}
			}
			map->mapX = mapX;
			map->mapZ = mapZ;
		}
		mapRedoGenList(map);
		map->center = map->chunks + (map->mapX + map->mapZ * area);
		mapMarkLazyChunk(map);
		#ifdef DEBUG
		//fprintf(stderr, "new map center: %d, %d (%d,%d)\n", map->mapX, map->mapZ, (int) pos[VX], (int) pos[VZ]);
		//mapShowChunks(map);
		#endif
		return True;
	}
	return False;
}

/* load and convert chunk to mesh */
void mapGenerateMesh(Map map)
{
	#ifndef SLOW_CHUNK_LOAD
	ULONG start = TimeMS();
	#else
	/* artifially slow down chunk loading */
	static int delay;
	delay ++;
	if (delay > 1)
	{
		/* 50 frames == 1 second */
		if (delay > 50) delay = 0;
		return;
	}
	#endif

	if (map->genList.lh_Head == NULL)
		return;

	while (map->genList.lh_Head)
	{
		static uint8_t directions[] = {12, 4, 6, 8, 0, 2, 9, 1, 3};
		int i, j, X, Z;

		Chunk list = (Chunk) ListRemHead(&map->genList);
		memset(&list->next, 0, sizeof list->next);

		if (list->cflags & CFLAG_HASMESH)
			continue;

		/* load 8 surrounding chunks too (mesh generation will need this) */
		for (i = 0, X = list->X, Z = list->Z; i < DIM(directions); i ++)
		{
			int   dir  = directions[i];
			Chunk load = list + chunkNeighbor[list->neighbor + dir];

			/* already loaded ? */
			if ((load->cflags & CFLAG_GOTDATA) == 0)
			{
				if (chunkLoad(load, map->path, X + (dir & 8 ? -16 : dir & 2 ? 16 : 0),
						Z + (dir & 4 ? -16 : dir & 1 ? 16 : 0)))
					load->cflags |= CFLAG_GOTDATA;
			}
		}

		/* second: push data to the GPU (only the first chunk) */
		for (i = 0, j = list->maxy; j > 0; j --, i ++)
		{
			ChunkData cd = list->layer[i];
			if (cd)
			{
				/* this is the function that will convert chunk into triangles */
				chunkUpdate(list, map->air, i);
				renderFinishMesh(False);
				particlesChunkUpdate(map, cd);
				if (cd->cdflags == CDFLAG_PENDINGDEL)
				{
					/* link within chunk has already been removed in chunkUpdate() */
					free(cd);
				}
				else if (cd->glBank)
				{
					map->GPUchunk ++;
					list->cflags |= CFLAG_HASMESH;
				}
			}
		}

		if ((list->cflags & CFLAG_HASENTITY) == 0)
			chunkExpandEntities(list);

		/* we are in the main rendering loop: don't hog the CPU for too long */
		#ifndef SLOW_CHUNK_LOAD
		if (TimeMS() - start > 15)
		#endif
			break;
	}
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
	int   i, j, n, dist = area - 4;

	if (chunks)
	{
		/* should be property of a map... */
		int8_t * ptr = realloc(frustum.spiral, dist * dist * 2 + (dist * 4 + 4) * 2);

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
			for (ptr = frustum.lazy, j = 0, dist += 2, i = dist >> 1; j < dist; j ++, ptr += 4)
			{
				ptr[0] = ptr[2] = j - i;
				ptr[1] = - i;
				ptr[3] =   i;
			}
			for (j = 0, dist -= 2; j < dist; j ++, ptr += 4)
			{
				ptr[1] = ptr[3] = j - (dist >> 1);
				ptr[0] = - i;
				ptr[2] =   i;
			}
			frustum.lazyCount = (ptr - frustum.lazy) >> 1;

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
	int area = (maxDist * 2) + 5;

	if (area == map->mapArea) return True;
	if (area < 7 || area > 63) return False; /* too small/too big */

	Chunk chunks = mapAllocArea(area);

	fprintf(stderr, "setting map size to %d (from %d)\n", area, map->mapArea);

	if (chunks)
	{
		/* we have all the memory we need: can't fail from this point */
		int oldArea = map->mapArea;
		int size    = ((oldArea < area ? oldArea : area) - 2) >> 1;
		int XZmid   = area >> 1;
		int loaded  = 0;
		int i, j, k;

		/* copy chunk information (including lazy chunks) */
		for (j = -size; j <= size; j ++)
		{
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
				memcpy(dest, source, sizeof *dest);
				source->cflags = 0;

				/* ChunkData ref needs to be readjusted */
				for (k = dest->maxy-1; k >= 0; k --)
				{
					ChunkData cd = dest->layer[k];
					cd->chunk = dest;
					loaded += cd->glBank != NULL;
				}
			}
		}

		int freed = 0;
		int genlist = 0;
		if (oldArea > area)
		{
			Chunk old;
			/* need to free chunk outside new render dist */
			for (i = 0, j = oldArea * oldArea, old = map->chunks; i < j; old ++, i ++)
			{
				if (old->cflags & (CFLAG_HASMESH|CFLAG_GOTDATA))
					chunkFree(old), freed ++;
			}
		}

		free(map->chunks);
		map->maxDist  = area - 4;
		map->mapArea  = area;
		map->mapSize  = area * area;
		map->mapZ     = map->mapX = XZmid;
		map->chunks   = chunks;
		map->GPUchunk = loaded;
		map->center   = map->chunks + map->mapX + map->mapZ * area;
		if (oldArea < area || map->genList.lh_Head)
		{
			mapRedoGenList(map);
			for (chunks = HEAD(map->genList); chunks; genlist ++, NEXT(chunks));
		}
		//if (map->genList.lh_Head == NULL)
		//	mapShowChunks(map);
		//fprintf(stderr, "chunk free = %d, genlist = %d, maxDist = %d\n", freed, genlist, map->maxDist);
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
	Map map = calloc(sizeof *map + sizeof *map->air + MIN_SECTION_MEM, 1);

	if (map)
	{
		ChunkData air = (ChunkData) (map + 1);
		map->maxDist = renderDist * 2 + 1;
		map->mapArea = renderDist * 2 + 5;
		map->mapSize = map->mapArea * map->mapArea;
		map->mapZ    = map->mapX = renderDist + 2;
		map->air     = air;

		/* all tables but skyLight will be 0 */
		air->blockIds = (DATA8) (air+1);
		/* fully lit */
		memset(air->blockIds + SKYLIGHT_OFFSET, 255, 2048);

		map->chunks = mapAllocArea(map->mapArea);
		map->center = map->chunks + (map->mapX + map->mapZ * map->mapArea);

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

		if (NBT_ToFloat(&nbt, NBT_FindNode(&nbt, 0, "pos"), xyz, 3))
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
		Chunk    c;
		int8_t * spiral;
		int      XC = CPOS(map->cx) << 4;
		int      ZC = CPOS(map->cz) << 4;
		int      n;
		for (spiral = frustum.spiral, n = map->maxDist * map->maxDist; n > 0; n --, spiral += 2)
		{
			c = &map->chunks[map->mapX + spiral[0] + (map->mapZ + spiral[1]) * map->mapArea];
			c->X = XC + (spiral[0] << 4);
			c->Z = ZC + (spiral[1] << 4);
			ListAddTail(&map->genList, &c->next);
		}

		fprintf(stderr, "center = %d, %d\n", map->center->X, map->center->Z);

		return map;
	}
	free(map);
	return NULL;
}

/* save any changes made to levelDat */
Bool mapSaveLevelDat(Map map)
{
	TEXT path[128];
	TEXT copy[128];
	CopyString(path, map->path, sizeof path);
	ParentDir(path);
	strcpy(copy, path);
	AddPart(path, "level.dat", sizeof path);
	AddPart(copy, "level.dat_old", sizeof copy);

	/* make a copy of level.dat first */
	return FileCopy(path, copy, True) && NBT_Save(&map->levelDat, path, NULL, 0) > 0;
}

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
	*prev = NULL;
	map->needSave = chunk;
	return ret;
}

NBTHdr mapLocateItems(MapExtraData sel)
{
	Chunk c = sel->chunk;
	int   offset = sel->offset;
	int   XYZ[3];
	DATA8 tile;

	XYZ[0] = offset & 15; offset >>= 4;
	XYZ[2] = offset & 15; offset >>= 4;
	XYZ[1] = offset + sel->cd->Y;

	tile = chunkGetTileEntity(c, XYZ);

	if (tile)
	{
		NBTFile_t nbt = {.mem = tile};
		offset = NBT_FindNode(&nbt, 0, "Items");
		if (offset >= 0)
			return (NBTHdr) (tile + offset);
	}
	return NULL;
}

/* read TileEntities.Items from a container */
void mapDecodeItems(Item container, int count, NBTHdr hdrItems)
{
	DATA8 mem;
	int   index;

	memset(container, 0, sizeof *container * count);

	if (hdrItems)
	for (index = hdrItems->count, mem = NBT_MemPayload(hdrItems); index > 0; index --)
	{
		NBTIter_t properties;
		NBTFile_t nbt = {.mem = mem};
		ItemBuf   item;
		int       off;
		memset(&item, 0, sizeof item);
		NBT_IterCompound(&properties, nbt.mem);
		while ((off = NBT_Iter(&properties)) >= 0)
		{
			switch (FindInList("id,Slot,Count,Damage", properties.name, 0)) {
			case 0:  item.id = itemGetByName(NBT_Payload(&nbt, off), True); break;
			case 1:  item.slot = NBT_ToInt(&nbt, off, 255); break;
			case 2:  item.count = NBT_ToInt(&nbt, off, 1); break;
			case 3:  item.uses = NBT_ToInt(&nbt, off, 0); break;
			default: if (! item.extra) item.extra = nbt.mem;
			}
		}
		if (item.id < ID(256, 0))
		{
			/* select a state with an inventory model */
			BlockState state = blockGetById(item.id);
			if (state->invId == 0)
			{
				Block b = &blockIds[item.id >> 4];
				if (b->special == BLOCK_TALLFLOWER)
					/* really weird state values :-/ */
					item.id += 10;
				else
					item.id = (item.id & ~15) | b->invState;
			}
		}
		if (item.uses > 0 && itemMaxDurability(&item) < 0)
			/* damage means meta-data from these items :-/ */
			item.id += item.uses, item.uses = 0;
		if (item.slot < count)
		{
			off = item.slot;
			item.slot = 0;
			container[off] = item;
		}
		mem += properties.offset;
	}
}

/* store information back into NBT structure */
Bool mapSerializeItems(MapExtraData sel, STRPTR listName, Item items, int itemCount, NBTFile ret)
{
	TEXT itemId[128];
	int  i;

	memset(ret, 0, sizeof *ret);
	ret->page = 127;

	if (sel)
	{
		Chunk c = sel->chunk;
		DATA8 tile;
		int   XYZ[3];
		int   offset = sel->offset;

		XYZ[0] = offset & 15; offset >>= 4;
		XYZ[2] = offset & 15; offset >>= 4;
		XYZ[1] = offset + sel->cd->Y;

		tile = chunkGetTileEntity(c, XYZ);

		if (tile)
		{
			/* quote tags from original tile entity */
			NBTIter_t iter;
			NBT_IterCompound(&iter, tile);
			while ((i = NBT_Iter(&iter)) >= 0)
			{
				if (strcasecmp(iter.name, listName))
					NBT_Add(ret, TAG_Raw_Data, NBT_HdrSize(tile+i), tile + i, TAG_End);
			}
		}
		else
		{
			/* not yet created: add required fields */
			NBT_Add(ret,
				TAG_String, "id", itemGetTechName(sel->blockId & ~15, itemId, sizeof itemId),
				TAG_Int,    "x",  XYZ[0] + c->X,
				TAG_Int,    "y",  XYZ[1],
				TAG_Int,    "z",  XYZ[2] + c->Z,
				TAG_End
			);
		}
	}

	int count;
	for (i = 0, count = 0; i < itemCount; count += items[i].id > 0, i ++);

	NBT_Add(ret,
		TAG_List_Compound, listName, count,
		TAG_End
	);

	for (i = 0; i < itemCount; i ++, items ++)
	{
		if (items->id == 0) continue;
		int id   = items->id;
		int data = id & 15;
		/* data (or damage) is only to get a specific inventory model: not needed in NBT */
		if (id <= ID(256, 0))
		{
			Block b = &blockIds[id>>4];
			if (b->invState == data)
				data = 0;
			else if (b->special == BLOCK_TALLFLOWER)
				data -= 10;
		}
		NBT_Add(ret,
			TAG_String, "id",     itemGetTechName(id & ~15, itemId, sizeof itemId),
			TAG_Byte,   "Slot",   i,
			TAG_Short,  "Damage", itemMaxDurability(items) > 0 ? items->uses : data,
			TAG_Byte,   "Count",  items->count,
			TAG_End
		);
		if (items->extra)
		{
			/* merge entries we didn't care about */
			NBTIter_t iter;
			DATA8     mem;
			int       off;
			NBT_IterCompound(&iter, mem = items->extra);
			while ((off = NBT_Iter(&iter)) >= 0)
			{
				if (FindInList("id,Slot,Count,Damage", iter.name, 0) >= 0)
					/* these one already are */
					continue;

				NBT_Add(ret, TAG_Raw_Data, NBT_HdrSize(mem+off), mem + off, TAG_End);
			}
		}
		NBT_Add(ret, TAG_Compound_End);
	}
	if (sel)
		NBT_Add(ret, TAG_Compound_End);

	return True;
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
	ret->blockId = iter.blockIds[iter.offset] << 4;
	ret->chunk   = iter.ref;
	ret->cd      = iter.cd;

	return offset;
}

/*
 * Frustum culling: the goal of these functions is to create a linked list of chunks
 * representing all the ones that are visible in the current view matrix (MVP)
 * check doc/internals.html for explanation on how this part works.
 */

#define FAKE_CHUNK_SIZE     (offsetof(struct ChunkData_t, blockIds)) /* fields after blockIds are completely useless for frustum */
#define UNVISITED           0x40
#define VISIBLE             0x80
//#define FRUSTUM_DEBUG

#if 0
static void mapPrintUsage(ChunkFake cf, int dir)
{
	uint32_t flags, i;
	cf->total += dir;
	fprintf(stderr, "%c%d: ", dir < 0 ? '-' : '+', cf->total);
	for (flags = cf->usage, i = 0; i < 16; i ++, flags >>= 1)
		fputc(flags & 1 ? '1' : '0', stderr);
	fputc('\n', stderr);
}
#else
#define mapPrintUsage(x, y)
#endif

static ChunkData mapAllocFakeChunk(Map map)
{
	ChunkFake * prev;
	ChunkFake   cf;
	ChunkData   cd;
	uint32_t    usage;
	int         slot;

	for (prev = &map->cdPool, cf = *prev; cf && cf->usage == 0xffffffff; prev = &cf->next, cf = cf->next);

	if (cf == NULL)
	{
		cf = calloc(sizeof *cf + FAKE_CHUNK_SIZE * 32, 1);
		if (cf == NULL) return NULL;
		*prev = cf;
	}

	usage = cf->usage;
	slot = frustum.firstFree[usage & 0xff];
	if (slot == 8)
	{
		usage >>= 8;
		slot += frustum.firstFree[usage & 0xff];
		if (slot == 16)
		{
			usage >>= 8;
			slot += frustum.firstFree[usage & 0xff];
			if (slot == 24)
				slot += frustum.firstFree[usage >> 8];
		}
	}

	cd = (ChunkData) (cf->buffer + FAKE_CHUNK_SIZE * slot);
	memset(cd, 0, FAKE_CHUNK_SIZE);
	cd->slot   = slot+1;
	cf->usage |= 1<<slot;

	mapPrintUsage(cf, 1);

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
	mapPrintUsage(cf, -1);
}

static int mapGetOutFlags(Map map, ChunkData cur, DATA8 outflags, int max)
{
	uint8_t out, i, sector;
	Chunk   chunk = cur->chunk;
	int     layer = cur->Y >> 4;
	int     neighbors = 0;
	for (i = out = neighbors = 0; i < max; i ++)
	{
		static uint8_t dir[] = {0, 2, 1, 3, 16, 16+2, 16+1, 16+3};
		Chunk neighbor = chunk + chunkNeighbor[chunk->neighbor + (dir[i] & 15)];
		if (neighbor->chunkFrame != map->frame)
		{
			memset(neighbor->outflags, UNVISITED, sizeof neighbor->outflags);
			neighbor->cdIndex = 255;
			neighbor->chunkFrame = map->frame;
		}
		int Y = layer + (dir[i]>>4);
		if ((sector = neighbor->outflags[Y]) & UNVISITED)
		{
			vec4 point = {neighbor->X, Y << 4, neighbor->Z, 1};

			sector &= ~0x7f;
			/* XXX global var for MVP, should be a function param */
			matMultByVec(point, frustum.mvp, point);
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
			neighbors |= frustum.neighbors[i];
		else
			out ++;
		outflags[i] = sector;
	}
	outflags[i] = out;
	return neighbors;
}

static ChunkData mapAddToVisibleList(Map map, Chunk from, int direction, int layer, int frame)
{
	static int8_t dir[] = {0, 1, -1};
	uint8_t offset = frustum.chunkOffsets[direction];
	Chunk c = from + chunkNeighbor[from->neighbor + (offset & 15)];
	Chunk center = map->center;
	ChunkData cd;

	int X = c->X - center->X;
	int Z = c->Z - center->Z;
	int Y = layer + dir[offset >> 4];
	int half = (map->maxDist >> 1) << 4;

	/* outside of render distance */
	if (X < -half || X > half || Z < -half || Z > half || Y < 0)
		return NULL;

	if (c->chunkFrame != frame)
	{
		memset(c->outflags, UNVISITED, sizeof c->outflags);
		c->cdIndex = 255;
		c->chunkFrame = frame;
	}

	/*
	 * special case: there might be chunks below but they are currently outside of the frustum.
	 * As we get farther from the camera, we might eventually intersect with these, that's why we have
	 * to add those fake chunks along the bottom plane of the frustum.
	 */
	if (Y >= c->maxy)
	{
		if (Y >= CHUNK_LIMIT)
			return NULL;
		struct ChunkData_t dummy;
		uint8_t out[9];
		dummy.chunk = c;
		dummy.Y = Y << 4;

		/* note: at this point, we know that the chunk is intersecting with the frustum */
		mapGetOutFlags(map, &dummy, out, 8);

		/* but, we only want chunks that are intersecting the bottom plane of the frustum */
		if (Y < c->cdIndex && c->outflags[Y] < VISIBLE)
		{
			/* fake chunks are alloced above ground, make sure we going downward if we alloc some */
			c->cdIndex = Y;
			cd = mapAllocFakeChunk(map);
			cd->Y = Y * 16;
			cd->chunk = c;
			c->layer[Y] = cd;
			#ifdef FRUSTUM_DEBUG
			fprintf(stderr, "alloc fake chunk at %d, %d: %d [from %d, %d, %d: %x,%x,%x,%x]\n", c->X, c->Z, Y, from->X, from->Z, layer, out[0], out[1], out[2], out[3]);
			#endif
		}
		else return NULL;
	}
	else
	{
		if ((c->cflags & CFLAG_HASMESH) == 0)
		{
			/* move to front of list of chunks needed to be generated */
			if (c->chunkFrame != frame)
			{
				ListRemove(&map->genList, &c->next);
				ListInsert(&map->genList, &c->next, (ListNode *) map->genLast);
				map->genLast = c;
				c->chunkFrame = frame;
			}
			return NULL;
		}
//		if (c->cdIndex > Y)
//			c->cdIndex = Y;
		cd = c->layer[Y];
	}
	if (cd && c->outflags[Y] < VISIBLE)
	{
		/* not visited yet */
		c->outflags[Y] |= VISIBLE;
		cd->visible = NULL;
		return cd;
	}
	return NULL;
}

void mapViewFrustum(Map map, mat4 mvp, vec4 camera)
{
	ChunkData * prev;
	ChunkData   cur, last;
	Chunk       chunk;
	int         frame;
	int         center[3];

	chunk = map->center;
	frustum.mvp = mvp;

	center[1] = CPOS(camera[1]);
	center[0] = chunk->X;
	center[2] = chunk->Z;

	map->firstVisible = NULL;
	map->genLast = NULL;
	renderClearBank();

	#if 0
	/*
	 * DEBUG: all chunks that have a mesh will be sent to GPU. If frustum culling function is FUBAR'ed
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
				if (cd) *prev = cd, prev = &cd->visible, renderAddToBank(cd);
			}
		}
	}
	*prev = NULL;
	renderAllocCmdBuffer();
	return;
	#endif

//	fprintf(stderr, "\nframe = %d\n", map->frame+1);

	if (center[1] < 0)
	{
		/* use trigonometry to find the first chunk */
		fprintf(stderr, "TODO\n");
		return;
	}
	else if (center[1] >= chunk->maxy)
	{
		if (center[1] >= CHUNK_LIMIT)
		{
			/* higher than build limit: we need to get below build limit using geometry */
			vec4 dir = {0, -1, 0, 1};

			center[1] = CHUNK_LIMIT-1;

			/* inverse MVP matrix is stored just after MVP matrix :-/ */
			matMultByVec(dir, mvp + 16, dir);

			/* dir is now the vector coplanar with bottom plane (anglev - FOV/2 doesn't seem to work: slightly off :-/) */
			if (dir[1] >= 0)
			{
				/* camera is pointing up above build limit: no chunks can be visible in this configuration */
				return;
			}

			float DYSlope = (CHUNK_LIMIT*16 - camera[1]) / dir[1];
			int   chunkX  = CPOS(camera[0] + dir[0] * DYSlope) - (chunk->X>>4);
			int   chunkZ  = CPOS(camera[2] + dir[2] * DYSlope) - (chunk->Z>>4);
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
//		fprintf(stderr, "init fake chunk at %d, %d: %d\n", chunk->X, chunk->Z, center[1]);
	}
	else cur = chunk->layer[center[1]];

	if (! cur) return;
	found_start:
	map->firstVisible = cur;
	prev = &map->firstVisible;
	frame = ++ map->frame;
	cur->visible = NULL;
	chunk->chunkFrame = frame;
	memset(chunk->outflags, UNVISITED, sizeof chunk->outflags);
	chunk->cdIndex = 255;
	chunk->outflags[cur->Y>>4] |= VISIBLE;

	for (last = cur; cur; cur = cur->visible)
	{
		uint8_t outflags[9];
		Chunk   chunk;
		int     i, neighbors;

//		if (cur->chunk->X == -160 && cur->chunk->Z == -32)
//			puts("here");

		/* 1st pass: check if chunk corners are in frustum */
		chunk     = cur->chunk;
		center[1] = cur->Y >> 4;
		neighbors = mapGetOutFlags(map, cur, outflags, 8);

		#ifdef FRUSTUM_DEBUG
		fprintf(stderr, "chunk %d, %d, %d: outflags = %d,%d,%d,%d,%d,%d,%d,%d\n", cur->chunk->X, cur->chunk->Z, cur->Y,
			outflags[0], outflags[1], outflags[2], outflags[3], outflags[4], outflags[5], outflags[6], outflags[7]);
		#endif

		/* up to 26 neighbor chunks can be added for the 8 corners */
		for (i = 0; neighbors; i ++, neighbors >>= 1)
		{
			if ((neighbors & 1) == 0) continue;
			ChunkData cd = mapAddToVisibleList(map, chunk, i, center[1], frame);
			if (cd)
			{
				last->visible = cd;
				last = cd;
			}
		}

		/* 2nd pass: try harder for chunks that have at least 2 corners out of frustum */
		if (outflags[8] >= 2)
		{
			static uint8_t faces[] = {
				/* numbers reference boxPts, order is S, E, N, W, T, B */
				0, 1, 4, 5,
				1, 3, 5, 7,
				3, 2, 7, 6,
				2, 0, 6, 4,
				4, 5, 6, 7,
				0, 1, 2, 3
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
					static uint8_t faceDir[] = {10, 14, 16, 12, 22, 4};
					ChunkData cd = mapAddToVisibleList(map, chunk, faceDir[i], center[1], frame);
					if (cd)
					{
						#ifdef FRUSTUM_DEBUG
						fprintf(stderr, "extra chunk added: %d, %d [%d]\n", cd->chunk->X, cd->chunk->Z, cd->Y);
						#endif
						last->visible = cd;
						last = cd;
					}
				}
			}
		}

		if (cur->slot > 0 || cur->glBank == NULL)
		{
			/* fake or empty chunk: remove from list */
			if (cur->slot > 0)
				mapFreeFakeChunk(cur);
			*prev = cur->visible;
		}
		else prev = &cur->visible, renderAddToBank(cur);
	}

	renderAllocCmdBuffer();
}
