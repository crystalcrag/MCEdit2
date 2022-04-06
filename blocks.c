/*
 * blocks.c : generic definition for blocks; specialized behavior is done in respective file.
 *
 * Written by T.Pierron, apr 2020
 */

#include <glad.h>
#include <string.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <malloc.h>
#include <math.h>
#include "zlib.h"
#include "blocks.h"
#include "items.h"
#include "NBT2.h"
#include "nanovg.h"   /* need stbi_load() for items.png */
#include "SIT.h"

struct Block_t        blockIds[256];
struct BlockState_t * blockStates;
struct BlockState_t * blockLast;
struct BlockPrivate_t blocks;
static BlockVertex    blockVertex;
static BlockVertex    stringPool;

#define STRICT_PARSING     /* check for misspelled property */

uint16_t blockStateIndex[256*16];

/* texture color need to be adjusted per biome (pair of UV tex coord from terrain.png) */
static uint8_t biomeDepend[] = {
	 0, 0, 3, 0, 7, 2, 8, 2, 4, 3, 5, 3,
	 8, 3, 4, 8, 5, 8,16,11,17,11,17,12,
	16,12, 5,12, 4,12,15, 8
};

uint8_t bboxIndices[] = {
	/* triangles for filling: ordered S, E, N, W, T, B (index in cubeVertex[]) */
	3, 0, 1,    2, 3, 1,
	2, 1, 5,    6, 2, 5,
	6, 5, 4,    7, 6, 4,
	7, 4, 0,    3, 7, 0,
	7, 3, 2,    6, 7, 2,
	0, 4, 5,    1, 0, 5,

	/* lines for edges */
	0, 1,   1, 5,   5, 4,   4, 0, /* top */
	3, 2,   2, 6,   6, 7,   7, 3, /* bottom */
	0, 3,   1, 2,   5, 6,   4, 7, /* sides */
};

uint8_t texCoordRevU[] = {
	1,0,    1,1,    0,1,    0,0,
	1,1,    0,1,    0,0,    1,0,
	0,1,    0,0,    1,0,    1,1,
	0,0,    1,0,    1,1,    0,1,
};

/* pre-defined bounding box models for some common blocks */
static float bboxModels[] = {
	/* faces,           SX,SY,SZ,  X, Y, Z */
	  63,               16,16,16,  0, 0, 0, /* full block (SOLID, TRANS) */
	  63,               16,8, 16,  0, 0, 0, /* bottom slab */
	  63,               16,8, 16,  0, 8, 0, /* top slab */
	  63,               12,8, 12,  2, 0, 2, /* QUAD_CROSS */
	  63,               15,16, 1, .5, 0, 0, /* QUAD_NORTH */
	  63,               15,16, 1, .5, 0,15, /* QUAD_SOUTH */
	  63,                1,16,15, 15, 0,.5, /* QUAD_EAST */
	  63,                1,16,15,  0, 0,.5, /* QUAD_WEST */
	  63,               15,1, 15, .5, 0,.5, /* QUAD_BOTTOM */
	  63,               14,9, 14,  1, 1, 1, /* QUAD_ASCE */
	 319+BHDR_FUSE,      2,16, 2,  7, 0, 7, /* glass pane / iron bars */
	 315+BHDR_INCFACEID, 2,16, 7,  7, 0, 9,
	 311+BHDR_INCFACEID, 7,16, 2,  9, 0, 7,
	 318+BHDR_INCFACEID, 2,16, 7,  7, 0, 0,
	  61+BHDR_INCFACEID, 7,16, 2,  0, 0, 7,
	 319+BHDR_FUSE,      7.0,1,7.0,  4.5,0, 4.5, /* rswire */
	 315+BHDR_INCFACEID, 7.0,1,4.5,  4.5,0,11.5,
	 311+BHDR_INCFACEID, 4.5,1,7.0, 11.5,0, 4.5,
	 318+BHDR_INCFACEID, 7.0,1,4.5,  4.5,0, 0,
	  61+BHDR_INCFACEID, 4.5,1,7.0,  0,  0, 4.5,
	 319+BHDR_FUSE,      4,24, 4,  6, 0, 6,     /* fence: simplified and higher */
	 315+BHDR_INCFACEID, 4,24, 6,  6, 0,10,
	 311+BHDR_INCFACEID, 6,24, 4, 10, 0, 6,
	 318+BHDR_INCFACEID, 4,24, 6,  6, 0, 0,
	  61+BHDR_INCFACEID, 6,24, 4,  0, 0, 6,
	 319+BHDR_FUSE,      8,16, 8,  4, 0, 4,     /* wall: simplified */
	 315+BHDR_INCFACEID, 8,16, 4,  4, 0,12,
	 311+BHDR_INCFACEID, 4,16, 8, 12, 0, 4,
	 318+BHDR_INCFACEID, 8,16, 4,  4, 0, 0,
	  61+BHDR_INCFACEID, 4,16, 8,  0, 0, 4,
};

/* how many arguments each BHDR_* tag takes */
static uint8_t modelTagArgs[] = {0, 1, 0, 0, 0, 3, 3, 3, 3, 3, 1, 255, 0, 0, 1, 2};

uint8_t blockTexResol;

/* convert some common block data into SIDE_* enum */
struct BlockSides_t blockSides = {
	.repeater = {SIDE_SOUTH,  SIDE_WEST, SIDE_NORTH, SIDE_EAST}, /* where the input is (output = input ^ 2) */
	.torch    = {SIDE_TOP,    SIDE_WEST, SIDE_EAST,  SIDE_NORTH, SIDE_SOUTH, SIDE_BOTTOM, SIDE_NONE,   SIDE_NONE},
	.lever    = {SIDE_TOP,    SIDE_WEST, SIDE_EAST,  SIDE_NORTH, SIDE_SOUTH, SIDE_BOTTOM, SIDE_BOTTOM, SIDE_TOP},
	.sign     = {SIDE_NONE,   SIDE_NONE, SIDE_SOUTH, SIDE_NORTH, SIDE_EAST,  SIDE_WEST,   SIDE_NONE,   SIDE_NONE},
	.piston   = {SIDE_BOTTOM, SIDE_TOP,  SIDE_NORTH, SIDE_SOUTH, SIDE_WEST,  SIDE_EAST,   SIDE_NONE,   SIDE_NONE},
	.SWNE     = {SIDE_SOUTH,  SIDE_WEST, SIDE_NORTH, SIDE_EAST},
};

/* keep static strings into some chained memory blocks */
STRPTR stringAddPool(STRPTR string, int extra)
{
	#define POOLMAX           4096
	if (string == NULL) return NULL;

	BlockVertex pool = stringPool;
	int         len  = strlen(string) + 1 + extra;

	if (pool == NULL || pool->usage + len > STR_POOL_SIZE)
	{
		/* technically not required to keep track of mem blocks, but it will make memory tracker happy */
		pool = malloc(POOLMAX);
		if (! pool) return NULL;
		pool->next = stringPool;
		pool->usage = 0;
		stringPool = pool;
	}

	string = strcpy(pool->buffer + pool->usage, string);
	pool->usage += len;

	return string;
	#undef POOLMAX
}

/* get name of block as it is stored in NBT */
static STRPTR blockGetTechName(STRPTR tmpl, STRPTR tech)
{
	if (! IsDef(tech))
	{
		STRPTR p;
		/* use block name to build technical name */
		tmpl = stringAddPool(tmpl, 0);
		for (p = tmpl; *p; p ++)
		{
			/* very simple conversion */
			if ('A' <= *p && *p <= 'Z') *p += 32; else
			if (' ' == *p) *p = '_';
		}
	}
	else tmpl = stringAddPool(tech, 0);

	return tmpl;
}

/* expand block state name to something human readable */
static STRPTR blockExpandName(int id, STRPTR base, STRPTR tmpl)
{
	#define BLOCKID     ((DATA8)buffer)[127]
	#define RESTART     buffer[126]
	static char buffer[128];
	STRPTR name;

	if (BLOCKID != id)
	{
		BLOCKID = id;
		strcpy(buffer, base);
		RESTART = strlen(buffer);
	}

	if (tmpl == NULL || (tmpl[0] == '-' && tmpl[1] == 0))
		return base; /* same as block name */

	if (tmpl[0] == '-' || tmpl[0] == '+')
	{
		STRPTR p;
		/* block state name: add after block name in parenthesis */
		name = buffer + RESTART;

		if (tmpl[1] == '(')
		{
			if (name[-1] != ' ')
				*name++ = ' ';
		}
		else
		{
			for (p = name; p > buffer && *p != '('; p --);
			if (p == buffer)
			{
				*name ++ = ' ';
				*name ++ = '(';
			}
		}
		strcpy(name, tmpl + 1);
		p = name - 1;
		name = strchr(name, 0);
		/* if there is an open parenthesis: close it */
		while (p >= buffer && *p != '(') p --;
		if (p > buffer) strcpy(name, ")"), name ++;
	}
	else strcpy(buffer, tmpl), RESTART = strlen(buffer);

	/* check for next checkpoint */
	name = strchr(buffer, '_');
	if (name)
	{
		if (name[-1] == '(')
			strcpy(name, name + 1);
		else
			*name++ = ' ';
		RESTART = name - buffer;
	}
	#undef BLOCKID
	#undef RESTART

	return stringAddPool(buffer, 0);
}

/* add block state: keep states contiguous for a given block ID */
static void blockAddState(struct BlockState_t * model, int id)
{
	#define POOLSTATES      128
	#define POOLMASK        (POOLSTATES-1)
	if ((blocks.totalStates & POOLMASK) == 0)
	{
		/* keep the entire table contiguous */
		BlockState reloc = realloc(blockStates, (blocks.totalStates + POOLSTATES) * sizeof *blockStates);
		if (reloc == NULL) return;
		blockStates = reloc;
	}

	BlockState state = blockStates + blocks.totalStates;
	memcpy(state, model, sizeof *state);
	state->id |= id << 4;
	blockStateIndex[state->id] = blocks.totalStates;
	blocks.totalStates ++;
	#undef POOLSTATES
	#undef POOLMASK
}

/* alloc vertex in chunk of roughly 4Kb, not necessarily contiguous */
static DATA16 blockAllocVertex(int count)
{
	BlockVertex list;
	int bytes = count * BYTES_PER_VERTEX + 2;

	for (list = blockVertex; list && list->usage + bytes > list->max; list = list->next);

	if (list == NULL)
	{
		#define POOLMAX           16384
		int max = (bytes + POOLMAX-1) & ~(POOLMAX-1);
		list = malloc(offsetof(struct BlockVertex_t, buffer) + max);
		list->next  = blockVertex;
		list->usage = 0;
		list->max   = max;
		blockVertex = list;
		blocks.totalVtx += max;
		#undef POOLMAX
	}
	DATA16 mem = (DATA16) (list->buffer + list->usage);
	mem[0] = count;
	list->usage += bytes;

	/* mem will remain on CPU, will be transfered when chunks are transformed into mesh */
	return mem + 1;
}


/* recompute normals based on vertex, because rotation will change the side of faces */
static void blockSetUVAndNormals(DATA16 vert, int inv, int setUV, float * vertex, float * texCube)
{
	static uint8_t Ucoord[] = {0, 2, 0, 2, 0, 0};
	static uint8_t Vcoord[] = {1, 1, 1, 1, 2, 2};
	static uint8_t invers[] = {2, 3, 0, 1, 5, 4};
	static uint8_t revers[] = {0, 1, 1, 0, 2, 0};
	static uint8_t norm2face[] = {1, 3, 4, 5, 0, 2};
	vec4 v1 = {vertex[3] - vertex[0], vertex[4] - vertex[1], vertex[5] - vertex[2], 1};
	vec4 v2 = {vertex[6] - vertex[0], vertex[7] - vertex[1], vertex[8] - vertex[2], 1};
	vec4 norm;
	int  dir, i, U, V;

	vecCrossProduct(norm, v1, v2);

	dir = 0; v1[0] = norm[0];
	if (fabsf(v1[0]) < fabsf(norm[VY])) dir = 2, v1[0] = norm[VY];
	if (fabsf(v1[0]) < fabsf(norm[VZ])) dir = 4, v1[0] = norm[VZ];
	if (v1[0] < 0) dir ++;

	dir = norm2face[dir];
	texCube += dir * 4;
	/* reverse normals */
	if (inv) dir = invers[dir];

	/* apply a cube map texture on face */
	if (setUV)
	{
		uint16_t tex[8];
		U = Ucoord[dir];
		V = Vcoord[dir];

		for (i = 0; i < 8; i += 2, texCube ++)
		{
			div_t res = div(texCube[0], 513);
			tex[i]   = res.rem;
			tex[i+1] = res.quot;
		}

		for (i = 0; i < 4; i ++, vert += INT_PER_VERTEX, vertex += 3)
		{
			float val = vertex[V];
			if (revers[dir] & 2) val = 1 - val;

			float pt1[] = {tex[2] + (tex[0]-tex[2]) * val, tex[3] + (tex[1] - tex[3]) * val};
			float pt2[] = {tex[4] + (tex[6]-tex[4]) * val, tex[5] + (tex[7] - tex[5]) * val};

			val = vertex[U];
			if (revers[dir] & 1) val = 1 - val;
			int Utex = roundf(pt1[0] + (pt2[0] - pt1[0]) * val);
			int Vtex = roundf(pt1[1] + (pt2[1] - pt1[1]) * val);
			SET_UVCOORD(vert, Utex, Vtex);
			vert[4] |= dir << 3;
		}
	}
	/* only set normals */
	else for (i = 0, vert += 4; i < 4; i ++, vert[0] |= dir<<3, vert += INT_PER_VERTEX);
}

/* needed by entity models */
void blockCenterModel(DATA16 vertex, int count, int dU, int dV, int faceId, Bool center, DATA16 sizes)
{
	uint16_t buffer[6];
	DATA16   start = vertex;
	DATA16   min, max;
	int      i, U, V;
	memset(min = buffer,   0xff, 3 * sizeof *min);
	memset(max = buffer+3, 0x00, 3 * sizeof *max);
	for (i = 0, faceId <<= 8; i < count; i ++, vertex += INT_PER_VERTEX)
	{
		uint16_t x = vertex[0], y = vertex[1], z = vertex[2];
		if (min[0] > x) min[0] = x;   if (max[0] < x) max[0] = x;
		if (min[1] > y) min[1] = y;   if (max[1] < y) max[1] = y;
		if (min[2] > z) min[2] = z;   if (max[2] < z) max[2] = z;

		/* shift texture U, V */
		if ((vertex[4] & 0x7f00) == faceId)
		{
			U = GET_UCOORD(vertex) + dU;
			V = GET_VCOORD(vertex) + dV;
			if (U == 512)  U = 511;
			if (V == 1024) V = 1023;
			CHG_UVCOORD(vertex, U, V);
		}
	}
	uint16_t shift[3];

	if (center < 2)
	{
		shift[0] = ((max[0] - min[0]) >> 1) + (min[0] - ORIGINVTX);
		shift[1] = ((max[1] - min[1]) >> 1) + (min[1] - ORIGINVTX);
		shift[2] = ((max[2] - min[2]) >> 1) + (min[2] - ORIGINVTX);
		if (center == 0) shift[VY] = 0;
	}
	else /* full block: always center in unit voxel, no matter what original block dimension is */
		shift[0] = shift[1] = shift[2] = BASEVTX/2;

	/* center vertex around 0, 0 */
	for (i = 0, vertex = start; i < count; i ++, vertex += INT_PER_VERTEX)
	{
		vertex[0] -= shift[0];
		vertex[1] -= shift[1];
		vertex[2] -= shift[2];
	}
	sizes[VX] = max[VX] - min[VX];
	sizes[VY] = max[VY] - min[VY];
	sizes[VZ] = max[VZ] - min[VZ];
}

