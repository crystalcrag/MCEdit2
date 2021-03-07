/*
 * debugInfo.c: routines to complement render.c with on screen debug info.
 *
 * Written by T.Pierron, aug 2020
 */

#define RENDER_IMPL
#include <glad.h>
#include <malloc.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include "blocks.h"
#include "render.h"
#include "nanovg.h"
#include "SIT.h"

extern struct RenderWorld_t render;

#define FRUSTUM_DEBUG

/* get info on block being pointed at (dumped on stderr though) */
void debugBlockVertex(Map map, SelBlock * select)
{
	if (select->sel & 1)
	{
		struct MapExtraData_t data;
		BlockState block = blockGetById(mapGetBlockId(map, select->current, &data));
		int        xyz[3], i;

		xyz[0] = data.offset & 15;
		xyz[2] = (data.offset >> 4) & 15;
		xyz[1] = data.offset >> 8;

		fprintf(stderr, "*** debug block info ***\n");
		fprintf(stderr, "found block %d-%d: %s from %c\n", block->id >> 4, block->id & 15, block->name, "SENWTB"[select->extra.side]);
		fprintf(stderr, "located at %d,%d,%d, offset = %d, sub-chunk: %d,%d,%d, chunk: %d,%d,%d\n",
			(int) select->current[0], (int) select->current[1], (int) select->current[2],
			data.offset, xyz[0], xyz[1], xyz[2], data.chunk->X, data.cd->Y, data.chunk->Z
		);
		fprintf(stderr, "intersection at %g,%g,%g, mouse at %d,%d\n", render.selection.extra.inter[0],
			render.selection.extra.inter[1], render.selection.extra.inter[2], render.mouseX, render.mouseY);

		#ifdef DEBUG
		xyz[1] += data.cd->Y;
		NBTHdr hdr = (NBTHdr) chunkGetTileEntity(data.chunk, xyz);
		if (hdr)
		{
			struct NBTIter_t iter;
			NBT_IterCompound(&iter, (DATA8) hdr);
			while (NBT_Iter(&iter) >= 0);
			fprintf(stderr, "TileEntity associated with block (%d bytes):\n", iter.offset);
			struct NBTFile_t nbt = {.mem = (DATA8) hdr};
			while ((i = NBT_Dump(&nbt, nbt.alloc, 3, stderr)) >= 0)
				nbt.alloc += i;
		}
		xyz[1] -= data.cd->Y;
		#endif

		/* get the sub buffer where the vertex data is located */
		DATA16  buffer, p;
		GPUBank bank = data.cd->glBank;
		GPUMem  mem  = bank->usedList + data.cd->glSlot;

		for (i = -1; bank; PREV(bank), i ++);
		fprintf(stderr, "bank: %d, offset: %d, size: %d\n", i, mem->offset, mem->size);

		if (block->type == SOLID || block->type == TRANS)
		{
			buffer = malloc(mem->size);
			bank = data.cd->glBank;
			glBindBuffer(GL_ARRAY_BUFFER, bank->vboTerrain);
			glGetBufferSubData(GL_ARRAY_BUFFER, mem->offset, mem->size, buffer);
			glBindBuffer(GL_ARRAY_BUFFER, 0);

			for (i = mem->size, p = buffer; i > 0; i -= 3*BYTES_PER_VERTEX, p += 3*INT_PER_VERTEX)
			{
				extern uint8_t texCoord[];
				extern uint8_t vertex[];
				extern uint8_t cubeIndices[]; /* from chunks.c */

				/* need to decode vertex buffer */
				uint8_t x = round((p[0] - 1920) * 0.00026041666666666666);
				uint8_t y = round((p[1] - 1920) * 0.00026041666666666666);
				uint8_t z = round((p[2] - 1920) * 0.00026041666666666666);
				uint8_t U = (p[3] & 511) >> 4;
				uint8_t V = (p[3] >> 10);
				uint8_t side = (p[4] >> 3) & 7;
				uint8_t j, k;
				DATA8   tex;

				/* only the side being pointed at */
				if (side != select->extra.side) continue; /* too verbose otherwise */

				for (j = 0, k = (block->rotate&3) * 8, tex = &block->nzU + side * 2; j < 4; j ++, k += 2)
				{
					DATA8 coord = vertex + cubeIndices[(side<<2) + j];
					if ((xyz[0] + coord[0]) == x &&
					    (xyz[1] + coord[1]) == y &&
					    (xyz[2] + coord[2]) == z &&
					    texCoord[k]   + tex[0] == U &&
					    texCoord[k+1] + tex[1] == V)
					{
						fprintf(stderr, "VERTEX: %d,%d,%d [%d] - NORM: %d (%c) - uv: %d,%d - OCS: %d - LIGHT: %d SKY:%d\n",
							x, y, z, (coord-vertex)/3, side, "SENWTB"[side], U, V, (p[4] >> 6) & 3, (p[4]>>8) & 15, p[4]>>12);
						/* found out vertex of a triangle, there must 2 more */
						p -= 2*INT_PER_VERTEX;
						i += 2*BYTES_PER_VERTEX;
						break;
					}
				}
			}
			free(buffer);
		}
	}
	else fprintf(stderr, "no block selected");
}

