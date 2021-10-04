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

static uint8_t ocsOffsets[] = { /* S, E, N, W, T, B:  3 * 4 coord per face */
	15,25,24,  15, 7, 6,  17, 7, 8,  25,17,26,
	23,17,26,  17, 5, 8,  11, 5, 2,  23,11,20,
	19,11,20,  11, 1, 2,   9, 1, 0,  19, 9,18,
	 9,21,18,   9, 3, 0,  15, 3, 6,  21,15,24,
	21,19,18,  21,25,24,  23,25,26,  23,19,20,
	 3, 7, 6,   3, 1, 0,   5, 1, 2,   7, 5, 8,
};

static uint8_t ocs2x2[] = {
	1, 2, 0, 3,
	2, 1, 3, 0,
	2, 1, 3, 0,
	1, 2, 0, 3,
	0, 3, 1, 2,
	1, 2, 0, 3
};

/* convert block index [0-26] to separate X, Y, Z offset [-1, 1] */
static uint8_t blockIndexToXYZ[] = {
	/* bitfield: 2bits per coord (0 => -1, 1 => 0, 2 => +1), ordered XZY */
	0,1,2,4,5,6,8,9,10,16,17,18,20,21,22,24,25,26,32,33,34,36,37,38,40,41,42
};

/* pairs of U, V dir (S,E,N,W,T,B) */
static uint8_t UVdirs[] = {1, 4, 0, 4, 1, 4, 0, 4, 1, 0, 1, 0};

static uint16_t vtxAdjust[] = {
	#define ADJUST(pt0U,pt0V, pt1U,pt1V, pt2U,pt2V, pt3U,pt3V)      pt0U|(pt0V<<2)|(pt1U<<4)|(pt1V<<6)|(pt2U<<8)|(pt2V<<10)|(pt3U<<12)|(pt3V<<14)
	#define OK    3
	ADJUST(VY,OK, OK,OK, VX,OK, VX,VY),
	ADJUST(VZ,VY, VZ,OK, OK,OK, VY,OK),
	ADJUST(VX,VY, VX,OK, OK,OK, VY,OK),
	ADJUST(VY,OK, OK,OK, VZ,OK, VZ,VY),
	ADJUST(OK,OK, VZ,OK, VX,VZ, VX,OK),
	ADJUST(VZ,OK, OK,OK, VX,OK, VX,VZ)
	#undef OK
};

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

	for (i = 0; i < DIM(ocsOffsets); i ++)
	{
		/* easier to deal with */
		j = ocsOffsets[i];
		/* store offset as XZY bitfield on 2 bits each (ie:mod 4, instead of mod 3) */
		ocsOffsets[i] = (j % 3) + ((j / 9)<<4) + ((j / 3) % 3 << 2);
	}
}

/* connected stairs model */
static DATA8 halfBlockGetConnectedModel(BlockState b, DATA16 blockIds)
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

	if (! blockIds) return model;
	if (! up) bits >>= 4;

	for (i = 0; i < 4; i ++, cnx += 3)
	{
		BlockState n = blockGetById(blockIds[cnx[0]]);

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

DATA8 halfBlockGetModel(BlockState b, int size, DATA16 blockIds)
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
		case BLOCK_STAIRS: return halfBlockGetConnectedModel(b, blockIds);
		default: return b->type == SOLID ? &fullySolid : NULL;
		}
	case 8: /* TODO */
		break;
	}
	return NULL;
}

static DATA16 halfBlockRelocCenter(int center, DATA16 blockIds, DATA16 buffer)
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
		buffer[i] = x2 < 0 || x2 > 2 || z2 < 0 || z2 > 2 ? 0 : blockIds[x2+z2*3+y];
	}

	/* looks dangerous, but first 10 items are not read */
	return buffer - 10;
}