int blockCountModelVertex(float * vert, int count)
{
	int i, arg, vertex, faces, mode;
	for (i = vertex = faces = 0, mode = BHDR_CUBEMAP; i < count; i += arg+1)
	{
		arg = vert[i];
		if (arg > BHDR_INCFACE) return 0;
		switch (arg) {
		case BHDR_FACES: faces = vert[i+1]; vertex += popcount(faces & 63) * 6; break;
		case BHDR_TEX:   arg = mode == BHDR_DETAIL ? popcount(faces & 63) * 4 :
		                       mode == BHDR_CUBEMAP ? 4 * 6 : 0; continue;
		case BHDR_CUBEMAP:
		case BHDR_DETAIL:
		case BHDR_INHERIT: mode = arg; break;
		}
		arg = modelTagArgs[arg];
	}
	return vertex;
}

/*
 * main function to generate vertex data from TileFinder numbers
 */
DATA16 blockParseModel(float * values, int count, DATA16 buffer, int forceRot90)
{
	float * vert;
	float * eof;
	float * tex;
	int     i, j, rotCas;
	int     faces, faceId;
	uint8_t rot90step;
	mat4    rotCascade;
	DATA16  start, p;

	faceId = 0;
	rotCas = 0;
	rot90step = forceRot90 < 0 ? 0 : forceRot90;
	tex = NULL;
	matIdent(rotCascade);

	/* count the vertex needed for this model */
	i = blockCountModelVertex(values, count);
	if (i == 0)
		return NULL;

	DATA16 out = buffer ? buffer : blockAllocVertex(i);

	/* scan each primitives */
	for (p = out, vert = values, eof = vert + count; vert < eof; )
	{
		float * coord;
		float   size[3];
		float   trans[6];
		int     idx;
		uint8_t nbRot, inv, detail, resetRC, center;
		mat4    rotation, rot90, tmp;

		if (vert[0] != BHDR_FACES) break;
		faces   = vert[1];
		vert   += 2;
		inv     = 0;
		center  = 1;
		nbRot   = 0;
		resetRC = 0;
		detail  = BHDR_CUBEMAP;
		matIdent(rotation);
		matIdent(rot90);
		trans[VX] = trans[VY] = trans[VZ] = -0.5f;
		memset(size,  0, sizeof size);
		/* get all the information about one primitive */
		while (vert < eof && vert[0] != BHDR_FACES)
		{
			switch ((int) vert[0]) {
			case BHDR_CUBEMAP: detail = BHDR_CUBEMAP; break;
			case BHDR_DETAIL:  detail = BHDR_DETAIL; break;
			case BHDR_INHERIT: detail = BHDR_INHERIT; break;
			case BHDR_INCFACE: faceId += 1<<8; resetRC = 1; break;
			case BHDR_INVERT:  inv = True; break;
			case BHDR_ROT90:   if (forceRot90 < 0) rot90step = vert[1] / 90; break;
			case BHDR_TR:
				trans[VX] = vert[1] / 16 - 0.5f;
				trans[VY] = vert[2] / 16 - 0.5f;
				trans[VZ] = vert[3] / 16 - 0.5f;
				break;
			case BHDR_REF:
				trans[VX+3] = vert[1] / 16;
				trans[VY+3] = vert[2] / 16;
				trans[VZ+3] = vert[3] / 16;
				center = False;
				break;
			case BHDR_ROTCAS:
				/* rotation cascading to other primitives */
				for (i = 1; i <= 3; i ++)
				{
					float v = vert[i];
					if (v != 0)
					{
						matRotate(tmp, v * DEG_TO_RAD, i-1);
						matMult(rotCascade, rotCascade, tmp);
						rotCas ++;
					}
				}
				break;
			case BHDR_SIZE:
				size[VX] = vert[1] / 16;
				size[VY] = vert[2] / 16;
				size[VZ] = vert[3] / 16;
				break;
			case BHDR_ROT:
				for (i = 1; i <= 3; i ++)
				{
					float v = vert[i];
					if (v != 0)
					{
						matRotate(tmp, v * DEG_TO_RAD, i-1);
						matMult(rotation, rotation, tmp);
						nbRot ++;
					}
				}
				break;
			case BHDR_TEX:
				if (detail != BHDR_INHERIT)
					tex = vert + 1, vert += (detail == BHDR_CUBEMAP ? 6 : popcount(faces)) * 4 + 1;
				continue;
			}
			vert += modelTagArgs[(int) vert[0]] + 1;
		}

		switch (rot90step) {
		case 1: matRotate(rot90, M_PI_2, VY); break;
		case 2: matRotate(rot90, M_PI, VY); break;
		case 3: matRotate(rot90, M_PI+M_PI_2, VY);
		}

		for (i = idx = 0, start = p; faces; i ++, faces >>= 1)
		{
			if ((faces & 1) == 0) { idx += 4; continue; }

			for (j = 0, coord = tmp; j < 4; j ++, idx ++, p += INT_PER_VERTEX, coord += 3)
			{
				DATA8 v = cubeVertex + cubeIndices[idx];
				int val;
				coord[VX] = v[VX] * size[VX];
				coord[VY] = v[VY] * size[VY];
				coord[VZ] = v[VZ] * size[VZ];
				if (nbRot > 0)
				{
					/* rotation centered on block */
					float tr[3];
					if (center)
					{
						tr[VX] = size[VX] * 0.5f;
						tr[VY] = size[VY] * 0.5f;
						tr[VZ] = size[VZ] * 0.5f;
					}
					else
					{
						tr[VX] = trans[VX+3] - 0.5f - trans[VX];
						tr[VY] = trans[VY+3] - 0.5f - trans[VY];
						tr[VZ] = trans[VZ+3] - 0.5f - trans[VZ];
					}
					vecSub(coord, coord, tr);
					matMultByVec3(coord, rotation, coord);
					vecAdd(coord, coord, tr);
				}
				coord[VX] += trans[VX];
				coord[VY] += trans[VY];
				coord[VZ] += trans[VZ];
				/* rotate entire model */
				if (rotCas > 0)
					matMultByVec3(coord, rotCascade, coord);
				/* only this block */
				if (rot90step > 0)
					matMultByVec3(coord, rot90, coord);

				/*
				 * X, Y, Z can vary between -7.5 and 23.5; each mapped to [0 - 65535];
				 * coord[] is centered around 0,0,0 (a cube of unit 1 has vertices of +/- 0.5)
				 */
				val = roundf((coord[VX] + 0.5f) * BASEVTX) + ORIGINVTX; p[VX] = MIN(val, 65535);
				val = roundf((coord[VY] + 0.5f) * BASEVTX) + ORIGINVTX; p[VY] = MIN(val, 65535);
				val = roundf((coord[VZ] + 0.5f) * BASEVTX) + ORIGINVTX; p[VZ] = MIN(val, 65535);
				/* needed for blockSetUVAndNormals() */
				coord[VX] += 0.5f;
				coord[VY] += 0.5f;
				coord[VZ] += 0.5f;
				if (detail == BHDR_DETAIL)
				{
					div_t res = div(tex[0], 513);
					tex ++;
					if (res.rem == 512) res.rem = 511;
					SET_UVCOORD(p, res.rem, res.quot);
				}
			}
			/* recompute normal vector because of rotation */
			blockSetUVAndNormals(p - 20, inv, detail != BHDR_DETAIL, tmp, tex);
			/* will allow the mesh generation to discard some faces (blocks with auto-connected parts) */
			p[-1] |= faceId; p[-11] |= faceId;
			p[-6] |= faceId; p[-16] |= faceId;
			/* opengl needs triangles not quads though */
			if (inv)
			{
				/* invert normals */
				uint8_t tmpbuf[BYTES_PER_VERTEX*2];
				/* triangles for faces (vertex index): from 0, 1, 2, 3, <p> to 3, 2, 1, 0, 2, 0 */
				memcpy(tmpbuf, p - 20, 2*BYTES_PER_VERTEX); /* save 0-1 */
				memcpy(p - 20, p - 5,  BYTES_PER_VERTEX);   /* 3 -> 0 */
				memcpy(p - 15, p - 10, BYTES_PER_VERTEX);   /* 2 -> 1 */
				memcpy(p - 5,  tmpbuf, BYTES_PER_VERTEX);
				memcpy(p - 10, tmpbuf+10, BYTES_PER_VERTEX);
			}
			memcpy(p,   p - 20, BYTES_PER_VERTEX);
			memcpy(p+5, p - 10, BYTES_PER_VERTEX);
			p += INT_PER_VERTEX*2;
		}
		if (resetRC) matIdent(rotCascade), rotCas = 0;
		/* marks the beginning of a new primitive (only needed by bounding box) */
		if (start > out) start[4] |= NEW_BBOX;
	}
	return out;
}

/* some blocks are just retextured from other models: too tedious to copy vertex data all over the place */
static DATA16 blockCopyModel(DATA16 model, DATA8 tex)
{
	uint16_t minUV[12];
	int      count, texU, texV, norm;
	DATA16   dst = model;
	DATA16   ret;

	/* fill minUV texture */
	for (count = dst[-1], memset(minUV, 0xff, sizeof minUV); count > 0; count --, dst += INT_PER_VERTEX)
	{
		texU = GET_UCOORD(dst); texU &= ~15;
		texV = GET_VCOORD(dst); texV &= ~15;
		norm = GET_NORMAL(dst) * 2;
		if (minUV[norm] > texU) minUV[norm] = texU; norm ++;
		if (minUV[norm] > texV) minUV[norm] = texV;
	}

	/* retexture model */
	dst = model;
	count = dst[-1];
	memcpy(ret = blockAllocVertex(count), dst, count * BYTES_PER_VERTEX);
	for (dst = ret; count > 0; count --, dst += INT_PER_VERTEX)
	{
		texU = GET_UCOORD(dst);
		texV = GET_VCOORD(dst);
		norm = GET_NORMAL(dst) * 2;
		texU = texU - minUV[norm] + tex[norm] * 16; norm ++;
		texV = texV - minUV[norm] + tex[norm] * 16;
		CHG_UVCOORD(dst, texU, texV);
	}
	return ret;
}

/* extract emitter location from custom model vertex */
static void blockExtractEmitterLoction(DATA16 model, DATA8 loc, int box)
{
	uint16_t min[3];
	uint16_t max[3];
	int      count, face, i;

	memset(min, 0xff, sizeof min);
	memset(max, 0x00, sizeof max);

	for (count = model[-1], face = 0; count > 0; count --, model += INT_PER_VERTEX)
	{
		if (face == box)
		{
			for (i = 0; i < 3; i ++)
			{
				uint16_t v = model[i] - ORIGINVTX;
				if (min[i] > v) min[i] = v;
				if (max[i] < v) max[i] = v;
			}
		}
		else if (face < box)
		{
			if (model[4] & NEW_BBOX)
				face ++;
		}
		else break;
	}
	/* convert from range [0-65536] to [0-16] */
	loc[0] = min[0] * 16 / BASEVTX; loc[3] = max[0] * 16 / BASEVTX;
	loc[2] = min[2] * 16 / BASEVTX; loc[5] = max[2] * 16 / BASEVTX;
	loc[1] = loc[4] = max[1] * 16 / BASEVTX;
}

/* convert symbols into numeric and parse float */
Bool blockParseModelJSON(vec table, int max, STRPTR value)
{
	int index;
	for (index = 0; index < max && IsDef(value); index ++)
	{
		/* identifier must be upper case */
		if ('A' <= value[0] && value[0] <= 'Z')
		{
			STRPTR end;
			int token;
			for (end = value + 1; *end && *end != ','; end ++);
			token = FindInList("FACES,TEX_CUBEMAP,TEX_DETAIL,TEX_INHERIT,SIZE,TR,ROT,ROTCAS,REF,ROT90,TEX,INVERT,INC_FACEID,COPY,SAME_AS", value, end-value) + 1;
			if (token == 0) return False;
			if (token == BHDR_MAXTOK) token = COPY_MODEL; else
			if (token == BHDR_MAXTOK+1) token = SAME_AS;
			table[index] = token;
			value = end;
		}
		else table[index]= strtof(value, &value);

		while (isspace(*value)) value ++;
		if (*value == ',')
			value ++;
		while (isspace(*value)) value ++;
	}
	return True;
}

/*
 * table has been parsed, look at what we collected: either a block description or block state
 * allocate all we need:
 * - blocks per id info
 * - blocks per state info
 * - vertex data for custom model
 * - bounding boxes
 */
