/*
 * blockParse.c : handle the parsing of blockTable.js
 *
 * written by T.Pierron, nov 2020.
 */

#include <string.h>
#include <stddef.h>
#include <stdlib.h>
#include <ctype.h>
#include <malloc.h>
#include <math.h>
#include "blocks.h"
#include "items.h"
#include "SIT.h"

struct Block_t        blockIds[256];
struct BlockState_t * blockStates;
struct BlockState_t * blockLast;
struct BlockPrivate_t blocks;
static BlockVertex    blockVertex;
static BlockVertex    stringPool;

#define STRICT_PARSING     /* check for misspelled property */

uint16_t blockStateIndex[256*16]; /* used by blockGetById() */

/* how many arguments each BHDR_* tag takes (255 == variable length) */
uint8_t modelTagArgs[] = {0, 1, 0, 0, 0, 3, 3, 3, 3, 3, 1, 255, 0, 0, 1, 0, 1, 2};




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
		if (faceId == 0xff00 || (vertex[4] & 0x7f00) == faceId)
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
	int i, arg, vertex, faces;
	for (i = vertex = faces = 0; i < count; i += arg+1)
	{
		arg = vert[i];
		if ((arg & 0xff) >= BHDR_MAXTOK) return 0;
		switch (arg & 0xff) {
		case BHDR_FACES:
			faces = vert[i+1];
		case BHDR_DUALSIDE:
			vertex += popcount(faces & 63) * 6;
			// no break;
		default:
			arg = modelTagArgs[arg];
			break;
		case BHDR_TEX:
			arg >>= 8;
		}
	}
	return vertex;
}

/*
 * main function to generate vertex data from TileFinder numbers
 * (per-parsed by blockParseModelJSON() though)
 */
