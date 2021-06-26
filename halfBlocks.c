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

/* auto-generated from modelsSize2[] */
static uint8_t modelsSize0[DIM(modelsSize2)];

static uint8_t dirs[] = {1, 4, 0, 4, 1, 4, 0, 4, 1, 0, 1, 0}; /* dir in <j> index */
static uint8_t axis[] = {2, 0, 0, 0, 1};
static uint8_t dir0[] = {2, 0, 2, 0, 1, 1}; /* index in pos/rect */
extern uint8_t skyBlockOffset[];

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

static Bool isVisible(DATA16 blockIds, DATA8 pos, int dir, int size)
{
	static uint8_t off[] = {16, 14, 10, 12, 22, 4};
	int id = blockIds[off[dir]];

	BlockState b = blockGetByIdData(id >> 4, id & 15);

	if (b->type == SOLID)
	{
		static int offsets[] = {0, 0, -2, -1, 2, 1, -4, 4, -56, -8, 56, 8, -448, 448};
		static int mask[]    = {1, 2, 4, 8, 16, 32, 64, 128};
		uint8_t model = 0;
		switch (b->special) {
		case BLOCK_HALF:   model = modelsSize2[(b->id&15) > 7]; goto case_common;
		case BLOCK_STAIRS: model = modelsSize2[(b->id&7)  + 2];
		case_common:
			id = pos[0] + size * (pos[2] + pos[3] * size) + offsets[dir+size];
			return (&model)[id>>3] & mask[id&7];
		default:
			return False;
		}
	}
	return True;
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
	switch (size) {
	case 0:
	case 1:
		switch (b->special) {
		case BLOCK_HALF:   return &modelsSize0[(b->id&15) > 7];
		case BLOCK_STAIRS: return &modelsSize0[(b->id&7)  + 2];
		default: return NULL;
		}
		break;
	case 2:
		switch (b->special) {
		case BLOCK_HALF:   return &modelsSize2[(b->id&15) > 7];
		case BLOCK_STAIRS: return halfBlockGetConnectedModel(b, blockIds);
		default: return NULL;
		}
	case 8: /* TODO */
		break;
	}
	return NULL;
}

/*
 * Main function to convert a detail block metadata into a triangle mesh:
 * contrary to chunk meshing, we try harder to make triangles as big as possible.
 */
