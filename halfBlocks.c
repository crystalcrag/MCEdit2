/*
 * halfblocks.c : handle meshing of half block (well, technically it is 1/8).
 *
 * Written by T.Pierron, Apr 2020.
 */

#include <string.h>
#include <stdio.h>
#include <malloc.h>
#include "maps.h"
#include "blocks.h"
#include "meshBanks.h"

#define BITS(bit1, bit2, bit3, bit4, bit5, bit6, bit7, bit8) \
	(bit1 | (bit2<<1) | (bit3<<2) | (bit4<<3) | (bit5<<4) | (bit6<<5) | (bit7<<6) | (bit8<<7))

/* some pre-defined models (ordered XZY) */
static uint8_t modelsSize2[] = {
	BITS(1, 1, 1, 1, 0, 0, 0, 0), /* bottom slab */
	BITS(0, 0, 0, 0, 1, 1, 1, 1), /* top slab */
	BITS(1, 1, 1, 1, 0, 1, 0, 1), /* bottom stairs, data:0 */
	BITS(1, 1, 1, 1, 1, 0, 1, 0), /* bottom stairs, data:1 */
	BITS(1, 1, 1, 1, 0, 0, 1, 1), /* bottom stairs, data:2 */
	BITS(1, 1, 1, 1, 1, 1, 0, 0), /* bottom stairs, data:3 */
	BITS(0, 1, 0, 1, 1, 1, 1, 1), /* top, stairs, data:4 */
	BITS(1, 0, 1, 0, 1, 1, 1, 1), /* top, stairs, data:5 */
	BITS(0, 0, 1, 1, 1, 1, 1, 1), /* top, stairs, data:6 */
	BITS(1, 1, 0, 0, 1, 1, 1, 1), /* top, stairs, data:7 */

	/* connected stairs model (bottom) */
	BITS(1, 1, 1, 1, 1, 0, 0, 0),
	BITS(1, 1, 1, 1, 0, 1, 0, 0),
	BITS(1, 1, 1, 1, 0, 0, 1, 0),
	BITS(1, 1, 1, 1, 0, 0, 0, 1),
	BITS(1, 1, 1, 1, 0, 1, 1, 1),
	BITS(1, 1, 1, 1, 1, 0, 1, 1),
	BITS(1, 1, 1, 1, 1, 1, 0, 1),
	BITS(1, 1, 1, 1, 1, 1, 1, 0),

	/* top */
	BITS(1, 0, 0, 0, 1, 1, 1, 1),
	BITS(0, 1, 0, 0, 1, 1, 1, 1),
	BITS(0, 0, 1, 0, 1, 1, 1, 1),
	BITS(0, 0, 0, 1, 1, 1, 1, 1),
	BITS(0, 1, 1, 1, 1, 1, 1, 1),
	BITS(1, 0, 1, 1, 1, 1, 1, 1),
	BITS(1, 1, 0, 1, 1, 1, 1, 1),
	BITS(1, 1, 1, 0, 1, 1, 1, 1),
};

/* pairs of U, V dir (S,E,N,W,T,B) */
static uint8_t UVdirs[] = {1, 4, 0, 4, 1, 4, 0, 4, 1, 0, 1, 0};

/* auto-generated from modelsSize2[] */
static uint8_t modelsSize0[DIM(modelsSize2)];
extern uint8_t skyBlockOffset[];

/* keep a cache of models surrounding a given blocks */
struct ModelCache_t
{
	uint32_t set;
	uint8_t  cache[28];
};

typedef struct ModelCache_t *     ModelCache;

void halfBlockInit(void)
{
	int i, j, k;
	for (i = 0; i < DIM(modelsSize2); i ++)
	{
		static uint8_t incfaces[] = {4+8+32, 4+2+32, 1+8+32, 1+2+32, 4+8+16, 4+2+16, 1+8+16, 1+2+16};
		uint8_t faces[6], model = modelsSize2[i];
		DATA8 face;
		memset(faces, 0, sizeof faces);
		for (j = 1, face = incfaces; j <= 128; j <<= 1, face ++)
		{
			uint8_t inc;
			if (model & j)
				for (inc = *face, k = 0; k < 6; k ++, inc >>= 1)
					if (inc & 1) faces[k] ++;
		}
		for (j = 0, model = 0; j < 6; j ++)
			if (faces[j] == 4) model |= 1 << j;
		modelsSize0[i] = model;
	}
}