Bool blockCreate(const char * file, STRPTR * keys, int line)
{
	static struct Block_t block;
	static uint8_t emitters[256]; /* particle emitter location per state */
	static uint8_t emitUsage;

	STRPTR value = jsonValue(keys, "id");
	if (value)
	{
		/* previous block emitter list: save it now into previous block def */
		if (emitUsage > 0)
		{
			DATA8 mem = blockIds[block.id].emitters = stringAddPool("", emitUsage + 16);
			memcpy(mem, emitters, emitUsage + 16);
		}
		memset(&block, 0, sizeof block);
		memset(blocks.modelRef, 0, 2 * sizeof blocks.modelRef);
		memset(emitters, 0, 16);
		emitUsage = 0;
		block.id = atoi(value);
		blocks.curVtxCount = 0;

		/* keep all custom models as backref */
		value = jsonValue(keys, "keepModel");
		blocks.modelKeep = value && atoi(value) > 0;

		if (block.id > 255)
		{
			SIT_Log(SIT_ERROR, "%s: invalid block id %d on line %d\n", file, block.id, line);
			return False;
		}

		/* main block type for rendering world */
		value = jsonValue(keys, "type");
		block.type = FindInList("INVIS,SOLID,TRANS,QUAD,CUST", value, 0);
		if (block.type < 0)
		{
			SIT_Log(SIT_ERROR, "%s: unknown block type '%s' on line %d\n", file, value, line);
			return False;
		}

		/* model to generate for inventory */
		value = jsonValue(keys, "inv");
		block.inventory = value ? FindInList("NONE,CUBE,ITEM2D,MODEL", value, 0) : 0;
		if (block.inventory < 0)
		{
			SIT_Log(SIT_ERROR, "%s: unknown inventory model type '%s' on line %d\n", file, value, line);
			return False;
		}
		/* category it will appear in creative inventory */
		value = jsonValue(keys, "cat");
		if (value)
		{
			block.category = FindInList("BUILD,DECO,REDSTONE,CROPS,RAILS,FILLBY", value, 0)+1;
			if (block.category == 0)
			{
				SIT_Log(SIT_ERROR, "%s: unknown inventory category '%s' on line %d\n", file, value, line);
				return False;
			}
		}

		/* bounding box model */
		value = jsonValue(keys, "bbox");
		block.bbox = value ? FindInList("NONE,AUTO,MAX,FULL,FIRSTBOX", value, 0) : BBOX_AUTO;
		if (block.bbox < 0)
		{
			SIT_Log(SIT_ERROR, "%s: unknown bounding box '%s' on line %d\n", file, value, line);
			return False;
		}
		/* bounding box for player */
		value = jsonValue(keys, "bboxPlayer");
		block.bboxPlayer = value ? FindInList("NONE,AUTO,MAX,FULL", value, 0) : (block.type == QUAD ? BBOX_NONE : block.bbox);
		if (block.bboxPlayer < 0)
			block.bboxPlayer = block.bbox;
		/* default bbox (cannot be overridden) */
		switch (block.type) {
		case INVIS:
			block.bbox = BBOX_NONE;
			break;
		case SOLID:
		case TRANS:
		case QUAD:
			block.bbox = BBOX_AUTO;
		}

		/* fence gate: opened state has no collision */
		value = jsonValue(keys, "bboxPlayerIgnoreBit");
		if (value) block.bboxIgnoreBit = atoi(value);

		/* how the block has to orient when placing it */
		value = jsonValue(keys, "orient");
		if (value)
		{
			block.orientHint = FindInList("LOG,FULL,BED,SLAB,TORCH,STAIRS,NSWE,SWNE,DOOR,RAILS,SE,LEVER,SNOW,VINES,HOPPER", value, 0)+1;
			if (block.orientHint == 0)
			{
				SIT_Log(SIT_ERROR, "%s: unknown orient hint '%s' on line %d\n", file, value, line);
				return False;
			}
		}

		/* what's the rules without the exceptions */
		value = jsonValue(keys, "special");
		if (value)
		{
			do {
				STRPTR next = strchr(value, '|');
				if (next) *next++ = 0;
				int flag = FindInList(
					"NORMAL,CHEST,DOOR,NOSIDE,HALF,STAIRS,GLASS,FENCE,FENCE2,"
					"WALL,RSWIRE,LEAVES,LIQUID,DOOR_TOP,TALLFLOWER,RAILS,TRAPDOOR,"
					"SIGN,PLATE,SOLIDOUTER,JITTER,POT,NOCONNECT,CNXTEX,DUALSIDE", value, 0
				);
				if (flag < 0)
				{
					SIT_Log(SIT_ERROR, "%s: unknown special tag '%s' on line %d\n", file, value, line);
					return False;
				}
				switch (flag) {
				/* these 2 needs to be flags, not enum */
				case BLOCK_LASTSPEC:   block.special |= BLOCK_NOCONNECT; break;
				case BLOCK_LASTSPEC+1: block.special |= BLOCK_CNXTEX; break;
				case BLOCK_LASTSPEC+2: block.special |= BLOCK_DUALSIDE; break;
				default:               block.special  = flag;
				}
				value = next;
			}
			while (value);
		}
		if (block.orientHint == ORIENT_BED)
			block.special = BLOCK_BED;

		/* need some extra check when placed */
		block.tall = block.special == BLOCK_BED ||
		             block.special == BLOCK_TALLFLOWER ||
		             block.special == BLOCK_DOOR;

		/* liquid physics */
		value = jsonValue(keys, "viscosity");
		if (value)
		{
			block.viscosity = atof(value);
			if (block.viscosity > 0)
				block.bboxPlayer = BBOX_NONE;
		}

		value = jsonValue(keys, "groundFriction");
		block.friction = value ? atof(value) : 1;

		/* grab inventory model from this block state */
		value = jsonValue(keys, "invState");
		if (value)
			block.invState = atoi(value)+1;

		/* how much light the block emit (max=15) */
		value = jsonValue(keys, "emitLight");
		if (value)
		{
			block.emitLight = atoi(value);
			/* light update won't work with values above MAXLIGHT */
			if (block.emitLight > MAXLIGHT)
				block.emitLight = MAXLIGHT;
		}

		/* how much sky light the block absorb (opaque = 15) */
		value = jsonValue(keys, "opacSky");
		if (value == NULL)
		{
			if (block.type == SOLID)
			{
				block.opacSky = MAXSKY;
				block.opacLight = MAXLIGHT;
			}
		}
		else block.opacSky = atoi(value);

		/* how much light the block absorb (opaque = 15) */
		value = jsonValue(keys, "opacLight");
		if (value == NULL)
		{
			if (block.type == SOLID)
				block.opacLight = MAXLIGHT;
		}
		else block.opacLight = atoi(value);

		/* block constraint it has to satisfy */
		value = jsonValue(keys, "placement");

		block.name = stringAddPool(jsonValue(keys, "name"), value ? strlen(value) + 1 : 0);
		block.tech = blockGetTechName(block.name, jsonValue(keys, "tech"));

		if (value)
		{
			/* store it after name for now */
			strcpy(strchr(block.name, 0) + 1, value);
			block.placement = 1;
		}

		value = jsonValue(keys, "gravity");
		if (value)
			block.gravity = atoi(value);

		/* can this block be affected by piston */
		value = jsonValue(keys, "pushable");
		/* default value */
		block.pushable = block.type == QUAD || block.id == 0 ? PUSH_DESTROY : PUSH_AND_RETRACT;
		if (value)
		{
			int type = FindInList("NO,PUSHONLY,DESTROY,DROPITEM,YES", value, 0);
			if (type < 0)
			{
				SIT_Log(SIT_ERROR, "%s: unknown pushable value '%s' on line %d\n", file, value, line);
				return False;
			}
			block.pushable = type;
		}

		/* check for tile entity for this block XXX somewhat useless */
		value = jsonValue(keys, "tile");
		if (value && atoi(value) > 0)
		{
			block.tileEntity = 1;
		}

		/* custom inventory model (instead of reusing the block model) */
		value = jsonValue(keys, "invmodel");
		if (value && value[0] == '[')
		{
			int count = StrCount(value, ',') + 1;
			vec table = alloca(count * sizeof *table);
			if (! blockParseModelJSON(table, count, value + 1))
			{
				SIT_Log(SIT_ERROR, "%s: bad value on line %d\n", file, line);
				return False;
			}
			if (table[0] == COPY_MODEL)
			{
				block.copyModel = table[1];
			}
			else block.model = blockParseModel(table, count, NULL, -1);
			block.invState = (block.orientHint != ORIENT_BED);
		}

		/* how redstone wire attach to this block */
		value = jsonValue(keys, "rswire");
		block.rswire = FindInList("ALLDIR,FRONTBACK,BACK", value, 0) + 1;

		/* blocks react to redstone update */
		value = jsonValue(keys, "rsupdate");
		if (value)
		{
			block.rsupdate = FindInList("RECEIVE,GENERATE,INOUT", value, 0) + 1;
			if (block.rsupdate == 0)
			{
				SIT_Log(SIT_ERROR, "%s: unknown rsupdate value '%s' specified on line %d", file, value, line);
				return False;
			}
		}

		/* types of particles emitted continuously */
		value = jsonValue(keys, "particle");
		block.emitInterval = 0xffff;
		block.particleTTL = 0xffff;
		if (value && *value == '[')
		{
			value ++;
			STRPTR p = strchr(value, ',');
			if (p)
			{
				*p ++ = 0;
				block.emitInterval = strtoul(p, &p, 10);
				if (*p == ',')
					block.particleTTL = strtoul(p+1, &p, 10);
			}
		}
		block.particle = FindInList("BITS,SMOKE,DUST,DRIP", value, 0) + 1;
		if (block.emitInterval == 0xffff)
		{
			/* default values */
			switch (block.particle) {
			case PARTICLE_BITS:
			case PARTICLE_SMOKE:
				block.emitInterval = 750;
				block.particleTTL = 500;
				break;
			case PARTICLE_DUST:
			case PARTICLE_DRIP:
				block.emitInterval = 4000;
				block.particleTTL = 800;
			}
		}

		/* density (g/cm�): used by particles and entity physics */
		value = jsonValue(keys, "density");
		if (value)
		{
			if (isdigit(value[0]))
			{
				block.density = strtod(value, NULL);
			}
			else switch (FindInList("WOOD,IRON,PLANTS,ICE,WATER,GLASS", value, 0)) {
			case 0: block.density =  0.8; break;
			case 1: block.density = 10.0; break;
			case 2: block.density =  0.7; break;
			case 3: block.density =  0.9; break;
			case 4: block.density =  1.0; break;
			case 5: block.density =  2.5; break;
			default:
				SIT_Log(SIT_ERROR, "%s: unknown density value '%s' specified on line %d", file, value, line);
				return False;
			}
		}
		else block.density = 5; /* stone */

		/* chunk meshing optization: mark block that will *automatically* update nearby blocks */
		switch (block.type) {
		case CUST:
			switch (block.special&31) {
			case BLOCK_CHEST:
			case BLOCK_GLASS:
			case BLOCK_FENCE:
			case BLOCK_FENCE2:
			case BLOCK_WALL:
			case BLOCK_RSWIRE:
			case BLOCK_LIQUID:
			case BLOCK_SOLIDOUTER:
				block.updateNearby = 1;
			}
			break;
		case SOLID: /* will produce AO/shadow on nearby blocks */
		case TRANS:
			block.updateNearby = 1;
		}
		if (block.rswire)
			block.updateNearby = 2;

		/* check for misspelled property name */
		#ifdef STRICT_PARSING
		while (*keys)
		{
			if (FindInList(
				"id,name,type,inv,invstate,cat,special,tech,bbox,orient,keepModel,particle,rsupdate,density,"
				"emitLight,opacSky,opacLight,tile,invmodel,rswire,placement,bboxPlayer,gravity,pushable,"
				"bboxPlayerIgnoreBit,groundFriction,viscosity", *keys, 0) < 0)
			{
				SIT_Log(SIT_ERROR, "%s: unknown property \"%s\" on line %d\n", file, *keys, line);
				return False;
			}
			keys += 2;
		}
		#endif
		/* all seems good */
		blockIds[block.id] = block;
	}
	else /* block state */
	{
		struct BlockState_t state = {0};

		value = jsonValue(keys, "state");
		state.id = value ? atoi(value) : 0;
		state.type = block.type;
		state.special = block.special & 31;

		if (block.type != QUAD)
		{
			/* 0, 0 is grayscale grass texture, use the dedicated "undefined" tex instead */
			static uint8_t defTex[] = {30, 0, 30, 0, 30, 0, 30, 0, 30, 0, 30, 0};
			memcpy(&state.nzU, defTex, 12);

			if (blocks.totalStates > 0)
			{
				/* reuse last tex definition fromm previous state */
				BlockState last = blockStates + blocks.totalStates - 1;
				if ((last->id >> 4) == block.id)
					memcpy(&state.nzU, &last->nzU, 12);
			}
		}

		if (state.id > 15)
		{
			SIT_Log(SIT_ERROR, "%s: invalid state number: %d, must be <= 15, on line %d\n", file, state.id, line);
			return False;
		}

		state.name = blockExpandName(block.id, block.name, jsonValue(keys, "name"));

		value = jsonValue(keys, "tex");
		if (value)
		{
			if (value[0] == '[')
			{
				/* must be an array */
				DATA8 tex;
				int   i;
				/* note: values are sanitized at this point */
				for (value ++, tex = &state.nzU, i = 0; *value && i < 12; i ++, tex ++)
				{
					*tex = strtoul(value, &value, 10);
					if (*value == ',') value ++;
				}
				if (*value)
					/* extension: 13th element = rotation */
					state.rotate = atoi(value);

				/* relocate tex that have biome dependant color */
				for (tex = &state.nzU, i &= ~1; i > 0; i -= 2, tex += 2)
				{
					/* color will be adjusted in the fragment shader: texColor * biomeColor */
					DATA8 biome;
					int   j;
					for (biome = biomeDepend, j = 0; biome < EOT(biomeDepend); biome += 2, j ++)
						if (biome[0] == tex[0] && biome[1] == tex[1])
							/* it will save 1 bit in vertex shader */
							tex[0] = j, tex[1] = 62;
				}
			}
			else
			{
				SIT_Log(SIT_ERROR, "%s: texture must be an array for block state %d:%d, on line %d\n",
					file, block.id, state.id, line);
				return False;
			}
		}

		/* rotate individually texture on 6 faces */
		value = jsonValue(keys, "rotate");
		if (value)
			state.rotate = atoi(value);

		/* grab inventory model */
		if (block.invState > 0)
		{
			if (block.invState-1 == state.id)
			{
				state.inventory = block.category | (block.inventory << 4);
				state.rotate |= TRIMNAME;
				if (block.copyModel)
				{
					Block copy = &blockIds[block.copyModel];

					if (copy->model)
						/* note: can't be done in the block branch, we need texture coord from block state */
						blockIds[block.id].model = blockCopyModel(copy->model, &state.nzU);
				}
			}
		}
		else
		{
			state.inventory = block.inventory == 0 ? 0 : block.category | (block.inventory << 4);
			value = jsonValue(keys, "inv");
			int inv;
			if (value && (inv = FindInList("NONE,CUBE,ITEM2D,MODEL", value, 0)) >= 0)
				state.inventory = inv == 0 ? 0 : block.category | (inv << 4);
		}

		/* list of quads to generate for a QUAD block */
		value = jsonValue(keys, "quad");
		if (value)
		{
			if (value[0] == '[')
			{
				DATA8 quad;
				int   i;
				/* note: values are sanitized at this point */
				for (value ++, quad = &state.pxU, i = 0; value && i < 10; i ++, quad ++)
				{
					STRPTR next = strchr(value, ',');
					if (next) *next++ = 0;
					int type = FindInList("CROSS,SQUARE,NORTH,SOUTH,EAST,WEST,BOTTOM,ASCE,ASCW,ASCN,ASCS", value, 0);
					if (type < 0)
					{
						SIT_Log(SIT_ERROR, "%s: unknown quad type %s on line %d\n", file, value, line);
						return False;
					}
					/* internal types that need to be skipped */
					if (type > QUAD_CROSS)  type ++;
					if (type > QUAD_SQUARE) type += 3;
					*quad = type;
					value = next;
				}
				if (state.pxU == QUAD_CROSS)
					state.pxV = QUAD_CROSS2;
				if (state.pxU == QUAD_SQUARE)
					state.pxV = QUAD_SQUARE2,
					state.pzU = QUAD_SQUARE3,
					state.pzV = QUAD_SQUARE4;
			}
			else
			{
				SIT_Log(SIT_ERROR, "%s: quad must be an array for block state %d:%d, on line %d\n",
					file, block.id, state.id, line);
				return False;
			}
		}

		/* vertex data for model */
		value = jsonValue(keys, "model");
		if (value && value[0] == '[')
		{
			/* pre-parse table */
			int count = StrCount(++ value, ',') + 1;
			vec table = alloca(sizeof (float) * count);
			if (! blockParseModelJSON(table, count, value))
			{
				SIT_Log(SIT_ERROR, "%s: bad value on line %d\n", file, line);
				return False;
			}

			if (table[0] == SAME_AS)
			{
				BlockState old = blockGetById((int) table[1]);

				if (count == 2)
				{
					state.custModel = old->custModel;
					/* we'll know that this model is a carbon copy of an earlier one (save some mem) */
					state.ref = blockStates + blocks.totalStates - old;
				}
				else
				{
					count = blocks.modelCount[old->id & 15];

					if (count > 0)
					{
						float * model = blocks.lastModel + blocks.modelRef[old->id & 15];

						state.custModel = blockParseModel(model, count, NULL, table[2] / 90);
					}
				}
			}
			else if (table[0] == COPY_MODEL)
			{
				BlockState copy = blockGetById((int) table[1]);
				if (copy->custModel)
					state.custModel = blockCopyModel(copy->custModel, &state.nzU);
			}
			else
			{
				state.custModel = blockParseModel(table, count, NULL, -1);

				if (state.custModel == NULL)
				{
					SIT_Log(SIT_ERROR, "%s: failed to parse model for block %d:%d, on line %d\n",
						file, block.id, state.id, line);
					return False;
				}
				int start = 0;
				if (blocks.modelKeep)
				{
					start = blocks.curVtxCount;
					blocks.curVtxCount += count;
				}

				if (blocks.maxVtxCust < start + count)
				{
					blocks.maxVtxCust = (start + count + 127) & ~127;
					blocks.lastModel  = realloc(blocks.lastModel, blocks.maxVtxCust * 4);
				}
				blocks.modelRef[state.id] = start;
				blocks.modelCount[state.id] = count;
				/* later model can reference this one */
				memcpy(blocks.lastModel + start, table, count * 4);

				if (block.special == BLOCK_RSWIRE)
				{
					/* need to change normal to 7: color will depend on metadata not on the normal */
					DATA16 vertex;
					for (vertex = state.custModel, count = vertex[-1]; count > 0; count --, vertex += INT_PER_VERTEX)
						vertex[4] |= 7<<3;
				}
			}
		}

		/*
		 * particle emitter location: there are saved in Block_t instead of BlockState_t
		 * because there are not that many blocks that have this property (30 or so out of
		 * 1500 in 1.12).
		 */
		value = jsonValue(keys, "emit");
		if (value)
		{
			if (*value == '[')
			{
				for (value ++; IsDef(value); )
				{
					static uint8_t faceLoc[] = { /* S, E, N, W, T, B */
						0,0,16, 16,16,16,
						16,0,0, 16,16,16,
						0,0,0,  16,16,0,
						0,0,0,  0,16,16,
						0,16,0, 16,16,16,
						0,0,0,  16,0,16,
						0,0,0,  0,0,0
					};
					int8_t chr = *value;
					if ('0' <= chr && chr <= '9')
					{
						blockExtractEmitterLoction(state.custModel, faceLoc + 36, strtoul(value, &value, 10));
						chr = 36;
					}
					else
					{
						value ++;
						switch (chr) {
						case 's': case 'S': chr = 0; break;
						case 'e': case 'E': chr = 6; break;
						case 'w': case 'W': chr = 12; break;
						case 'n': case 'N': chr = 18; break;
						case 't': case 'T': chr = 24; break;
						case 'b': case 'B': chr = 30; break;
						default: chr = 255;
						}
					}
					if (chr < 255 && emitUsage < 256-6-16)
					{
						DATA8 p = emitters + state.id;
						if (p[0] == 0)
							p[0] = emitUsage+16-state.id;
						else
							emitters[emitUsage + 15] |= 0x80; /* more emitter location follows */
						memcpy(emitters + emitUsage + 16, faceLoc + chr, 6);
						emitUsage += 6;
					}
					if (*value == ',')
						value ++;
				}
			}
			else
			{
				SIT_Log(SIT_ERROR, "%s: emit must be an array for block state %d:%d, on line %d\n",
					file, block.id, state.id, line);
				return False;
			}
		}

		blockAddState(&state, block.id);

		/* check for mis-spelling */
		#ifdef STRICT_PARSING
		while (*keys)
		{
			if (FindInList("state,name,tex,quad,inv,model,rotate,emit", *keys, 0) < 0)
			{
				SIT_Log(SIT_ERROR, "%s: unknown property \"%s\" on line %d\n", file, *keys, line);
				return False;
			}
			keys += 2;
		}
		#endif
	}

	return True;
}

