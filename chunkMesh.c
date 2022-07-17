/*
 * chunkMesh.c: very important part: convert a sub-chunk into a mesh of triangles.
 *              See internals.html for a quick explanation on how this part works.
 *
 * Reader warning: Look up table hell.
 *
 * Written by T.Pierron, jan 2020.
 */

#define CHUNK_IMPL
#include <stdio.h>
#include <stddef.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "meshBanks.h"
#include "blocks.h"
#include "render.h"
#include "entities.h"
#include "tileticks.h"
#include "NBT2.h"
#include "sign.h"
#include "particles.h"

uint8_t cubeVertex[] = { /* 8 vertices of a 1x1x1 cube */
	0,0,1,  1,0,1,  1,1,1,  0,1,1,
	0,0,0,  1,0,0,  1,1,0,  0,1,0,
};
uint8_t cubeIndices[6*4] = { /* face (quad) of cube: S, E, N, W, T, B */
	9, 0, 3, 6,    6, 3, 15, 18,     18, 15, 12, 21,     21, 12, 0, 9,    21, 9, 6, 18,      0, 12, 15, 3
/*  3, 0, 1, 2,    2, 1,  5,  6,      6,  5,  4,  7,      7,  4, 0, 3,     7, 3, 2,  6,      0,  4,  5, 1 */
};
uint8_t texCoord[] = { /* tex coord for each face: each line is a rotation, indexed by (Block.rotate&3)*8 */
	0,0,    0,1,    1,1,    1,0,
	0,1,    1,1,    1,0,    0,0,
	1,1,    1,0,    0,0,    0,1,
	1,0,    0,0,    0,1,    1,1,
};
uint8_t skyBlockOffset[] = { /* where to get skylight to shade a vertex of a cube: grab max of 4 values per vertex */
	15, 16, 25, 24,    6,  7, 16, 15,    7,  8, 16, 17,    16, 17, 25, 26,
	14, 17, 23, 26,    5, 14, 17,  8,    5, 11, 14,  2,    11, 14, 23, 20,
	10, 11, 19, 20,    1, 10, 11,  2,    1,  9, 10,  0,     9, 10, 19, 18,
	 9, 12, 21, 18,    3,  9, 12,  0,    3, 12, 15,  6,    12, 15, 21, 24,
	19, 21, 22, 18,   21, 22, 25, 24,   22, 23, 25, 26,    19, 22, 23, 20,
	 3,  4,  7,  6,    1,  3,  4,  0,    1,  4,  5, 2,      4,  5,  7,  8
};
uint8_t quadIndices[] = { /* coord within <vertex> to make a quad from a QUAD block type */
	 9,  0, 15, 18,       /* QUAD_CROSS */
	21, 12,  3,  6,       /* QUAD_CROSS (2nd part) */
	 9,  0,  3,  6,       /* QUAD_SQUARE */
	 6,  3, 15, 18,       /* QUAD_SQUARE2 */
	18, 15, 12, 21,       /* QUAD_SQUARE3 */
	21, 12,  0,  9,       /* QUAD_SQUARE4 */
	21, 12, 15, 18,       /* QUAD_NORTH */
	 6,  3,  0,  9,       /* QUAD_SOUTH */
	18, 15,  3,  6,       /* QUAD_EAST */
	 9,  0, 12, 21,       /* QUAD_WEST */
	12,  0,  3, 15,       /* QUAD_BOTTOM */
	18, 12,  0,  6,       /* QUAD_ASCE */
	 9,  3, 15, 21,       /* QUAD_ASCW */
	21,  0,  3, 18,       /* QUAD_ASCN */
	 6, 15, 12,  9,       /* QUAD_ASCS */
};

/* normal vector for given quad type (QUAD_*); note: 6 = none */
uint8_t quadSides[] = {6, 6, 2, 3, 0, 1, 0, 2, 3, 1, 4, 4, 4, 4, 4};

uint8_t openDoorDataToModel[] = {
	5, 6, 7, 4, 3, 0, 1, 2
};

static uint8_t offsetConnected[] = { /* S, E, N, W, T, B (4 coords per face) */
	9+13, 1+13, -9+13, -1+13,     9+13, -3+13, -9+13,  3+13,    9+13, -1+13, -9+13,  1+13,
	9+13, 3+13, -9+13, -3+13,    -3+13,  1+13,  3+13, -1+13,    3+13,  1+13, -3+13, -1+13
};
int8_t cubeNormals[] = { /* normal per face */
	 0,  0,  1, 0,
	 1,  0,  0, 0,
	 0,  0, -1, 0,
	-1,  0,  0, 0,
	 0,  1,  0, 0,
	 0, -1,  0, 0
};

/* check which face has a hole in it */
uint8_t slotsY[] = {1<<SIDE_BOTTOM,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1<<SIDE_TOP};
uint8_t slotsXZ[256];

#define VTX_1      (BASEVTX + ORIGINVTX)
#define VTX_0      ORIGINVTX
uint8_t  axisCheck[] = {2, 0, 2, 0, 1, 1};
uint16_t axisAlign[] = {VTX_1, VTX_1, VTX_0, VTX_0, VTX_1, VTX_0};
#undef VTX_0
#undef VTX_1
static uint8_t norm2axis1[] = {2, 0, 2, 0, 0, 0};
static uint8_t norm2axis2[] = {1, 1, 1, 1, 2, 2};

static int8_t  subChunkOff[64];
static uint8_t oppositeMask[64];
static int16_t blockOffset[64];
static int16_t blockOffset2[64];

/* this table is used to list neighbor chunks, if a block is updated at a boundary (180 bytes) */
uint32_t chunkNearby[] = {
0x00000000, 0x00008000, 0x00002000, 0x0001a000, 0x00000400, 0x00000000,
0x00002c00, 0x00000000, 0x00001000, 0x0000d000, 0x00000000, 0x00000000,
0x00001600, 0x00000000, 0x00000000, 0x00000000, 0x00200000, 0x01208000,
0x00602000, 0x0361a000, 0x00240400, 0x00000000, 0x006c2c00, 0x00000000,
0x00301000, 0x01b0d000, 0x00000000, 0x00000000, 0x00361600, 0x00000000,
0x00000000, 0x00000000, 0x00000010, 0x00008090, 0x00002030, 0x0001a1b0,
0x00000412, 0x00000000, 0x00002c36, 0x00000000, 0x00001018, 0x0000d0d8,
0x00000000, 0x00000000, 0x0000161b
};

/*
 * cave culling tables: see internals.html to know how these tables work
 */

/* given a S,E,N,W,T,B bitfield, will give what face connections we can reach */
uint16_t faceCnx[] = {
0, 0, 0, 1, 0, 2, 32, 35, 0, 4, 64, 69, 512, 518, 608, 615, 0, 8, 128, 137, 1024,
1034, 1184, 1195, 4096, 4108, 4288, 4301, 5632, 5646, 5856, 5871, 0, 16, 256, 273,
2048, 2066, 2336, 2355, 8192, 8212, 8512, 8533, 10752, 10774, 11104, 11127, 16384,
16408, 16768, 16793, 19456, 19482, 19872, 19899, 28672, 28700, 29120, 29149, 32256,
32286, 32736, 32767
};