/* connected stairs model */
static DATA8 halfBlockGetConnectedModel(BlockState b, DATA16 neighborBlockIds)
{
	/* only need the 4 surrounding blocks (S, E, N, w) */
	#define ANDOR(and, or)      ((((~and)&15)<<4) | or)
	static uint8_t connection[] = {
		14, 3, ANDOR(8,0), 14, 2, ANDOR(2,0), 12, 2, ANDOR(0,4), 12, 3, ANDOR(0,1),
		12, 3, ANDOR(4,0), 12, 2, ANDOR(1,0), 14, 2, ANDOR(0,8), 14, 3, ANDOR(0,2),
		16, 0, ANDOR(4,0), 16, 1, ANDOR(8,0), 10, 0, ANDOR(0,2), 10, 1, ANDOR(0,1),
		10, 0, ANDOR(1,0), 10, 1, ANDOR(2,0), 16, 0, ANDOR(0,8), 16, 1, ANDOR(0,4),
	};
	#undef ANDOR

	DATA8   model = modelsSize2 + (b->id & 7) + 2;
	uint8_t bits  = *model;
	DATA8   cnx   = connection + (b->id & 3) * 12;
	int     up    = b->id & 4;
	int     i;

	if (! neighborBlockIds) return model;
	if (! up) bits >>= 4;

	for (i = 0; i < 4; i ++, cnx += 3)
	{
		BlockState n = blockGetById(neighborBlockIds[cnx[0]]);

		if (n->special == BLOCK_STAIRS && (n->id&4) == up && (n->id&3) == cnx[1])
		{
			uint8_t andor = cnx[2];
			bits &= andor >> 4;
			bits |= andor & 15;
		}
	}
	up <<= 1;
	static uint8_t bit2ord[] = {0, 0, 1, 0, 2, 0, 0, 0, 3};
	switch (popcount(bits)) {
	case 1: return modelsSize2 + 10 + bit2ord[bits] + up;
	case 3: return modelsSize2 + 14 + bit2ord[bits^15] + up;
	}
	return model;
}

DATA8 halfBlockGetModel(BlockState b, int size, DATA16 neighborBlockIds)
{
	static uint8_t fullySolid = 0xff;
	switch (size) {
	case 0:
	case 1:
		switch (b->special) {
		case BLOCK_HALF:   return &modelsSize0[(b->id&15) > 7];
		case BLOCK_STAIRS: return &modelsSize0[(b->id&7)  + 2];
		default: return b->type == SOLID ? &fullySolid : NULL;
		}
		break;
	case 2:
		switch (b->special) {
		case BLOCK_HALF:   return &modelsSize2[(b->id&15) > 7];
		case BLOCK_STAIRS: return halfBlockGetConnectedModel(b, neighborBlockIds);
		default: return b->type == SOLID ? &fullySolid : NULL;
		}
	case 8: /* TODO */
		break;
	}
	return NULL;
}

static DATA16 halfBlockRelocCenter(int center, DATA16 neighborBlockIds, DATA16 buffer)
{
	static int8_t NWES[] = {0, -1, -1, 0, 1, 0, 0, 1};
	int8_t i, x, z, y;

	x = (center % 3);
	z = (center / 3) % 3;
	y = (center / 9) * 9;
	for (i = 0; i < 8; i += 2)
	{
		int8_t x2 = x + NWES[i];
		int8_t z2 = z + NWES[i+1];
		buffer[i] = x2 < 0 || x2 > 2 || z2 < 0 || z2 > 2 ? 0 : neighborBlockIds[x2+z2*3+y];
	}

	/* looks dangerous, but first 10 items are not read */
	return buffer - 10;
}

static Bool isVisible(DATA16 neighborBlockIds, ModelCache models, DATA8 pos, int dir)
{
	static int offsets[] = {-2, -1, 2, 1, -4, 4};
	static uint8_t blockIndex[] = {16, 14, 10, 12, 22, 4};
	uint8_t off = blockIndex[dir];

	if ((models->set & (1 << off)) == 0)
	{
		DATA16 buffer = alloca(14);
		DATA8 model2x2 = halfBlockGetModel(blockGetById(neighborBlockIds[off]), 2, halfBlockRelocCenter(off, neighborBlockIds, buffer));
		models->set |= 1<<off;
		models->cache[off] = model2x2 ? model2x2[0] : 0;
	}

	uint8_t bit = 1 << ((pos[0] + 2 * (pos[2] + pos[1] * 2) + offsets[dir]) & 7);
	return (models->cache[off] & bit) == 0;
}

/*
 * main function to convert a detail block metadata into a triangle mesh
 */