/* relocated texture to make it is easy for the meshing phase to generate connected models */
void blockParseConnectedTexture(void)
{
	Block b;
	DATA8 cnx;
	int   i;
	int   row = 32;
	blockLast = blockStates + blocks.totalStates;
	for (b = blockIds; b < EOT(blockIds); b ++)
	{
		/* while we are scanning blocks, also pre-parse block placement constaints */
		if (b->placement > 0)
		{
			STRPTR fmt = strchr(b->name, 0) + 1;
			DATA8  cnt = fmt;
			DATA8  p   = cnt + 1;
			int    num = 0;

			b->placement = fmt - b->name;

			while (*fmt)
			{
				STRPTR next;
				for (next = fmt; *next && *next !=','; next ++);
				if (*next) *next++ = 0;
				switch (FindInList("wall,ground,solid", fmt, 0)) {
				case 0: /* any type of solid wall */
					num ++;
					p[0] = PLACEMENT_WALL >> 8;
					p[1] = 0;
					p += 2;
					break;
				case 1: /* any type of flat ground */
					num ++;
					p[0] = PLACEMENT_GROUND >> 8;
					p[1] = 0;
					p += 2;
					break;
				case 2: /* any face that is solid */
					num ++;
					p[0] = PLACEMENT_SOLID >> 8;
					p[1] = 0;
					p += 2;
					break;
				default: /* this particular block */
					i = itemGetByName(fmt, False);
					if (i <= 0) break;
					num ++;
					p[0] = i >> 8;
					p[1] = i & 255;
					p += 2;
				}
				fmt = next;
			}
			cnt[0] = num;
		}
		if (b->invState > 0)
			b->invState --;
		if ((b->special & BLOCK_CNXTEX) == 0) continue;
		BlockState state = blockGetById(b->id << 4);

		/* gather connected texture info (texture will be generated in blockPostProcessTexture) */
		while ((state->id >> 4) == b->id)
		{
			state->rotate |= CNXTEX;
			/* check if already added */
			for (cnx = blocks.cnxTex, i = blocks.cnxCount; i > 0 && ! (cnx[0] == state->nzU && cnx[1] == state->nzV); i --, cnx += 4);
			if (i == 0)
			{
				cnx[0] = state->nzU;
				cnx[1] = state->nzV;
				cnx[2] = 0;
				cnx[3] = row ++;
				blocks.cnxCount ++;
			}

			DATA8 tex;
			int   srcU = state->nzU * 16;
			int   srcV = state->nzV * 16;
			int   j;

			/* relocate tex from block state */
			for (j = 0, tex = &state->nzU; j < 6; tex[0] = cnx[2], tex[1] = cnx[3], j ++, tex += 2);

			if (b->type == CUST && state->custModel)
			{
				/* also relocate vertex data */
				DATA16 vertex = state->custModel;
				for (j = vertex[-1]; j > 0; j --, vertex += INT_PER_VERTEX)
				{
					int U = GET_UCOORD(vertex);
					int V = GET_VCOORD(vertex);
					if (srcU <= U && U <= srcU+16 &&
						srcV <= V && V <= srcV+16)
					{
						/* only U texture will have to be shifted in the meshing phase */
						U = U - srcU + cnx[2] * 16;
						V = V - srcV + cnx[3] * 16;
						CHG_UVCOORD(vertex, U, V);
					}
				}
			}
			state ++;
		}
	}
}

/*
 * generate models for blocks/item that will be displayed in inventory: they are
 * somewhat similar to normal block models, but all models will be rendered
 * using an othogonal projection.
 */
int blockInvModelCube(DATA16 ret, BlockState b, DATA8 textureCoord)
{
	DATA8 uv = &b->nzU;
	int   rotate, i, j;
	for (i = 0, rotate = b->rotate; i < 6; i ++, rotate >>= 2, uv += 2)
	{
		DATA8   tex = textureCoord + (rotate&3) * 8;
		uint8_t U   = uv[0];
		uint8_t V   = uv[1];
		if (V == 62) V = 63; /* biome dependent color: line below will contain sample tex with a default biome color */

		/* 4 vertex per face (QUAD) */
		for (j = 0; j < 4; j ++, tex += 2, ret += INT_PER_VERTEX)
		{
			DATA8 vtx = cubeVertex + cubeIndices[i*4+j];
			ret[0] = VERTEX(vtx[0]);
			ret[1] = VERTEX(vtx[1]);
			ret[2] = VERTEX(vtx[2]);
			/* tex coord and normals */
			int texU = (tex[0] + U) * 16;
			int texV = (tex[1] + V) * 16;
			if (b->special == BLOCK_HALF)
			{
				/* half-slab model */
				ret[1] = (ret[1]-ORIGINVTX) / 2 + ORIGINVTX;
				if (i < 4) texV = tex[1] * 8 + V * 16 + 8;
				if ((b->id & 15) > 7)
				{
					/* top slab */
					ret[1] += BASEVTX/2;
					if (i < 4) texV -= 8;
				}
			}
			if (texU == 512)  texU = 511;
			if (texV == 1024) texV = 1023;
			SET_UVCOORD(ret, texU, texV);
			ret[4] |= (i << 3) | (0xf0 << 8);
		}
		/* convert to triangles */
		memcpy(ret,   ret - 20, BYTES_PER_VERTEX);
		memcpy(ret+5, ret - 10, BYTES_PER_VERTEX);
		ret += INT_PER_VERTEX*2;
	}
	return 36;
}

/* generate vertex data to render a flat square */
static int blockInvModelQuad(DATA16 ret, DATA8 UV)
{
	DATA8   tex;
	uint8_t U = UV[0];
	uint8_t V = UV[1];
	int     j;

	if (V == 62 && U < 17) V = 63; /* biome dependent color */

	for (j = 0, tex = texCoordRevU; j < 4; j ++, tex += 2, ret += INT_PER_VERTEX)
	{
		DATA8 vtx = cubeVertex + cubeIndices[8+j];
		ret[0] = VERTEX(vtx[0]);
		ret[1] = VERTEX(vtx[1]);
		ret[2] = VERTEX(vtx[2]); /* Z value doesn't really matter: we'll use a 2d projection on XY planes */
		/* tex coord and normals */
		int texU = (tex[0] + U) * 16;
		int texV = (tex[1] + V) * 16;
		if (texU == 512)  texU = 511;
		if (texV == 1024) texV = 1023;
		SET_UVCOORD(ret, texU, texV);
		ret[4] |= (6 << 3) | (0xf0 << 8);
	}
	/* convert to triangles */
	memcpy(ret,   ret - 20, BYTES_PER_VERTEX);
	memcpy(ret+5, ret - 10, BYTES_PER_VERTEX);
	return 6;
}

/* need to remove custom faceId and set light level to max */
int blockInvCopyFromModel(DATA16 ret, DATA16 model, int connect)
{
	int count, vtx;

	for (count = model[-1], vtx = 0; count > 0; count --, model += INT_PER_VERTEX)
	{
		uint8_t faceId = (model[4] >> FACEIDSHIFT) & 31;
		if (faceId > 0 && (connect & (1 << (faceId-1))) == 0) continue;
		memcpy(ret, model, BYTES_PER_VERTEX);
		ret[4] = (ret[4] & 0xff) | (0xf0 << 8);
		vtx ++; ret += INT_PER_VERTEX;
	}
	return vtx;
}

int blockInvCountVertex(DATA16 model, int connect)
{
	int count, vtx;
	for (count = model[-1], vtx = 0; count > 0; count --, model += INT_PER_VERTEX)
	{
		uint8_t faceId = (model[4] >> FACEIDSHIFT) & 31;
		if (faceId == 0 || (connect & (1 << (faceId-1)))) vtx ++;
	}
	return vtx;
}

/* inventory models generation */
void blockParseInventory(int vbo)
{
	BlockState state;
	Block      b;
	DATA16     vertex;
	int        i, j, vtx, total;

	/* first: count vertex needed for inventory models */
	for (state = blockStates, total = 0; state < blockLast; state ++)
	{
		switch (state->inventory & MODELFLAGS) {
		case CUBE3D: vtx = 36; break;
		case ITEM2D: vtx = 6;  break;
		case MODEL:
			b = &blockIds[state->id>>4];
			if (b->orientHint == ORIENT_BED && b->model)
				vtx = b->model[-1];
			else if (b->special == BLOCK_CHEST)
				/* don't want double chest models */
				vtx = blockInvCountVertex(state->custModel, 2);
			else if (b->model)
				vtx = blockInvCountVertex(b->model, ALLFACEIDS);
			else if (state->custModel)
				vtx = blockInvCountVertex(state->custModel, ALLFACEIDS);
			else
				vtx = 36; /* assume cube */
			if (b->special == BLOCK_SOLIDOUTER)
				vtx += 36;
			break;
		default: continue;
		}
		blocks.totalInv ++;
		total += vtx;
	}

	/* add inventory models for items */
	total += 6 * itemGetCount();

	blocks.vboInv = vbo;
	blocks.totalInv += itemGetCount();
	blocks.invModelOff = malloc(blocks.totalInv * 2 + 4);
	blocks.invModelOff[0] = 0;

	/* these vertex will stay on the GPU */
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, total * BYTES_PER_VERTEX, NULL, GL_STATIC_DRAW);
	vertex = glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);

	fprintf(stderr, "custom model vertex = %d bytes\n", blocks.totalVtx);
	// total wasted = 4014 / 409600
	// fprintf(stderr, "inventory = %d vertex, total = %d\n", total, blocks.totalInv);

	/* generate mesh: will use the same shader than block models */
	for (state = blockStates, vtx = 0, j = 0; state < blockLast; state ++)
	{
		switch (state->inventory & MODELFLAGS) {
		case CUBE3D:
			total = blockInvModelCube(vertex, state, texCoordRevU);
			break;
		case ITEM2D:
			total = blockInvModelQuad(vertex, &state->nzU);
			break;
		case MODEL:
			b = &blockIds[state->id>>4];
			if (b->orientHint == ORIENT_BED && b->model)
				total = blockInvCopyFromModel(vertex, b->model, 1 << (state->id & 15));
			else if (b->special == BLOCK_WALL)
				total = blockInvCopyFromModel(vertex, state->custModel, 2+8+16+32);
			else if (b->special == BLOCK_CHEST)
				/* don't want double chest models */
				total = blockInvCopyFromModel(vertex, state->custModel, 1);
			else if (b->model)
				total = blockInvCopyFromModel(vertex, b->model, ALLFACEIDS);
			else if (state->custModel)
				total = blockInvCopyFromModel(vertex, state->custModel, ALLFACEIDS);
			else
				total = blockInvModelCube(vertex, state, texCoordRevU);
			if (b->special == BLOCK_SOLIDOUTER)
				total += blockInvModelCube(vertex + total * INT_PER_VERTEX, state, texCoordRevU);
			break;
		default:
			continue;
		}
		blocks.invModelOff[j] = vtx;
		vertex += total * INT_PER_VERTEX;
		vtx    += total;
		state->invId = j;
		j ++;
	}

	/* inventory models for items */
	for (i = 0, total = itemGetCount(); i < total; i ++)
	{
		ItemDesc item = itemGetByIndex(i);
		uint8_t  tex[2] = {item->texU + ITEM_ADDTEXU, item->texV + ITEM_ADDTEXV};

		vertex += blockInvModelQuad(vertex, tex) * INT_PER_VERTEX;

		item->glInvId = j;
		blocks.invModelOff[j] = vtx;
		j ++; vtx += 6;
	}

	blocks.invModelOff[j] = vtx;

	glUnmapBuffer(GL_ARRAY_BUFFER);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}