void halfBlockGenMesh(WriteBuffer write, DATA8 model, int size /* 2 or 8 */, DATA8 xyz, DATA8 tex, DATA16 blockIds, DATA8 skyBlock)
{
	int16_t offset[6];
	uint8_t pos[4];
	DATA32  out;
	DATA8   xsides, ysides, zsides;
	DATA8   faces, face;
	int     total, i, j, k, texSz;

	total = size * size * size;
	faces = alloca(total + size * 3); i = size - 1;
	texSz = size == 2 ? 3 : 1;
	memset(xsides = faces + total, 10, size); xsides[0] =  2; xsides[i] =  8;
	memset(ysides = xsides + size, 48, size); ysides[0] = 16; ysides[i] = 32;
	memset(zsides = ysides + size,  5, size); zsides[0] =  1; zsides[i] =  4;
	offset[0] = size;
	offset[1] = 1;
	offset[2] = -size;
	offset[3] = -1;
	offset[4] = size*size;
	offset[5] = - offset[4];

	/* expand binary field (ordered XZY, like chunks) */
	for (i = 0, j = 1, k = *model++, face = faces; i < total; i ++, j <<= 1, face ++)
	{
		if (j > 128)
		{
			j = 1;
			k = *model++;
		}
		/* empty == 255, sub-voxel == 0 */
		*face = (k & j) ? 0 : 255;
	}

	/* do the meshing */
	#define x    pos[0]
	#define y    pos[1]
	#define z    pos[2]
	for (face = faces, out = write->cur, i = total, memset(pos, 0, sizeof pos); i > 0; i --, face ++)
	{
		uint8_t flags = *face, sides;
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
		for (j = 0; j < 6; j ++)
		{
			int8_t rect[3], mask = 1 << j;
			/* already processed? */
			if (flags & mask) continue;

			/* is face visible (empty space in neighbor block) */
			if ((sides & mask) ? face[offset[j]] < 255 : !isVisible(blockIds, pos, j, size)) { *face |= mask; continue; }

			/* check if we can expand in one of 2 directions */
			rect[0] = rect[1] = rect[2] = 1;
			rect[dir0[j]] = 0;

			/* expand in first direction */
			uint8_t cur[4];
			memcpy(cur, pos, sizeof cur);
			int dirU = dirs[j*2], coord = pos[k = axis[dirU]] + 1;
			DATA8 face2 = face;
			for (;;)
			{
				/* we are not at the edge of direction <dirU> */
				if (coord == size) break;
				/* advance one sub-block */
				face2 += offset[dirU];
				/* not an empty cube */
				if (*face2 & mask) break;
				/* but must be empty in the normal direction */
				cur[k] ++;
				if ((sides & mask) ? face2[offset[j]] < 255 : !isVisible(blockIds, cur, j, size)) break;
				*face2 |= mask;
				rect[k] ++;
				coord ++;
			}

			/* expand in second direction */
			int dirV = dirs[j*2+1], k2 = axis[dirV];
			coord = pos[k2] + 1;
			face2 = face;
			memcpy(cur, pos, sizeof cur);
			for (;;)
			{
				DATA8 face3;
				int length;
				if (coord == size) break;
				/* advance one sub-block */
				face2 += offset[dirV];
				cur[k2] ++;
				for (length = rect[k], face3 = face2; length > 0; length --, face3 += offset[dirU])
				{
					/* not an empty cube */
					if (*face3 & mask) break;
					/* but must be empty in the normal direction */
					if ((sides & mask) ? face3[offset[j]] < 255 : !isVisible(blockIds, cur, j, size)) break;
					cur[k] ++;
				}
				if (length == 0)
				{
					/* mark the line as visited */
					for (length = rect[k], face3 = face2; length > 0; length --, *face3 |= mask, face3 += offset[dirU]);
					cur[k] -= rect[k];
					rect[k2] ++;
					coord ++;
				}
				else break;
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
			DATA8 UV = tex + (j << 1);
			if (j == 1) rect[0] ++;
			if (j == 4) rect[1] ++;
			if (j == 0) rect[2] ++;

			{
				static uint8_t coordU[] = {0, 2, 0, 2, 0, 0};
				static uint8_t coordV[] = {1, 1, 1, 1, 2, 2};
				static uint8_t invUV[]  = {0, 1, 1, 0, 2, 0};
				uint16_t X1, Y1, Z1, U, V, Usz, Vsz;
				uint8_t  vtx[4];
				#define  base     vtx[4]

				face2 = cubeIndices + j * 4;
				DATA8 idx = vertex + face2[3];
				/* first vertex */
				X1 = VERTEX(pos[0] + (idx[0] * rect[0]) + xyz[0]);
				Y1 = VERTEX(pos[1] + (idx[1] * rect[1]) + xyz[1]);
				Z1 = VERTEX(pos[2] + (idx[2] * rect[2]) + xyz[2]);
				out[0] = X1 | (Y1 << 16);

				/* second vertex */
				idx = vertex + face2[0];
				vtx[0] = pos[0] + (idx[0] * rect[0]);
				vtx[1] = pos[1] + (idx[1] * rect[1]);
				vtx[2] = pos[2] + (idx[2] * rect[2]);
				out[1] = Z1 | (RELDX(vtx[0] + xyz[0]) << 16);
				out[2] = RELDY(vtx[1] + xyz[1]) | (RELDZ(vtx[2] + xyz[2]) << 14);

				/* UV coord */
				base = vtx[coordU[j]] << texSz; U = (UV[0] << 4) + (invUV[j] == 1 ? 16 - base : base);
				base = vtx[coordV[j]] << texSz; V = (UV[1] << 4) + (invUV[j] != 2 ? 16 - base : base);

				/* third vertex */
				idx = vertex + face2[2];
				vtx[0] = pos[0] + (idx[0] * rect[0]);
				vtx[1] = pos[1] + (idx[1] * rect[1]);
				vtx[2] = pos[2] + (idx[2] * rect[2]);

				out[3] = RELDX(vtx[0] + xyz[0]) | (RELDY(vtx[1] + xyz[1]) << 14) | ((V & 512) << 19);
				out[4] = RELDZ(vtx[2] + xyz[2]) | (U << 14) | (V << 23);

				/* tex size and normal */
				base = vtx[coordU[j]] << texSz; Usz = (UV[0] << 4) + (invUV[j] == 1 ? 16 - base : base);
				base = vtx[coordV[j]] << texSz; Vsz = (UV[1] << 4) + (invUV[j] != 2 ? 16 - base : base);
				out[5] = ((Usz + 128 - U) << 16) | ((Vsz + 128 - V) << 24) | (j << 8);
				out[6] = 0;

				/* skylight, blocklight, ocs */
				for (k = 0; k < 4; k ++)
				{
					uint8_t off = j * 16 + k * 4;
					uint8_t max, l, skyval;
					for (l = skyval = max = 0; l < 4; l ++, off ++)
					{
						uint8_t  skyvtx = skyBlock[skyBlockOffset[off]];
						uint16_t light  = skyvtx & 15;
						skyvtx &= 0xf0;
						/* max for block light */
						if (max < light) max = light;
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
 * generate accurate bounding box from half-slab/stairs/chiseled blocks
 */
void halfBlockGetBBox(DATA16 blockIds, VTXBBox array, int max)
{
	uint8_t visited[8];
	uint8_t pos[4];
	int     total, size, i, j, k;

	/* XXX for now only take care of stairs */
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
		for (j = size * size, p = face + j, y ++; y < size; p += j, z ++)
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