/* compute ambient occlusion for half slab (only one quad here) */
static uint32_t halfBlockGetOCS(DATA16 blockIds, DATA8 ocsval, uint8_t pos[3], int norm, int quadrant, ModelCache models)
{
	uint32_t occlusion;
	uint8_t  i, j;
	DATA8    ocs = ocsOffsets + norm * 12;
	uint8_t  corner = ocs2x2[norm * 4 + quadrant];

	for (i = occlusion = 0; i < 4; i ++)
	{
		uint8_t vtxocs;
		for (j = 0, vtxocs = 0; j < 3; j ++, ocs ++)
		{
			/* hmm, we are doing unconventionnal memory access: this trick will circumvent -Warray-bound :-/ */
			DATA16 buffer = alloca(14);
			uint8_t xzy = *ocs;
			/* these value can range from -1 to <size> */
			int8_t  xc = pos[0] + (xzy & 3) - 1;
			int8_t  zc = pos[2] + ((xzy >> 2) & 3) - 1;
			int8_t  yc = pos[1] + (xzy >> 4) - 1;

			uint8_t off = ((xc + 2) >> 1) + ((yc + 2) >> 1) * 9 + ((zc + 2) >> 1) * 3;

			if ((models->set & (1 << off)) == 0)
			{
				DATA8 model2x2 = halfBlockGetModel(blockGetById(blockIds[off]), 2, halfBlockRelocCenter(off, blockIds, buffer));
				models->set |= 1<<off;
				models->cache[off] = model2x2 ? model2x2[0] : 0;
			}

			if (models->cache[off] & (1<<(((2 + xc) & 1) | (((2 + zc) & 1) << 1) | (((2 + yc) & 1) << 2))))
			{
				vtxocs |= 1<<j;

				if (corner == i)
				{
					/* check if occlusion is 1 or 2 sub-voxel high */
					int8_t * normal = normals + norm * 4;
					uint8_t  flag = corner * 3 + j;
					xc += normal[0];
					yc += normal[1];
					zc += normal[2];
					occlusion |= 1 << flag;
					off = ((xc + 2) >> 1) + ((yc + 2) >> 1) * 9 + ((zc + 2) >> 1) * 3;

					if ((models->set & (1 << off)) == 0)
					{
						DATA8 model2x2 = halfBlockGetModel(blockGetById(blockIds[off]), 2, halfBlockRelocCenter(off, blockIds, buffer));
						models->set |= 1<<off;
						models->cache[off] = model2x2 ? model2x2[0] : 0;
					}

					if (models->cache[off] & (1<<(((2 + xc) & 1) | (((2 + zc) & 1) << 1) | (((2 + yc) & 1) << 2))))
						occlusion |= 1 << (flag+12);
				}
			}
		}
		switch (vtxocs&3) {
		case 3: vtxocs = 2; break;
		case 2:
		case 1: vtxocs = 1; break;
		default: vtxocs = (vtxocs & 4 ? 1 : 0);
		}
		ocsval[i] = vtxocs;
	}
	return occlusion;
}

static int halfBlockSkyOffset(DATA8 vtx, int vertex, int xyz, int adjust)
{
	uint8_t pos[4];
	switch (vertex) {
	case 0: memcpy(pos, vtx+4, 4); break;
	case 1: pos[0] = vtx[8]  + vtx[4] - vtx[0];
	        pos[1] = vtx[9]  + vtx[5] - vtx[1];
	        pos[2] = vtx[10] + vtx[6] - vtx[2];
	        pos[3] = 0; break;
	case 2: memcpy(pos, vtx+8, 4); break;
	case 3: memcpy(pos, vtx, 4);
	}

	pos[0] += 2 + (xyz&3) - 1;
	pos[1] += 2 + (xyz>>4) - 1;
	pos[2] += 2 + ((xyz>>2)&3) - 1;

	pos[adjust&3] --;
	pos[adjust>>2] --;

	return (pos[0]>>1) + (pos[2]>>1) * 3 + (pos[1]>>1) * 9;
}

static Bool isVisible(DATA16 blockIds, ModelCache models, DATA8 pos, int dir)
{
	static int offsets[] = {-2, -1, 2, 1, -4, 4};
	static uint8_t blockIndex[] = {16, 14, 10, 12, 22, 4};
	uint8_t off = blockIndex[dir];

	if ((models->set & (1 << off)) == 0)
	{
		DATA16 buffer = alloca(14);
		DATA8 model2x2 = halfBlockGetModel(blockGetById(blockIds[off]), 2, halfBlockRelocCenter(off, blockIds, buffer));
		models->set |= 1<<off;
		models->cache[off] = model2x2 ? model2x2[0] : 0;
	}

	uint8_t bit = 1 << ((pos[0] + 2 * (pos[2] + pos[1] * 2) + offsets[dir]) & 7);
	return (models->cache[off] & bit) == 0;
}

/*
 * Main function to convert a detail block metadata into a triangle mesh:
 * contrary to chunk meshing, we try harder to make triangles as big as possible.
 */