/*
 * generate bounding box for blocks
 */

VTXBBox blockGetBBoxForVertex(BlockState b)
{
	/* bbox used for display (they are slightly offseted to prevent z-fighting) */
	int index = b->bboxId;
	return index == 0 ? NULL : blocks.bbox + index;
}

VTXBBox blockGetBBox(BlockState b)
{
	/* bbox for collision detection */
	int index = b->bboxId;
	if (b->special == BLOCK_FENCE || b->special == BLOCK_FENCE2)
	{
		/* use a simplified bounding box for fence (note: NOCONNECT == fence gate) */
		return blocks.bboxExact + ((blockIds[b->id>>4].special & BLOCK_NOCONNECT) ? index : 21);
	}
	return index == 0 ? NULL : blocks.bboxExact + index;
}

static void blockBBoxInit(VTXBBox box)
{
	memset(box, 0, sizeof *box);
	box->pt1[0] = box->pt1[1] = box->pt1[2] = 65535;
}

/* generate bounding box for collision detection based on custom vertex data */
static void blockGenBBox(DATA16 buffer, int len, int type)
{
	DATA16  data  = buffer;
	VTXBBox box   = blocks.bbox + blocks.bboxMax, first = box;
	VTXBBox exact = blocks.bboxExact + blocks.bboxMax;
	VTXBBox ref   = NULL;
	int     i, j;

	if (len == 0) return;

	blockBBoxInit(box);
	box->aabox = 1;

	/* get bounding boxes from raw vertex data */
	for (j = 0, first->cont = 1; len > 0; len --, data += INT_PER_VERTEX, j ++)
	{
		DATA16 pt1, pt2;
		if (type >= BBOX_FULL)
		{
			if ((data[4] & (31<<8)) == 0 && !ref) ref = box;
			if (data[4] & NEW_BBOX)
			{
				if (type == BBOX_FIRST)
					break;
				/* start of a new box */
				pt1 = box->pt1;
				/* ignore box if one of its axis has a 0 width */
				if (pt1[0] != pt1[3] && pt1[1] != pt1[4] && pt1[2] != pt1[5])
				{
					/* start of a new box */
					first->cont ++;
					box ++;
					blockBBoxInit(box);
				}
			}
			/* faceId of an optional box (connected models) */
			box->flags = (data[4] >> 8) & 31;
		}
		if (j == 5)
		{
			/* one face: check if it is axis aligned */
			int axis1 = 0, axis2 = 0;
			pt1 = data - 5 * INT_PER_VERTEX;
			if (pt1[0] == pt1[INT_PER_VERTEX])   axis1 |= 1;
			if (pt1[1] == pt1[INT_PER_VERTEX+1]) axis1 |= 2;
			if (pt1[2] == pt1[INT_PER_VERTEX+2]) axis1 |= 4;
			if (pt1[0] == pt1[INT_PER_VERTEX*2])   axis2 |= 1;
			if (pt1[1] == pt1[INT_PER_VERTEX*2+1]) axis2 |= 2;
			if (pt1[2] == pt1[INT_PER_VERTEX*2+2]) axis2 |= 4;
			if ((axis1 & axis2) == 0)
				first->aabox = 0;
			j = -1;
		}

		/* get the min and max coord of all the vertices */
		for (pt1 = box->pt1, pt2 = box->pt2, i = 0; i < 3; i ++, pt1 ++, pt2 ++)
		{
			uint16_t coord = data[i];
			if (*pt1 > coord) *pt1 = coord;
			if (*pt2 < coord) *pt2 = coord;
		}
		box->sides |= 1 << GET_NORMAL(data);
	}

	/* 1st: adjust vertex data for drawing lines/faces */
	for (box = blocks.bbox + blocks.bboxMax, i = box->cont; i > 0; i --, box ++, exact ++)
	{
		/* keep a non-shifted copy first: will be needed for collision detection/correction */
		memcpy(exact, box, sizeof *box);

		#define SHIFT     ((int) (0.01 * BASEVTX))
		box->pt1[0] -= SHIFT;
		box->pt1[1] -= SHIFT;
		box->pt1[2] -= SHIFT;
		box->pt2[0] += SHIFT;
		box->pt2[1] += SHIFT;
		box->pt2[2] += SHIFT;
		#undef SHIFT
	}

	/* 2nd: check for intersecting box: adjust vertex to prevent overdraw (and make edges visible) */
	for (box = blocks.bbox + blocks.bboxMax, j = first->cont; j > 0; box ++, j --)
	{
		if (ref && ref != box)
		{
			/* check intersection between these boxes */
			int     inter[6];
			DATA16  pt1 = box->pt1;
			DATA16  pt2 = ref->pt1;
			for (i = 0; i < 3; i ++)
			{
				if (pt1[i] < pt2[i])
					inter[i] = pt2[i], inter[i+3] = box->pt2[i];
				else
					inter[i] = pt1[i], inter[i+3] = ref->pt2[i];
			}
			inter[3] -= inter[0];
			inter[4] -= inter[1];
			inter[5] -= inter[2];
			if (inter[3] > 0 && inter[4] > 0 && inter[5] > 0)
			{
				/* they are intersecting: get smallest intersecting axis */
				i = 0;
				if (inter[4] < inter[3]) i = 1;
				if (inter[5] < inter[3+i]) i = 2;

				/* this is the axis we need to move vertices: toward the opposite corner */
				if (inter[i] == pt1[i])
					pt1[i] = inter[i] + inter[i+3];
				else
					box->pt2[i] = inter[i];
			}
		}
	}
	blocks.bboxMax += first->cont;
}

/* generate vertex data for custom bounding box */
static int blockGenCommonBBox(float * bbox)
{
	float * start = bbox;
	DATA16  vtx, p;
	int     faces;
	int     i, j, faceId, cont;

	/* cube, quad, glass panes: no need to redefine bbox for each block */
	vtx = alloca(5 * 6 * BYTES_PER_VERTEX * 4);

	for (p = vtx, faceId = 0, cont = 1; cont; bbox += 6)
	{
		faces = bbox[0];
		cont = faces & BHDR_CONTINUE;
		if (faces & BHDR_INCFACEID)
			faceId += 1<<8;
		faces &= 63;
		DATA16 first = p;
		for (i = 0, bbox ++; faces; faces >>= 1)
		{
			if ((faces & 1) == 0) { i += 4; continue; }

			for (j = 0; j < 4; j ++, i ++, p += INT_PER_VERTEX)
			{
				DATA8 v = cubeVertex + cubeIndices[i];
				float x = (v[0] * bbox[0] + bbox[3]) / 16;
				float y = (v[1] * bbox[1] + bbox[4]) / 16;
				float z = (v[2] * bbox[2] + bbox[5]) / 16;

				p[0] = roundf(x * BASEVTX) + ORIGINVTX;
				p[1] = roundf(y * BASEVTX) + ORIGINVTX;
				p[2] = roundf(z * BASEVTX) + ORIGINVTX;
				p[3] = 0;
				p[4] = faceId | (i<<1);
			}
		}
		if (first > vtx) first[4] |= NEW_BBOX;
	}
	/* not that trivial of a function */
	blockGenBBox(vtx, (p - vtx) / INT_PER_VERTEX, BBOX_FULL);

	return bbox - start;
}


/* remove some faces/lines from bbox models: too difficult to do this in a vertex/geometry shader */
static int blockBBoxFuse(BlockState b, VTXBBox list, int cnxFlags, DATA16 buffer)
{
	VTXBBox bbox;
	DATA16  p;
	int     vtxOff = 0;
	int     face, i, j, ret;

	/* first: setup face vertices */
	for (bbox = list, face = 0, p = buffer; face < list->cont; bbox ++, face ++)
	{
		if (face > 0 && (cnxFlags & (1 << (face-1))) == 0) continue;
		for (i = 0; i < 6; i ++)
		{
			/* discard this face if another bbox hides it */
			if (i < 4 && (bbox == list ? (cnxFlags & (1 << i)) : i == ((face-1)^2))) continue;
			for (j = 0; j < 6; j ++, p ++)
				*p = vtxOff + bboxIndices[i*6+j];
		}
		vtxOff += 8;
	}
	ret = p - buffer;

	/* second: setup line vertices */
	for (bbox = list, face = 0, vtxOff = 0; face < list->cont; bbox ++, face ++)
	{
		if (face > 0 && (cnxFlags & (1 << (face-1))) == 0) continue;
		for (i = 0; i < 4; i ++)
		{
			if (bbox == list ? cnxFlags & (1 << i) : i == ((face-1)^2)) continue;
			DATA8 index = bboxIndices + 36 + i * 2;
			/* top and bottom lines */
			*p++ = vtxOff + index[0];
			*p++ = vtxOff + index[1];
			*p++ = vtxOff + index[8];
			*p++ = vtxOff + index[9];
		}
		for (i = 0; i < 4; i ++)
		{
			static uint8_t flags[] = {9, 3, 6, 12};
			static uint8_t discard[] = {0, 2, 0, 0, 1, 3, 3, 1, 2};
			if (bbox == list ? popcount(flags[i] & cnxFlags) == 1 : i == discard[face] || i == discard[face+4]) continue;
			DATA8 index = bboxIndices + 36 + 16 + i * 2;
			*p++ = vtxOff + index[0];
			*p++ = vtxOff + index[1];
		}
		vtxOff += 8;
	}
	return ret | ((p - buffer - ret) << 16);
}

/* fill vertex buffers for selection shader */
int blockGenVertexBBox(BlockState b, VTXBBox box, int flag, int * vbo, int textureCoord, int offsets)
{
	glBindBuffer(GL_ARRAY_BUFFER, vbo[0]);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, vbo[1]);
	vec    vertex = glMapBuffer(GL_ARRAY_BUFFER, GL_READ_WRITE);
	DATA16 index  = glMapBuffer(GL_ELEMENT_ARRAY_BUFFER, GL_WRITE_ONLY);

	enum { PT1X, PT1Y, PT1Z, PT2X, PT2Y, PT2Z, PTU, PTV };
	/* 8 vertices of a VTXBBox */
	static uint8_t vtx[] = {
		PT1X, PT1Y, PT2Z, PTU, PTV,
		PT2X, PT1Y, PT2Z, PTU, PTV,
		PT2X, PT2Y, PT2Z, PTU, PTV,
		PT1X, PT2Y, PT2Z, PTU, PTV,

		PT1X, PT1Y, PT1Z, PTU, PTV,
		PT2X, PT1Y, PT1Z, PTU, PTV,
		PT2X, PT2Y, PT1Z, PTU, PTV,
		PT1X, PT2Y, PT1Z, PTU, PTV,
	};
	int idx;
	float U = ((textureCoord >> 4) * 16 + 8) / 512.;
	float V = ((textureCoord & 15) * 16 + 8) / 1024.;
	uint8_t bbox = blockIds[b->id>>4].bbox;

	index  += offsets & 0xffff; offsets >>= 16;
	vertex += offsets;
	offsets /= 5;

	if (box->aabox == 0 && b->custModel && bbox >= BBOX_FULL)
	{
		/* generate vertex data from custom model */
		int    i, j, k;
		DATA16 p = b->custModel, v, lines;
		int    count = p[-1];
		DATA8  vtxIndex = alloca(count);
		vec    vtxData;

		/* first gather vertex */
		for (i = k = 0, v = (DATA16) vertex; count > 0; count --, p += INT_PER_VERTEX, i ++)
		{
			DATA16 check;
			/* check for unique vertices */
			if (bbox == BBOX_FIRST && (p[4] & NEW_BBOX)) break;
			for (check = (DATA16) vertex, j = 0; check != v && memcmp(check, p, 6); check += 10, j ++);
			if (check == v) memcpy(v, p, 6), v += 10, k ++;
			vtxIndex[i] = j;
		}

		/* convert to float */
		for (i = k, vtxData = vertex; i > 0; i --, vtxData += 5)
		{
			v = (DATA16) vtxData;
			vtxData[2] = (v[2] - ORIGINVTX) * (1. / BASEVTX);
			vtxData[1] = (v[1] - ORIGINVTX) * (1. / BASEVTX);
			vtxData[0] = (v[0] - ORIGINVTX) * (1. / BASEVTX);
			vtxData[3] = U;
			vtxData[4] = V;
		}

		/* adjust vertex data and fill indices buffer */
		for (p = b->custModel, count = p[-1], idx = 0, lines = index + count; count > 0; count -= 6, vtxIndex += 6, p += INT_PER_VERTEX * 6)
		{
			float shift[3], pts[9];

			/* analyze one face at a time: only need 3 points */
			memcpy(pts,   vertex + vtxIndex[0] * 5, 12);
			memcpy(pts+3, vertex + vtxIndex[1] * 5, 12);
			memcpy(pts+6, vertex + vtxIndex[2] * 5, 12);

			pts[3] -= pts[0];   pts[6] -= pts[0];
			pts[4] -= pts[1];   pts[7] -= pts[1];
			pts[5] -= pts[2];   pts[8] -= pts[2];

			/* get normal vector */
			vecCrossProduct(pts, pts+3, pts+6);
			vecNormalize(pts, pts);
			shift[0] = pts[0] * 0.01f;
			shift[1] = pts[1] * 0.01f;
			shift[2] = pts[2] * 0.01f;

			/* shift vertex by normal */
			for (j = 0; j < 4; j ++, lines += 2)
			{
				k = vtxIndex[j];
				vtxData = vertex + k * 5;
				vtxData[0] += shift[0];
				vtxData[1] += shift[1];
				vtxData[2] += shift[2];
				index[j] = lines[0] = k + offsets;
				lines[1] = vtxIndex[(j+1) & 3] + offsets;
			}
			index[4] = vtxIndex[4] + offsets;
			index[5] = vtxIndex[5] + offsets;
			idx += 6 | (8 << 16);
			index += 6;
		}
	}
	else
	{
		/* 1st: fill vertex data */
		VTXBBox  list;
		uint32_t boxes;
		int      i, j, off;
		for (list = box, i = box->cont, boxes = 0; i > 0; i --, box ++)
		{
			idx = box->flags & 0x7f;
			if (idx > 0 && (flag & (1 << (idx - 1))) == 0) continue;
			for (j = 0; j < DIM(vtx); j ++)
			{
				switch (vtx[j]) {
				case PTU: vertex[j] = U; break;
				case PTV: vertex[j] = V; break;
				default:  vertex[j] = (box->pt1[vtx[j]] - ORIGINVTX) * (1. / BASEVTX);
				}
			}
			boxes |= 1 << i;
			vertex += DIM(vtx);
		}

		if ((list->flags & BHDR_FUSED) == 0)
		{
			/* 2nd: indices for face using glDrawElements */
			for (box = list, i = box->cont, off = offsets, idx = 0; i > 0; i --, box ++)
			{
				if ((boxes & (1<<i)) == 0) continue;
				for (j = 0; j < 36; index ++, j ++)
					index[0] = off + bboxIndices[j];
				off += 8;
				idx += 36;
			}
			/* 3rd: indices for lines */
			for (box = list, i = box->cont, off = offsets; i > 0; i --, box ++)
			{
				if ((boxes & (1<<i)) == 0) continue;
				for (j = 0; j < 24; index ++, j ++)
					index[0] = off + bboxIndices[36+j];
				off += 8;
				idx += 24<<16;
			}
		}
		else idx = blockBBoxFuse(b, list, flag, index);
	}
	glUnmapBuffer(GL_ARRAY_BUFFER);
	glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER);

	return idx;
}

