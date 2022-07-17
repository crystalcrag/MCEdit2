/*
 * blockModels.c : generate models for inventory or preview; also include terrain texture post-processing.
 *
 * written by T.Pierron, nov 2020
 */

#include <glad.h>
#include <string.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <malloc.h>
#include "blocks.h"
#include "items.h"
#include "meshBanks.h"
#include "nanovg.h"   /* need stbi_load() for items.png */

extern struct BlockPrivate_t blocks;

/* pixel size of unit texture in atlas (default: 16) XXX not honored everywhere :-/ */
uint8_t blockTexResol;

uint8_t texCoordRevU[] = {
	1,0,    1,1,    0,1,    0,0,
	1,1,    0,1,    0,0,    1,0,
	0,1,    0,0,    1,0,    1,1,
	0,0,    1,0,    1,1,    0,1,
};

/* texture color need to be adjusted per biome (pair of UV tex coord from terrain.png) */
static uint8_t biomeDepend[] = {
	 0, 0, 3, 0, 7, 2, 8, 2, 4, 3, 5, 3,
	 8, 3, 4, 8, 5, 8,16,11,17,11,17,12,
	16,12, 5,12, 4,12,15, 8
};

/* modulate colors of redstone wire (to avoid doing it in fragment shader) */
static uint8_t rswireTex[] = {
	31, 3 /*color to modulate with*/, 4, 10, 5, 11
};


/*
 * generate models for blocks/item that will be displayed in inventory: they will use items.vsh instead
 * terrain.vsh though.
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



/*
 * main entry point for inventory models generation
 * (blockTable.js needs to be parsed obviously)
 */
void blockParseInventory(int vbo)
{
	BlockState state;
	Block      b;
	DATA16     vertex;
	int        i, j, vtx;
	int        totalVtx, totalInv;

	/* first: count vertex needed for inventory models */
	for (state = blockStates, totalVtx = totalInv = 0; state < blockLast; state ++)
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
		totalInv ++;
		totalVtx += vtx;
	}

	/* add inventory models for items (they will be rendered as ITEM2D) */
	totalVtx += 6 * itemGetCount();
	totalInv += itemGetCount();

	blocks.vboInv = vbo;
	blocks.invModelOff = malloc(totalInv * 2 + 4);
	blocks.invModelOff[0] = 0;

	/* these vertex will stay on the GPU */
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, totalVtx * BYTES_PER_VERTEX, NULL, GL_STATIC_DRAW);
	vertex = glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);

	// fprintf(stderr, "custom model vertex = %d bytes\n", blocks.totalVtx);
	// total wasted = 4014 / 409600
	// fprintf(stderr, "inventory = %d vertex, total = %d\n", totalVtx, totalInv);

	/* generate mesh: will use the same shader than block models */
	for (state = blockStates, vtx = 0, j = 0; state < blockLast; state ++)
	{
		switch (state->inventory & MODELFLAGS) {
		case CUBE3D:
			totalVtx = blockInvModelCube(vertex, state, texCoordRevU);
			break;
		case ITEM2D:
			totalVtx = blockInvModelQuad(vertex, &state->nzU);
			break;
		case MODEL:
			b = &blockIds[state->id>>4];
			if (b->orientHint == ORIENT_BED && b->model)
				totalVtx = blockInvCopyFromModel(vertex, b->model, 1 << (state->id & 15));
			else if (b->special == BLOCK_WALL)
				totalVtx = blockInvCopyFromModel(vertex, state->custModel, 2+8+16+32);
			else if (b->special == BLOCK_CHEST)
				/* don't want double chest models */
				totalVtx = blockInvCopyFromModel(vertex, state->custModel, 1);
			else if (b->model)
				totalVtx = blockInvCopyFromModel(vertex, b->model, ALLFACEIDS);
			else if (state->custModel)
				totalVtx = blockInvCopyFromModel(vertex, state->custModel, ALLFACEIDS);
			else
				totalVtx = blockInvModelCube(vertex, state, texCoordRevU);
			if (b->special == BLOCK_SOLIDOUTER)
				totalVtx += blockInvModelCube(vertex + totalVtx * INT_PER_VERTEX, state, texCoordRevU);
			break;
		default:
			continue;
		}
		blocks.invModelOff[j] = vtx;
		vertex += totalVtx * INT_PER_VERTEX;
		vtx    += totalVtx;
		state->invId = j;
		j ++;
	}

	/* inventory models for items */
	for (i = 0, totalVtx = itemGetCount(); i < totalVtx; i ++)
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
		uint16_t U2  = bitfieldExtract(source[6], 0,  9);
		uint16_t V2  = bitfieldExtract(source[6], 9, 10);
		uint16_t U1  = bitfieldExtract(source[5], 0,  9);
		uint16_t V1  = bitfieldExtract(source[5], 9, 10);
		uint8_t  Xeq = source[5] & FLAG_TEX_KEEPX;
		uint16_t rem = bitfieldExtract(source[5], 19, 3) << 3;

		rem |= 0xf000; /* sky/block light */
		dest[0] = source[0];
		dest[1] = source[0] >> 16;
		dest[2] = source[1];
		/* will set dest[3] and dest[4] */
		if (Xeq) SET_UVCOORD(dest, U1, V2);
		else     SET_UVCOORD(dest, U2, V1);
		dest[4] |= rem;

		dest[5] = source[1] >> 16;
		dest[6] = source[2];
		dest[7] = source[2] >> 16;
		SET_UVCOORD(dest+5, U1, V1);
		dest[9] |= rem;

		dest[10] = source[3];
		dest[11] = source[3] >> 16;
		dest[12] = source[4] >> 16;
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

	struct MeshWriter_t write = {
		.start = temp, .cur = temp, .end = EOT(temp)
	};
	BlockState b = blockGetById(blockId);
	memset(blockIds3x3, 0, sizeof blockIds3x3);
	blockIds3x3[13] = blockId;
	meshHalfBlock(&write, halfBlockGetModel(b, 2, blockIds3x3), 2, pos, b, blockIds3x3, LIGHT_SKY15_BLOCK0);

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

