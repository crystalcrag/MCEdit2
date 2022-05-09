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

static int8_t  subChunkOff[64];
static uint8_t oppositeMask[64];
static int16_t blockOffset[64];
static int16_t blockOffset2[64];

#define IDS(id1,id2)   (1<<id1)|(1<<id2)
#define IDC(id)        (1<<id)
static int occlusionIfNeighbor[] = { /* indexed by cube indices: 4 vertex per face (S,E,N,W,T,B) */
	IDS(15,25),  IDS(15, 7),  IDS(17, 7),  IDS(25,17),
	IDS(23,17),  IDS(17, 5),  IDS(11, 5),  IDS(23,11),
	IDS(19,11),  IDS(11, 1),  IDS( 9, 1),  IDS(19,9),
	IDS(21, 9),  IDS( 9, 3),  IDS(15, 3),  IDS(21,15),
	IDS(21,19),  IDS(25,21),  IDS(23,25),  IDS(23,19),
	IDS( 7, 3),  IDS( 3, 1),  IDS( 5, 1),  IDS( 7, 5)
};
static int occlusionIfCorner[] = {
	IDC(24),  IDC( 6),  IDC( 8),  IDC(26),
	IDC(26),  IDC( 8),  IDC( 2),  IDC(20),
	IDC(20),  IDC( 2),  IDC( 0),  IDC(18),
	IDC(18),  IDC( 0),  IDC( 6),  IDC(24),
	IDC(18),  IDC(24),  IDC(26),  IDC(20),
	IDC( 6),  IDC( 0),  IDC( 2),  IDC(8),
};
#undef IDC
#undef IDS

/* slab will produce slightly dimmer occlusion */
#define SLABLOC(id0,id1,id2,id3,id4,id5,id6,id7,id8) \
	((1<<id0) | (1<<id1) | (1<<id2) | (1<<id3) | (1<<id4) | (1<<id5) | (1<<id6) | (1<<id7) | (1<<id8))
static uint32_t occlusionIfSlab[] = {
	SLABLOC( 6, 7, 8,15,16,17,24,25,26),
	SLABLOC( 2, 5, 8,11,14,17,20,23,26),
	SLABLOC( 0, 1, 2, 9,10,11,18,19,20),
	SLABLOC( 0, 3, 6, 9,12,15,18,21,24),
	SLABLOC(18,19,20,21,22,23,24,25,26),
	SLABLOC( 0, 1, 2, 3, 4, 5, 6, 7, 8),
};
#undef SLABLOC

/* indicates whether we can find the neighbor block in the current chunk (sides&1)>0 or in the neighbor (sides&1)==0 */
static uint8_t xsides[] = { 2, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,  8};
static uint8_t zsides[] = { 1,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  4};
static uint8_t ysides[] = {16, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 32};

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
static uint16_t mask16bit[] = {
	0x0001, 0x0002, 0x0004, 0x0008, 0x0010, 0x0020, 0x0040, 0x0080,
	0x0100, 0x0200, 0x0400, 0x0800, 0x1000, 0x2000, 0x4000, 0x8000,
};

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
		if (pos == 400) pos = 0;

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
				(visited[400+(xzy>>3)] & mask8bit[xzy&7]) == 0)
			{
				visited[last++] = x | (z << 4);
				visited[last++] = y;
				if (last == 400) last = 0;
				visited[400+(xzy>>3)] |= mask8bit[xzy&7];
				init |= slotsXZ[xzy&0xff] | slotsY[xzy>>8];
				cnx |= faceCnx[init];
			}
		}
	}
	return cnx;
}


static void chunkGenQuad(ChunkData neighbors[], MeshWriter writer, BlockState b, int pos);
static void chunkGenCust(ChunkData neighbors[], MeshWriter writer, BlockState b, DATAS16 chunkOffsets, int pos);
static void chunkGenCube(ChunkData neighbors[], MeshWriter writer, BlockState b, DATAS16 chunkOffsets, int pos);
static void chunkGenFOG(ChunkData cur, MeshWriter writer, DATA16 holesSENW);
static void chunkFillCaveHoles(ChunkData cur, BlockState, int pos, DATA16 holes);
static void chunkMergeQuads(ChunkData, HashQuadMerge);

void chunkMakeObservable(ChunkData cd, int offset, int side);


#include "globals.h" /* only needed for .breakPoint */

#define CAVE_FOG_BITMAP        (DATA16) (visited+512+400)

/*
 * transform chunk data into something useful for the vertex shader (blocks.vsh)
 * this is the "meshing" function for our world.
 * note: this part must be re-entrant, it will be called in a multi-threaded context.
 */