/* show limits of the chunk where the player is currently */
void debugToggle(Map map)
{
	/* debug chunk data: will use terrainVert.glsl */
	ChunkData air = map->air;

	if (! air->glBank)
	{
		/* need to store vertex first */
		uint16_t buffer[16 * 4 * 4 * INT_PER_VERTEX];
		DATA16   p;
		int      i, j;

		/* 4 faces */
		for (i = 0, p = buffer; i < 4; i ++)
		{
			uint8_t X = i & 1 ? 2  : 0;
			uint8_t m = i > 1 ? 16 : 0;
			uint8_t Z = 2 - X;
			/* 16 vertical lines */
			for (j = 0; j < 16; j ++)
			{
				p[X] = i == 1 || i == 2 ? VERTEX(16-j) : VERTEX(j);
				p[1] = VERTEX(0);
				p[Z] = VERTEX(m);
				p[3] = (31*16+8) | (1 << 10);
				p[4] = (6<<3) | (0xff << 8);
				p += INT_PER_VERTEX;
				memcpy(p, p - INT_PER_VERTEX, BYTES_PER_VERTEX);
				p[1] = VERTEX(16);
				p += INT_PER_VERTEX;
			}
			/* 16 horizontal lines */
			for (j = 0; j < 16; j ++)
			{
				p[X] = VERTEX(0);
				p[1] = VERTEX(j);
				p[Z] = VERTEX(m);
				p[3] = (31*16+8) | (j == 0 ? (2 << 10) : (1 << 10));
				p[4] = (6<<3) | (0xff << 8);
				p += INT_PER_VERTEX;
				memcpy(p, p - INT_PER_VERTEX, BYTES_PER_VERTEX);
				p[X] = VERTEX(16);
				p += INT_PER_VERTEX;
			}
		}

		struct WriteBuffer_t write = {.start = buffer, .cur = p};
		renderInitBuffer(air);
		renderFlush(&write);
		renderFinishMesh();
	}
}

static void nvgMultiLineText(NVGcontext * vg, float x, float y, STRPTR start, STRPTR end)
{
	for (;;)
	{
		STRPTR eol;
		for (eol = start; eol < end && *eol != '\n'; eol ++);
		nvgText(vg, x, y, start, eol);
		if (*eol == 0) break;
		y += FONTSIZE;
		start = eol + 1;
	}
}

void debugCoord(APTR vg, vec4 camera, int total)
{
	TEXT message[256];
	int  len = sprintf(message, "XYZ: %.2f, %.2f, %.2f", camera[0], camera[1], camera[2]);
	int  vis;
	ChunkData cd;

	len += sprintf(message + len, "\nTriangles: %d", total);
	for (cd = render.level->firstVisible, vis = 0; cd; vis ++, cd = cd->visible);
	len += sprintf(message + len, "\nChunks: %d/%d", vis, render.level->GPUchunk);

	nvgFontSize(vg, FONTSIZE);
	nvgTextAlign(vg, NVG_ALIGN_TOP);
	nvgFillColorRGBA8(vg, "\0\0\0\xff");
	nvgMultiLineText(vg, 12, 12, message, message+len);
	nvgFillColorRGBA8(vg, "\xff\xff\xff\xff");
	nvgMultiLineText(vg, 10, 10, message, message+len);
}