/* relocate rs wire model to make it easier for meshing to generate tex coord based on signal strength */
static void blockRelocateWire(DATA8 reloc, int nb)
{
	BlockState state = blockGetByIdData(RSWIRE, 0);
	DATA16 vertex;
	int count;

	for (vertex = state->custModel, count = vertex[-1]; count > 0; count --, vertex += INT_PER_VERTEX)
	{
		int U = GET_UCOORD(vertex);
		int V = GET_VCOORD(vertex), i;
		DATA8 cnx = reloc;

		for (i = 0; i < nb; i ++, cnx += 4)
		{
			if ((cnx[0] << 4) <= U && U <= (cnx[0] << 4) + 16 &&
				(cnx[1] << 4) <= V && V <= (cnx[1] << 4) + 16)
			{
				U += (cnx[2] << 4) - (cnx[0] << 4);
				V += (cnx[3] << 4) - (cnx[1] << 4);
				CHG_UVCOORD(vertex, U, V);
				break;
			}
		}
	}
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
APTR blockPostProcessTexture(DATA8 * data, int * width, int * height, int bpp)
{
	int   w   = *width;
	int   h   = *height;
	DATA8 dst = realloc(*data, w * bpp * h * 2); /* not enough space in terrain.png :-/ */
	DATA8 s, d;
	int   i, j, k, sz = w / 32, stride = w * bpp;

	if (dst == NULL) return NULL;

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
	for (s = biomeDepend; s < EOT(biomeDepend); s += 2)
	{
		DATA8 s2 = dst + s[0] * sz + s[1] * stride * sz / bpp;

		/* copy texture using a default biome color just below (for inventory textures) */
		for (i = 0; i < sz; i += bpp, s2 += stride)
		{
			DATA8 col;
			for (col = s2, j = sz; j > 0; j -= bpp, col += bpp)
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
	DATA8 cnx = blocks.cnxTex;
	for (i = 0; i < blocks.cnxCount; i ++, cnx += 4)
	{
		/* block.cnxTex: contains UV source + UV dest (4 values) */
		uint8_t empty[4];
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

	/* generate redstone wire shading */
	s = dst + rswireTex[0] * sz + rswireTex[1] * sz * w + 8 * stride;
	d = dst + cnx[-1] * sz * w;
	for (i = 2; i < sizeof rswireTex; i += 2, cnx += 4)
	{
		cnx[0] = rswireTex[i];
		cnx[1] = rswireTex[i+1];
		cnx[2] = 0;
		cnx[3] = cnx[-1] + 1;

		DATA8 src = dst + cnx[0] * sz + cnx[1] * sz * w;
		DATA8 d2, s2;

		int level;
		for (level = 0, d += w * sz, d2 = d; level < 16*4; level += 4, d2 -= w * sz - sz)
		{
			for (j = 0, s2 = src; j < sz; j += bpp, d2 += stride, s2 += stride)
			{
				for (k = 0; k < sz; k += bpp)
				{
					/*
					 * This will prevent having a special case in the terrain fragment shader that does:
					 *   color *= texture(blockTex, vec2(0.96875 + float(rsSignalStrength-1) * 0.001953125, 0.0556640625));
					 */
					d2[k]   = (s2[k]   * s[level])   / 255;
					d2[k+1] = (s2[k+1] * s[level+1]) / 255;
					d2[k+2] = (s2[k+2] * s[level+2]) / 255;
					d2[k+3] = s2[k+3];
				}
			}
		}
	}

	blockRelocateWire(blocks.cnxTex + blocks.cnxCount * 4, (sizeof rswireTex-2) / 2);

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

	return NULL;
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