void chunkUpdate(Chunk c, ChunkData empty, DATAS16 chunkOffsets, int layer, MeshInitializer meshinit)
{
	MeshWriter_t alpha, opaque;
	uint8_t      visited[400 + 512 + 264];
	ChunkData    neighbors[7];    /* S, E, N, W, T, B, current */
	ChunkData    cur;
	int          i, pos, air;
	uint16_t     emitters[PARTICLE_MAX];
	uint8_t      Y, hasLights;

	/* single-thread and multi-thread have completely different allocation strategies */
	if (! meshinit(neighbors[6] = cur = c->layer[layer], &opaque, &alpha))
		/* MT can cancel allocation */
		return;

	/* 6 surrounding chunks (+center) */
	neighbors[5] = layer > 0 ? c->layer[layer-1] : NULL;
	neighbors[4] = layer+1 < c->maxy ? c->layer[layer+1] : empty;
	for (i = 0, pos = 1; i < 4; i ++, pos <<= 1)
	{
		neighbors[i] = c->noChunks & pos ? empty : (c + chunkOffsets[c->neighbor + pos])->layer[layer];
		if (neighbors[i] == NULL)
			neighbors[i] = empty;
	}
	if (cur->emitters)
		cur->emitters[0] = 0;

	/* default sorting for alpha quads */
	cur->yaw = M_PIf * 1.5f;
	cur->pitch = 0;
	cur->cdFlags &= ~(CDFLAG_CHUNKAIR | CDFLAG_PENDINGMESH | CDFLAG_NOALPHASORT | CDFLAG_HOLE);

	memset(visited, 0, sizeof visited);
	hasLights = (cur->cdFlags & CDFLAG_NOLIGHT) == 0;

//	if (c->X == 32 && cur->Y == 64 && c->Z == -16)
//		globals.breakPoint = 1;

	for (Y = 0, pos = air = 0; Y < 16; Y ++)
	{
		int XZ;
		if ((Y & 1) == 0)
			memset(emitters, 0, sizeof emitters);

		/* scan one XZ layer at a time */
		for (XZ = 0; XZ < 256; XZ ++, pos ++)
		{
			BlockState state;
			uint8_t    data;
			uint8_t    block;

			data  = cur->blockIds[DATA_OFFSET + (pos >> 1)]; if (pos & 1) data >>= 4; else data &= 15;
			block = cur->blockIds[pos];
			state = blockGetById(ID(block, data));

//			if (globals.breakPoint && pos == 722)
//				globals.breakPoint = 2;

			/* 3d flood fill for cave culling */
			if ((slotsXZ[pos & 0xff] || slotsY[pos >> 8]) && ! blockIsFullySolid(state))
			{
				if ((visited[pos>>3] & mask8bit[pos&7]) == 0)
					cur->cnxGraph |= chunkGetCnxGraph(cur, pos, visited);
				/* cave fog quad */
				if (hasLights && slotsXZ[pos & 0xff])
					chunkFillCaveHoles(cur, state, pos, CAVE_FOG_BITMAP);
				cur->cdFlags |= (slotsXZ[pos & 0xff] | slotsY[pos >> 8]) << 9;
			}

			if (hasLights)
			{
				/* build list of particles emitters */
				uint8_t particle = blockIds[block].particle;
				if (particle > 0 && particleCanSpawn(cur, pos, data, particle))
					chunkAddEmitters(cur, blockIds[block].emitInterval, pos, particle - 1, emitters);

				if (block == RSOBSERVER)
					chunkMakeObservable(cur, pos, blockSides.piston[data&7]);
			}

			/* voxel meshing starts here */
			switch (state->type) {
			case QUAD:
				chunkGenQuad(neighbors, &opaque, state, pos);
				break;
			case CUST:
				if (state->custModel)
				{
					chunkGenCust(neighbors, STATEFLAG(state, ALPHATEX) ? &alpha : &opaque, state, chunkOffsets, pos);
					/* SOLIDOUTER: custom block with ambient occlusion */
					if (state->special != BLOCK_SOLIDOUTER)
						break;
				}
				/* else no break; */
			case TRANS:
			case SOLID:
				chunkGenCube(neighbors, STATEFLAG(state, ALPHATEX) ? &alpha : &opaque, state, chunkOffsets, pos);
				break;
			default:
				if (state->id == 0) air ++;
			}
		}
	}
	/* entire sub-chunk is composed of air: check if we can get rid of it */
	if (air == 4096 && (cur->cdFlags & CDFLAG_NOLIGHT) == 0)
	{
		/* block light must be all 0 and skylight be all 15 */
		if (memcmp(cur->blockIds + BLOCKLIGHT_OFFSET, empty->blockIds + BLOCKLIGHT_OFFSET, 2048) == 0 &&
			memcmp(cur->blockIds + SKYLIGHT_OFFSET,   empty->blockIds + SKYLIGHT_OFFSET,   2048) == 0)
		{
			if ((cur->Y >> 4) == c->maxy-1)
			{
				/* yes, can be freed */
				c->layer[cur->Y >> 4] = NULL;
				c->maxy --;
				/* cannot delete it now, but will be done after VBO has been cleared */
				cur->cdFlags = CDFLAG_PENDINGDEL;
				chunkMarkForUpdate(c, CHUNK_NBT_SECTION);

				/* check if chunk below are also empty */
				for (i = c->maxy-1; i >= 0; i --)
				{
					cur = c->layer[i];
					if (cur == NULL || (cur->cdFlags & (CDFLAG_CHUNKAIR|CDFLAG_PENDINGMESH)) == CDFLAG_CHUNKAIR)
					{
						/* empty chunk with no pending update: it can be deleted now */
						c->layer[i] = NULL;
						c->maxy = i;
						free(cur);
					}
					else break;
				}
				return;
			}
			else cur->cdFlags |= CDFLAG_CHUNKAIR;
		}
	}
	chunkGenFOG(neighbors[6], &opaque, CAVE_FOG_BITMAP);

	if (opaque.cur > opaque.start)
		opaque.flush(&opaque);
	if (alpha.cur  > alpha.start)  alpha.flush(&alpha);
	if (alpha.isCOP) cur->cdFlags |= CDFLAG_NOALPHASORT;

	if (opaque.merge)
		chunkMergeQuads(cur, opaque.merge);
}

#define BUF_LESS_THAN(buffer,min)   (((DATA8)buffer->end - (DATA8)buffer->cur) < min)
#define META(cd,off)                ((cd)->blockIds[DATA_OFFSET + (off)])
#define LIGHT(cd,off)               ((cd)->blockIds[BLOCKLIGHT_OFFSET + (off)])
#define SKYLIT(cd,off)              ((cd)->blockIds[SKYLIGHT_OFFSET + (off)])