/*
 * side-view functions: mostly used to debug SkyLight, BlockLight and HeightMap values
 */

static struct
{
	SIT_Widget label;
	SIT_Widget toggles[3];
	SIT_Widget app;
	SIT_Widget showChunk;
	float      sliceSz;
	uint8_t    sliceAxis;
	int        sliceDir;
	int        showLightValue;
	int        showHeightMap;
	int        showChunks;
	int        zoom;
	int        cellH, cellV;
	int        xoff,  yoff;
	int        mX,    mY;
	char *     vector;
	int        slice;
	int        minXZ, maxXZ;
	int        pos[3], orig[3], top[3];
	SelBlock   sel;

}	debug;

static char debugVector[] = {
	-1, 0,  0, 1,
	 0, 0,  1, 1,
	 1, 0,  0, 1,
	 0, 0, -1, 1,
};

static int debugExit(SIT_Widget w, APTR cd, APTR ud)
{
	* (int *) ud = 2;
	return 1;
}

/* init current pos for side view */
void debugSetPos(APTR app, int * exitCode)
{
	float * pos = render.selection.sel ? render.selection.current : render.camera;
	debug.pos[0] = pos[0];
	debug.pos[1] = pos[1];
	debug.pos[2] = pos[2];
	memcpy(debug.orig, debug.pos, sizeof debug.orig);

	debug.sliceDir = render.direction;
	debug.sliceAxis = render.direction & 1 ? 2 : 0;
	debug.sliceSz = roundf(render.width / debug.zoom);
	debug.vector = debugVector + render.direction * 4;
	debug.slice = 0;
	debug.app = app;

	fprintf(stderr, "direction = %d\n", render.direction);

	/* max coord range */
	Chunk c = render.level->center;
	int max = (render.level->maxDist+1) >> 1;
	int base = render.direction & 1 ? c->Z : c->X;
	debug.minXZ = base - max * 16;
	debug.maxXZ = base + max * 16 + 16;

	/* debug info toolbar */
	SIT_CreateWidgets(app,
		"<canvas name=toolbar left=FORM right=FORM>"
		" <button name=skylight.left title=SkyLight buttonType=", SITV_ToggleButton, ">"
		" <button name=blocklight.center title=BlockLight buttonType=", SITV_ToggleButton, "left=WIDGET,skylight,2>"
		" <button name=none.right title=None buttonType=", SITV_ToggleButton, "left=WIDGET,blocklight,2>"
		" <button name=chunk title='Show chunk boundaries' buttonType=", SITV_CheckBox, "checkState=", debug.showChunks,
		"  curValue=", &debug.showChunks, "left=WIDGET,none,1em top=MIDDLE,skylight>"
		" <button name=heightmap title='Show heightmap' buttonType=", SITV_CheckBox, "checkState=", debug.showHeightMap,
		"  curValue=", &debug.showHeightMap, "left=WIDGET,chunk,1em top=MIDDLE,skylight>"
		" <button name=back title='3D view' right=FORM>"
		" <label name=slice right=WIDGET,back,1em top=MIDDLE,back>"
		"</canvas>"
	);
	debug.label = SIT_GetById(app, "slice");
	debug.showChunk = SIT_GetById(app, "chunk");

	debugMoveSlice(0);

	int i;
	for (i = 0; i < 3; i ++)
	{
		static STRPTR names[] = {"skylight", "blocklight", "none"};
		SIT_SetValues(debug.toggles[i] = SIT_GetById(app, names[i]),
			SIT_CheckState, debug.showLightValue == i,
			SIT_RadioGroup, 1,
			SIT_RadioID,    i,
			SIT_CurValue,   &debug.showLightValue,
			NULL
		);
	}

	SIT_AddCallback(SIT_GetById(app, "back"), SITE_OnActivate, debugExit, exitCode);
}