void meshHalfBlock(MeshWriter write, DATA8 model, int size /* 2 or 8 */, DATA8 xyz, BlockState b, DATA16 neighborBlockIds, int genSides)
{
	static uint8_t xsides[] = { 2, 8};
	static uint8_t ysides[] = {16,32};
	static uint8_t zsides[] = { 1, 4};
	static int8_t  offset[] = {2,1,-2,-1,4,-4};

	struct ModelCache_t models;

	uint8_t faces[8];
	uint8_t pos[4];
	DATA32  out;
	DATA8   face;
	int     i, j, k;

	/* expand binary field (ordered XZY, like chunks) */
	genSides ^= 63;
	models.set = 1<<13;
	models.cache[13] = model[0];
	for (i = 0, j = 1, k = model[0], face = faces; i < 8; i ++, *face++ = (k & j) ? genSides : 255, j <<= 1);

	/* do the meshing */
	#define x      pos[0]
	#define y      pos[1]
	#define z      pos[2]
	for (face = faces, out = write->cur, i = 8, memset(pos, 0, sizeof pos); i > 0; i --, face ++)
	{
		uint8_t flags = *face, sides;
		uint16_t rotate;
		if ((flags & 63) == 63) continue; /* empty (or done) sub-voxel */

		k = face - faces;
		if (size == 2)
		{
			x = k&1;
			z = (k>>1) & 1;
			y = k>>2;
		}
		else x = k&7, z = (k>>3) & 7, y = k>>6;
		sides = xsides[x] | ysides[y] | zsides[z];

		/* scan missing face on this sub-block */
		for (j = 0, rotate = b->rotate; j < 6; j ++, rotate >>= 2)
		{
			static uint8_t dir0[]  = {2, 0, 2, 0, 1, 1}; /* index in pos/rect */
			static uint8_t axis[]  = {2, 0, 0, 0, 1};
			static uint8_t invUV[] = {0, 1, 1, 0, 2, 0};
			uint8_t rect[4], mask = 1 << j;
			/* already processed? */
			if (flags & mask) continue;

			/* is face visible (empty space in neighbor block) */
			if ((sides & mask) ? face[offset[j]] < 255 : !isVisible(neighborBlockIds, &models, pos, j)) { *face |= mask; continue; }

			/* check if we can expand in one of 2 directions */
			memset(rect, 1, 4);
			rect[dir0[j]] = 0;

			/* try to expand initial rect */
			uint8_t  cur[4];
			int8_t   faceOff[3];
			uint8_t  dirU  = UVdirs[j*2];
			uint8_t  dirV  = UVdirs[j*2+1];
			uint8_t  axisU = axis[dirU];
			uint8_t  axisV = axis[dirV];
			uint8_t  rev   = invUV[j];
			DATA8    face2;

			faceOff[0] = faceOff[2] = offset[dirU];
			faceOff[1] = offset[dirV] - faceOff[0];
			memcpy(cur, pos, 4);

			for (k = 0, face2 = face; k < 3; k ++)
			{
				static int8_t subVoxel[] = {1,-1,1,0,1,0};
				cur[axisU] += subVoxel[k];
				cur[axisV] += subVoxel[k+3];
				face2 += faceOff[k];
				if (cur[axisU] == 2 || cur[axisV] == 2 || (*face2 & mask)) continue;
				if ((sides & mask) ? face2[offset[j]] < 255 : !isVisible(neighborBlockIds, &models, cur, j)) continue;
			}

			if (write->end - out < VERTEX_INT_SIZE)
			{
				write->cur = out;
				write->flush(write);
				out = write->start;
			}

			#undef  VERTEX
			#define VERTEX(x)     ((x) * (BASEVTX/2) + ORIGINVTX)

			/* add rect [pos x rect] to mesh */
			DATA8 UV = &b->nzU + (j << 1);
			if (j == 1) rect[0] ++;
			if (j == 4) rect[1] ++;
			if (j == 0) rect[2] ++;

			{
				static uint8_t coordU[] = {0, 2, 0, 2, 0, 0};
				static uint8_t coordV[] = {1, 1, 1, 1, 2, 2};
				uint16_t U, V, Usz, Vsz, base;
				uint8_t  vtx[12];
				#define texSz    3

				face2 = cubeIndices + j * 4;
				DATA8 idx = cubeVertex + face2[3];
				/* first vertex */
				vtx[0] = pos[0] + idx[0] * rect[0];
				vtx[1] = pos[1] + idx[1] * rect[1];
				vtx[2] = pos[2] + idx[2] * rect[2];

				/* second vertex */
				idx = cubeVertex + face2[0];
				vtx[4] = pos[0] + (idx[0] * rect[0]);
				vtx[5] = pos[1] + (idx[1] * rect[1]);
				vtx[6] = pos[2] + (idx[2] * rect[2]);

				/* UV coord */
				base = vtx[4+coordU[j]] << texSz; U = (UV[0] << 4) + (rev == 1 ? 16 - base : base);
				base = vtx[4+coordV[j]] << texSz; V = (UV[1] << 4) + (rev != 2 ? 16 - base : base);

				/* third vertex */
				idx = cubeVertex + face2[2];
				vtx[8]  = pos[0] + (idx[0] * rect[0]);
				vtx[9]  = pos[1] + (idx[1] * rect[1]);
				vtx[10] = pos[2] + (idx[2] * rect[2]);

				/* tex size and normal */
				base = vtx[8+coordU[j]] << texSz; Usz = (UV[0] << 4) + (rev == 1 ? 16 - base : base);
				base = vtx[8+coordV[j]] << texSz; Vsz = (UV[1] << 4) + (rev != 2 ? 16 - base : base);
				switch (rotate & 3) {
				case 1: swap(V, Vsz); break;
				case 3: swap(U, Usz); break;
				case 2: swap(U, Usz); swap(V, Vsz); break;
				}
				base = (rotate & 3) * 8;
				out[0] = VERTEX(vtx[0] + xyz[0]) | (VERTEX(vtx[1] + xyz[1]) << 16);
				out[1] = VERTEX(vtx[2] + xyz[2]) | (VERTEX(vtx[4] + xyz[0]) << 16);
				out[2] = VERTEX(vtx[5] + xyz[1]) | (VERTEX(vtx[6] + xyz[2]) << 16);
				out[3] = VERTEX(vtx[8] + xyz[0]) | (VERTEX(vtx[9] + xyz[1]) << 16);
				out[4] = (VERTEX(vtx[10] + xyz[2]) << 16);
				out[5] = U | (V << 9) | (j << 19) | (texCoord[base] == texCoord[base+6] ? FLAG_TEX_KEEPX : 0);
				out[6] = Usz | (Vsz << 9);

				static uint8_t oppSideBlock[] = {16, 14, 10, 12, 22, 4};
				if (blockIds[neighborBlockIds[oppSideBlock[j]] >> 4].special == BLOCK_LIQUID)
					/* use water fog instead of atmospheric one */
					out[5] |= FLAG_UNDERWATER;
			}
			out += VERTEX_INT_SIZE;
		}
	}
	write->cur = out;
}