/* tall grass, flowers, rails, ladder, vines, ... */
static void chunkGenQuad(ChunkData neighbors[], MeshWriter buffer, BlockState b, int pos)
{
	DATA8   tex   = &b->nzU;
	DATA8   sides = &b->pxU;
	Chunk   chunk = neighbors[6]->chunk;
	int     seed  = neighbors[6]->Y ^ chunk->X ^ chunk->Z;
	uint8_t x, y, z, light;

	if ((neighbors[6]->cdFlags & CDFLAG_NOLIGHT) == 0)
	{
		x =  LIGHT(neighbors[6], pos>>1);
		y = SKYLIT(neighbors[6], pos>>1);

		if (pos & 1) light = (y & 0xf0) | (x >> 4);
		else         light = (y << 4)   | (x & 15);
	}
	else light = 0xf0; /* brush */

	x = (pos & 15);
	z = (pos >> 4) & 15;
	y = (pos >> 8);

	if (b->special == BLOCK_TALLFLOWER && (b->id&15) == 10)
	{
		uint8_t data;
		/* state 10 is used for top part of all tall flowers :-/ need to look at bottom part to know which top part to draw */
		if (y == 0)
			data = neighbors[5]->blockIds[DATA_OFFSET + ((pos + 256*15) >> 1)];
		else
			data = neighbors[6]->blockIds[DATA_OFFSET + ((pos - 256) >> 1)];
		if (pos & 1) data >>= 4;
		else         data &= 15;
		b += data & 7;
		tex = &b->nzU;
	}

	do {
		uint16_t U, V, X1, Y1, Z1;
		uint8_t  side, norm, j;
		DATA32   out;
		DATA8    coord;

		if (BUF_LESS_THAN(buffer, VERTEX_DATA_SIZE))
			buffer->flush(buffer);

		out   = buffer->cur;
		side  = *sides;
		norm  = quadSides[side];

		coord = cubeVertex + quadIndices[side*4+3];

		/* first vertex */
		X1 = VERTEX(coord[0] + x);
		Y1 = VERTEX(coord[1] + y);
		Z1 = VERTEX(coord[2] + z);

		j = (b->rotate&3) * 8;
		U = (texCoord[j]   + tex[0]) << 4;
		V = (texCoord[j+1] + tex[1]) << 4;

		/* second and third vertex */
		coord  = cubeVertex + quadIndices[side*4];
		out[0] = X1 | (Y1 << 16);
		out[1] = Z1 | (RELDX(coord[0] + x) << 16) | ((V & 512) << 21);
		out[2] = RELDY(coord[1] + y) | (RELDZ(coord[2] + z) << 14);
		coord  = cubeVertex + quadIndices[side*4+2];
		out[3] = RELDX(coord[0] + x) | (RELDY(coord[1] + y) << 14);
		out[4] = RELDZ(coord[2] + z) | (U << 14) | (V << 23);

		/* tex size, norm and ocs: none */
		out[5] = (((texCoord[j+4] + tex[0]) * 16 + 128 - U) << 16) | FLAG_DUAL_SIDE |
		         (((texCoord[j+5] + tex[1]) * 16 + 128 - V) << 24) | (norm << 9);

		if (texCoord[j] == texCoord[j + 6]) out[5] |= FLAG_TEX_KEEPX;
		/* skylight/blocklight: uniform on all vertices */
		out[6] = light | (light << 8) | (light << 16) | (light << 24);

		if (b->special == BLOCK_JITTER)
		{
			/* add some jitter to X,Z coord for QUAD_CROSS */
			uint8_t jitter = seed ^ (x ^ y ^ z);
			if (jitter & 1) out[0] += BASEVTX/16;
			if (jitter & 2) out[1] += BASEVTX/16;
			if (jitter & 4) out[0] -= (BASEVTX/16) << 16;
			if (jitter & 8) out[0] -= (BASEVTX/32) << 16;
		}
		else if (norm < 6)
		{
			/* offset 1/16 of a block in the direction of their normal */
			int8_t * normal = cubeNormals + norm * 4;
			int      base   = side <= QUAD_SQUARE4 ? BASEVTX/4 : BASEVTX/16;
			out[0] += normal[0] * base + (normal[1] * base << 16);
			out[1] += normal[2] * base;
		}
		sides ++;
		buffer->cur = out + VERTEX_INT_SIZE;
	} while (*sides);
}


/*
 * Neighbor is a half-block (slab or stairs): skyval and blocklight will be 0 for these: not good.
 * it will create a dark patch to the side this half-block is connected.
 * To prevent this, we have to reapply skyval and blocklight propagation as if the block was transparent :-/
 */
static uint8_t chunkPatchLight(struct BlockIter_t iter)
{
	uint8_t sky = 0, light = 0, i;

	/* for this, we have to look around the 6 voxels of the slab/stairs */
	for (i = 0; i < 6; i ++)
	{
		mapIter(&iter, xoff[i], yoff[i], zoff[i]);
		uint8_t skyval   = iter.blockIds[SKYLIGHT_OFFSET   + (iter.offset >> 1)];
		uint8_t blockval = iter.blockIds[BLOCKLIGHT_OFFSET + (iter.offset >> 1)];
		if (iter.offset & 1) skyval >>= 4, blockval >>= 4;
		else skyval &= 15, blockval &= 15;
		if (sky < skyval) sky = skyval;
		if (light < blockval) light = blockval;
	}
	/* decrease intensity by 1 if possible */
	if (sky > 0 && sky < MAXSKY) sky --;
	if (light > 0) light --;
	return (sky << 4) | light;
}