/* render side view of world */
void debugWorld(int time)
{
	static uint8_t skyColor[]  = {0x7e, 0xdf, 0xff, 0xff}; /* RGBA */
	static uint8_t caveColor[] = {0x33, 0x33, 0x33, 0xff};
	static uint8_t chunkSep[]  = {0xff, 0xf0, 0x00, 0xff};
	static float   dirAngle[]  = {M_PI_2, M_PI, -M_PI_2, 0};
	static char    skyVal[]    = "0 1 2 3 4 5 6 7 8 9 101112131415";
	struct BlockIter_t iter;
	NVGcontext * vg = render.nvgCtx;

	int  x, y;
	int  i, j;
	vec4 top;
	char dir[4];
	char back[4];

	top[0] = debug.pos[0];
	top[1] = debug.pos[1];
	top[2] = debug.pos[2];
	memcpy(dir, debug.vector, sizeof dir);
	memcpy(back, dir, sizeof back);
	debug.cellH = ceil(render.width / debug.sliceSz) + 1;
	debug.cellV = ceil(render.height / debug.sliceSz) + 1;
	back[debug.sliceAxis] *= - debug.cellH;
	top[debug.sliceAxis] -= dir[debug.sliceAxis] * (debug.cellH>>1);
	top[1] += debug.cellV>>1;
	memcpy(debug.top, top, sizeof top);

	mapInitIter(render.level, &iter, top, False);

	/* zoom = number of tile per screen width, make font size inversely proportionnal to this */
	i = -12 * debug.zoom / 11 + 560/11;
	if (i < 13) i = 13;

	nvgBeginFrame(vg, render.width, render.height, 1);
	nvgFontFaceId(vg, render.debugFont);
	nvgFontSize(vg, i);
	nvgTextAlign(vg, NVG_ALIGN_TOP);
	nvgStrokeWidth(vg, 1);
	float tile = debug.sliceSz / 16.;
	int   xoff = (debug.sliceSz - nvgTextBounds(vg, 0, 0, "99", NULL, NULL)) * 0.5;

	for (j = debug.cellV, y = debug.yoff; j > 0; j --, y += debug.sliceSz)
	{
		for (i = debug.cellH, x = debug.xoff; i > 0; i --, x += debug.sliceSz)
		{
			nvgBeginPath(vg);
			nvgRect(vg, x, y, debug.sliceSz, debug.sliceSz);
			if (iter.cd)
			{
				uint8_t color[4];
				int block = iter.blockIds[iter.offset];
				int data  = iter.blockIds[DATA_OFFSET + (iter.offset>>1)];
				int sky   = iter.blockIds[SKYLIGHT_OFFSET + (iter.offset>>1)];
				int light = iter.blockIds[BLOCKLIGHT_OFFSET + (iter.offset>>1)];
				if (iter.offset & 1) sky >>= 4, data >>= 4, light >>= 4;
				else                 sky &= 15, data &= 15, light &= 15;
				BlockState b = blockGetById(ID(block, data));

				float a = sky * (1/15.);
				color[0] = skyColor[0] * a + caveColor[0] * (1-a);
				color[1] = skyColor[1] * a + caveColor[1] * (1-a);
				color[2] = skyColor[2] * a + caveColor[2] * (1-a);
				color[3] = 255;

				nvgFillColorRGBA8(vg, color);
				nvgFill(vg);

				if (debug.showLightValue)
					sky = light;

				if (b->id > 0)
				{
					static uint8_t toTrigo[] = {0, 3, 2, 1};
					DATA8 tex = b->type == QUAD ? &b->nzU : &b->nzU + debug.sliceDir * 2;
					int   ang = toTrigo[(b->rotate >> debug.sliceDir*2) & 3];
					int   U   = tex[0];
					int   V   = tex[1];
					if (tex[0] < 16 && V == 62) V = 63;
					switch (ang) {
					case 1: // 90
						U = 64 - (V + 1);
						V = tex[0];
						break;
					case 2: // 180
						U = 32 - (U + 1);
						V = 64 - (V + 1);
						break;
					case 3: // 270
						U = V;
						V = 64 - (tex[0] + 1);
					}
					nvgFillPaint(vg, nvgImagePattern(vg, x - (U * 16) * tile, y - (V * 16) * tile,
						512 * tile, 1024 * tile, ang * M_PI_2, render.nvgTerrain, 1));
					nvgFill(vg);
					if (sky > 0 && debug.showLightValue < 2) goto showSky;
				}
				else if (sky > 0 && debug.showLightValue < 2)
				{
					/* air: show sky/block light values */
					STRPTR skyTxt;
					showSky:
					skyTxt = skyVal + sky*2;
					nvgFillColorRGBA8(vg, debug.showLightValue ? "\xff\xff\x88\xff" : "\xff\xff\xff\xff");
					nvgText(vg, x + xoff, y + 3, skyTxt, skyTxt+2);
				}
			}
			else /* no ChunkData: show sky */
			{
				nvgFillColorRGBA8(vg, skyColor);
				nvgFill(vg);
			}
			if (debug.showHeightMap && iter.yabs == iter.ref->heightMap[iter.x+iter.z*16])
			{
				nvgStrokeColorRGBA8(vg, "\xff\x0\xff\xff");
				int bottom = y + debug.sliceSz - 1;
				nvgBeginPath(vg);
				nvgStrokeWidth(vg, 4);
				nvgMoveTo(vg, x, bottom);
				nvgLineTo(vg, x + debug.sliceSz, bottom);
				nvgStroke(vg);
			}
			mapIter(&iter, dir[0], dir[1], dir[2]);
		}
		if (iter.yabs > 0)
			mapIter(&iter, back[0], -1, back[2]);
		else
			break;
	}
	y += debug.sliceSz;
	if (y < render.height)
	{
		nvgFillColorRGBA8(vg, "\0\0\0\xff");
		nvgBeginPath(vg);
		nvgRect(vg, 0, y, render.width, render.height - y);
		nvgFill(vg);
	}

	/* show chunk boundaries */
	if (debug.showChunks)
	{
		i = top[1];
		j = top[debug.sliceAxis];
		y = debug.yoff + (i - ((i + 15) & ~15) + 1) * debug.sliceSz;
		x = debug.xoff;
		i = 16 * debug.sliceSz;

		if (y < 0) y += i;
		nvgStrokeColorRGBA8(vg, chunkSep);
		nvgStrokeWidth(vg, 1);
		while (y < render.height)
		{
			nvgBeginPath(vg);
			nvgMoveTo(vg, 0, y);
			nvgLineTo(vg, render.width, y);
			nvgStroke(vg);
			y += i;
		}
		i = dir[debug.sliceAxis];
		if (i < 0) j ++;
		while (x < render.width)
		{
			if ((j & 15) == 0)
			{
				nvgBeginPath(vg);
				nvgMoveTo(vg, x, 0);
				nvgLineTo(vg, x, render.height);
				nvgStroke(vg);
			}
			x += debug.sliceSz;
			j += i;
		}
	}

	/* show current player position */
	x = (debug.top[debug.sliceAxis] - debug.orig[debug.sliceAxis]) * debug.sliceSz + debug.xoff;
	y = (debug.top[1] - debug.orig[1]) * debug.sliceSz + debug.yoff;

	nvgStrokeColorRGBA8(vg, "\0\0\0\xff");
	nvgStrokeWidth(vg, 1);
	nvgBeginPath(vg);
	nvgMoveTo(vg, x, y - 10);
	nvgLineTo(vg, x, y + 10);
	nvgMoveTo(vg, x - 10, y);
	nvgLineTo(vg, x + 10, y);
	nvgStroke(vg);

	#ifdef FRUSTUM_DEBUG
	/* show ChunkData state */
	mapInitIter(render.level, &iter, top, False);
	nvgFontSize(vg, 20);

	for (j = debug.cellV, y = debug.yoff; j > 0; j --, y += debug.sliceSz)
	{
		if (iter.y == 0)
		{
			int axis = debug.sliceAxis >> 1;
			for (i = debug.cellH, x = debug.xoff; i > 0; i --, x += debug.sliceSz)
			{
				if ((&iter.x)[axis] == 0)
				{
					char message[64];
					message[0] = 0;
					if (iter.cd == NULL)
					{
						strcpy(message, "NO CHUNKDATA");
					}
					else if (iter.ref->chunkFrame == render.level->frame)
					{
						if (iter.ref->outflags[iter.yabs>>4] & 0x80)
						{
							strcpy(message, "VISIBLE");
							if ((iter.ref->cflags & CFLAG_HASMESH) == 0)
								strcat(message, "- NOMESH");
							else if (iter.cd->glBank)
							{
								ChunkData cd;
								for (cd = render.level->firstVisible; cd && cd != iter.cd; cd = cd->visible);
								if (cd == NULL)
									strcat(message, "- NOTINLIST");
							}
							else strcat(message, "- EMPTYMESH");
						}
						else if ((iter.ref->cflags & CFLAG_HASMESH) == 0)
						{
							strcpy(message, "NOMESH");
						}
						else sprintf(message, "%02x", iter.ref->outflags[iter.yabs>>4]);
					}
					else strcpy(message, "NOTINFRUSTUM");
					if (message[0])
					{
						nvgFillColorRGBA8(vg, "\0\0\0\xff");
						nvgFontBlur(vg, 2);
						nvgText(vg, x, y, message, NULL);
						nvgFontBlur(vg, 0);
						nvgFillColorRGBA8(vg, "\xff\xff\xff\xff");
						nvgText(vg, x, y, message, NULL);
					}
				}
				mapIter(&iter, dir[0], dir[1], dir[2]);
			}
			mapIter(&iter, back[0], -1, back[2]);
		}
		else mapIter(&iter, 0, -1, 0);
		if (iter.yabs < 0) break;
	}
	#endif

	if (render.debugInfo & DEBUG_BLOCK)
	{
//		nvgFontSize(vg, FONTSIZE);
//		debugBlockInfo(vg, &debug.sel, &debug.mX);
	}

	float scale = render.height * 0.15;
	nvgSave(vg);
	nvgTranslate(vg, render.width - scale, render.height - scale); scale -= 20;
	nvgRotate(vg, dirAngle[debug.sliceDir]);
	nvgBeginPath(vg);
	nvgRect(vg, -scale, -scale, scale*2, scale*2);
	nvgFillPaint(vg, nvgImagePattern(vg, -scale, -scale, scale*2, scale*2, 0, render.compass, 1));
	nvgFill(vg);
	nvgRestore(vg);

	nvgEndFrame(vg);

	SIT_RenderNodes(time);
}