/* init static stuff for bounding box */
void blockParseBoundingBox(void)
{
	/* count the vertex/bbox data we'll need */
	BlockState state;
	int        bbox, i, j;
	for (state = blockStates, bbox = 0; state < blockLast; state ++)
	{
		DATA16 p;
		Block  b = blockIds + (state->id >> 4);
		switch (b->bbox) {
		case BBOX_FULL:
			p = state->custModel;
			if (p == NULL || state->ref) continue;
			for (j = p[-1], bbox ++; j > 0; j --, p += INT_PER_VERTEX)
				if (p[4] & NEW_BBOX) bbox ++;
			break;
		case BBOX_FIRST:
		case BBOX_MAX:
			bbox ++;
		}
	}

	bbox += DIM(bboxModels) / 7 + 1;

//	fprintf(stderr, "bbox count = %d (%d bytes)\n", bbox, 2 * bbox * sizeof *blocks.bbox);

	/*
	 * first set is used to render bbox on screen with a slight offset (0.01 unit) to avoid
	 * z-fighting: we will also need to shift vertex, which is a not so trivial operation;
	 * second (bboxExact) will be used for collision without offset.
	 */
	blocks.bbox = calloc(sizeof *blocks.bbox, bbox * 2);
	blocks.bboxExact = blocks.bbox + bbox;

	/* first: generate common bounding boxes */
	for (blocks.bboxMax = 1, j = i = 0; i < DIM(bboxModels); j ++)
	{
		int index = blocks.bboxMax;
		bbox = bboxModels[i];
		i += blockGenCommonBBox(bboxModels + i);
		if (bbox & BHDR_FUSE)
			/* will need special processing when generating mesh */
			blocks.bbox[index].flags |= BHDR_FUSED;
		bboxModels[j] = index;
	}

	/* second: generate model bounding boxes and assign to state bboxId */
	for (state = blockStates; state < blockLast; state ++)
	{
		Block b = blockIds + (state->id >> 4);
		switch (b->bbox) {
		case BBOX_NONE:
			state->bboxId = 0;
			break;
		case BBOX_AUTO:
			j = 0;
			switch (b->type) {
			case SOLID:
				if (b->special == BLOCK_HALF)
					j = (state->id & 15) < 8 ? 1 : 2;
				break;
			case CUST:
				switch (b->special&31) {
				case BLOCK_GLASS:  j = 10; break;
				case BLOCK_RSWIRE: j = 11; break;
				case BLOCK_FENCE:  j = 12; break;
				case BLOCK_WALL:   j = 13; break;
				}
				break;
			case QUAD:
				j = state->pxU;
				if (j > QUAD_SQUARE4)
				{
					if (j > QUAD_ASCE) j = QUAD_ASCE;
					j = (j - QUAD_SQUARE4) + 3;
				}
				else j = 3;
			}
			state->bboxId = bboxModels[j];
			break;
		case BBOX_MAX:
		case BBOX_FULL:
		case BBOX_FIRST:
			if (state->custModel == NULL)
			{
				/* assume full block */
				state->bboxId = bboxModels[0];
				continue;
			}
			if (state->ref > 0)
			{
				/* same as previous block */
				state->bboxId = state[-state->ref].bboxId;
				continue;
			}
			/* get bounding box from custom model */
			state->bboxId = blocks.bboxMax;
			blockGenBBox(state->custModel, state->custModel[-1], b->bbox);
			if (b->special == BLOCK_DOOR)
			{
				/* enlarge Y axis to highlight top and bottom part at the same time */
				blocks.bbox[state->bboxId].pt2[VY] += BASEVTX;
			}
		}
	}
}

/* generate vertex data for CPU (collision detection) */
Bool blockGetBoundsForFace(VTXBBox box, int face, vec4 V0, vec4 V1, vec4 offset, int cnxFlags)
{
	static uint8_t offsets[] = { /* S, E, N, W, T, B */
		0, 1, 2, 1,
		1, 2, 0, 1,
		0, 1, 2, 0,
		1, 2, 0, 0,
		0, 2, 1, 1,
		0, 2, 1, 0
	};

	DATA8 dir = offsets + face * 4;
	uint8_t x = dir[0];
	uint8_t y = dir[1];
	uint8_t z = dir[2];

	V0[3] = V1[3] = 1;
	if (box == NULL)
	{
		/* assume full block */
		V0[x] = offset[x];
		V0[y] = offset[y];
		V0[z] = offset[z] + dir[3];
		V1[x] = offset[x] + 1;
		V1[y] = offset[y] + 1;
		V1[z] = offset[z] + dir[3];
	}
	else
	{
		float   pt[6];
		uint8_t t = z;
		uint8_t cnx = box->flags & 31;
		if (cnx > 0 && (cnxFlags & (1 << (cnx - 1))) == 0)
			return False;

		if (box->flags & BHDR_FUSED)
		{
			if ((cnxFlags & (1 << face)))
				return False;
		}
		else if ((box->sides & (1 << face)) == 0)
		{
			return False;
		}

		if (dir[3]) t += 3;
		/* same as vertex shader items.vsh */
		pt[0] = FROMVERTEX(box->pt1[0]);
		pt[1] = FROMVERTEX(box->pt1[1]);
		pt[2] = FROMVERTEX(box->pt1[2]);

		pt[3] = FROMVERTEX(box->pt2[0]);
		pt[4] = FROMVERTEX(box->pt2[1]);
		pt[5] = FROMVERTEX(box->pt2[2]);

		V0[x] = offset[x] + pt[x];
		V0[y] = offset[y] + pt[y];
		V0[z] = offset[z] + pt[t];

		V1[x] = offset[x] + pt[x+3];
		V1[y] = offset[y] + pt[y+3];
		V1[z] = offset[z] + pt[t];
	}
	return True;
}


/*
 * preview models generation
 */
static int blockGenWireModel(DATA16 buffer, int count)
{
	uint16_t edges[128];
	uint8_t  edgeFace[64];
	DATA16   p, cur, edge, dup, vertex;
	int      total, i, j, faces;

	memset(edgeFace, 0, sizeof edgeFace);
	vertex = alloca(count * BYTES_PER_VERTEX);
	memcpy(vertex, buffer, count * BYTES_PER_VERTEX);

	/* generate mesh wire from mesh triangles */
	for (p = vertex, edge = edges, cur = vertex, faces = total = i = 0; i < count; i += 6, p += 2 * INT_PER_VERTEX)
	{
		uint16_t index[4];
		uint8_t  normal = GET_NORMAL(p);
		for (j = 0; j < 4; j ++, p += INT_PER_VERTEX)
		{
			for (dup = vertex; dup < cur; dup += INT_PER_VERTEX)
				if (dup[0] == p[0] && dup[1] == p[1] && dup[2] == p[2]) break;
			if (dup == cur)
			{
				memmove(cur, p, BYTES_PER_VERTEX);
				CHG_UVCOORD(cur, 31*16+8, 0);
				index[j] = total;
				cur += INT_PER_VERTEX;
				total += INT_PER_VERTEX;
			}
			else index[j] = dup - vertex;
		}
		/* add edge of faces to the list */
		for (j = 0; j < 4; j ++)
		{
			uint16_t p1 = index[j];
			uint16_t p2 = index[(j+1)&3];
			if (p2 < p1) swap(p1, p2);
			for (dup = edges; dup < edge && ! (dup[0] == p1 && dup[1] == p2); dup += 2);
			if (dup == edge)
			{
				edge[0] = p1;
				edge[1] = p2;
				edge += 2;
			}
			uint8_t flag = 1 << normal;
			vertex[p1+4] |= flag;
			vertex[p2+4] |= flag;
			edgeFace[(dup - edges) >> 1] |= flag;
			faces |= flag;
		}
	}

	/* shift vertex */
	for (p = vertex; p < cur; p += INT_PER_VERTEX)
	{
		for (j = p[4], p[4] = 0, i = 0; i < 12; i += 2, j >>= 1)
		{
			static int8_t shift[] = {2, 11, 0, 11, 2, -11, 0, -11, 1, 11, 1, -11};
			if ((j & 1) == 0) continue;
			p[shift[i]] += shift[i+1];
		}
	}

	for (cur = edges, dup = buffer + count * INT_PER_VERTEX, total = i = 0; cur < edge; cur += 2, i ++)
	{
		if (popcount(edgeFace[i]) <= 1) continue;
		memcpy(dup, vertex + cur[0], BYTES_PER_VERTEX); dup[4] |= 0xf000; dup += INT_PER_VERTEX;
		memcpy(dup, vertex + cur[1], BYTES_PER_VERTEX); dup[4] |= 0xf000; dup += INT_PER_VERTEX;
		total += 2;
	}
	return total;
}

/* generate a model compatible with items.vsh for quad blocks */
static int blockModelQuad(BlockState b, DATA16 buffer)
{
	extern uint8_t quadIndices[]; /* from chunks.c */
	extern uint8_t quadSides[];

	DATA8  sides = &b->pxU;
	DATA16 p = buffer;

	do {
		uint8_t i, j, tex, side = quadSides[*sides];
		for (i = 0, j = *sides * 4, tex = b->rotate * 8; i < 4; i ++, j ++, p += INT_PER_VERTEX, tex += 2)
		{
			DATA8 coord = cubeVertex + quadIndices[j];
			int   U = b->nzU;
			int   V = b->nzV;
			if (V == 62 && U < 17) V = 63; /* biome dependent color */
			p[0] = VERTEX(coord[0]);
			p[1] = VERTEX(coord[1]);
			p[2] = VERTEX(coord[2]);
			U = (texCoordRevU[tex]   + U) * 16;
			V = (texCoordRevU[tex+1] + V) * 16;
			if (V == 1024) V = 1023;
			SET_UVCOORD(p, U, V);
			p[4] |= 0xf000;

			if (side < 6 && *sides >= QUAD_NORTH)
			{
				int8_t * normal = cubeNormals + side * 4;
				p[0] += normal[0] * (BASEVTX/16);
				p[1] += normal[1] * (BASEVTX/16);
				p[2] += normal[2] * (BASEVTX/16);
			}
		}
		/* convert quad to triangles */
		memcpy(p,   p - 20, BYTES_PER_VERTEX);
		memcpy(p+5, p - 10, BYTES_PER_VERTEX);
		p += INT_PER_VERTEX*2;
		if (side == 6)
		{
			/* need to add other side to prevent quad from being culled by glEnable(GL_CULLFACE) */
			memcpy(p, p-10, BYTES_PER_VERTEX*2); p += 10;
			memcpy(p, p-35, BYTES_PER_VERTEX);   p += 5;
			memcpy(p, p-30, BYTES_PER_VERTEX);   p += 5;
			memcpy(p, p-25, BYTES_PER_VERTEX*2); p += 10;
		}
		sides ++;
	}
	while (*sides);
	return (p - buffer) / INT_PER_VERTEX;
}

static Bool blockCanBePlacedOnGround(Block b)
{
	if (b->placement > 0)
	{
		DATA8 p = b->name + b->placement;
		int   i;
		for (i = p[0], p ++; i > 0; p += 2, i --)
		{
			int id = (p[0] << 8) | p[1];
			if (id == PLACEMENT_GROUND)
				return True;
		}
		return False;
	}
	return True;
}

/* auto-adjust orient of blocks based on direction/face being pointed */
int blockAdjustOrient(int blockId, BlockOrient info, vec4 inter)
{
	/* these tables will convert SIDE_* enum (info->side) into block data */
	static uint8_t orientFull[]   = {3, 5, 2, 4, 1, 0};
	static uint8_t orientTorch[]  = {3, 1, 4, 2};
	static uint8_t orientLOG[]    = {8, 4, 8, 4, 0, 0};
	static uint8_t orientSE[]     = {0, 1, 0, 1};
	static uint8_t orientStairs[] = {3, 1, 2, 0};
	static uint8_t orientDoor[]   = {7, 3, 1, 5, 2, 4, 6, 0};
	static uint8_t orientLever[]  = {3, 1, 4, 2, 5, 7, 6, 0};
	static uint8_t orientSWNE[]   = {0, 3, 2, 1};
	static uint8_t orientSNEW[]   = {0, 2, 1, 3};
	static uint8_t orientHopper[] = {2, 4, 3, 5};
	extern int8_t  opp[];

	uint8_t side = info->side;
	Block   b    = &blockIds[blockId >> 4];
	blockId &= 0xfff;

	if (b->invState == (blockId & 15))
		blockId &= ~15;

	switch (b->orientHint) {
	case ORIENT_FULL:
		return blockId + orientFull[side];
	case ORIENT_BED:
		/* only use direction (note: blockId contains color) */
		return (blockId & ~15) | orientSWNE[info->direction] | ((blockId & 15) << 12);
	case ORIENT_NSWE:
		if (side >= 4) side = opp[info->direction];
		return blockId + orientFull[side];
	case ORIENT_SWNE:
		if (blockCanBePlacedOnGround(b))
			side = opp[info->direction];
		else
			side = opp[side];
		if (b->special == BLOCK_FENCEGATE)
			side = opp[side];
		return blockId + orientSWNE[side];
	case ORIENT_RAILS:
		if (side >= 4) side = opp[info->direction];
		return blockId + orientSE[side];
	case ORIENT_LOG:
		if ((blockId & 15) >= 12) return blockId;
		return blockId + orientLOG[side];
	case ORIENT_SLAB:
		if (side == SIDE_TOP && (info->pointToId & ~8) == (blockId & ~8))
		{
			/* add slab on top of same slab: convert to double-slab block */
			info->keepPos = 1;
			return blockId - 16;
		}
		return blockId + (info->topHalf ? 8 : 0);
	case ORIENT_STAIRS:
		if (side >= 4) side = opp[info->direction];
		side = orientStairs[side];
		if (info->topHalf) side += 4;
		return blockId + side;
	case ORIENT_TORCH:
		if (side == 5) return 0;
		if (side == 4) return blockId+5;
		return blockId + orientTorch[side];
	case ORIENT_DOOR:
		side = (fabs(inter[VX] - (int) inter[VX]) <= 0.5 ? 1 : 0) | (fabs(inter[VZ] - (int) inter[VZ]) <= 0.5 ? 2 : 0);
		return (blockId & ~15) | orientDoor[info->direction&1 ? side+4 : side];
		break;
	case ORIENT_LEVER:
		/* 8 possible orientations */
		if (info->side >= 4 && (info->direction & 1))
			side = orientLever[info->side + 2];
		else
			side = orientLever[info->side];
		if (strstr(b->tech, "button") && side >= 6)
			side = side == 7 ? 0 : 5;
		return (blockId & ~15) | side;
		break;
	case ORIENT_SNOW:
		if ((blockId >> 4) == (info->pointToId >> 4) && (blockId & 7) < 7)
		{
			/* add one layer instead */
			info->keepPos = 1;
			return info->pointToId + 1;
		}
		break;
	case ORIENT_HOPPER:
		if (side == SIDE_TOP || side == SIDE_BOTTOM)
			return (blockId & ~15);
		return (blockId & ~15) | orientHopper[side];
	default:
		/* special block placement */
		switch (b->special) {
		case BLOCK_TRAPDOOR:
			return (blockId & ~15) | orientSNEW[info->side < 4 ? opp[info->side] : info->direction] | (info->topHalf || info->side == 5 ? 8 : 0);
		case BLOCK_SIGN:
			if (side >= 4)
			{
				int data = (info->yaw + M_PIf/32) / (M_PIf/8);
				if (data < 0)  data += 16; else
				if (data > 15) data -= 16;
				return ID(63, (data+4) & 15);
			}
			else return blockId + orientFull[side];
		}
	}
	return blockId;
}