/* get sky/block light and occlusion value around block */
static int chunkGetLight(BlockIter iter, DATA16 blockIds3x3, DATA8 skyBlock, int * slabOut, int hasLights)
{
	static int8_t iterNext[] = {
		1,0,0,   1,0,0,   -2,0,1,
		1,0,0,   1,0,0,   -2,0,1,
		1,0,0,   1,0,0,   -2,1,-2
	};
	int8_t * next = iterNext;
	int i, slab = 0, occlusion;

	memset(skyBlock,    0, 27);
	memset(blockIds3x3, 0, 27 * 2);

	mapIter(iter, -1, -1, -1);

	/* only compute that info if block is visible (highly likely it is not) */
	for (i = occlusion = 0; i < 27; i ++)
	{
		uint16_t block;
		uint16_t offset = iter->offset;
		uint8_t  data;
		data  = iter->blockIds[DATA_OFFSET + (offset >> 1)];
		block = iter->blockIds[offset] << 4;

		if (hasLights)
		{
			uint8_t sky, light;
			sky   = iter->blockIds[SKYLIGHT_OFFSET   + (offset >> 1)];
			light = iter->blockIds[BLOCKLIGHT_OFFSET + (offset >> 1)];
			if (offset & 1) skyBlock[i] = (light >> 4) | (sky & 0xf0);
			else            skyBlock[i] = (light & 15) | (sky << 4);
		}
		else skyBlock[i] = 0xf0; /* brush don't have sky/block light info */

		blockIds3x3[i] = block | (offset & 1 ? data >> 4 : data & 15);
		BlockState nbor = blockGetById(block);

		if (nbor->type == CUST && blockIds[block>>4].opacSky == 15)
		{
			/* farmland mostly */
			if (hasLights)
				skyBlock[i] = chunkPatchLight(*iter);
		}
		else if (nbor->type == SOLID || (nbor->type == CUST && nbor->special == BLOCK_SOLIDOUTER))
		{
			if (nbor->special == BLOCK_HALF || nbor->special == BLOCK_STAIRS)
			{
				if (hasLights)
					skyBlock[i] = chunkPatchLight(*iter);
				slab |= 1 << i;
			}
			else occlusion |= 1 << i;
		}

		mapIter(iter, next[0], next[1], next[2]);
		next += 3;
		if (next == EOT(iterNext)) next = iterNext;
	}
	*slabOut = slab;
	return occlusion;
}

/* get sky/light values for CUST face */
static uint32_t chunkFillCustLight(DATA16 model, DATA8 skyBlock, DATA32 ocs, int occlusion)
{
	uint8_t norm = GET_NORMAL(model);
	if (norm < 6)
	{
		static uint8_t norm2axis1[] = {2, 0, 2, 0, 0, 0};
		static uint8_t norm2axis2[] = {1, 1, 1, 1, 2, 2};
		uint32_t out = 0;
		DATA8    offset;
		uint8_t  i, axis1, axis2, hasOCS;
		offset = sampleOffset + norm * 8;
		axis1 = norm2axis1[norm];
		axis2 = norm2axis2[norm];
		hasOCS = norm == 4 && model[INT_PER_VERTEX*2+VX] - model[VX] == BASEVTX &&
			                  model[INT_PER_VERTEX*2+VZ] - model[VZ] == BASEVTX;
		norm = axisCheck[norm];

		/* ocs: only on top (mostly for snow layer and carpet :-/) */
		for (i = 0; i < 4; i ++, model += INT_PER_VERTEX, offset += 2)
		{
			uint8_t dxyz = offset[0], skyval, blockval, n;
			int XYZ[] = {
				model[0] - ORIGINVTX + BASEVTX + (dxyz & 3) - 1,
				model[1] - ORIGINVTX + BASEVTX + ((dxyz & 12) >> 2) - 1,
				model[2] - ORIGINVTX + BASEVTX + ((dxyz & 48) >> 4) - 1
			};
			dxyz = offset[1];
			char DXYZ[] = {(dxyz & 3) - 1, ((dxyz & 12) >> 2) - 1, ((dxyz & 48) >> 4) - 1};
			XYZ[norm] += DXYZ[norm];

			if (hasOCS)
			{
				/* if cust is in lower half, check blocks at current level */
				int check = model[VY] < ORIGINVTX+BASEVTX/2 ? occlusion << 9 : occlusion;
				/* only on top (mostly needed by snow layer and carpet) */
				switch (popcount(check & occlusionIfNeighbor[i+16])) {
				case 2:  ocs[0] |= 3 << i*2; break;
				case 1:  ocs[0] |= 1 << i*2; break;
				default: ocs[0] |= (check & occlusionIfCorner[i+16] ? 1 : 0) << i*2;
				}
			}

			for (skyval = skyBlock[13], blockval = skyval & 15, skyval &= 0xf0, n = 0; n < 4; n ++)
			{
				uint8_t skyvtx = skyBlock[TOVERTEXint(XYZ[0]) + TOVERTEXint(XYZ[2]) * 3 + TOVERTEXint(XYZ[1]) * 9];
				uint8_t light  = skyvtx & 15;
				skyvtx &= 0xf0;
				/* max for block light */
				if (blockval < light) blockval = light;
				/* minimum if != 0 */
				if (skyvtx > 0 && (skyval > skyvtx || skyval == 0)) skyval = skyvtx;
				switch (n) {
				case 0: XYZ[axis1] += DXYZ[axis1]; break;
				case 1: XYZ[axis2] += DXYZ[axis2]; break;
				case 2: XYZ[axis1] -= DXYZ[axis1];
				}
			}
			out |= (skyval | blockval) << (i << 3);
		}
		return out;
	}
	else
	{
		/* rswire mostly */
		uint8_t light = skyBlock[13];
		return light | (light << 8) | (light << 16) | (light << 24);
	}
}