void halfBlockGenMesh(WriteBuffer write, DATA8 model, int size /* 2 or 8 */, DATA8 xyz, BlockState b, DATA16 blockIds, DATA8 skyBlock, int genSides)
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
			if ((sides & mask) ? face[offset[j]] < 255 : !isVisible(blockIds, &models, pos, j)) { *face |= mask; continue; }

			/* check if we can expand in one of 2 directions */
			memset(rect, 1, 4);
			rect[dir0[j]] = 0;

			/* try to expand initial rect */
			uint8_t  cur[4];
			uint8_t  ocs[16];
			uint32_t occlusion;
			int8_t   faceOff[3];
			uint8_t  dirU  = UVdirs[j*2];
			uint8_t  dirV  = UVdirs[j*2+1];
			uint8_t  axisU = axis[dirU];
			uint8_t  axisV = axis[dirV];
			uint8_t  rev   = invUV[j];
			DATA8    face2;

			static uint8_t dummyVal[] = {255,254,253,252, 251,250,249,248, 247,246,245,244};
			faceOff[0] = faceOff[2] = offset[dirU];
			faceOff[1] = offset[dirV] - faceOff[0];
			memcpy(cur, pos, 4);
			memcpy(ocs+4, dummyVal, sizeof dummyVal);
			occlusion = halfBlockGetOCS(blockIds, ocs, cur, j, 0, &models);

			for (k = 0, face2 = face; k < 3; k ++)
			{
				static int8_t subVoxel[] = {1,-1,1,0,1,0};
				cur[axisU] += subVoxel[k];
				cur[axisV] += subVoxel[k+3];
				face2 += faceOff[k];
				if (cur[axisU] == 2 || cur[axisV] == 2 || (*face2 & mask)) continue;
				if ((sides & mask) ? face2[offset[j]] < 255 : !isVisible(blockIds, &models, cur, j)) continue;
				occlusion |= halfBlockGetOCS(blockIds, ocs + 4 + (k<<2), cur, j, k+1, &models);
			}

			uint16_t ocsval = 0;
			uint8_t  ocsext = 0;
			if (ocs[4] < 16 && ocs[8] < 16 && ocs[12] < 16)
			{
				/* 2x2: use a detailed ocs map */
				static uint8_t merge[] = {
					 8, 1,  6, 15,
					12, 5,  2, 11,
					 0, 9, 14, 7,
				};
				DATA8 p;
				face2 = merge + rev * 4;
				for (ocsval = 256, k = 0, p = face2 + 4; k < 8; k += 2, p += 2, occlusion >>= 3)
				{
					ocsval |= ocs[face2[k>>1]] << k;
					uint8_t allTall = (occlusion & 7) > 0 && ((occlusion >> 12) & 7) == (occlusion & 7);
					if ((occlusion & 2) || allTall) ocsext |= 1 << k;
					if ((occlusion & 1) || allTall) ocsext |= 1 << (k+1);
				}
				rect[axisU] = 2;
				rect[axisV] = 2;
				face2 = face + faceOff[0]; face2[0] |= mask;
				face2 += faceOff[1];       face2[0] |= mask;
				face2 += faceOff[2];       face2[0] |= mask;
			}
			else /* 2x1, 1x2 or 1x1 */
			{
				/* tables used to merge quads according to OCS values */
				static uint8_t ocsIdxU[] = {
					2, 6, 3, 7, /* S,W,B */
					0, 4, 1, 5, /* N,E */
					2, 6, 3, 7, /* T */
				};
				static uint8_t ocsIdxV[] = {
					0, 8, 3, 11, /* S,W,B */
					0, 8, 3, 11, /* N,E */
					1, 9, 2, 10, /* T */
				};
				/* merge quads according to OCS values */
				if (ocs[4] < 16)
				{
					/* 2x1 */
					DATA8 p = ocsIdxU + rev * 4;
					ocs[p[0]] = ocs[p[1]];
					ocs[p[2]] = ocs[p[3]];
					rect[axisU] = 2;
					face[faceOff[0]] |= mask;
					ocsext = 1;
				}
				else if (ocs[8] < 16) /* 1x2 */
				{
					DATA8 p = ocsIdxV + rev * 4;
					ocs[p[0]] = ocs[p[1]];
					ocs[p[2]] = ocs[p[3]];
					rect[axisV] = 2;
					face[offset[dirV]] |= mask;
					ocsext = 2;
				}
				else ocsext = 3;
				ocsval = ocs[0] | (ocs[1] << 2) | (ocs[2] << 4) | (ocs[3] << 6);
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
				uint16_t X1, Y1, Z1, U, V, Usz, Vsz, base;
				#define vtx      (ocs+4)
				#define texSz    3

				face2 = cubeIndices + j * 4;
				DATA8 idx = vertex + face2[3];
				/* first vertex */
				vtx[0] = pos[0] + idx[0] * rect[0];
				vtx[1] = pos[1] + idx[1] * rect[1];
				vtx[2] = pos[2] + idx[2] * rect[2];
				X1 = VERTEX(vtx[0] + xyz[0]);
				Y1 = VERTEX(vtx[1] + xyz[1]);
				Z1 = VERTEX(vtx[2] + xyz[2]);
				out[0] = X1 | (Y1 << 16);

				/* second vertex */
				idx = vertex + face2[0];
				vtx[4] = pos[0] + (idx[0] * rect[0]);
				vtx[5] = pos[1] + (idx[1] * rect[1]);
				vtx[6] = pos[2] + (idx[2] * rect[2]);

				/* UV coord */
				base = vtx[4+coordU[j]] << texSz; U = (UV[0] << 4) + (rev == 1 ? 16 - base : base);
				base = vtx[4+coordV[j]] << texSz; V = (UV[1] << 4) + (rev != 2 ? 16 - base : base);

				/* third vertex */
				idx = vertex + face2[2];
				vtx[8]  = pos[0] + (idx[0] * rect[0]);
				vtx[9]  = pos[1] + (idx[1] * rect[1]);
				vtx[10] = pos[2] + (idx[2] * rect[2]);

				/* tex size and normal */
				base = vtx[8+coordU[j]] << texSz; Usz = (UV[0] << 4) + (rev == 1 ? 16 - base : base);
				base = vtx[8+coordV[j]] << texSz; Vsz = (UV[1] << 4) + (rev != 2 ? 16 - base : base);
				switch (rotate & 3) {
				case 1: swap(V, Vsz); break;
				case 3: base = U; U = Vsz; Vsz = Usz; Usz = V; V = base; break;
				case 2: swap(U, Usz); swap(V, Vsz); break;
				}
				out[1] = Z1 | (RELDX(vtx[4] + xyz[0]) << 16) | ((V & 512) << 21);
				out[2] = RELDY(vtx[5] + xyz[1]) | (RELDZ(vtx[6] + xyz[2]) << 14) | ((ocsext & 0xf0) << 24);

				out[3] = RELDX(vtx[8]  + xyz[0]) | (RELDY(vtx[9] + xyz[1]) << 14) | (ocsext << 28);
				out[4] = RELDZ(vtx[10] + xyz[2]) | (U << 14) | (V << 23);

				out[5] = ((Usz + 128 - U) << 16) | ((Vsz + 128 - V) << 24) | (j << 9) | ocsval;
				out[6] = 0;

				base = (rotate & 3) * 8;
				if (texCoord[base] == texCoord[base + 6]) out[5] |= FLAG_TEX_KEEPX;

				/* skylight, blocklight */
				for (k = 0, face2 = skyBlockOffset + j * 16; k < 4; k ++)
				{
					uint8_t max, l, skyval;
					uint8_t adjust = (vtxAdjust[j] >> k*4) & 15;
					for (l = skyval = max = 0; l < 4; l ++, face2 ++)
					{
						//uint8_t  skyvtx = skyBlock[face2[0]];
						uint8_t  skyvtx = skyBlock[halfBlockSkyOffset(vtx, k, blockIndexToXYZ[face2[0]], adjust)];
						uint16_t light  = skyvtx & 15;
						skyvtx &= 0xf0;
						/* max for block light */
						if (max < light) max = light;
						/* minimum if != 0 */
						if (skyvtx > 0 && (skyval > skyvtx || skyval == 0)) skyval = skyvtx;
					}
					out[6] |= (skyval | max) << (k << 3);
				}
			}
			out += VERTEX_INT_SIZE;
		}
	}
	write->cur = out;
}

/*
 * generate accurate bounding box from half-blocks
 */
void halfBlockGetBBox(DATA16 blockIds, VTXBBox array, int max)
{
	uint8_t visited[8];
	uint8_t pos[4];
	int     total, size, i, j, k;

	/* XXX for now only take care of stairs/slabs */
	memset(visited, 0, sizeof visited);
	size  = 2;
	total = size * size * size;
	array->cont = 0;

	DATA8 model = halfBlockGetModel(blockGetById(blockIds[13]), 2, blockIds);
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