/* blockId is not supposed to be an inventory item: adjust it */
int blockAdjustInventory(int blockId)
{
	BlockState b;
	switch (blockIds[blockId >> 4].orientHint) {
	case ORIENT_LOG:
		return  4 <= (blockId & 15) && (blockId & 15) < 12 ? blockId & ~12 : blockId;
	case ORIENT_SLAB:
		return blockId & ~8; /* cancel top slab */
	default:
		for (blockId &= ~15, b = blockGetById(blockId); (b->id & ~15) == blockId && b->inventory == 0; b ++);
		if ((b->id & ~15) != blockId) return 0;
		return b->id;
	}
}

/* check if block <blockId> is attached to given side (SIDE_*) */
Bool blockIsAttached(int blockId, int side, Bool def)
{
	/* note: side is relative to current block, not neighbor */
	Block b = &blockIds[blockId >> 4];

	switch (b->orientHint) {
	case ORIENT_TORCH:
		return blockSides.torch[blockId&7] == side;
	case ORIENT_LEVER:
		return blockSides.lever[blockId&7] == side;
	case ORIENT_SWNE:
		return blockSides.SWNE[blockId&3] == side;
	default:
		switch (b->special) {
		case BLOCK_RSWIRE:
			return side == SIDE_BOTTOM;
		case BLOCK_SIGN:
			return blockSides.sign[blockId&7] == side;
		}
	}
	return def;
}

Bool blockIsSolidSide(int blockId, int side)
{
	Block b = &blockIds[blockId >> 4];
	if (b->type == SOLID)
	{
		/* check for stairs or slab */
		static uint8_t defOrient[] = {2, 1, 3, 0};
		switch (b->special) {
		case BLOCK_HALF:
			/* top slab and inverted stairs have flat ground */
			switch (side) {
			case SIDE_TOP:    return (blockId&15) >= 8;
			case SIDE_BOTTOM: return (blockId&15) <  8;
			default:          return False;
			}
		case BLOCK_STAIRS:
			switch (side) {
			case SIDE_TOP:    return (blockId&15) >= 8;
			case SIDE_BOTTOM: return (blockId&15) <  8;
			default:          return (blockId&3)  == defOrient[side];
			}
		default: return True;
		}
	}
	return False;
}

static void fillVertex(DATA16 face, DATA16 dest, int axis)
{
	static uint8_t axis1[] = {0, 2, 0, 2, 0, 0};
	static uint8_t axis2[] = {1, 1, 1, 1, 2, 2};

	uint8_t a1 = axis1[axis];
	uint8_t a2 = axis2[axis];

	dest[0] = face[a1];
	dest[1] = face[a1+INT_PER_VERTEX*2];
	dest[2] = face[a2];
	dest[3] = face[a2+INT_PER_VERTEX*2];
	if (dest[1] < dest[0]) dest[0] = dest[1], dest[1] = face[a1];
	if (dest[3] < dest[2]) dest[2] = dest[3], dest[3] = face[a2];
}

/* check if vertices from <face> would be hidden by block on <side> */
Bool blockIsSideHidden(int blockId, DATA16 face, int side)
{
	BlockState state = blockGetById(blockId);
	switch (state->type) {
	case SOLID: return state->special != BLOCK_HALF && state->special != BLOCK_STAIRS;
	case TRANS:
	case INVIS:
	case QUAD:  return False;
	case CUST:
		if (state->custModel)
		{
			extern int8_t   opp[];
			extern uint8_t  axisCheck[];
			extern uint16_t axisAlign[];
			/* be a bit more aggressive with custom models */
			uint16_t bounds1[4];
			uint16_t bounds2[4];
			DATA16   model;
			int      count;
			fillVertex(face, bounds1, opp[side]);
			/* need to analyze vertex data */
			for (model = state->custModel, count = model[-1]; count > 0; count -= 6, model += INT_PER_VERTEX * 6)
			{
				uint8_t norm = GET_NORMAL(model);
				if (norm != side || model[axisCheck[norm]] != axisAlign[norm]) continue;
				fillVertex(model, bounds2, side);
				/* <face> is covered by neighbor: discard */
				if (bounds2[0] <= bounds1[0] && bounds2[2] <= bounds1[2] &&
				    bounds2[1] >= bounds1[1] && bounds2[3] >= bounds1[3])
					return True;
			}
			return False;
		}
		/* else assume SOLID */
	default:
		return True;
	}
}

int blockAdjustPlacement(int blockId, BlockOrient info)
{
	Block b = &blockIds[blockId >> 4];
	Block d = &blockIds[info->pointToId >> 4];
	DATA8 p = b->name + b->placement;
	char  check = 0;
	char  i;

	for (i = p[0], p ++; i > 0; i --, p += 2)
	{
		int id = (p[0] << 8) | p[1];
		switch (id) {
		case PLACEMENT_GROUND:
			check |= 3;
			if (info->side != 4) break;
			check |= 4;
			if (blockIsSolidSide(info->pointToId, SIDE_TOP))
				return PLACEMENT_OK;
			break;
		case PLACEMENT_WALL:
			check |= 2;
			if (info->side >= 4) /* top or bottom side */
				continue;
			check |= 4;

			if (d->type == SOLID)
			{
				if (d->special == BLOCK_STAIRS)
				{
					static uint8_t sides[] = {3, 0, 2, 1};
					if (sides[info->side] == (id & 3))
						return PLACEMENT_OK;
				}
				else if (d->special != BLOCK_HALF)
				{
					return PLACEMENT_OK;
				}
			}
		case PLACEMENT_SOLID:
			return blockIsSolidSide(info->pointToId, info->side);
		default:
			if ((check & 6) != 2 && d->id == (id >> 4))
				return PLACEMENT_OK;
		}
	}
	/* if pointing to the side of a block, check if there is a ground nearby */
	if ((check & 1) && info->side < 4)
		return PLACEMENT_GROUND;
	return PLACEMENT_NONE;
}


/* need to build model from 2 blocks, and pick the right color and orientation */
static int blockModelBed(DATA16 buffer, int blockId)
{
	BlockState b = blockGetById(blockId & 0xfff);

	/* blockId >> 12 == color from 0 to 15, faceId varies from 1 to 16 */
	return blockInvCopyFromModel(buffer, b->custModel, 1 << (blockId >> 12));
}

/* convert between terrain vertex to model vertetx */
static int blockConvertVertex(DATA32 source, DATA32 end, DATA16 dest, int max)
{
	int i;
	for (i = 0; source < end; source += VERTEX_INT_SIZE, i += 6, dest += INT_PER_VERTEX*6, max -= INT_PER_VERTEX*6)
	{
		if (max < INT_PER_VERTEX*6)
			return 0;

		/* yep, tedious busy work :-/ */
		uint16_t U2  = bitfieldExtract(source[5], 16, 8);
		uint16_t V2  = bitfieldExtract(source[5], 24, 8);
		uint16_t U1  = bitfieldExtract(source[4], 14, 9);
		uint16_t V1  = bitfieldExtract(source[4], 23, 9) | (bitfieldExtract(source[1], 30, 1) << 9);
		uint8_t  Xeq = bitfieldExtract(source[5], 12, 1);
		uint16_t rem = bitfieldExtract(source[5],  9, 3) << 3;

		rem |= 0xf000; /* sky/block light */
		U2 = U1 + U2 - 128;
		V2 = V1 + V2 - 128;
		dest[0] = source[0];
		dest[1] = source[0] >> 16;
		dest[2] = source[1];
		if (Xeq) SET_UVCOORD(dest, U1, V2);
		else     SET_UVCOORD(dest, U2, V1);
		dest[4] |= rem;

		dest[5] = dest[0] + bitfieldExtract(source[1], 16, 14) - MIDVTX;
		dest[6] = dest[1] + bitfieldExtract(source[2],  0, 14) - MIDVTX;
		dest[7] = dest[2] + bitfieldExtract(source[2], 14, 14) - MIDVTX;
		SET_UVCOORD(dest+5, U1, V1);
		dest[9] |= rem;

		dest[10] = dest[0] + bitfieldExtract(source[3],  0, 14) - MIDVTX;
		dest[11] = dest[1] + bitfieldExtract(source[3], 14, 14) - MIDVTX;
		dest[12] = dest[2] + bitfieldExtract(source[4],  0, 14) - MIDVTX;
		SET_UVCOORD(dest+10, U2, V2);
		dest[14] |= rem;

		memcpy(dest + 15, dest + 10, BYTES_PER_VERTEX);
		memcpy(dest + 20, dest + 5,  BYTES_PER_VERTEX);

		dest[25] = dest[10] + dest[5] - dest[0];
		dest[26] = dest[11] + dest[6] - dest[1];
		dest[27] = dest[12] + dest[7] - dest[2];
		if (Xeq) SET_UVCOORD(dest+25, U2, V1);
		else     SET_UVCOORD(dest+25, U1, V2);
		dest[29] |= rem;
	}
	return i;
}

int blockModelStairs(DATA16 buffer, int blockId)
{
	uint32_t temp[VERTEX_INT_SIZE * 30];
	uint16_t blockIds3x3[27];
	uint8_t  pos[] = {0, 0, 0};

	struct WriteBuffer_t write = {
		.start = temp, .cur = temp, .end = EOT(temp)
	};
	BlockState b = blockGetById(blockId);
	memset(blockIds3x3, 0, sizeof blockIds3x3);
	blockIds3x3[13] = blockId;
	halfBlockGenMesh(&write, halfBlockGetModel(b, 2, blockIds3x3), 2, pos, b, blockIds3x3, (DATA8) blockIds3x3, 63);

	return blockConvertVertex(temp, write.cur, buffer, 300);
}

/* generate vertex data for any block (compatible with item.vsh) */
int blockGenModel(int vbo, int blockId)
{
	BlockState b = blockGetById(blockId & 0xfff);
	DATA16     buffer;
	int        vtx, i;

	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	buffer = glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
	vtx = 0;

	switch (b->type) {
	case SOLID:
	case TRANS:
		if (b->special == BLOCK_STAIRS)
			vtx = blockModelStairs(buffer, blockId);
		else
			vtx = blockInvModelCube(buffer, b, texCoord);
		break;
	case CUST:
		if (b->custModel)
		{
			switch (SPECIALSTATE(b)) {
			case BLOCK_GLASS:
				/* only grab center piece */
				vtx = blockInvCopyFromModel(buffer, b->custModel, 63 << 12);
				break;
			case BLOCK_BED:
				/* only grab one color */
				vtx = blockModelBed(buffer, blockId);
				break;
			case BLOCK_RSWIRE:
				/* grab center */
				vtx = blockInvCopyFromModel(buffer, b->custModel, 1 << 8);
				break;
			case BLOCK_FENCE:
			case BLOCK_FENCE2:
			case BLOCK_WALL:
				/* only center piece */
				vtx = blockInvCopyFromModel(buffer, b->custModel, 0);
				break;
			case BLOCK_CHEST:
				vtx = blockInvCopyFromModel(buffer, b->custModel, 1);
				break;
			case BLOCK_SOLIDOUTER:
				vtx = blockInvCopyFromModel(buffer, b->custModel, ALLFACEIDS);
				vtx += blockInvModelCube(buffer + vtx * INT_PER_VERTEX, b, texCoord);
				break;
			case BLOCK_DOOR:
				/* generate bottom and top part */
				vtx = i = blockInvCopyFromModel(buffer, b->custModel, ALLFACEIDS);
				vtx += blockInvCopyFromModel(buffer + vtx * INT_PER_VERTEX, b[8].custModel, ALLFACEIDS);
				/* shift top part 1 block up */
				for (buffer += i * INT_PER_VERTEX; i < vtx; buffer[1] += BASEVTX, i ++, buffer += INT_PER_VERTEX);
				buffer -= vtx * INT_PER_VERTEX;
				break;
			default:
				vtx = blockInvCopyFromModel(buffer, b->custModel, ALLFACEIDS);
			}
		}
		else vtx = blockInvModelCube(buffer, b, texCoord);
		break;
	case QUAD:
		vtx = blockModelQuad(b, buffer);
	}

	i = b->type != QUAD ? blockGenWireModel(buffer, vtx) : 0;

	glUnmapBuffer(GL_ARRAY_BUFFER);

	return vtx | (i << 10);
}

/*
 * Tile entity for common blocks
 */
DATA8 blockCreateTileEntity(int blockId, vec4 pos, APTR arg)
{
	TEXT      itemId[64];
	NBTFile_t ret = {.page = 127};
	int       id  = blockId & 0xfff;
	Block     b   = &blockIds[id >> 4];
	int       i;

	if (! b->tileEntity)
		return NULL;

	/* standard fields for tile entity */
	NBT_Add(&ret,
		TAG_String, "id", itemGetTechName(id, itemId, sizeof itemId, False),
		TAG_Int,    "x",  (int) pos[VX],
		TAG_Int,    "y",  (int) pos[VY],
		TAG_Int,    "z",  (int) pos[VZ],
		TAG_End
	);

	switch (b->special) {
	case BLOCK_BED:
		NBT_Add(&ret, TAG_Int, "color", blockId >> 12, TAG_End);
		break;
	case BLOCK_SIGN:
		for (i = 0; i < 4; i ++)
		{
			TEXT   prop[6];
			STRPTR text = arg ? ((STRPTR *)arg)[i] : NULL;
			if (! text) continue;
			strcpy(prop, "text1");
			prop[4] = i + '1';
			NBT_Add(&ret, TAG_String, prop, text, TAG_End);
		}
		break;
	default:
		if (b->id == RSCOMPARATOR)
			NBT_Add(&ret, TAG_Int, "OutputSignal", 0, TAG_End);
	}
	NBT_Add(&ret, TAG_Compound_End);

	return ret.mem;
}