/* custom model mesh: anything that doesn't fit quad or full/half block */
static void chunkGenCust(ChunkData neighbors[], MeshWriter buffer, BlockState b, DATAS16 chunkOffsets, int pos)
{
	static uint8_t connect6blocks[] = {
		/*B*/7, 5, 1, 3, 4,   /*M*/16, 14, 10, 12,   /*T*/25, 23, 19, 21, 22
	};

	Chunk    c = neighbors[6]->chunk;
	DATA32   out;
	DATA16   model;
	DATA8    cnxBlock;
	int      count, connect, x, y, z;
	uint8_t  dualside, hasLights;
	uint16_t blockIds3x3[27];
	uint8_t  skyBlock[27];
	int      occlusion;

	x = (pos & 15);
	z = (pos >> 4) & 15;
	y = (pos >> 8);
	hasLights = (neighbors[6]->cdFlags & CDFLAG_NOLIGHT) == 0;

	{
		struct BlockIter_t iter;
		mapInitIterOffset(&iter, neighbors[6], pos);
		iter.nbor = chunkOffsets;
		occlusion = chunkGetLight(&iter, blockIds3x3, skyBlock, &occlusion, hasLights);
	}
	model = b->custModel;
	count = connect = 0;

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
		/* redstone wire: only use base model */
		skyBlock[13] = (skyBlock[13] & 0xf0) | (b->id & 15);
		b -= b->id & 15;
		// no break;
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
		cnxBlock = chunkGetTileEntity(neighbors[6], pos);
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
		cnxBlock = chunkGetTileEntity(neighbors[6], pos);
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
			c->signList = signAddToList(b->id, neighbors[6], pos, c->signList, skyBlock[13]);
		break;
	default:
		/* piston head with a tile entity: head will be rendered as an entity if it is moving */
		if ((b->id >> 4) == RSPISTONHEAD && chunkGetTileEntity(neighbors[6], pos))
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

	x *= BASEVTX;
	y *= BASEVTX;
	z *= BASEVTX;
	dualside = blockIds[b->id >> 4].special & BLOCK_DUALSIDE;

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
			extern int8_t opp[];
			struct BlockIter_t iter;
			int8_t * normal = cubeNormals + norm * 4;
			mapInitIterOffset(&iter, neighbors[6], pos);
			iter.nbor = chunkOffsets;
			mapIter(&iter, normal[0], normal[1], normal[2]);

			if (blockIsSideHidden(getBlockId(&iter), model, opp[norm]))
				/* skip entire face (6 vertices) */
				continue;
		}

		if (BUF_LESS_THAN(buffer, VERTEX_DATA_SIZE))
			buffer->flush(buffer);

		out = buffer->cur;
		DATA16 coord = model + INT_PER_VERTEX * 3;
		uint16_t X1 = coord[0] + x;
		uint16_t Y1 = coord[1] + y;
		uint16_t Z1 = coord[2] + z;
		uint16_t U  = GET_UCOORD(model);
		uint16_t V  = GET_VCOORD(model);

		#define RELX(x)     ((x) + MIDVTX - X1)
		#define RELY(x)     ((x) + MIDVTX - Y1)
		#define RELZ(x)     ((x) + MIDVTX - Z1)

		coord  = model;
		out[0] = X1 | (Y1 << 16);
		out[1] = Z1 | (RELX(coord[0]+x) << 16) | ((V & 512) << 21);
		out[2] = RELY(coord[1]+y) | (RELZ(coord[2]+z) << 14);
		coord  = model + INT_PER_VERTEX * 2;
		out[3] = RELX(coord[0]+x) | (RELY(coord[1]+y) << 14);
		out[4] = RELZ(coord[2]+z) | (U << 14) | (V << 23);
		out[5] = ((GET_UCOORD(coord) + 128 - U) << 16) |
		         ((GET_VCOORD(coord) + 128 - V) << 24) | (GET_NORMAL(model) << 9);
		out[6] = chunkFillCustLight(model, skyBlock, out + 5, occlusion);
		coord  = model + INT_PER_VERTEX * 3;
		if (dualside) out[5] |= FLAG_DUAL_SIDE;

		/* flip tex */
		if (U == GET_UCOORD(coord)) out[5] |= FLAG_TEX_KEEPX;

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

				out[4] += flag << 18;
			}
			else if (13 <= faceId && faceId <= 16)
			{
				/* center piece connected to another part of glass pane */
				if (connect & (1 << (faceId-9)))
					continue;

				int flag = 0;
				if ((connect & (1<<16)) == 0) flag |= 1;
				if ((connect & (1<<17)) == 0) flag |= 4;

				out[4] += flag << 18;
			}
		}
		buffer->cur = out + VERTEX_INT_SIZE;
	}
}