/* this view won't load new chunk */
static void debugClampXZView(void)
{
	int * axis = &debug.pos[debug.sliceAxis];
	int   off  = debug.cellH >> 1, min;
	if (debug.sliceDir == 0 || debug.sliceDir == 3)
	{
		min = axis[0] + off;
		if (min >= debug.maxXZ)
			axis[0] = debug.maxXZ - off - 1, debug.xoff = 0;
		if (min - debug.cellH < debug.minXZ-1)
			axis[0] = debug.minXZ + debug.cellH - off - 1,
			debug.xoff = render.width - debug.cellH * debug.sliceSz;
	}
	else
	{
		min = axis[0] - off;
		if (min < debug.minXZ)
			axis[0] = debug.minXZ + off, debug.xoff = 0;
		if (min + debug.cellH > debug.maxXZ)
			axis[0] = debug.maxXZ - debug.cellH + off,
			debug.xoff = render.width - debug.cellH * debug.sliceSz;
	}
}

void debugScrollView(int dx, int dy)
{
	int slice = debug.sliceSz;
	int dir   = debug.vector[debug.sliceAxis];

	dx += debug.xoff;
	dy += debug.yoff;

	debug.pos[1] += dy / slice;
	debug.pos[debug.sliceAxis] -= dir * dx / slice;

	debug.yoff = dy % slice;
	debug.xoff = dx % slice;

	if (debug.xoff > 0)
		debug.xoff -= slice, debug.pos[debug.sliceAxis] -= dir;
	if (debug.yoff > 0)
		debug.yoff -= slice, debug.pos[1] ++;

//	fprintf(stderr, "top = %d, %d\n", debug.pos[debug.sliceAxis] - debug.vector[debug.sliceAxis] * (debug.cellH>>1), debug.pos[1] + (debug.cellV>>1));

	debugClampXZView();
}