/*
 * terrain.png post processing
 */
static void texset(DATA8 dest, DATA8 px, int size)
{
	uint32_t s;
	DATA32   d;
	for (d = (DATA32) dest, s = * (DATA32) px; size > 0; size -= 4, *d++ = s);
}

/* post-process main texture terrain.png: generate connected texture for glass */
void blockPostProcessTexture(DATA8 * data, int * width, int * height, int bpp)
{
	int   w   = *width;
	int   h   = *height;
	DATA8 dst = realloc(*data, w * bpp * h * 2); /* not enough space in terrain.png :-/ */
	DATA8 s, d;
	int   i, j, k, sz = w / 32, stride = w * bpp;

	if (dst == NULL) return;

	blockTexResol = sz;

	/* copy tex 31, 31 in the bottom half (it is an unused tex: if something goes wrong we will get something predictable) */
	for (i = 0, *data = dst, *height = h*2, sz *= bpp, s = dst + 31 * sz * w + 31 * sz, d = dst + stride * h; i < sz; i += bpp, s += stride)
	{
		/* copy scanlines */
		for (j = 0; j < 32; j ++, memcpy(d, s, sz), d += sz);
	}

	/* copy the remaining 31 lines of textures */
	for (i = 1, k = d - (dst + stride * h); i < 32; i ++, d += k)
		memcpy(d, d - k, k);

	/* check which texture are translucent: will need to be rendered in a second pass */
	uint8_t alphaFlags[128]; /* 1024 bits = 32x32 textures */
	memset(alphaFlags, 0, sizeof alphaFlags);
	for (j = k = 0; j < 32; j ++)
	{
		for (i = 0; i < 32; i ++, k ++)
		{
			int x, y;
			s = dst + sz * i + j * sz * w;
			for (y = 0; y < sz; y += bpp, s += stride)
			{
				for (x = 3; x < sz; x += bpp)
				{
					uint8_t alpha = s[x];
					if (8 < alpha && alpha < 248)
					{
						alphaFlags[k>>3] |= 1 << (k&7);
						y = sz;
						break;
					}
				}
			}
		}
	}

	/* mark blocks that will require a 2nd pass rendering */
	BlockState state;
	for (state = blockStates; state < blockLast; state ++)
	{
		if (state->type == QUAD) continue;
		/* unlikely that only one side is translucent */
		uint8_t U = state->nzU;
		uint8_t V = state->nzV;
		if (V > 31)
		{
			/* relocated texture */
			DATA8 cnxtex;
			for (cnxtex = blocks.cnxTex, j = blocks.cnxCount; j > 0; j -- , cnxtex += 4)
			{
				if (cnxtex[2] == U && cnxtex[3] == V)
				{
					U = cnxtex[0]; V = cnxtex[1];
					break;
				}
			}
			if (V > 31) continue;
		}
		j = U + V * 32;
		if (alphaFlags[j>>3] & (1 << (j&7)))
			state->rotate |= ALPHATEX;
	}

	/* copy biome dependent tex to bottom of texture map */
	for (s = biomeDepend, d = dst + stride * 62 * sz / bpp; s < EOT(biomeDepend); s += 2, d += sz)
	{
		/* this will free 1 (ONE) bit for the vertex shader */
		DATA8 s2 = dst + s[0] * sz + s[1] * stride * sz / bpp;
		DATA8 d2 = d;
		/* but given how packed information is, this is worth it */
		for (i = 0; i < sz; i += bpp, memcpy(d2, s2, sz), d2 += stride, s2 += stride);

		/* copy texture using a default biome color just below (for inventory textures) */
		for (i = 0, s2 -= sz * w; i < sz; i += bpp, d2 += stride, s2 += stride)
		{
			memcpy(d2, s2, sz);
			DATA8 col;
			for (col = d2, j = sz; j > 0; j -= bpp, col += bpp)
			{
				if (col[0] == col[1] && col[1] == col[2])
				{
					col[0] = col[0] * 105 / 255;
					col[1] = col[1] * 196 / 255;
					col[2] = col[2] *  75 / 255;
				}
			}
		}
	}

	/*
	 * generate connected texture for various glass types
	 * this information was gathered in blockParseConnectedTexture()
	 */
	for (i = 0; i < blocks.cnxCount; i ++)
	{
		uint8_t empty[4];
		DATA8   cnx = blocks.cnxTex + i * 4;
		s = dst + cnx[0] * sz + cnx[1] * w * sz;
		d = dst + cnx[2] * sz + cnx[3] * w * sz;
		/* grab pixel 1,1 to use it as a border eraser */
		memcpy(empty, s + stride + 4, 4);
		/* XXX not resolution independent */
		for (j = 15; j >= 0; j --, d += sz)
		{
			DATA8 s2, d2;
			/* copy texture at destination */
			for (k = 0, s2 = s, d2 = d; k < sz; k += bpp, s2 += stride, d2 += stride)
			{
				memcpy(d2, s2, sz);
				if (k > 0 && k < sz-bpp)
				{
					if ((j & 8) == 0) texset(d2, empty, bpp);
					if ((j & 2) == 0) texset(d2+sz-bpp, empty, bpp);
				}
			}
			/* and remove part to simulate connection */
			d2 -= stride;
			if ((j & 1) == 0) texset(d+bpp,  empty, sz - bpp*2);
			if ((j & 4) == 0) texset(d2+bpp, empty, sz - bpp*2);
			/* corners */
			if ((j & 9) == 0)  texset(d,         empty, bpp);
			if ((j & 3) == 0)  texset(d+sz-bpp,  empty, bpp);
			if ((j & 6) == 0)  texset(d2+sz-bpp, empty, bpp);
			if ((j & 12) == 0) texset(d2,        empty, bpp);
		}
	}

	/* also load item texture */
	DATA8 image = stbi_load(RESDIR "items.png", &w, &h, &bpp, 4);

	/* image must be 16x15 tiles, using the same resolution than terrain.png */
	if (sz == (w / 16) * bpp && sz == (h / 14) * bpp)
	{
		/* it is the size we expect, copy into tex */
		for (s = image, k = w * bpp, d = dst + (ITEM_ADDTEXV * sz * *width) + ITEM_ADDTEXU * sz; h > 0; h --, s += k, d += stride)
			memcpy(d, s, k);
	}

	free(image);

	/* and paintings texture */
	image = stbi_load(RESDIR "paintings.png", &w, &h, &bpp, 4);

	/* image must be 16x9 tiles */
	if (sz == (w / PAINTINGS_TILE_W) * bpp && sz == (h / PAINTINGS_TILE_H) * bpp)
	{
		for (s = image, k = w * bpp, d = dst + ((PAINTINGS_TILE_Y) * sz * *width) + PAINTINGS_TILE_X * sz; h > 0; h --, s += k, d += stride)
			memcpy(d, s, k);
	}

	free(image);

	/* convert alpha part of texture into a bitmap */
	w = *width;
	h = *height;
	blocks.alphaTex = calloc((w+7) >> 3, h*2);

	for (d = blocks.alphaTex, s = dst, j = h, k = blocks.alphaStride = (w+7) >> 3; j > 0; j --, d += k)
	{
		for (i = 0; i < w; i ++, s += bpp)
			if (s[3] >= 248) d[i>>3] |= mask8bit[i&7];
	}

	/* durability colors: located in tile 31, 3 */
	blocks.duraColors = malloc(sz);
	blocks.duraMax    = sz >> 2;
	memcpy(blocks.duraColors, dst + 31 * sz + 3 * sz * *width, sz);
}

/* extract texture alpha for U, V coord from <terrain.png> */
Bool blockGetAlphaTex(DATA8 bitmap, int U, int V)
{
	/* <bitmap> must be at least blockTexResol x blockTexResol bytes */
	DATA8 src;
	int   i, j;
	if (0 <= U && U < 31 && 0 <= V && V < 63)
	{
		for (j = blockTexResol, U *= j, V *= j, src = blocks.alphaTex + V * blocks.alphaStride; j > 0; j --, src += blocks.alphaStride)
		{
			for (i = 0; i < blockTexResol; i ++, bitmap ++)
				bitmap[0] = src[(i+U)>>3] & mask8bit[i&7] ? 255 : 0;
		}
		return True;
	}
	return False;
}

/*
 * particles emitter location: max height from model
 */
void blockGetEmitterLocation(int blockId, float loc[3])
{
	Block b = &blockIds[blockId >> 4];
	if (b->emitters)
	{
		/* custom emitters location */
		DATA8 bbox = b->emitters + (blockId & 15);
		if (bbox[0] > 0)
		{
			bbox += bbox[0];
			loc[0] = RandRange(bbox[0], bbox[3]) * 0.0625f;
			loc[1] = RandRange(bbox[1], bbox[4]) * 0.0625f;
			loc[2] = RandRange(bbox[2], bbox[5]&31) * 0.0625f;
			return;
		}
	}

	/* use first bounding box */
	BlockState state = blockGetById(blockId);

	VTXBBox bbox = blocks.bbox + state->bboxId;

	loc[0] = (RandRange(bbox->pt1[0], bbox->pt2[0]) - ORIGINVTX) * (1.f/BASEVTX);
	loc[2] = (RandRange(bbox->pt1[2], bbox->pt2[2]) - ORIGINVTX) * (1.f/BASEVTX);
	loc[1] = (bbox->pt2[1] - ORIGINVTX) * (1.f/BASEVTX);
}


/* check if the 4 surounding blocks (S, E, N, W) are of the same as <type> */
int blockGetConnect4(DATA16 neighbors, int type)
{
	static uint8_t stairsOrient[] = { /* introduced in 1.12 */
		8, 2, 4, 1, 8, 2, 4, 1
	};
	int i, ret = 0;
	/* neighbors need to be ordered S, E, N, W */
	for (i = 1; i < 16; i <<= 1, neighbors ++)
	{
		BlockState nbor = blockGetById(neighbors[0]);
		uint8_t    spec = nbor->special;
		if (spec == BLOCK_STAIRS)
		{
			if (stairsOrient[nbor->id&7] == i)
				ret |= i;
		}
		else if (spec != BLOCK_HALF && ((nbor->type == SOLID && (spec & BLOCK_NOCONNECT) == 0) || SPECIALSTATE(nbor) == type))
		{
			ret |= i;
		}
	}
	return ret;
}

/* check which parts (S, E, N, W) a redstone wire connect to */
static int blockConnectRedstone(int blockId, DATA16 neighbors)
{
	/* indexed by connected info */
	static uint8_t straight[] = {0, 1, 2, 0, 1, 1, 0, 0, 2, 0, 2, 0, 0, 0, 0, 0};
	int i, ret;

	/* bottom part */
	for (i = 1, ret = 0; i < 16; i <<= 1, neighbors ++)
	{
		if ((neighbors[0] >> 4) != blockId) continue;
		Block b = &blockIds[neighbors[5] >> 4];
		if (b->type != SOLID || b->special == BLOCK_HALF)
			/* slab allow power to go down, but not up */
			ret |= i;
	}

	/* middle part */
	for (i = 1, neighbors ++; i < 16; i <<= 1, neighbors ++)
	{
		static uint8_t validOrientFB[] = {0, 1, 0, 0, 0, 0, 0, 1};
		static uint8_t validOrientBO[] = {3, 5, 0, 2, 0, 0, 0, 4};
		switch (blockIds[neighbors[0] >> 4].rswire) {
		case ALLDIR:
			ret |= i;
			break;
		case FRONTBACK: /* repeater */
			if ((neighbors[0]&1) == validOrientFB[i-1])
				ret |= i;
			break;
		case BACKONLY:  /* observer */
			if ((neighbors[0]&7) == validOrientBO[i-1])
				ret |= i;
		}
	}
	/* top part */
	if (blockIds[neighbors[4]>>4].type != SOLID)
	{
		for (i = 1; i < 16; i <<= 1, neighbors ++)
			if ((neighbors[0] >> 4) == blockId) ret |= i|(i<<4);
	}

	/* connected to 1 direction or 2 straight parts (N/S or E/W) */
	i = straight[ret&15];
	if (i > 0)
	{
		/* use straight line instead */
		ret &= ~15;
		return ret | (1 << (8 + i));
	}
	else return ret | 256;
}

int blockGetConnect(BlockState b, DATA16 neighbors)
{
	BlockState nbor;
	int        ret = 0, type = b->special;
	int        middle;
	switch (type) {
	case BLOCK_CHEST: /* 4 neighbors */
		/* use orientation to check connection */
		ret = 1;
		type = b->id >> 4;
		if ((b->id & 15) < 4)
		{
			/* oriented N/S */
			if ((neighbors[3] >> 4) == type) ret = 2; else
			if ((neighbors[1] >> 4) == type) ret = 4;
		}
		else /* oriented E/W */
		{
			if ((neighbors[2] >> 4) == type) ret = 4; else
			if ((neighbors[0] >> 4) == type) ret = 2;
		}
		if (ret > 1 && (b->id & 1)) ret = 6-ret;
		break;
	case BLOCK_FENCE:
	case BLOCK_FENCE2: /* 4 neighbors */
		return blockGetConnect4(neighbors, type);
	case BLOCK_WALL: /* 5 neighbors */
		ret = blockGetConnect4(neighbors, type);
		/* if not connected to exactly 2 walls, drawn a bigger center piece */
		if ((ret != 5 && ret != 10) || neighbors[4] > 0)
			ret |= 16;
//		nbor = blockGetById(neighbors[4]);
//		if (nbor->type == SOLID)
//			ret |= 32; /* do some face culling */
		break;
	case BLOCK_GLASS: /* 12 bit parts - glass pane/iron bars */
		/* middle: bit4~7 */
		middle = blockGetConnect4(neighbors+5, type);
		/* bottom: bit0~3. neighbors[4] == block below <b> */
		nbor = blockGetById(neighbors[4]);
		ret = (nbor->special == type ? blockGetConnect4(neighbors, type) ^ 15 : 15) & middle;
		/* bottom center piece cap */
		if (nbor->special != type) ret |= 1<<17;
		/* top: bit8~11, neighbors[13] == block above <b> */
		nbor = blockGetById(neighbors[13]);
		ret |= ((nbor->special == type ? blockGetConnect4(neighbors+9, type) ^ 15 : 15) & middle) << 8;
		ret |= middle << 4;
		/* top center piece cap */
		if (nbor->special != type) ret |= 1<<16;

		/* center piece sides (bit12~15: SENW) */
		for (middle = 1<<12, neighbors += 5; middle < (1<<16); middle <<= 1, neighbors ++)
			if (neighbors[0] != b->id) ret |= middle;
		break;
	case BLOCK_RSWIRE:
		ret = blockConnectRedstone(b->id >> 4, neighbors);
	}
	return ret;
}

int blockInvGetModelSize(int glInvId)
{
	DATA16 off = blocks.invModelOff + glInvId;

	return ((off[1] - off[0]) << 20) | off[0];
}

DATA8 blockGetDurability(float dura)
{
	if (dura < 0)
		return blocks.duraColors;

	return blocks.duraColors + ((int) (blocks.duraMax * dura) << 2);
}

