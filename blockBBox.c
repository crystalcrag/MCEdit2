/*
 * blocksBBox.c : block bounding box creation/manipulation (collision detection and highlight preview).
 *
 * Written by T.Pierron, apr 2020
 */

#include <glad.h>
#include <string.h>
#include <stddef.h>
#include <stdlib.h>
#include <malloc.h>
#include <math.h>
#include "blocks.h"
#include "items.h"
#include "NBT2.h"

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


/* convert some common block data into SIDE_* enum */
struct BlockSides_t blockSides = {
	.repeater = {SIDE_SOUTH,  SIDE_WEST, SIDE_NORTH, SIDE_EAST}, /* where the input is (output = input ^ 2) */
	.torch    = {SIDE_TOP,    SIDE_WEST, SIDE_EAST,  SIDE_NORTH, SIDE_SOUTH, SIDE_BOTTOM, SIDE_NONE,   SIDE_NONE},
	.lever    = {SIDE_TOP,    SIDE_WEST, SIDE_EAST,  SIDE_NORTH, SIDE_SOUTH, SIDE_BOTTOM, SIDE_BOTTOM, SIDE_TOP},
	.sign     = {SIDE_NONE,   SIDE_NONE, SIDE_SOUTH, SIDE_NORTH, SIDE_EAST,  SIDE_WEST,   SIDE_NONE,   SIDE_NONE},
	.piston   = {SIDE_BOTTOM, SIDE_TOP,  SIDE_NORTH, SIDE_SOUTH, SIDE_WEST,  SIDE_EAST,   SIDE_NONE,   SIDE_NONE},
	.SWNE     = {SIDE_SOUTH,  SIDE_WEST, SIDE_NORTH, SIDE_EAST},
};

extern struct BlockPrivate_t blocks;


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
 * block orient/placement adjustment
 */
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
	if (b->containerSize > 0)
		ret.page = 511;

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