void debugBlock(int x, int y)
{
	debug.mX = x;
	debug.mY = y;

	memcpy(debug.sel.current, debug.top, sizeof debug.sel.current);

	debug.sel.current[1] -= (int) ((y - debug.yoff) / debug.sliceSz);
	debug.sel.current[debug.sliceAxis] += debug.vector[debug.sliceAxis] * (int) ((x - debug.xoff) / debug.sliceSz);

	debug.sel.blockId = 0;
	mapGetBlockId(render.level, debug.sel.current, &debug.sel.extra);
}

void debugToggleInfo(int what)
{
	switch (what) {
	case DEBUG_LIGHT:
		debug.showLightValue ++;
		if (debug.showLightValue > 2)
			debug.showLightValue = 0;
		SIT_SetValues(debug.toggles[debug.showLightValue], SIT_CheckState, 1, NULL);
		break;
	case DEBUG_CHUNK:
		debug.showChunks = ! debug.showChunks;
		SIT_SetValues(debug.showChunk, SIT_CheckState, debug.showChunks, NULL);
	}
}

void debugZoomView(int x, int y, int dir)
{
	debug.zoom = debug.zoom * (dir < 0 ? 3/2. : 2/3.);
	if (debug.zoom < 10)  debug.zoom = 10;
	if (debug.zoom > 100) debug.zoom = 100;
	debug.sliceSz = render.width / debug.zoom;
	debug.cellH = ceil(render.width / debug.sliceSz) + 1;
	debug.cellV = ceil(render.height / debug.sliceSz) + 1;
	debug.xoff %= (int) debug.sliceSz;
	debug.yoff %= (int) debug.sliceSz;
	debugClampXZView();
}