/* most common block within a chunk */
static void chunkGenCube(ChunkData neighbors[], MeshWriter buffer, BlockState b, DATAS16 chunkOffsets, int pos)
{
	uint16_t blockIds3x3[27];
	uint8_t  skyBlock[27];
	DATA32   out;
	DATA8    tex;
	DATA8    blocks = neighbors[6]->blockIds;
	int      side, sides, occlusion, slab, rotate;
	int      i, j, k, n;
	uint8_t  x, y, z, data, hasLights, liquid;

	x = (pos & 15);
	z = (pos >> 4) & 15;
	y = (pos >> 8);
	hasLights = (neighbors[6]->cdFlags & CDFLAG_NOLIGHT) == 0;
	sides = xsides[x] | ysides[y] | zsides[z];
	liquid = 0;

	/* outer loop: iterate over each faces (6) */
	for (i = 0, side = 1, occlusion = -1, tex = &b->nzU, rotate = b->rotate, j = (rotate&3) * 8, slab = 0; i < DIM(cubeIndices);
		 i += 4, side <<= 1, rotate >>= 2, tex += 2, j = (rotate&3) * 8)
	{
		BlockState nbor;
		n = pos;

		/* face hidden by another opaque block: 75% of SOLID blocks will be culled by this test */
		if (b->special != BLOCK_LEAVES)
		{
			/* check if neighbor is opaque: discard face if yes */
			if ((sides&side) == 0)
			{
				/* neighbor is not in the same chunk */
				ChunkData cd = neighbors[i>>2];
				if (cd == NULL) continue;
				n += blockOffset[side];
				data = META(cd, n>>1);
				nbor = blockGetByIdData(cd->blockIds[n], n & 1 ? data >> 4 : data & 0xf);
			}
			else
			{
				static int offsets[] = {
					/* neighbors: S, E, N, W, T, B */
					16, 1, -16, -1, 256, -256
				};
				n += offsets[i>>2];
				data = blocks[DATA_OFFSET + (n >> 1)];
				nbor = blockGetByIdData(blocks[n], n & 1 ? data >> 4 : data & 0xf);
			}

			switch (nbor->type) {
			case SOLID:
				if (b->special == BLOCK_LIQUID && i == SIDE_TOP * 4)
					/* top of liquid: slightly lower than a full block */
					break;
				switch (nbor->special) {
				case BLOCK_HALF:
				case BLOCK_STAIRS:
					if (oppositeMask[*halfBlockGetModel(nbor, 0, NULL)] & side) continue;
					break;
				default: continue;
				}
				break;
			case TRANS:
				if (b->id == nbor->id) continue;
				if (b->special == BLOCK_LIQUID && nbor->id == ID(79,0)) continue;
			}
		}

		/* ambient occlusion neighbor: look after 26 surrounding blocks (only 20 needed by AO) */
		if (occlusion == -1)
		{
			struct BlockIter_t iter;
			mapInitIterOffset(&iter, neighbors[6], pos);
			iter.nbor = chunkOffsets;
			occlusion = chunkGetLight(&iter, blockIds3x3, skyBlock, &slab, hasLights);

			/* CUST with no model: don't apply ambient occlusion, like CUST model will */
			if (b->type == CUST && b->special != BLOCK_SOLIDOUTER)
				occlusion = slab = 0, memset(skyBlock, skyBlock[13], 27);
			if (STATEFLAG(b, CNXTEX))
			{
				static uint8_t texUV[12];
				DATA8 cnx, uv;
				int   id = b->id;
				memcpy(texUV, &b->nzU, 12);
				/* check for connected textures */
				for (k = 0, uv = texUV, cnx = offsetConnected; k < DIM(offsetConnected); k += 4, cnx += 4, uv += 2)
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
				for (k = 18; k < 27; k ++)
				{
					static uint8_t raisedEdge[] = {2, 3, 1, 10, 15, 5, 8, 12, 4};
					if (blockIds[blockIds3x3[k]>>4].special == BLOCK_LIQUID)
						liquid |= raisedEdge[k-18];
				}
				liquid ^= 15;
			}
		}

		if (b->special == BLOCK_HALF || b->special == BLOCK_STAIRS)
		{
			uint8_t xyz[3] = {x<<1, y<<1, z<<1};
			//fprintf(stderr, "meshing %s at pos %d, %d, %d\n", b->name, x, y, z);
			meshHalfBlock(buffer, halfBlockGetModel(b, 2, blockIds3x3), 2, xyz, b, blockIds3x3, skyBlock, 63);
			break;
		}
		if (occlusionIfSlab[i>>2] & slab)
		{
			uint8_t xyz[3] = {x<<1, y<<1, z<<1};
			DATA8 model = halfBlockGetModel(b, 2, blockIds3x3);
			if (model)
			{
				meshHalfBlock(buffer, model, 2, xyz, b, blockIds3x3, skyBlock, 1 << (i>>2));
				continue;
			}
		}

		if (BUF_LESS_THAN(buffer, VERTEX_DATA_SIZE))
			buffer->flush(buffer);

		/* generate one quad (see internals.html for format) */
		{
			DATA8    coord = cubeVertex + cubeIndices[i+3];
			uint16_t texU  = (texCoord[j]   + tex[0]) << 4;
			uint16_t texV  = (texCoord[j+1] + tex[1]) << 4;
			uint16_t X1, Y1, Z1;

			X1 = VERTEX(coord[0]+x);
			Y1 = VERTEX(coord[1]+y);
			Z1 = VERTEX(coord[2]+z);
			out = buffer->cur;

			/* write one quad */
			coord  = cubeVertex + cubeIndices[i];
			out[0] = X1 | (Y1 << 16);
			out[1] = Z1 | (RELDX(coord[0]+x) << 16) | ((texV & 512) << 21);
			out[2] = RELDY(coord[1]+y) | (RELDZ(coord[2]+z) << 14);
			coord  = cubeVertex + cubeIndices[i+2];
			out[3] = RELDX(coord[0]+x) | (RELDY(coord[1]+y) << 14);
			out[4] = RELDZ(coord[2]+z) | (texU << 14) | (texV << 23);
			out[5] = (((texCoord[j+4] + tex[0]) * 16 + 128 - texU) << 16) |
			         (((texCoord[j+5] + tex[1]) * 16 + 128 - texV) << 24) | (i << 7);
			out[6] = 0;

			/* flip tex */
			if (texCoord[j] == texCoord[j + 6]) out[5] |= FLAG_TEX_KEEPX;

			static uint8_t oppSideBlock[] = {16, 14, 10, 12, 22, 4};
			if (blockIds[blockIds3x3[oppSideBlock[i>>2]] >> 4].special == BLOCK_LIQUID)
				/* use water fog instead of atmospheric one */
				out[5] |= FLAG_UNDERWATER;

			/* sky/block light values: 2*4bits per vertex = 4 bytes needed, ambient occlusion: 2bits per vertex = 1 byte needed */
			for (k = 0; k < 4; k ++)
			{
				uint8_t skyval, blockval, off, ocs;

				n = 4;
				switch (popcount(occlusion & occlusionIfNeighbor[i+k])) {
				case 2: ocs = 3; n = 3; break;
				case 1: ocs = 1; break;
				default: ocs = occlusion & occlusionIfCorner[i+k] ? 1 : 0;
				}

				for (skyval = skyBlock[13], blockval = skyval & 15, skyval &= 0xf0, off = (i+k) * 4; n > 0; off ++, n --)
				{
					uint8_t skyvtx = skyBlock[skyBlockOffset[off]];
					uint8_t light  = skyvtx & 15;
					skyvtx &= 0xf0;
					/* max for block light */
					if (blockval < light) blockval = light;
					/* minimum if != 0 */
					if (skyvtx > 0 && (skyval > skyvtx || skyval == 0)) skyval = skyvtx;
				}
				out[6] |= (skyval | blockval) << (k << 3);

				if (b->special == BLOCK_LIQUID && i == SIDE_TOP * 4)
				{
					/* reduce ambient occlusion a bit */
					static uint8_t lessAmbient[] = {0, 1, 1, 1};
					ocs = lessAmbient[ocs];
				}
				out[5] |= ocs << k*2;
			}
			if (b->special == BLOCK_LIQUID)
			{
				/* lower some edges of a liquid block if we can (XXX need a better solution than this :-/) */
				uint8_t edges = 0;
				switch (i >> 2) {
				case SIDE_SOUTH: edges = (liquid&12)>>2; break;
				case SIDE_NORTH: edges = ((liquid&1)<<1) | ((liquid&2)>>1); break;
				case SIDE_EAST:  edges = (liquid&1) | ((liquid & 4) >> 1); break;
				case SIDE_WEST:  edges = (liquid&2) | ((liquid & 8) >> 3); break;
				case SIDE_TOP:   edges = liquid;
				}
				if (edges)
				{
					out[5] |= FLAG_TRIANGLE | FLAG_UNDERWATER | FLAG_DUAL_SIDE;
					out[2] |= edges << 28;
				}
				else out[5] |= FLAG_UNDERWATER | FLAG_DUAL_SIDE;
			}
			if (buffer->merge)
				meshQuadMergeAdd(buffer->merge, out);
		}
		buffer->cur = out + VERTEX_INT_SIZE;
	}
}