/*
 * generate accurate bounding box from half-blocks
 */
void halfBlockGetBBox(DATA16 neighborBlockIds, VTXBBox array, int max)
{
	uint8_t visited[8];
	uint8_t pos[4];
	int     total, size, i, j, k;

	/* XXX for now only take care of stairs/slabs */
	memset(visited, 0, sizeof visited);
	size  = 2;
	total = size * size * size;
	array->cont = 0;

	DATA8 model = halfBlockGetModel(blockGetById(neighborBlockIds[13]), 2, neighborBlockIds);
	DATA8 faces = alloca(total + size);
	DATA8 zero  = faces + total;
	DATA8 face;
	VTXBBox bbox;

	/* expand binary field (ordered XZY, like chunks) */
	for (i = 0, j = 1, k = *model++, face = faces, memset(zero, 0, size); i < total; i ++, j <<= 1, face ++)
	{
		if (j > 128)
		{
			j = 1;
			k = *model++;
		}
		*face = (k & j) ? 0 : 255;
	}

	/* build VTXBBox list */
	for (face = faces, bbox = array, memset(pos, 0, sizeof pos), k = 0; k < total && array->cont < max; k ++, face ++)
	{
		uint8_t flags = *face;
		uint8_t rect[4];
		if (flags) continue; /* empty or visited sub-voxel */

		/* x, y, z are still alias to pos[0], pos[1], pos[2] :-/ */
		if (size == 2)
		{
			x = k&1;
			z = (k>>1) & 1;
			y = k>>2;
		}
		else x = k&7, z = (k>>3) & 7, y = k>>6;

		/* try to expand in 3 directions: X first */
		bbox->pt1[VX] = VERTEX(x);
		bbox->pt1[VY] = VERTEX(y);
		bbox->pt1[VZ] = VERTEX(z);
		DATA8 p;
		for (rect[VX] = 1, p = face + 1, x ++; x < size && *p == 0; *p++ = 255, rect[VX] ++, x ++);
		/* then Z */
		for (rect[VZ] = 1, p = face + size, z ++; z < size && memcmp(p, zero, rect[VX]) == 0; memset(p, 255, rect[VX]), p += size, rect[VZ] ++, z ++);
		/* then Y */
		for (j = size * size, p = face + j, y ++; y < size; p += j, y ++)
		{
			uint8_t m;
			for (m = rect[VZ]; m > 0 && memcmp(p, zero, rect[VX]) == 0; m --, p += size);
			if (m == 0)
			{
				for (m = rect[VZ], p = face + j; m > 0; memset(p, 255, rect[VX]), m --, p += size);
			}
			else break;
 		}

		/* generate new bbox */
		bbox->pt2[VX] = VERTEX(x);
		bbox->pt2[VY] = VERTEX(y);
		bbox->pt2[VZ] = VERTEX(z);
		bbox ++;
		array->cont ++;
	}
}