void debugMoveSlice(int dz)
{
	static char dir90[] = {1, -1, -1, 1};
	TEXT slice[64];
	debug.slice += dz;
	debug.pos[2-debug.sliceAxis] += dz * dir90[debug.sliceDir];

	sprintf(slice, "Slice %d, %C: %d", debug.slice, debug.sliceAxis ? 'X' : 'Z', debug.pos[2-debug.sliceAxis] & 15);

	SIT_SetValues(debug.label, SIT_Title, slice, NULL);
}

void debugRotateView(int dir)
{
	debug.sliceDir += dir;
	if (debug.sliceDir < 0) debug.sliceDir = 3; else
	if (debug.sliceDir > 3) debug.sliceDir = 0;

	debug.pos[debug.sliceAxis] = debug.orig[debug.sliceAxis];
	debug.sliceAxis = debug.sliceDir & 1 ? 2 : 0;
	debug.vector = debugVector + debug.sliceDir * 4;

	/* max coord range */
	Chunk c = render.level->center;
	int max = (render.level->maxDist+1) >> 1;
	int base = debug.sliceDir & 1 ? c->Z : c->X;
	debug.minXZ = base - max * 16;
	debug.maxXZ = base + max * 16 + 16;

	fprintf(stderr, "direction = %d, range = %d - %d\n", debug.sliceDir, debug.minXZ, debug.maxXZ);

	SIT_ForceRefresh();
}

void debugLoadSaveState(STRPTR path, Bool load)
{
	if (load)
	{
		INIFile ini = (INIFile) path;

		debug.showChunks = GetINIValueInt(ini, "DBGShowChunks", 0);
		debug.showLightValue = GetINIValueInt(ini, "DBGLightValue", 0);
		debug.showHeightMap = GetINIValueInt(ini, "DBGShowHeightMap", 0);
		debug.zoom = GetINIValueInt(ini, "DBGZoom", 32);
	}
	else
	{
		SetINIValueInt(path, "DBGShowChunks", debug.showChunks);
		SetINIValueInt(path, "DBGLightValue", debug.showLightValue);
		SetINIValueInt(path, "DBGShowHeightMap", debug.showHeightMap);
//		SetINIValueInt(path, "DBGZoom", debug.zoom);
	}
}