/*
 * generate some dummy quads to cover holes in caves (only in the S, E, N, W planes). Minecraft does
 * fake this by changing the entire sky color if you get below a certain Y coord: meh.
 */
static void chunkFillCaveHoles(ChunkData cur, BlockState state, int pos, DATA16 holes)
{
	uint8_t sides = slotsXZ[pos & 0xff];
	uint8_t y = pos >> 8;
	uint8_t x = pos & 15;
	uint8_t z = (pos >> 4) & 15;
	uint8_t sky;

	/* sky value need to be 0 for the hole to be covered */
	if (state->special == BLOCK_STAIRS || state->special == BLOCK_HALF)
	{
		/* half-slab/stairs always have a skylight of 0, but will need patching if a sky light is around */
		struct BlockIter_t iter;
		mapInitIterOffset(&iter, cur, pos);
		if (chunkPatchLight(iter) >> 4 > 0)
			holes += 16;
	}
	else if (state->special != BLOCK_LIQUID)
	{
		sky = cur->blockIds[SKYLIGHT_OFFSET + (pos >> 1)];

		if (pos & 1) sky >>= 4;
		else         sky &= 15;

		if (sky > 0) holes += 16;
	}
	else /* "cave" fog will also be applied to ocean (to cover "holes" at the edge of the render distance) */
	{
		sky = 0;
		if (y >= 15)
		{
			uint8_t layer = (cur->Y >> 4) + 1;
			if (layer < cur->chunk->maxy && (cur = cur->chunk->layer[layer]))
				sky = cur->blockIds[pos & 255];
		}
		else sky = cur->blockIds[pos+256];
		if (blockIds[sky].special != BLOCK_LIQUID)
		{
			if (sides & 1) holes[32]  |= 1 << y;
			if (sides & 2) holes[65]  |= 1 << y;
			if (sides & 4) holes[98]  |= 1 << y;
			if (sides & 8) holes[131] |= 1 << y;
		}
	}

	/* S, E, N, W sides */
	if (sides & 1) holes[   y] |= mask16bit[x];
	if (sides & 2) holes[33+y] |= mask16bit[z];
	if (sides & 4) holes[66+y] |= mask16bit[x];
	if (sides & 8) holes[99+y] |= mask16bit[z];
}

/* generate actual quads, but we need holes data first (result from previous function) */
static void chunkGenFOG(ChunkData cur, MeshWriter buffer, DATA16 holesSENW)
{
	uint8_t side;
	uint8_t XYZ[4], w, h;

	for (side = 0; side < 4; side ++, holesSENW += 33)
	{
		uint16_t holes, avoid;
		DATA16   p;
		uint8_t  i, axis;

		axis = 2 - axisCheck[side];
		XYZ[2 - axis] = side < 2 ? 16 : 0;

		/* check first if we can get away with only one quad */
		for (i = 0, p = holesSENW, holes = avoid = 0; i < 16 && (p[0] == 0 || p[16] == 0xffff); p ++, i ++);

		/* surface chunks usually don't have any cave holes */
		if (i == 16) continue;

		int8_t * normal = cubeNormals + side * 4;

		for (XYZ[VY] = i, h = i, p = holesSENW + i, holes = p[0], avoid = p[16], p ++, i ++; i < 16; i ++, p ++)
		{
			if (((holes | p[0]) & (avoid | p[16])) == 0)
			{
				holes |= p[0];
				avoid |= p[16];
				if (p[0] > 0) h = i;
				continue;
			}
			/* can't expand further: flush current hole accumulation */
			flush_holes:
			for (XYZ[axis] = ZEROBITS(holes), holes >>= XYZ[axis], h ++; holes; )
			{
				if (holes & 1)
				{
					static uint8_t startV13[] = {3, 2, 0, 3};
					static uint8_t startV2[]  = {0, 3, 3, 2};
					for (w = 1, holes >>= 1; holes & 1; w ++, holes >>= 1);

					if (BUF_LESS_THAN(buffer, VERTEX_DATA_SIZE))
						buffer->flush(buffer);

					XYZ[3] = XYZ[axis] + w;

					DATA32 out = buffer->cur;
					uint8_t XYZ2[8];
					uint16_t mask;
					memcpy(XYZ2,   XYZ, 4);
					memcpy(XYZ2+4, XYZ, 4);
					XYZ2[axis]   = XYZ[startV13[side]];
					XYZ2[4+axis] = XYZ[startV2[side]];

					/* reduce vertical span of quad if we can */
					for (XYZ2[VY+4] = h, mask = ((1 << w) - 1) << XYZ[axis]; (holesSENW[XYZ2[VY+4]-1] & mask) == 0; XYZ2[VY+4] --);
					for (XYZ2[VY] = XYZ[VY]; (holesSENW[XYZ2[VY]] & mask) == 0; XYZ2[VY] ++);

					out[0] = (VERTEX(XYZ2[VX]) + normal[VX]) | (VERTEX(XYZ2[VY+4]) << 16);
					out[1] = (VERTEX(XYZ2[VZ]) + normal[VZ]) | (16 << 16);
					out[2] = (16 << 14) | (XYZ2[VY] + 16 - XYZ2[VY+4]);
					out[3] = (16 << 14) | (XYZ2[VX+4] + 16 - XYZ2[VX]);
					out[4] = (XYZ2[VZ+4] + 16 - XYZ2[VZ]);
					out[5] = side << 9;

					/* liquid blocks are lowered by 0.2 unit in geometry shader, need to also be applied on fog quad :-/ */
					if (holesSENW[32] & (1 << (XYZ2[VY+4]-1)))
					{
						/* lower vertex V1 and V3 */
						out[5] |= FLAG_TRIANGLE;
						out[2] |= 5 << 28;
					}

					/*
					 * cave fog does not really work with anything but block light == 0
					 * need to investigate for a better method :-/
					 */
					out[6] = 0; //getFogLight(cur, XYZ2);
					buffer->cur = out + VERTEX_INT_SIZE;
					XYZ[axis] += w;
					globals.level->fogCount ++;
				}
				else
				{
					w = ZEROBITS(holes);
					holes >>= w;
					XYZ[axis] += w;
				}
			}
			for (; i < 16 && p[0] == 0; i ++, p ++);
			XYZ[VY] = h = i;
			if (i >= 16) break;
			holes = p[0];
			avoid = p[16];
		}
		if (holes)
		{
			h = 15;
			goto flush_holes;
		}
	}
}