/* given two faces (encoded as bitfield S,E,N,W,T,B), return connection bitfield */
uint16_t hasCnx[] = {
0, 0, 0, 1, 0, 2, 32, 0, 0, 4, 64, 0, 512, 0, 0, 0, 0, 8, 128, 0, 1024, 0, 0, 0,
4096, 0, 0, 0, 0, 0, 0, 0, 0, 16, 256, 0, 2048, 0, 0, 0, 8192, 0, 0, 0, 0, 0, 0,
0, 16384, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

uint8_t mask8bit[] = {0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01};

/* used to compute light for CUST model */
#define DXYZ(dx,dy,dz)       (dx+1) | ((dy+1)<<2) | ((dz+1)<<4)
uint8_t sampleOffset[48] = { /* S, E, N, W, T, B */
	DXYZ( 0,-1,-1), DXYZ(-1, 1, 1), DXYZ( 0, 0,-1), DXYZ(-1,-1, 1), DXYZ(-1, 0,-1), DXYZ( 1,-1, 1), DXYZ(-1,-1,-1), DXYZ( 1, 1, 1),
	DXYZ(-1,-1,-1), DXYZ( 1, 1, 1), DXYZ(-1, 0,-1), DXYZ( 1,-1, 1), DXYZ(-1, 0, 0), DXYZ( 1,-1,-1), DXYZ(-1,-1, 0), DXYZ( 1, 1,-1),
	DXYZ(-1,-1, 0), DXYZ( 1, 1,-1), DXYZ(-1, 0, 0), DXYZ( 1,-1,-1), DXYZ( 0, 0, 0), DXYZ(-1,-1,-1), DXYZ( 0,-1, 0), DXYZ(-1, 1,-1),
	DXYZ( 0,-1, 0), DXYZ(-1, 1,-1), DXYZ( 0, 0, 0), DXYZ(-1,-1,-1), DXYZ( 0, 0,-1), DXYZ(-1,-1, 1), DXYZ( 0,-1,-1), DXYZ(-1, 1, 1),
	DXYZ( 0,-1, 0), DXYZ(-1, 1,-1), DXYZ( 0,-1,-1), DXYZ(-1, 1, 1), DXYZ(-1,-1,-1), DXYZ( 1, 1, 1), DXYZ(-1,-1, 0), DXYZ( 1, 1,-1),
	DXYZ( 0, 0,-1), DXYZ(-1,-1, 1), DXYZ( 0, 0, 0), DXYZ(-1,-1,-1), DXYZ(-1, 0, 0), DXYZ( 1,-1,-1), DXYZ(-1, 0,-1), DXYZ( 1,-1, 1),
};
#undef DXYZ

/* yep, more look-up table init */
void chunkInitStatic(void)
{
	int8_t i, x, z;
	int    pos;

	for (i = 0; i < 64; i ++)
	{
		int8_t layer = 0;
		if (i & 16) layer ++;
		if (i & 32) layer --;
		subChunkOff[i] = layer;
		pos = 0;
		if (i & 1) pos -= 15*16;
		if (i & 2) pos -= 15;
		if (i & 4) pos += 15*16;
		if (i & 8) pos += 15;
		if (i & 16) pos -= 15*256;
		if (i & 32) pos += 15*256;
		blockOffset[i] = pos;

		pos = 0;
		if (i & 1) pos += 16;
		if (i & 2) pos ++;
		if (i & 4) pos -= 16;
		if (i & 8) pos --;
		if (i & 16) pos += 256;
		if (i & 32) pos -= 256;
		blockOffset2[i] = pos;

		pos = 0;
		if (i & 1)  pos |= 4;
		if (i & 2)  pos |= 8;
		if (i & 4)  pos |= 1;
		if (i & 8)  pos |= 2;
		if (i & 16) pos |= 32;
		if (i & 32) pos |= 16;
		oppositeMask[i] = pos;
	}

	for (pos = 0; pos < 256; pos ++)
	{
		x = pos & 15;
		z = pos >> 4;
		slotsXZ[pos] = (x == 0 ? 1 << SIDE_WEST  : x == 15 ? 1 << SIDE_EAST  : 0) |
		               (z == 0 ? 1 << SIDE_NORTH : z == 15 ? 1 << SIDE_SOUTH : 0);
	}
}

/* check doc/internals.html to have an overview on how particle emitters are managed */
static void chunkAddEmitters(ChunkData cd, int interval, int pos, int type, DATA16 emitters)
{
	/* list[0] == number of emitters, list[1] == capacity of list (CHUNK_EMIT_SIZE items) */
	DATA16 list = cd->emitters;
	if (emitters[type] > 0)
	{
		/* maybe already in the list */
		DATA16 emit = list + emitters[type];
		if (emit[1] != interval)
		{
			/* same type, but different interval, check if there is one with the same interval */
			DATA16 eof;
			for (eof = list + list[0] * CHUNK_EMIT_SIZE + 2; emit < eof; emit += CHUNK_EMIT_SIZE)
				if (((emit[0] >> 3) & 31) == type && emit[1] == interval) goto found;
			/* no emitters with same type and interval: alloc a new one (this case is pretty rare though) */
			goto alloc;
		}
		found:
		list = emit;
		if (list[0] < 0xff00)
			list[0] += 0x100; /* count */
	}
	else /* not created yet: add one */
	{
		alloc:
		if (list == NULL || list[0] == list[1])
		{
			int max = list ? list[1] + 8 : 8;
			list = realloc(list, max * CHUNK_EMIT_SIZE*2 + 4);
			if (list == NULL) return;
			if (max == 8) list[0] = 0;
			list[1] = max;
			cd->emitters = list;
		}
		list[0] ++;
		list += (list[0]-1) * CHUNK_EMIT_SIZE + 2;
		emitters[type] = list - cd->emitters;
		/* type, pos and count */
		list[0] = (pos >> 9) | (type << 3);
		list[1] = interval;
		/* 32bit bitfield for location hint */
		list[2] = list[3] = 0;
	}
	/* mark this X row as containing at least one emitter */
	list[2 + ((pos >> 8) & 1)] |= 1 << ((pos >> 4) & 15);
}

/*
 * flood-fill for getting face connection, used by cave culling.
 */
static int chunkGetCnxGraph(ChunkData cd, int start, DATA8 visited)
{
	DATA8 blocks = cd->blockIds;
	int init = slotsXZ[start&0xff] | slotsY[start>>8];
	int cnx = faceCnx[init];
	int last = 2;
	int pos = 0;
	visited[0] = start;
	visited[1] = start >> 8;

	while (pos != last)
	{
		int x0 = visited[pos] & 15;
		int z0 = visited[pos] >> 4;
		int y0 = visited[pos+1];
		int i;

		pos += 2;
		if (pos == 512) pos = 0;

		for (i = 0; i < 6; i ++)
		{
			uint8_t x = x0 + relx[i];
			uint8_t y = y0 + rely[i];
			uint8_t z = z0 + relz[i];

			/* clipping (not 100% portable, but who cares?) */
			if (x >= 16 || y >= 16 || z >= 16) continue;
			int   xzy = CHUNK_BLOCK_POS(x, z, y);
			Block b = blockIds + blocks[xzy];
			/* only fully opaque blocks will stop flood: we could be more precise, but not worth the time spent */
			if (! blockIsFullySolid(b) &&
				(visited[512+(xzy>>3)] & mask8bit[xzy&7]) == 0)
			{
				visited[last++] = x | (z << 4);
				visited[last++] = y;
				if (last == 512) last = 0;
				visited[512+(xzy>>3)] |= mask8bit[xzy&7];
				init |= slotsXZ[xzy&0xff] | slotsY[xzy>>8];
				cnx |= faceCnx[init];
			}
		}
	}
	return cnx;
}


/* like mapIter, but increase iter.offset by 1 instead */
static inline void chunkIter(BlockIter iter)
{
	iter->offset ++;
	iter->x = iter->offset & 15;
	iter->z = (iter->offset >> 4) & 15;
	iter->y = iter->offset >> 8;
	iter->yabs = (iter->yabs & ~15) + iter->y;
}

static void chunkGenQuad(BlockIter, MeshWriter, BlockState);
static void chunkGenCust(BlockIter, MeshWriter, BlockState);
static void chunkGenCube(BlockIter, MeshWriter, BlockState);
static void chunkMergeQuads(ChunkData, HashQuadMerge);
static void chunkGenLight(Map, BlockIter, MeshWriter);

void chunkMakeObservable(ChunkData cd, int offset, int side);


#include "globals.h" /* only needed for .breakPoint */

/*
 * transform chunk data into something useful for the vertex shader (terrain.vsh)
 * this is the "meshing" function for our world.
 * note: this part must be re-entrant, it will be called in a multi-threaded context.
 */
void chunkUpdate(Map map, Chunk c, ChunkData empty, int layer, MeshInitializer meshinit)
{
	uint16_t emitters[PARTICLE_MAX];
	uint8_t  visited[512 + 512];
	uint8_t  hasLights;
	int      air;

	/* single-thread and multi-thread have completely different allocation strategies */
	struct BlockIter_t iter;
	mapInitIterOffset(&iter, c->layer[layer], 0);
	iter.nbor = map->chunkOffsets;

	/* alloc memory to store quads these functions will generate */
	MeshWriter_t writer;
	if (! meshinit(iter.cd, &writer))
		/* MT-generation can cancel allocation */
		return;

	if (iter.cd->emitters)
		iter.cd->emitters[0] = 0;

	/* default sorting for alpha quads */
	hasLights = (iter.cd->cdFlags & CDFLAG_NOLIGHT) == 0;
	iter.cd->yaw = M_PIf * 1.5f;
	iter.cd->pitch = 0;
	iter.cd->cdFlags &= ~(CDFLAG_CHUNKAIR | CDFLAG_PENDINGMESH | CDFLAG_NOALPHASORT | CDFLAG_HOLE);
	if (hasLights)
		chunkGenLight(map, &iter, &writer);
	else
		/* no need to alloc a 3d tex when lighting is constant within the entire chunk */
		iter.cd->glLightId = LIGHT_SKY15_BLOCK0;

	memset(visited, 0, sizeof visited);
	iter.cd->cnxGraph = 0;

//	if (c->X == 240 && iter.cd->Y == 96 && c->Z == 992)
//		globals.breakPoint = 1;

	for (air = 0; iter.y < 16; )
	{
		if ((iter.y & 1) == 0)
			memset(emitters, 0, sizeof emitters);

		/* scan one XZ layer at a time */
		for ( ; iter.offset < 4096; chunkIter(&iter))
		{
			int blockId = getBlockId(&iter);
			BlockState state = blockGetById(blockId);

//			if (globals.breakPoint && iter.offset == 2885)
//				globals.breakPoint = 2;

			/* 3d flood fill for cave culling */
			if ((slotsXZ[iter.offset & 0xff] || slotsY[iter.offset >> 8]) && ! blockIsFullySolid(state))
			{
				if ((visited[iter.offset >> 3] & mask8bit[iter.offset & 7]) == 0)
					iter.cd->cnxGraph |= chunkGetCnxGraph(iter.cd, iter.offset, visited);

				/* starting chunk "holes" for cave culling */
				iter.cd->cdFlags |= (slotsXZ[iter.offset & 0xff] | slotsY[iter.offset >> 8]) << 9;
			}

			if (hasLights)
			{
				/* build list of particles emitters */
				Block b = &blockIds[blockId >> 4];
				if (b->particle > 0 && particleCanSpawn(iter, blockId, b->particle))
					chunkAddEmitters(iter.cd, b->emitInterval, iter.offset, b->particle - 1, emitters);

				if (blockId >> 4 == RSOBSERVER)
					chunkMakeObservable(iter.cd, iter.offset, blockSides.piston[blockId&7]);
			}

			/* voxel meshing starts here */
			switch (state->type) {
			case QUAD:
				chunkGenQuad(&iter, &writer, state);
				break;
			case CUST:
				if (state->custModel)
				{
					chunkGenCust(&iter, &writer, state);
					/* SOLIDOUTER: custom block with ambient occlusion */
					if (state->special != BLOCK_SOLIDOUTER)
						break;
				}
				/* else no break; */
			case TRANS:
			case SOLID:
				chunkGenCube(&iter, &writer, state);
				break;
			default:
				if (state->id == 0) air ++;
			}
		}
	}
	/* entire sub-chunk is composed of air: check if we can get rid of it */
	if (air == 4096 && (iter.cd->cdFlags & CDFLAG_NOLIGHT) == 0)
	{
		/* block light must be all 0 and skylight be all 15 */
		if (memcmp(iter.cd->blockIds + BLOCKLIGHT_OFFSET, empty->blockIds + BLOCKLIGHT_OFFSET, 2048) == 0 &&
			memcmp(iter.cd->blockIds + SKYLIGHT_OFFSET,   empty->blockIds + SKYLIGHT_OFFSET,   2048) == 0)
		{
			if ((iter.cd->Y >> 4) == c->maxy-1)
			{
				/* yes, can be freed */
				c->layer[iter.cd->Y >> 4] = NULL;
				c->maxy --;
				/* cannot delete it now, but will be done after VBO has been cleared */
				iter.cd->cdFlags = CDFLAG_PENDINGDEL;
				chunkMarkForUpdate(c, CHUNK_NBT_SECTION);

				/* check if chunk below are also empty */
				for (air = c->maxy-1; air >= 0; air --)
				{
					iter.cd = c->layer[air];
					if (iter.cd == NULL || (iter.cd->cdFlags & (CDFLAG_CHUNKAIR|CDFLAG_PENDINGMESH)) == CDFLAG_CHUNKAIR)
					{
						/* empty chunk with no pending update: it can be deleted now */
						c->layer[air] = NULL;
						c->maxy = air;
						free(iter.cd);
					}
					else break;
				}
				return;
			}
			else iter.cd->cdFlags |= CDFLAG_CHUNKAIR;
		}
	}

	if (writer.cur > writer.start)
		writer.flush(&writer);

	if (writer.merge)
		chunkMergeQuads(iter.cd, writer.merge);
}

#define BUF_LESS_THAN(buffer,min)   (((DATA8)buffer->end - (DATA8)buffer->cur) < min)

/* tall grass, flowers, rails, ladder, vines, ... */
static void chunkGenQuad(BlockIter iterator, MeshWriter buffer, BlockState b)
{
	struct BlockIter_t iter = *iterator;

	DATA8 tex   = &b->nzU;
	DATA8 sides = &b->pxU;
	Chunk chunk = iter.ref;
	int   seed  = iter.cd->Y ^ chunk->X ^ chunk->Z;


	if (b->special == BLOCK_TALLFLOWER && (b->id&15) == 10)
	{
		/* state 10 is used for top part of all tall flowers :-/ need to look at bottom part to know which top part to draw */
		mapIter(&iter, 0, 1, 0);
		b += getBlockId(&iter) & 15;
		tex = &b->nzU;
		iter = *iterator;
	}

	do {
		int     U, V, DX, DY, DZ;
		uint8_t side, norm, j;
		DATA8   coord1, coord2;
		DATA32  out;

		if (BUF_LESS_THAN(buffer, VERTEX_DATA_SIZE))
			buffer->flush(buffer);

		out    = buffer->cur;
		side   = *sides;
		norm   = quadSides[side];
		coord1 = cubeVertex + quadIndices[side*4+3];
		coord2 = cubeVertex + quadIndices[side*4];
		DX = DY = DZ = 0;

		if (b->special == BLOCK_JITTER)
		{
			/* add some jitter to X,Z coord for QUAD_CROSS */
			uint8_t jitter = seed ^ (iter.x ^ iter.y ^ iter.z);
			if (jitter & 1) DX = BASEVTX/16;
			if (jitter & 2) DZ = BASEVTX/16;
			if (jitter & 4) DY = (BASEVTX/16);
			if (jitter & 8) DY -= (BASEVTX/32);
		}
		else if (norm < 6)
		{
			/* offset 1/16 of a block in the direction of their normal */
			int8_t * normal = cubeNormals + norm * 4;
			int      base   = side <= QUAD_SQUARE4 ? BASEVTX/4 : BASEVTX/16;
			DX = normal[0] * base;
			DY = normal[1] * base;
			DZ = normal[2] * base;
		}

		/* first vertex */
		j = (b->rotate&3) * 8;
		U = (texCoord[j]   + tex[0]) << 4;
		V = (texCoord[j+1] + tex[1]) << 4;

		/* second and third vertex */
		#define VERTEX_OFF(v, off)      (VERTEX(v)+off)
		out[0] = VERTEX_OFF(coord1[0] + iter.x, DX) | (VERTEX_OFF(coord1[1] + iter.y, DY) << 16);
		out[1] = VERTEX_OFF(coord1[2] + iter.z, DZ) | (VERTEX_OFF(coord2[0] + iter.x, DX) << 16);
		out[2] = VERTEX_OFF(coord2[1] + iter.y, DY) | (VERTEX_OFF(coord2[2] + iter.z, DZ) << 16);
		coord1 = cubeVertex + quadIndices[side*4+2];
		out[3] = VERTEX_OFF(coord1[0] + iter.x, DX) | (VERTEX_OFF(coord1[1] + iter.y, DY) << 16);
		out[4] = iter.cd->glLightId | (VERTEX_OFF(coord1[2] + iter.z, DZ) << 16);
		out[5] = U | (V << 9) | (norm << 19) | (texCoord[j] == texCoord[j + 6] ? FLAG_TEX_KEEPX|FLAG_DUAL_SIDE : FLAG_DUAL_SIDE);
		out[6] = ((texCoord[j+4] + tex[0]) << 4) | ((texCoord[j+5] + tex[1]) << 13);

		sides ++;
		buffer->cur = out + VERTEX_INT_SIZE;
	} while (*sides);
}

/* get 27 blockIds around block */
static int chunkGetBlockIds(struct BlockIter_t iter, DATA16 blockIds3x3)
{
	static int8_t iterNext[] = {
		1,0,0,   1,0,0,   -2,0,1,
		1,0,0,   1,0,0,   -2,0,1,
		1,0,0,   1,0,0,   -2,1,-2
	};
	int8_t * next = iterNext;
	int i, slab;

	mapIter(&iter, -1, -1, -1);

	/* only compute that info if block is visible (highly likely it is not) */
	for (i = slab = 0; i < 27; i ++)
	{
		BlockState nbor = blockGetById(blockIds3x3[i] = getBlockId(&iter));

		if (nbor->type == SOLID || (nbor->type == CUST && nbor->special == BLOCK_SOLIDOUTER))
		{
			if (nbor->special == BLOCK_HALF || nbor->special == BLOCK_STAIRS)
				slab |= 1 << i;
		}

		mapIter(&iter, next[0], next[1], next[2]);
		next += 3;
		if (next == EOT(iterNext)) next = iterNext;
	}
	return slab;
}

/* custom model mesh: anything that doesn't fit quad or full/half block */
static void chunkGenCust(BlockIter iterator, MeshWriter buffer, BlockState b)
{
	static uint8_t connect6blocks[] = {
		/*B*/7, 5, 1, 3, 4,   /*M*/16, 14, 10, 12,   /*T*/25, 23, 19, 21, 22
	};

	struct BlockIter_t iter = *iterator;

	Chunk    c = iter.ref;
	DATA32   out;
	DATA16   model;
	DATA8    cnxBlock;
	int      count, connect, hasLights;
	uint32_t dualside;
	uint16_t blockIds3x3[27];

	hasLights = (iter.cd->cdFlags & CDFLAG_NOLIGHT) == 0;
	chunkGetBlockIds(iter, blockIds3x3);

	model = b->custModel;
	count = connect = 0;
	dualside = blockIds[b->id >> 4].special & BLOCK_DUALSIDE ? FLAG_DUAL_SIDE : 0; /* fire */

	switch (b->special) {
	case BLOCK_DOOR:
		/*
		 * bottom part data:
		 * - bit0: orient
		 * - bit1: orient
		 * - bit2: 1 if open
		 * - bit3: 0 = bottom part
		 * top part data:
		 * - bit0: hinge on right
		 * - bit1: powered
		 * - bit2: unused
		 * - bit3: 1 = top part
		 */
		{
			uint8_t top = blockIds3x3[13] & 15;
			uint8_t bottom;
			if (top & 8) /* top part: get bottom part */
			{
				bottom = blockIds3x3[4] & 15;
				count = 8;
			}
			else /* bottom part: get top part */
			{
				bottom = top;
				top = blockIds3x3[22] & 15;
			}

			uint8_t side = (bottom & 3) | ((top&1) << 2);
			b -= b->id & 15;
			if (bottom & 4) side = openDoorDataToModel[side];
			model = b[side+count].custModel;
		}
		count = 0;
		break;
	case BLOCK_CHEST:
	case BLOCK_FENCE:
	case BLOCK_FENCE2:
		cnxBlock = connect6blocks + 5;
		count = 4;
		break;
	case BLOCK_RSWIRE:
	case BLOCK_GLASS:
		/* need: 14 surrounding blocks (S, E, N, W): 5 bottom, 4 middle, 5 top */
		cnxBlock = connect6blocks;
		count = 14;
		break;
	case BLOCK_WALL:
		/* need: 4 surrounding blocks (S, E, W, N), 1 bottom (only for face culling), 1 top */
		cnxBlock = connect6blocks + 5;
		count = 9;
		break;
	case BLOCK_POT:
		/* flower pot: check if there is a plant in the pot */
		cnxBlock = chunkGetTileEntity(iter.cd, iter.offset);
		if (cnxBlock)
		{
			struct NBTFile_t nbt = {.mem = cnxBlock};
			uint8_t data;
			cnxBlock = NBT_Payload(&nbt, NBT_FindNode(&nbt, 0, "Item"));
			data = NBT_GetInt(&nbt, NBT_FindNode(&nbt, 0, "Data"), 0);
			if (cnxBlock && strncmp(cnxBlock, "minecraft:", 10) == 0)
			{
				extern TEXT flowerPotList[];
				cnxBlock += 10;
				/* yellow_flower,red_flower,sapling,cactus,brown_mushroom,red_mushroom,cactus,tall_grass */
				switch (FindInList(flowerPotList, cnxBlock, 0)) {
				/* bitfield must ordered in the way they are in blockTable.js */
				case 0: connect = 1; break;
				case 1: connect = 1 << (1 + data); break;
				case 2: connect = 1 << (10 + data); break;
				case 3: connect = 1 << 16; break;
				case 4: connect = 1 << 17; break;
				case 5: connect = 1 << 18; break;
				case 6: connect = 1 << (data == 0 ? 19 : 20); break;
				}
			}
		}
		break;
	case BLOCK_BED:
		cnxBlock = chunkGetTileEntity(iter.cd, iter.offset);
		if (cnxBlock)
		{
			struct NBTFile_t nbt = {.mem = cnxBlock};
			connect = 1 << NBT_GetInt(&nbt, NBT_FindNode(&nbt, 0, "color"), 14);
		}
		/* default color: red */
		else connect = 1 << 14;
		break;
	case BLOCK_SIGN:
		if (hasLights) /* don't render sign text for brush */
			c->signList = signAddToList(b->id, iter.cd, iter.offset, c->signList, 0);
		break;
	default:
		/* piston head with a tile entity: head will be rendered as an entity when it is moving */
		if ((b->id >> 4) == RSPISTONHEAD && chunkGetTileEntity(iter.cd, iter.offset))
			return;
	}

	if (model == NULL)
		return;

	if (count > 0)
	{
		/* retrieve blockIds for connected models (<cnxBlock> is pointing to connect6blocks[]) */
		uint16_t blockIdAndData[14];
		uint8_t  i;
		for (i = 0; i < count; blockIdAndData[i] = blockIds3x3[cnxBlock[i]], i ++);
		if (b->special == BLOCK_WALL) blockIdAndData[4] = blockIdAndData[8];
		connect = blockGetConnect(b, blockIdAndData);
	}

	int x = iter.x * BASEVTX;
	int y = iter.y * BASEVTX;
	int z = iter.z * BASEVTX;

	/* vertex and light info still need to be adjusted */
	for (count = model[-1]; count > 0; count -= 6, model += 6 * INT_PER_VERTEX)
	{
		uint8_t faceId = (model[4] >> FACEIDSHIFT) & 31;
		/* this is how we discard useless parts of connected models */
		if (faceId > 0 && (connect & (1 << (faceId-1))) == 0)
		{
			/* discard vertex */
			continue;
		}
		/* check if we can eliminate even more faces */
		uint8_t norm = GET_NORMAL(model);
		/* iron bars and glass pane already have their model culled */
		if (model[axisCheck[norm]] == axisAlign[norm] && b->special != BLOCK_GLASS)
		{
			struct BlockIter_t neighbor = iter;
			int8_t * normal = cubeNormals + norm * 4;
			mapIter(&neighbor, normal[0], normal[1], normal[2]);

			if (blockIsSideHidden(getBlockId(&neighbor), model, opp[norm]))
				/* skip entire face (6 vertices) */
				continue;
		}

		if (BUF_LESS_THAN(buffer, VERTEX_DATA_SIZE))
			buffer->flush(buffer);

		out = buffer->cur;
		DATA16 coord1 = model + INT_PER_VERTEX * 3;
		DATA16 coord2 = model;
		int U = GET_UCOORD(model);
		int V = GET_VCOORD(model);

		out[0] = (coord1[0] + x) | ((coord1[1] + y) << 16);
		out[1] = (coord1[2] + z) | ((coord2[0] + x) << 16);
		out[2] = (coord2[1] + y) | ((coord2[2] + z) << 16);
		coord2 = model + INT_PER_VERTEX * 2;
		out[3] = (coord2[0] + x) | ((coord2[1] + y) << 16);
		out[4] = iter.cd->glLightId | ((coord2[2] + z) << 16);
		out[5] = U | (V << 9) | (GET_NORMAL(model) << 19) | dualside | (U == GET_UCOORD(coord1) ? FLAG_TEX_KEEPX : 0);
		out[6] = GET_UCOORD(coord2) | (GET_VCOORD(coord2) << 9);

		if (b->special == BLOCK_RSWIRE)
		{
			/* texture based on redstone signal strength */
			out[5] += (b->id & 15) << 4;
			out[6] += (b->id & 15) << 4;
		}

		if (STATEFLAG(b, CNXTEX))
		{
			/* glass pane (stained and normal): relocate to simulate connected textures (only middle parts) */
			if (5 <= faceId && faceId <= 8)
			{
				int face = (faceId - 1) & 3;
				int flag = 15;

				/* remove top/bottom part */
				if ((connect & (1 << face)) > 0)     flag &= ~4; /* bottom */
				if ((connect & (1 << (face+8))) > 0) flag &= ~1; /* top */

				/* left/right side */
				if ((connect & (1 << (face+12))) && /* connected to same block */
				    (connect & 0x0f0) > 0)
				{
					flag &= ((GET_NORMAL(model)+1) & 3) == face ? ~2 : ~8;
				}

				out[5] += flag << 4;
				out[6] += flag << 4;
			}
			else if (13 <= faceId && faceId <= 16)
			{
				/* center piece connected to another part of glass pane */
				if (connect & (1 << (faceId-9)))
					continue;

				int flag = 0;
				if ((connect & (1<<16)) == 0) flag |= 1;
				if ((connect & (1<<17)) == 0) flag |= 4;

				out[5] += flag << 4;
				out[6] += flag << 4;
			}
		}
		if (buffer->merge)
		{
			/* only merge full block */
			uint8_t axis1 = norm2axis1[norm];
			uint8_t axis2 = norm2axis2[norm];
			if (model[INT_PER_VERTEX*2+axis1] - model[axis1] == BASEVTX &&
			    model[INT_PER_VERTEX*2+axis2] - model[axis2] == BASEVTX)
			{
				meshQuadMergeAdd(buffer->merge, out);
			}
		}

		buffer->cur = out + VERTEX_INT_SIZE;
	}
}

/* most common block within a chunk */
static void chunkGenCube(BlockIter iterator, MeshWriter buffer, BlockState b)
{
	uint16_t blockIds3x3[27];
	uint8_t  texUV[12];
	DATA32   out;
	DATA8    tex;
	int      slab, rotate;
	int      normal, texOff;
	uint8_t  liquid, discard;

	struct BlockIter_t iter = *iterator;
	struct BlockIter_t neighbor = iter;

	/* outer loop: iterate over each faces (6) */
	for (normal = 0, liquid = 0, slab = -1, tex = &b->nzU, rotate = b->rotate, texOff = (rotate&3) * 8; normal < 6;
		 normal ++, rotate >>= 2, tex += 2, texOff = (rotate&3) * 8)
	{
		mapIter(&neighbor, xoff[normal], yoff[normal], zoff[normal]);
		BlockState nbor = blockGetById(getBlockId(&neighbor));
		discard = 0;

		switch (nbor->type) {
		case SOLID:
			if (b->special == BLOCK_LIQUID && normal == SIDE_TOP)
				/* top of liquid: slightly lower than a full block */
				break;
			switch (nbor->special) {
			case BLOCK_HALF:
			case BLOCK_STAIRS:
				if (oppositeMask[*halfBlockGetModel(nbor, 0, NULL)] & (1 << normal)) continue;
				break;
			default: continue;
			}
			break;
		case TRANS:
			if (nbor->special == BLOCK_LEAVES)
			{
				/* this quad can be discarded if too far */
				discard = b->special == BLOCK_LEAVES;
			}
			else if (b->id == nbor->id)
			{
				continue;
			}
			if (b->special == BLOCK_LIQUID && nbor->id == ID(79,0) /*ICE*/) continue;
		}

		/* blockIds surrounding this block */
		if (slab == -1)
		{
			slab = chunkGetBlockIds(iter, blockIds3x3);

			if (STATEFLAG(b, CNXTEX))
			{
				DATA8 cnx, uv;
				int   id = b->id;
				memcpy(texUV, &b->nzU, 12);
				/* check for connected textures */
				for (uv = texUV, cnx = offsetConnected; cnx < EOT(offsetConnected); cnx += 4, uv += 2)
				{
					uint8_t flags = 0;
					if (blockIds3x3[cnx[0]] == id) flags |= 1;
					if (blockIds3x3[cnx[1]] == id) flags |= 2;
					if (blockIds3x3[cnx[2]] == id) flags |= 4;
					if (blockIds3x3[cnx[3]] == id) flags |= 8;
					uv[0] += flags;
				}
				tex = texUV + (tex - &b->nzU);
			}
			if (b->special == BLOCK_LIQUID)
			{
				int i;
				for (i = 18; i < 27; i ++)
				{
					static uint8_t raisedEdge[] = {2, 3, 1, 10, 15, 5, 8, 12, 4};
					if (blockIds[blockIds3x3[i]>>4].special == BLOCK_LIQUID)
						liquid |= raisedEdge[i-18];
				}
				liquid ^= 15;
			}
		}

		/* if there is a snow layer on top, use snowed grass block */
		if (b->id == ID(2,0) && blockIds[blockIds3x3[22]>>4].orientHint == ORIENT_SNOW)
			tex = &b[1].nzU + (tex - &b->nzU), b ++;

		if (b->special == BLOCK_HALF || b->special == BLOCK_STAIRS)
		{
			uint8_t xyz[3] = {iter.x<<1, iter.y<<1, iter.z<<1};
			meshHalfBlock(buffer, halfBlockGetModel(b, 2, blockIds3x3), 2, xyz, b, blockIds3x3, iter.cd->glLightId);
			break;
		}

		if (BUF_LESS_THAN(buffer, VERTEX_DATA_SIZE))
			buffer->flush(buffer);
		out = buffer->cur;
		buffer->cur = out + VERTEX_INT_SIZE;

		/* generate one quad (see internals.html for format) */
		{
			DATA8    coord1 = cubeVertex + cubeIndices[(normal<<2)+3];
			DATA8    coord2 = cubeVertex + cubeIndices[(normal<<2)];
			uint16_t texU   = (texCoord[texOff]   + tex[0]) << 4;
			uint16_t texV   = (texCoord[texOff+1] + tex[1]) << 4;

			/* write one quad */
			out[0] = VERTEX(coord1[0]+iter.x) | (VERTEX(coord1[1]+iter.y) << 16);
			out[1] = VERTEX(coord1[2]+iter.z) | (VERTEX(coord2[0]+iter.x) << 16);
			out[2] = VERTEX(coord2[1]+iter.y) | (VERTEX(coord2[2]+iter.z) << 16);
			coord1 = cubeVertex + cubeIndices[(normal<<2)+2];
			out[3] = VERTEX(coord1[0]+iter.x) | (VERTEX(coord1[1]+iter.y) << 16);
			out[4] = iter.cd->glLightId | (VERTEX(coord1[2]+iter.z) << 16);
			out[5] = texU | (texV << 9) | (normal << 19) | (texCoord[texOff] == texCoord[texOff+6] ? FLAG_TEX_KEEPX : 0);
			out[6] = ((texCoord[texOff+4] + tex[0]) << 4) |
			         ((texCoord[texOff+5] + tex[1]) << 13);
			/* prevent quad merging between discard and normal quads */
			if (discard) out[6] |= FLAG_DISCARD;
			if (STATEFLAG(b, ALPHATEX)) out[6] |= FLAG_ALPHATEX;

			static uint8_t oppSideBlock[] = {16, 14, 10, 12, 22, 4};
			if (blockIds[blockIds3x3[oppSideBlock[normal]] >> 4].special == BLOCK_LIQUID)
				/* use water fog instead of atmospheric one */
				out[5] |= FLAG_UNDERWATER;

			if (b->special == BLOCK_LIQUID)
			{
				/* lower some edges of a liquid block if we can (XXX need a better solution than this :-/) */
				uint8_t edges = 0;
				switch (normal) {
				case SIDE_SOUTH: edges = (liquid&12)>>2; break;
				case SIDE_NORTH: edges = ((liquid&1)<<1) | ((liquid&2)>>1); break;
				case SIDE_EAST:  edges = (liquid&1) | ((liquid & 4) >> 1); break;
				case SIDE_WEST:  edges = (liquid&2) | ((liquid & 8) >> 3); break;
				case SIDE_TOP:   edges = liquid;
				}
				if (edges)
					out[5] |= FLAG_LIQUID | FLAG_UNDERWATER | FLAG_DUAL_SIDE | (edges << 28);
				else
					out[5] |= FLAG_UNDERWATER | FLAG_DUAL_SIDE;
			}
			if (buffer->merge)
				meshQuadMergeAdd(buffer->merge, out);
		}
	}
}

/*
 * pre-generate lighting used for sky/block light and ambient occlusion
 * note: if sky and block light are all zeros, nothing will be generated:
 * this will cut the lighting tex needed by 20% on a typical minecraft landscape
 */
#define AO_HARSHNESS         0x55  /* max: 255 */

static void chunkGenLight(Map map, BlockIter iterator, MeshWriter writer)
{
	unsigned x, y, z;

	//iterator->cd->glLightId = LIGHT_SKY15_BLOCK0;
	//return;

	if (memcmp(iterator->blockIds + BLOCKLIGHT_OFFSET, chunkAir->blockIds + BLOCKLIGHT_OFFSET, 2048) == 0 &&
	    memcmp(iterator->blockIds + SKYLIGHT_OFFSET,   chunkAir->blockIds + BLOCKLIGHT_OFFSET, 2048) == 0)
	{
	    /* all zeros: no need to allocate a texture for this */
		x = iterator->cd->glLightId;
		if (x < 0xfffe)
			mapFreeLightingSlot(map, x);
		iterator->cd->glLightId = LIGHT_SKY0_BLOCK0;
		return;
	}

	struct BlockIter_t iter = *iterator;
	DATA8 skyBlock = (DATA8) (writer->cur + 1);
	/* need to untangle block and sky light values */
	mapIter(&iter, -1, -1, -1);
	for (y = 0; y < 18; y ++, mapIter(&iter, 0, 1, -18))
	{
		for (z = 0; z < 18; z ++, mapIter(&iter, -18, 0, 1))
		{
			for (x = 0; x < 18; x ++, mapIter(&iter, 1, 0, 0), skyBlock += 2)
			{
				/* would have been nice if sky and block light were fused in a single table in the NBT stream :-/ */
				uint8_t sky   = iter.blockIds[SKYLIGHT_OFFSET   + (iter.offset >> 1)];
				uint8_t block = iter.blockIds[BLOCKLIGHT_OFFSET + (iter.offset >> 1)];

				if (iter.offset & 1) sky >>= 4, block >>= 4;
				else sky &= 15, block &= 15;
				/* sigh, opengl doesn't have a 2-channel 4bit texture (it has RGB4 and RGBA4, but no RG4): use RG8 then */
				skyBlock[0] = sky * 17;
				skyBlock[1] = block * 17;
				if (iter.blockIds[iter.offset] == 0 && sky == 0)
					skyBlock[0] = 1;
			}
		}
	}

	/* need another pass, to "unharshen" values by letting sky/block "seeps" into opaque blocks */
	#if 1
	skyBlock = (DATA8) (writer->cur + 1);
	for (y = 0; y < 18; y ++)
	{
		for (z = 0; z < 18; z ++)
		{
			for (x = 0; x < 18; x ++, skyBlock += 2)
			{
				if (skyBlock[0] > 0 || skyBlock[1] > 0)
					continue;

				uint8_t maxSky = 0;
				uint8_t maxBlock = 0;
				uint8_t i;
				for (i = 0; i < 6; i ++)
				{
					if (x + relx[i] < 18 &&
					    y + rely[i] < 18 &&
					    z + relz[i] < 18)
					{
						static int offset[] = {18*2,2,-18*2,-2,18*18*2,-18*18*2}; /* S,E,N,W,T,B */
						DATA8 skyBlock2 = skyBlock + offset[i];
						if (maxSky < skyBlock2[0])
							maxSky = skyBlock2[0];
						if (maxBlock < skyBlock2[1])
							maxBlock = skyBlock2[1];
					}
				}

				skyBlock[0] = maxSky   < AO_HARSHNESS ? 0 : maxSky   - AO_HARSHNESS;
				skyBlock[1] = maxBlock < AO_HARSHNESS ? 0 : maxBlock - AO_HARSHNESS;
			}
		}
	}
	#endif

	x = iterator->cd->glLightId;
	if (x >= 0xfffe)
		x = iterator->cd->glLightId = mapAllocLightingTex(map);
	/* will mark this block as a lighting tex, not as vertex buffer */
	writer->cur[0] = QUAD_LIGHT_ID | x;

	#if 0
	if (map->center == iterator->ref && iterator->cd->Y == 0)
	{
		FILE * out = fopen("light2.ppm", "wb");
		DATA8 data = (DATA8) (writer->cur + 1);

		fprintf(stderr, "dumping light from %d, %d, %d: %d info to light2.ppn\n", map->center->X, map->center->Z, iterator->cd->Y, x >> 7);
		fprintf(out, "P6\n%d %d 255\n", 18, 18*18);
		for (y = 0; y < 18; y ++, data += 18*18*2)
		{
			DATA8 tex2;
			for (z = 0, tex2 = data; z < 18; z ++, tex2 += 18*2)
			{
				uint8_t rgb[18*3];
				for (x = 0; x < 18; x ++)
				{
					rgb[x*3]   = tex2[x*2];
					rgb[x*3+1] = tex2[x*2+1];
					rgb[x*3+2] = 0;
				}
				fwrite(rgb, 1, 18*3, out);
			}
		}
		fclose(out);
	}
	#endif


	writer->cur += TEX_MESH_INT_SIZE;
}


/*
 * scan all quads from SOLID blocks and check if they can be merged (a.k.a greedy meshing).
 * sadly, we need all quad data first :-/
 */
static uint8_t quadDirections[] = { /* SENWTB */
	VX, 1, VY, 1,
	VZ, 0, VY, 1,
	VX, 0, VY, 1,
	VZ, 1, VY, 1,
	VX, 1, VZ, 0,
	VX, 1, VZ, 1,
};

static void chunkMergeQuads(ChunkData cd, HashQuadMerge hash)
{
	HashQuadEntry entry;
	int index;
	for (index = hash->firstAdded; index != 0xffff; index = entry->nextAdded)
	{
		entry = hash->entries + index;
		DATA32 quad = entry->quad;
		/* check if already processed */
		if (quad == NULL) continue;
		entry->quad = NULL;

		uint32_t ref[VERTEX_INT_SIZE];
		uint8_t  min, max, axis;
		uint8_t  min2, max2, axis2;
		DATA8    directions = quadDirections + (((quad[5] >> 19) & 7) << 2);

		memcpy(ref, quad, VERTEX_DATA_SIZE);
		axis = directions[0];
		switch (axis) {
		default: max = ((quad[0] & 0xffff) - ORIGINVTX) >> 11; break;
		case VY: max = ((quad[0] >> 16) - ORIGINVTX) >> 11; break;
		case VZ: max = ((quad[1] & 0xffff) - ORIGINVTX) >> 11;
		}
		min = max -= directions[1];
		while (max < 16)
		{
			max ++;
			switch (axis) {
			case VX: ref[0] += BASEVTX; break;
			case VY: ref[0] += BASEVTX<<16; break;
			case VZ: ref[1] += BASEVTX;
			}
			index = meshQuadMergeGet(hash, ref);
			if (index < 0) break;

			/* yes, can be merged: mark next one as processed */
			HashQuadEntry merged = &hash->entries[index];
			merged->quad[0] = 0;
			merged->quad = NULL;
		}
		/* check if we can expand this even further in 2nd direction */
		axis2 = directions[2];
		memcpy(ref, quad, VERTEX_DATA_SIZE);
		switch (axis2) {
		default: max2 = ((quad[0] & 0xffff) - ORIGINVTX) >> 11; break;
		case VY: max2 = ((quad[0] >> 16)    - ORIGINVTX) >> 11; break;
		case VZ: max2 = ((quad[1] & 0xffff) - ORIGINVTX) >> 11;
		}
		min2 = max2 -= directions[3];
		while (max2 < 16)
		{
			uint16_t indices[16];
			uint32_t start[2];
			DATA16   p, eof;
			max2 ++;
			switch (axis2) {
			case VX: ref[0] += BASEVTX; break;
			case VY: ref[0] += BASEVTX<<16; break;
			case VZ: ref[1] += BASEVTX;
			}
			memcpy(start, ref, sizeof start);
			for (p = indices, eof = p + (max - min); p < eof; p ++)
			{
				p[0] = meshQuadMergeGet(hash, ref);
				if (p[0] == 0xffff) { max2--; goto done; }
				switch (axis) {
				case VX: ref[0] += BASEVTX; break;
				case VY: ref[0] += BASEVTX<<16; break;
				case VZ: ref[1] += BASEVTX;
				}
			}
			/* mark quads as processed */
			for (p = indices; p < eof; p ++)
			{
				HashQuadEntry merged = &hash->entries[p[0]];
				merged->quad[0] = 0;
				merged->quad = NULL;
			}
			memcpy(ref, start, sizeof start);
		}
		max2 --;

		done:
		max --;

		if (min < max || min2 < max2)
		{
			/* more than 1 quad to merge */
			uint16_t incAxis1 = (max  - min)  * BASEVTX;
			uint16_t incAxis2 = (max2 - min2) * BASEVTX;
			switch ((quad[5] >> 19) & 7) {
			default:
				quad[0] += incAxis1 | (incAxis2 << 16);
				quad[2] += incAxis2;
				quad[3] += incAxis1;
				break;
			case SIDE_EAST:
				quad[0] += incAxis2 << 16;
				quad[2] += incAxis2 | (incAxis1 << 16);
				break;
			case SIDE_NORTH:
				quad[0] += incAxis2 << 16;
				quad[1] += incAxis1 << 16;
				quad[2] += incAxis2;
				break;
			case SIDE_WEST:
				quad[0] += incAxis2 << 16;
				quad[1] += incAxis1;
				quad[2] += incAxis2;
				quad[4] += incAxis1 << 16;
				break;
			case SIDE_TOP:
				quad[0] += incAxis1;
				quad[3] += incAxis1;
				quad[4] += incAxis2 << 16;
				break;
			case SIDE_BOTTOM:
				quad[0] += incAxis1;
				quad[1] += incAxis2;
				quad[2] += incAxis2 << 16;
				quad[3] += incAxis1;
			}
			/* need to increase texture size too */
			int U1 = (quad[5] & 0x1ff);
			int V1 = (quad[5] >> 9) & 0x3ff;
			int U2 = (quad[6] & 0x1ff);
			int V2 = (quad[6] >> 9) & 0x3ff;

			if (quad[5] & FLAG_TEX_KEEPX)
			{
				/* XXX just fiddle around with these, not sure about the math behind :-/ */
				swap(min, min2);
				swap(max, max2);
			}

			int minU = MIN(U1, U2);
			int minV = MIN(V1, V2);
			int maxU = minU + (max - min + 1) * 16;
			int maxV = minV + (max2 - min2 + 1) * 16;

			if (U1 == minU)
				quad[6] = (quad[6] & ~0x1ff) | maxU;
			else
				quad[5] = (quad[5] & ~0x1ff) | maxU;
			if (V1 == minV)
				quad[6] = (quad[6] & ~(0x3ff<<9)) | (maxV << 9);
			else
				quad[5] = (quad[5] & ~(0x3ff<<9)) | (maxV << 9);
			quad[5] |= FLAG_REPEAT;
		}
	}
}

/*
 * This function is similar to chunkGenQuad, but vertex data will use inventory model format instead of terrain.
 * Needed for entities, but all the LUT are here.
 */
extern uint8_t texCoordRevU[];
int chunkGenQuadModel(BlockState b, DATA16 out)
{
	DATA16  p     = out;
	DATA8   tex   = &b->nzU;
	DATA8   sides = &b->pxU;
	int     vtx   = BYTES_PER_VERTEX*12;
	int     side, i, j, texOrient;

	if (out == NULL)
	{
		/* get amount of bytes needed to store model */
		for (vtx /= BYTES_PER_VERTEX, i = vtx, sides ++; *sides; sides ++, i += vtx);
		return i;
	}

	do {
		side = quadSides[*sides];
		for (i = 0, j = *sides * 4, texOrient = (b->rotate&3) * 8; i < 4; i ++, j ++, p += INT_PER_VERTEX, texOrient += 2)
		{
			DATA8 coord = cubeVertex + quadIndices[j];
			int V = tex[1];
			/* biome dependant color: entities can't handle this */
			if (V == 62) V = 63;
			V = (texCoordRevU[texOrient+1] + V) << 4;
			if (V == 1024) V = 1023;

			p[0] = VERTEX(coord[0]);
			p[1] = VERTEX(coord[1]);
			p[2] = VERTEX(coord[2]);
			p[3] = ((texCoordRevU[texOrient] + tex[0]) << 4) | ((V & ~7) << 6);
			p[4] = (side << 3) | (V & 7);

			if (side < 6)
			{
				/* offset 1/16 of a block in the direction of their normal */
				int8_t * normal = cubeNormals + side * 4;
				p[0] += normal[0] * (BASEVTX/16);
				p[1] += normal[1] * (BASEVTX/16);
				p[2] += normal[2] * (BASEVTX/16);
			}
		}
		/* convert quad to triangles */
		memcpy(p,   p-4*INT_PER_VERTEX, BYTES_PER_VERTEX);
		memcpy(p+5, p-2*INT_PER_VERTEX, BYTES_PER_VERTEX);
		p += 2*INT_PER_VERTEX;

		if (vtx == 12*BYTES_PER_VERTEX)
		{
			/* need to add other side to prevent quad from being culled by glEnable(GL_CULLFACE) */
			memcpy(p, p-2*INT_PER_VERTEX, 2*BYTES_PER_VERTEX); p += 2*INT_PER_VERTEX;
			memcpy(p, p-7*INT_PER_VERTEX,   BYTES_PER_VERTEX); p += INT_PER_VERTEX;
			memcpy(p, p-6*INT_PER_VERTEX,   BYTES_PER_VERTEX); p += INT_PER_VERTEX;
			memcpy(p, p-5*INT_PER_VERTEX, 2*BYTES_PER_VERTEX); p += 2*INT_PER_VERTEX;
		}
		sides ++;
	} while (*sides);
	return (p - out) / INT_PER_VERTEX;
}