DATA16 blockParseModel(float * values, int count, DATA16 buffer, int forceRot90)
{
	float * vert;
	float * eof;
	float * tex;
	float   refRC[3];
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
	memset(refRC, 0, sizeof refRC);

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
		vec     angles;
		int     idx;
		uint8_t nbRot, inv, detail, resetRC, center, dualside;
		mat4    rotation, rot90, tmp;

		if (vert[0] != BHDR_FACES) break;
		faces    = vert[1];
		vert    += 2;
		inv      = 0;
		center   = 1;
		nbRot    = 0;
		resetRC  = 0;
		angles   = NULL;
		detail   = BHDR_CUBEMAP;
		dualside = 0;
		matIdent(rotation);
		matIdent(rot90);
		trans[VX] = trans[VY] = trans[VZ] = -0.5f;
		memset(size,  0, sizeof size);
		/* get all the information about one primitive */
		while (vert < eof && vert[0] != BHDR_FACES)
		{
			switch ((int) vert[0] & 0xff) {
			case BHDR_CUBEMAP:  detail = BHDR_CUBEMAP; break;
			case BHDR_DETAIL:   detail = BHDR_DETAIL; break;
			case BHDR_INHERIT:  detail = BHDR_INHERIT; break;
			case BHDR_INCFACE:  faceId += 1<<8; resetRC = 1; break;
			case BHDR_INVERT:   inv = True; break;
			case BHDR_ROT90:    if (forceRot90 < 0) rot90step = vert[1] / 90; break;
			case BHDR_DUALSIDE: dualside = 1; break;
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
				angles = vert + 1;
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
		if (resetRC)
			matIdent(rotCascade), rotCas = 0;
		if (angles)
		{
			for (i = 0; i < 3; i ++)
			{
				float v = angles[i];
				if (v != 0)
				{
					matRotate(tmp, v * DEG_TO_RAD, i);
					matMult(rotCascade, rotCascade, tmp);
					if (rotCas == 0 && ! center)
					{
						refRC[VX] = trans[VX+3] - 0.5f;
						refRC[VY] = trans[VY+3] - 0.5f;
						refRC[VZ] = trans[VZ+3] - 0.5f;
					}
					rotCas ++;
				}
			}
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
				{
					vecSub(coord, coord, refRC);
					matMultByVec3(coord, rotCascade, coord);
					vecAdd(coord, coord, refRC);
				}
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

			if (dualside)
			{
				/* need to add other side to prevent quad from being culled by glEnable(GL_CULLFACE) */
				memcpy(p, p-2*INT_PER_VERTEX, 2*BYTES_PER_VERTEX); p += 2*INT_PER_VERTEX;
				memcpy(p, p-7*INT_PER_VERTEX,   BYTES_PER_VERTEX); p += INT_PER_VERTEX;
				memcpy(p, p-6*INT_PER_VERTEX,   BYTES_PER_VERTEX); p += INT_PER_VERTEX;
				memcpy(p, p-5*INT_PER_VERTEX, 2*BYTES_PER_VERTEX); p += 2*INT_PER_VERTEX;
			}
		}
		/* marks the beginning of a new primitive (only needed by bounding box) */
		if (start > out) start[4] |= NEW_BBOX;
	}
	return out;
}

/* some blocks are just retextured from other models: too tedious to copy vertex data all over the place (in blockTable.js) */
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

/* convert symbols from TileFinder models into numeric and parse float */
Bool blockParseModelJSON(vec table, int max, STRPTR value)
{
	int index, token, mode, faces;
	for (index = token = faces = mode = 0; index < max && IsDef(value); index ++)
	{
		/* identifier must be upper case */
		uint8_t chr = value[0];
		if ('A' <= chr && chr <= 'Z')
		{
			STRPTR end;
			for (end = value + 1; *end && *end != ','; end ++);
			token = FindInList(
				"FACES,TEX_CUBEMAP,TEX_DETAIL,TEX_INHERIT,SIZE,TR,ROT,ROTCAS,REF,ROT90,"
				"TEX,INVERT,INC_FACEID,NAME,DUALSIDE,COPY,SAME_AS", value, end-value
			) + 1;
			switch (token) {
			case 0: return False;
			case BHDR_MAXTOK:   token = COPY_MODEL; break;
			case BHDR_MAXTOK+1: token = SAME_AS; break;
			case BHDR_CUBEMAP:
			case BHDR_DETAIL:
			case BHDR_INHERIT:  mode = token; break;
			case BHDR_TEX:      token |= (mode == BHDR_CUBEMAP ? 4*6 : (mode == BHDR_DETAIL ? popcount(faces) * 4 : 0)) << 8; /* easier to parse later */
			}
			table[index] = token;
			value = end;
		}
		else if (('0' <= chr && chr <= '9') || chr == '-')
		{
			table[index] = strtof(value, &value);
			if (token == BHDR_FACES)
				faces = table[index];
		}
		else if (chr == '\"')
		{
			table[index] = 0;
			for (value ++; *value && *value != '\"'; value ++);
			if (*value) value ++;
		}
		else return False;

		while (isspace(*value)) value ++;
		if (*value == ',')
			value ++;
		while (isspace(*value)) value ++;
	}
	while (index < max)
		table[index++] = 0;
	return True;
}


/*
 * main function to parse entries from blockTable.js
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
					"NORMAL,CHEST,DOOR,HALF,STAIRS,GLASS,FENCE,FENCE2,"
					"WALL,RSWIRE,LEAVES,LIQUID,DOOR_TOP,TALLFLOWER,RAILS,TRAPDOOR,"
					"SIGN,PLATE,SOLIDOUTER,JITTER,POT,NOCONNECT,CNXTEX,DUALSIDE", value, 0
				);
				if (flag < 0)
				{
					SIT_Log(SIT_ERROR, "%s: unknown special tag '%s' on line %d\n", file, value, line);
					return False;
				}
				switch (flag) {
				/* these 3 needs to be flags, not enum */
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

		/* density (g/cm³): used by particles and entity physics */
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
		case SOLID: /* will produce AO/shadow on nearby blocks */
		case TRANS:
			block.updateNearby = 1;
		}
		if (block.rswire)
			block.updateNearby = 2;

		/* XXX might be interesting to make it available from blockTable.js */
		block.containerSize = 0;
		value = strchr(block.tech, '_');
		if (value && strcmp(value+1, "shulker_box") == 0)
			block.containerSize = 27;
		else switch (FindInList("chest,trapped_chest,ender_chest,dispenser,dropper,furnace,lit_furnace,brewing_stand,hopper", block.tech, 0)) {
		case 0:
		case 1:
		case 2: block.containerSize = 27; break;
		case 3:
		case 4: block.containerSize = 9; break;
		case 5:
		case 6:
		case 7: block.containerSize = 3; break;
		case 8: block.containerSize = 5;
		}

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
				#if 0
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
				#endif
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


/*
 * relocated texture to make it is easy for the meshing phase to generate connected models
 * (need to be called after blockTable.js has been parsed)
 */
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