/*
 * scan all quads from SOLID blocks and check if they can be merged.
 * sadly, we need all quad data first :-/
 */
static uint8_t quadDirections[] = { /* SENWTB */
	VY, 1, VX, 1,
	VY, 1, VZ, 0,
	VY, 1, VX, 0,
	VY, 1, VZ, 1,
	VZ, 0, VX, 1,
	VZ, 1, VX, 1,
};

static void chunkMergeQuads(ChunkData cd, HashQuadMerge hash)
{
	int index, merged = 0;
	for (index = hash->firstAdded; index != 0xffff; )
	{
		HashQuadEntry entry = hash->entries + index;
		DATA32 quad = entry->quad;
		/* check if already processed */
		if (quad == NULL) goto next;
		entry->quad = NULL;

		/* ocs will constraint the directions we can go */
		uint8_t ocs1 = quad[5] & 3;
		uint8_t ocs2 = (quad[5] >> 2) & 3;
		uint8_t ocs3 = (quad[5] >> 4) & 3;
		uint8_t ocs4 = (quad[5] >> 6) & 3;
		uint8_t dir;

		if (ocs1 == ocs2 && ocs1 == ocs3 && ocs1 == ocs4)
		{
			dir = 3;
		}
		else if (ocs1 == ocs2 && ocs3 == ocs4)
		{
			dir = 1;
		}
		else if (ocs1 == ocs4 && ocs2 == ocs3)
		{
			dir = 2;
		}
		else goto next;

		uint32_t ref[VERTEX_INT_SIZE];
		uint8_t  min, max, axis;
		DATA8    directions = quadDirections + (((quad[5] >> 9) & 7) << 2);

		if ((dir & 1) == 0) directions += 2, dir >>= 1;
		memcpy(ref, quad, VERTEX_DATA_SIZE);
		axis = directions[0];
		switch (axis) {
		case VX: max = ((quad[0] & 0xffff) - ORIGINVTX) >> 11; break;
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
			if (index < 0) { max --; break; }

			/* yes, can be merged: mark next one as processed */
			hash->entries[index].quad = NULL;
			merged ++;
		}
		#if 0
		// XXX this branch should be useless
		/* go to the opposite direction */
		memcpy(ref, quad, 2 * sizeof *ref);
		while (min > 0)
		{
			min --;
			switch (axis) {
			case VX: ref[0] -= BASEVTX; break;
			case VY: ref[0] -= BASEVTX << 16; break;
			case VZ: ref[1] -= BASEVTX;
			}
			index = meshQuadMergeGet(hash, ref);
			if (index < 0) { min ++; break; }

			/* yes, can be merged: mark next one as processed */
			hash->entries[index].quad = NULL;
			merged ++;
		}
		#endif

		if (dir > 1)
		{
			/* check if we can expand this even further in 2nd direction */
			uint8_t max2, axis2;
			axis2 = directions[2];
			memcpy(ref, quad, VERTEX_DATA_SIZE);
			switch (axis2) {
			case VX: max2 = ((quad[0] & 0xffff) - ORIGINVTX) >> 11; break;
			case VY: max2 = ((quad[0] >> 16) - ORIGINVTX) >> 11; break;
			case VZ: max2 = ((quad[1] & 0xffff) - ORIGINVTX) >> 11;
			}
			max2 -= directions[3];
			max ++;
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
					if (p[0] == 0xffff) goto next;
					switch (axis) {
					case VX: ref[0] += BASEVTX; break;
					case VY: ref[0] += BASEVTX<<16; break;
					case VZ: ref[1] += BASEVTX;
					}
				}
				/* mark quads as processed */
				for (p = indices, merged += max - min; p < eof; p ++)
					hash->entries[p[0]].quad = NULL;
				memcpy(ref, start, sizeof start);
			}
		}

		next:
		index = entry->nextAdded;
	}
	cd->glMerge = merged;
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
