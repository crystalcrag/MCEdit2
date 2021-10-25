/*
 * selection.c: handle extended selection and operations that can be done with it.
 *              (fill, replace, geometric brush, delete, clone, copy, ...)
 *              note: import/export is done in library.c
 *
 * Written by T.Pierron, aug 2021.
 */

#define SELECTION_IMPL
#include <glad.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <malloc.h>
#include "selection.h"
#include "mapUpdate.h"
#include "blockUpdate.h"
#include "player.h"
#include "render.h"
#include "globals.h"
#include "SIT.h"

struct Selection_t selection;
extern uint8_t bboxIndices[]; /* from blocks.c */

/*
 * selection render / nudge
 */

/* init VBO and VAO */
void selectionInitStatic(int shader)
{
	selection.shader = shader;
	selection.infoLoc = glGetUniformLocation(shader, "info");
	selection.cloneRepeat = 1;
	selection.copyAir = 1;
	selection.copyWater = 1;
	selection.wait = MutexCreate();
	selection.ext[0] = selection.ext[3] = "";
	selection.ext[1] = selection.ext[2] = "-rev";

	/* will use selection.vsh and indexed rendering */
	glGenBuffers(3, &selection.vboVertex);
	glBindBuffer(GL_ARRAY_BUFFER, selection.vboVertex);
	glBufferData(GL_ARRAY_BUFFER, MAX_VERTEX * 20, NULL, GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, selection.vboIndex);
	glBufferData(GL_ARRAY_BUFFER, MAX_INDEX * 2, NULL, GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, selection.vboLOC);
	glBufferData(GL_ARRAY_BUFFER, MAX_REPEAT * 12, NULL, GL_STATIC_DRAW);

	/* already populate data for 1st and 2nd point */
	BlockState b = blockGetById(ID(1,0));
	VTXBBox bbox = blockGetBBoxForVertex(b);
	blockGenVertexBBox(b, bbox, 0xff, &selection.vboVertex, ID(31, 1), 0);
	blockGenVertexBBox(b, bbox, 0xff, &selection.vboVertex, ID(31, 2), (24+36) | ((8*5) << 16));

	glGenVertexArrays(1, &selection.vao);
	glBindVertexArray(selection.vao);
	glBindBuffer(GL_ARRAY_BUFFER, selection.vboVertex);
	/* 3 for vertex, 2 for tex coord */
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 20, 0);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 20, (APTR) 12);
	glEnableVertexAttribArray(1);
	glBindBuffer(GL_ARRAY_BUFFER, selection.vboLOC);
	glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 0, 0);
	glEnableVertexAttribArray(2);
	glVertexAttribDivisor(2, 1);
	glBindVertexArray(0);
}

/* show size of selection in the "nudge" window */
void selectionSetSize(void)
{
	SIT_Widget w;
	if ((w = selection.nudgeSize))
	{
		TEXT buffer[32];
		int size[] = {
			(int) fabsf(selection.firstPt[VX] - selection.secondPt[VX]) + 1,
			(int) fabsf(selection.firstPt[VZ] - selection.secondPt[VZ]) + 1,
			(int) fabsf(selection.firstPt[VY] - selection.secondPt[VY]) + 1
		};
		if (globals.direction & 1)
			swap(size[0], size[1]);
		sprintf(buffer, "%dW x %dL x %dH", size[0], size[1], size[2]);
		SIT_SetValues(w, SIT_Title, buffer, NULL);
	}
	if ((w = selection.brushSize))
	{
		TEXT buffer[32];
		Map brush = selection.brush;
		int size[] = {brush->size[VX]-2, brush->size[VZ]-2, brush->size[VY]-2};
		if (globals.direction & 1)
			swap(size[0], size[1]);
		sprintf(buffer, "%dW x %dL x %dH", size[0], size[1], size[2]);
		SIT_SetValues(w, SIT_Title, buffer, NULL);
	}
	if (selection.editBrush)
	{
		/* show the orientattion of roll command (way too complicated to make it follow all orientations) */
		w = SIT_GetById(selection.editBrush, "roll");
		TEXT buffer[64];
		sprintf(buffer, "<pchar src=roll%s.png> Roll", selection.ext[globals.direction]);
		SIT_SetValues(w, SIT_Title, buffer, NULL);
	}
}

/* build a rect for selection shader */
static void selectionSetRect(int pointId)
{
	vec vtx, size;
	int i;
	if (pointId == SEL_POINT_BOX)
	{
		for (i = 0; i < 3; i ++)
		{
			float pt1 = selection.firstPt[i];
			float pt2 = selection.secondPt[i];
			selection.regionPt[i] = (pt1 < pt2 ? pt1 : pt2);
			selection.regionSize[i] = fabsf(pt2 - pt1) + 1;
		}
	}

	switch (pointId) {
	case SEL_POINT_BOX:   size = selection.regionSize; i = (8*5) * 2; break;
	case SEL_POINT_CLONE: size = selection.cloneSize;  i = (8*2 + 36 + 24) * 5; break;
	default: return;
	}

	glBindBuffer(GL_ARRAY_BUFFER, selection.vboVertex);
	vtx = (vec) glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY) + i;

	/* add a bit of padding to prevent z-fighting */
	vec4 pad = {
		size[VX] + VTX_EPSILON*2,
		size[VY] + VTX_EPSILON*2,
		size[VZ] + VTX_EPSILON*2
	};

	/* build a box big enough to cover the whole region */
	for (i = 0; i < 24; i ++, vtx += 5)
	{
		static uint8_t coordU[] = {0, 2, 0, 2, 0, 0};
		static uint8_t coordV[] = {1, 1, 1, 1, 2, 2};
		DATA8 p  = cubeVertex + cubeIndices[i];
		DATA8 uv = texCoord + (i & 3) * 2;
		vtx[0] = p[VX] * pad[VX];
		vtx[1] = p[VY] * pad[VY];
		vtx[2] = p[VZ] * pad[VZ];
		vtx[3] = uv[0] * size[coordU[i>>2]]; if (vtx[3] > 0) vtx[3] -= 0.01f;
		vtx[4] = uv[1] * size[coordV[i>>2]]; if (vtx[4] > 0) vtx[4] -= 0.01f;
		vtx[3] /= 16;
		vtx[4] /= 32;
		if ((i & 3) == 3)
		{
			/* convert to triangles */
			memcpy(vtx + 5,  vtx - 15, 20);
			memcpy(vtx + 10, vtx - 5,  20);
			vtx += 10;
		}
	}
	/* lines around the box */
	for (i = 36; i < 36+24; i ++, vtx += 5)
	{
		DATA8 p = cubeVertex + bboxIndices[i] * 3;
		vtx[0] = p[VX] * pad[VX];
		vtx[1] = p[VY] * pad[VY];
		vtx[2] = p[VZ] * pad[VZ];
		vtx[3] = (31*16+8) / 512.;
		vtx[4] = 8 / 1024.;
	}
	glUnmapBuffer(GL_ARRAY_BUFFER);

	if (pointId == SEL_POINT_BOX)
		selectionSetSize();
}

/* SITE_OnActivate on "Nudge" buttons */
static int selectionNudge(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_OnMouse * msg = cd;
	switch (msg->state) {
	case SITOM_ButtonPressed:
		switch (msg->button) {
		case SITOM_ButtonLeft:
			selection.nudgePoint = (int) ud;
			selection.nudgeStep  = 1;
			return 2;
		case SITOM_ButtonRight:
			/* button don't normally react to RMB: do it manually */
			SIT_SetValues(w, SIT_CheckState, True, NULL);
			selection.nudgePoint = (int) ud;
			selection.nudgeStep  = 16;
			return 2;
		default: break;
		}
		break;
	case SITOM_ButtonReleased:
		selection.nudgePoint = 0;
	default: break;
	}
	return 1;
}

static void selectionBrushRotate(void);
static void selectionBrushFlip(void);
static void selectionBrushMirror(void);
static void selectionBrushRoll(void);

/* OnTimer cb */
static int cancelActivation(SIT_Widget w, APTR cd, APTR ud)
{
	/* remove :active state on button activated through kbd shortcut */
	SIT_SetValues(w, SIT_CheckState, False, NULL);
	return -1;
}

/* nudge selection using directional keys normally used for player movement */
Bool selectionProcessKey(int key, int mod)
{
	if (selection.nudgePoint > 0) /* one button must be held down */
	{
		static int8_t axisSENW[] = {2,0,2,0};
		static int8_t axisMain[] = {1,1,-1,-1};
		static int8_t axisRot[]  = {1,-1,-1,1};
		int8_t axis = 0;
		int8_t dir  = globals.direction; /* S,E,N,W */

		/* selection is being cloned: can't move first and second point (but can move clone) */
		if (selection.brush && selection.nudgePoint < 4)
			return False;

		switch (key) {
		case FORWARD:  axis = axisSENW[dir];   dir =  axisMain[dir]; break;
		case BACKWARD: axis = axisSENW[dir];   dir = -axisMain[dir]; break;
		case LEFT:     axis = 2-axisSENW[dir]; dir =  axisRot[dir]; break;
		case RIGHT:    axis = 2-axisSENW[dir]; dir = -axisRot[dir]; break;
		case 'q':      axis = 1; dir =  1; break;
		case 'z':      axis = 1; dir = -1; break;
		default:       return False;
		}
		if (selection.nudgeStep == 16 && selection.nudgePoint == 3)
		{
			/* move by integral amount of selection size */
			dir *= fabsf(selection.firstPt[axis] - selection.secondPt[axis]) + 1;
		}
		if (selection.nudgePoint & 1)
			selection.firstPt[axis] += dir;
		if (selection.nudgePoint & 2)
			selection.secondPt[axis] += dir;
		if (selection.nudgePoint & 4)
			selection.clonePt[axis] += dir,
			selectionSetClonePt(NULL, SEL_CLONEPT_IS_SET);
		selectionSetRect(SEL_POINT_BOX);
		return True;
	}
	else if (selection.brush)
	{
		static STRPTR ctrlName[] = {"rotate", "roll", "flip", "mirror"};
		uint8_t ctrl;
		switch (key) {
		case 'r': ctrl = 0; selectionBrushRotate(); break;
		case 't': ctrl = 1; selectionBrushRoll(); break;
		case 'l': ctrl = 2; selectionBrushFlip(); break;
		case 'm': ctrl = 3; selectionBrushMirror(); break;
		default: return False;
		}
		/* highlight button used */
		double timeMS = FrameGetTime();
		SIT_Widget w = SIT_GetById(selection.editBrush, ctrlName[ctrl]);
		SIT_SetValues(w, SIT_CheckState, True, NULL);
		SIT_ActionAdd(w, timeMS + 100, timeMS + 100, cancelActivation, NULL);
	}
	else if (key == 'r' && renderRotatePreview(mod & SITK_FlagShift ? -1 : 1))
	{
		return True;
	}
	return False;
}

/* set the position of one of the 2 extended selection point */
void selectionSetPoint(float scale, vec4 pos, int point)
{
	memcpy(point ? selection.secondPt : selection.firstPt, pos, 16);

	globals.selPoints |= 1<<point;

	if (globals.selPoints == 3)
	{
		if (selection.nudgeDiag == NULL)
		{
			SIT_Widget diag = selection.nudgeDiag = SIT_CreateWidget("selection.mc", SIT_DIALOG, globals.app,
				SIT_DialogStyles,  SITV_Plain,
				SIT_Bottom,        SITV_AttachForm, NULL, (int) (24 * scale),
				SIT_TopAttachment, SITV_AttachNone,
				NULL
			);
			SIT_CreateWidgets(diag,
				"<button name=whole title=Nudge left=", SITV_AttachPosition, SITV_AttachPos(50), SITV_OffsetCenter, ">"
				"<label name=size top=WIDGET,whole,0.3em left=FORM right=FORM style='text-align: center; color: white'>"
				"<button name=first title=Nudge top=WIDGET,size,0.3em>"
				"<button name=second title=Nudge top=OPPOSITE,first left=WIDGET,first,0.5em>"
			);
			selection.nudgeSize = SIT_GetById(diag, "size");
			SIT_AddCallback(SIT_GetById(diag, "whole"),  SITE_OnClick, selectionNudge, (APTR) 3);
			SIT_AddCallback(SIT_GetById(diag, "first"),  SITE_OnClick, selectionNudge, (APTR) 1);
			SIT_AddCallback(SIT_GetById(diag, "second"), SITE_OnClick, selectionNudge, (APTR) 2);
			selectionSetRect(SEL_POINT_BOX);
			SIT_ManageWidget(diag);
		}
		else selectionSetRect(SEL_POINT_BOX);
	}
}

/* remove everything about extended selection */
void selectionCancel(void)
{
	if (selection.nudgeDiag)
	{
		SIT_CloseDialog(selection.nudgeDiag);
		selection.nudgeDiag = NULL;
		selection.nudgeSize = NULL;
	}
	if (selection.brush)
		selectionCancelClone(NULL, NULL, NULL);
	globals.selPoints = 0;
}

vec selectionGetPoints(void)
{
	return selection.firstPt;
}

/* draw selection point/boxes */
static void selectionDrawPoint(vec4 point, int pointId)
{
	/* first: draw fllled box */
	vec4 loc = {
		point[VX] - VTX_EPSILON,
		point[VY] - VTX_EPSILON,
		point[VZ] - VTX_EPSILON, pointId*4+4+1
	};
	glEnable(GL_DEPTH_TEST);
	glProgramUniform4fv(selection.shader, selection.infoLoc, 1, loc);
	switch (pointId) {
	case 0: loc[VT] --;   glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_SHORT, 0); break; /* first pt */
	case 1: loc[VT] --;   glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_SHORT, (APTR) ((24+36)*2)); break; /* first pt */
	case 2: loc[VT] = 0;  glDrawArrays(GL_TRIANGLES, 8*2, 36); break; /* box that cover 1st and 2nd point */
	case 3: loc[VT] = 20; glDrawArraysInstanced(GL_TRIANGLES, 8*2+36+24, 36, selection.cloneRepeat);  /* box that cover cloned selection */
	}

	glDisable(GL_DEPTH_TEST);
	glProgramUniform4fv(selection.shader, selection.infoLoc, 1, loc);
	switch (pointId) {
	case 0: glDrawElements(GL_LINES, 24, GL_UNSIGNED_SHORT, (APTR) (36 * 2)); break;
	case 1: glDrawElements(GL_LINES, 24, GL_UNSIGNED_SHORT, (APTR) ((24+36*2)*2)); break;
	case 2: glDrawArrays(GL_LINES, 8*2+36, 24); break;
	case 3: glDrawArraysInstanced(GL_LINES, 8*2+36*2+24, 24, selection.cloneRepeat);
	}
}

/* render everything related to selection: points, box, brush */
void selectionRender(void)
{
	if (globals.selPoints)
	{
		glDepthMask(GL_FALSE);
		glUseProgram(selection.shader);
		glBindVertexArray(selection.vao);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, selection.vboIndex);

		switch (globals.selPoints & 3) {
		case 1: selectionDrawPoint(selection.firstPt,  0); break;
		case 2: selectionDrawPoint(selection.secondPt, 1); break;
		case 3: selectionDrawPoint(selection.firstPt,  0);
		        selectionDrawPoint(selection.secondPt, 1);
		        selectionDrawPoint(selection.regionPt, 2);
		}
		if (globals.selPoints & (1 << SEL_POINT_CLONE))
		{
			/* draw the brush (only once, no matter how many repeats there are) */
			glDepthMask(GL_TRUE);
			glEnable(GL_DEPTH_TEST);
			renderDrawMap(selection.brush);
			glDisable(GL_DEPTH_TEST);

			/* box overlay */
			glDepthMask(GL_FALSE);
			glUseProgram(selection.shader);
			glBindVertexArray(selection.vao);
			selectionDrawPoint(selection.clonePt, 3);
		}
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
		glEnable(GL_DEPTH_TEST);
		glDepthMask(GL_TRUE);
	}
}

/*
 * clone selection tool: create a mini-map from the selected blocks
 */
#define chunkDeleteIterTE(iter,ext)    chunkDeleteTileEntity((iter).ref, (int[3]){(iter).x-1, (iter).yabs-1, (iter).z-1}, ext)
#define chunkAddIterTE(iter,tile) \
{ \
	int xyz[] = {(iter).x-1, (iter).yabs-1, (iter).z-1}; \
	chunkAddTileEntity((iter).ref, xyz, tile); \
	chunkUpdateTilePosition((iter).ref, xyz, tile); \
}


void selectionSetClonePt(vec4 pos, int side)
{
	static uint8_t axis[] = { /* S, E, N, W, T, B */
		0, 1, 2,  0,
		2, 1, 0,  0,
		0, 1, 2,  1,
		2, 1, 0,  1,
		0, 2, 1,  0,
		0, 2, 1,  1,
	};

	DATA8 off = axis + (side&7) * 4;
	int i, j;

	if (side >= 0)
	{
		i = off[0]; selection.clonePt[i] = pos[i] - floorf(selection.cloneSize[i] * 0.5f);
		i = off[1]; selection.clonePt[i] = pos[i] - floorf(selection.cloneSize[i] * 0.5f);
		i = off[2]; selection.clonePt[i] = pos[i] + (off[3] ? -floorf(selection.cloneSize[i]) : 1);
	}

	/* offset from original selection */
	if (selection.editBrush)
	{
		for (i = 0; i < 3; i ++)
		{
			selection.cloneOff[i] = selection.clonePt[i] - selection.regionPt[i];
			if (side != SEL_CLONEOFF_IS_SET)
				SIT_SetValues(selection.brushOff[i], SIT_Title, NULL, NULL);
		}

		/* set VBO location for instanced rendering */
		glBindBuffer(GL_ARRAY_BUFFER, selection.vboLOC);
		vec loc = glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);

		for (i = selection.cloneRepeat, j = 0; i > 0; i --, j ++, loc += 3)
		{
			loc[VX] = selection.clonePt[VX] + selection.cloneOff[VX] * j - VTX_EPSILON;
			loc[VY] = selection.clonePt[VY] + selection.cloneOff[VY] * j - VTX_EPSILON;
			loc[VZ] = selection.clonePt[VZ] + selection.cloneOff[VZ] * j - VTX_EPSILON;
		}

		glUnmapBuffer(GL_ARRAY_BUFFER);
	}

	/* set brush location in world coord */
	if (selection.brush)
	{
		Map brush = selection.brush;
		pos = selection.clonePt;
		brush->cx = pos[0] - 1;
		brush->cy = pos[1] - 1;
		brush->cz = pos[2] - 1;
	}

	if (side & SEL_CLONEMOVE_STOP)
		renderSetSelectionPoint(RENDER_SEL_STOPMOVE);
}

/* SITE_OnChange on brush offsets */
static int selectionChangeCoord(SIT_Widget w, APTR cd, APTR ud)
{
	int axis = (int) ud;
	selection.clonePt[axis] = selection.regionPt[axis] + selection.cloneOff[axis];
	selectionSetClonePt(NULL, SEL_CLONEOFF_IS_SET);
	return 1;
}

/* SITE_OnChange on repeat count */
static int selectionRepeat(SIT_Widget w, APTR cd, APTR ud)
{
	selectionSetClonePt(NULL, SEL_CLONEOFF_IS_SET);
	return 1;
}

/* copy blocks from brush into map */
int selectionCopyBlocks(SIT_Widget w, APTR cd, APTR ud)
{
	struct BlockIter_t dst;
	struct BlockIter_t src;
	Map  brush = selection.brush;
	Map  map   = globals.level;
	vec4 pos;

	memcpy(pos, selection.clonePt, sizeof pos);
	mapInitIter(map, &dst, selection.clonePt, True);
	mapInitIterOffset(&src, brush->firstVisible, 256+16+1);
	mapUpdateInit(&dst);
	src.nbor = brush->chunkOffsets;

	uint8_t air = selection.copyAir;
	uint8_t water = selection.copyWater;
	uint8_t count = selection.cloneRepeat; /* max repeat is 128 */

	while (dst.yabs < BUILD_HEIGHT)
	{
		int dx = brush->size[VX] - 2;
		int dy = brush->size[VY] - 2;
		int dz = brush->size[VZ] - 2;
		while (dy > 0)
		{
			int z, x;
			for (z = 0; z < dz; z ++, mapIter(&src, -dx, 0, 1), mapIter(&dst, -dx, 0, 1))
			{
				for (x = 0; x < dx; x ++, mapIter(&src, 1, 0, 0), mapIter(&dst, 1, 0, 0))
				{
					uint8_t data = src.blockIds[DATA_OFFSET + (src.offset >> 1)];
					int blockId = (src.blockIds[src.offset] << 4) | (src.offset & 1 ? data >> 4 : data & 15);
					if (dst.ref == NULL) continue; /* outside map :-/ */
					if (air == 0 && blockId == 0) continue;
					if (water == 0 && blockGetById(blockId)->special == LIKID) continue;
					DATA8 tile = chunkDeleteIterTE(src, True);
					mapUpdate(map, NULL, blockId, tile, UPDATE_SILENT);
				}
			}
			mapIter(&src, 0, 1, -dz);
			mapIter(&dst, 0, 1, -dz);
			dy --;
		}
		count --;
		if (count == 0) break;

		pos[VX] += selection.cloneOff[VX];
		pos[VY] += selection.cloneOff[VY];
		pos[VZ] += selection.cloneOff[VZ];

		mapInitIter(map, &dst, pos, True);
		mapInitIterOffset(&src, brush->firstVisible, 256+16+1);
	}

	mapUpdateEnd(map);
	selectionCancelClone(NULL, NULL, NULL);
	return 1;
}

/* SITE_OnActivate: initiate rotate, flip, mirror or roll */
static int selectionTransform(SIT_Widget w, APTR cd, APTR ud)
{
	switch ((int) ud) {
	case 0: selectionBrushRotate(); break;
	case 1: selectionBrushRoll(); break;
	case 2: selectionBrushFlip(); break;
	case 3: selectionBrushMirror(); break;
	}
	return 1;
}

/* constructor */
Map selectionAllocBrush(uint16_t sizes[3])
{
	uint16_t chunks[] = {
		(sizes[VX] + 15) >> 4,
		(sizes[VY] + 15) >> 4,
		(sizes[VZ] + 15) >> 4
	};

	int grid  = chunks[VX] * chunks[VZ];
	int total = grid * (chunks[VY] * (SKYLIGHT_OFFSET + sizeof (struct ChunkData_t)) + sizeof (struct Chunk_t)) + sizeof (struct Map_t);

	Map brush = calloc(1, total);

	if (brush)
	{
		ChunkData cd;
		Chunk     chunk;
		DATA8     blocks;
		int       x, y, z;

		brush->chunkOffsets = (DATAS16) brush->path;
		brush->chunks = (Chunk) (brush + 1);
		brush->GPUMaxChunk = grid * chunks[VY] * (16 * 1024);
		if (brush->GPUMaxChunk > 512 * 1024)
			brush->GPUMaxChunk = 512 * 1024;
		/* numbers for these fields don't matter, as long as center won't be relocated */
		brush->maxDist = total;
		brush->mapArea = -1;
		cd = (ChunkData) (brush->chunks + grid);
		blocks = (DATA8) (cd + grid * chunks[VY]);
		memcpy(brush->size, sizes, 6);

		/* does not matter: there will be no wrap around chunks */
		brush->center = brush->chunks;
		/* mapInitIter() / mapIter() need to be working though */
		for (x = 0; x < 16; x ++)
		{
			int offset = 0;
			if (x & 1) offset += chunks[VX];
			if (x & 2) offset += 1;
			if (x & 4) offset -= chunks[VX];
			if (x & 8) offset -= 1;
			brush->chunkOffsets[x] = offset;
		}

		ChunkData * first = &brush->firstVisible;

		/* init Chunk and ChunkData */
		for (z = 0, chunk = brush->chunks; z < chunks[VZ]; z ++)
		{
			for (x = 0; x < chunks[VX]; x ++, chunk ++)
			{
				/* setup chunk */
				chunk->maxy = chunks[VY];
				chunk->cflags |= CFLAG_GOTDATA;
				chunk->X = x * 16;
				chunk->Z = z * 16;
				/* brush don't have lazy chunks all around */
				uint8_t missing = 0;
				if (x == 0) missing |= 1 << SIDE_WEST;
				if (z == 0) missing |= 1 << SIDE_NORTH;
				if (x == chunks[VX]-1) missing |= 1 << SIDE_EAST;
				if (z == chunks[VZ]-1) missing |= 1 << SIDE_SOUTH;
				chunk->noChunks = missing;
				for (y = 0; y < chunks[VY]; y ++, cd ++, blocks += SKYLIGHT_OFFSET)
				{
					chunk->layer[y] = cd;
					/* setup ChunkData */
					*first = cd;
					first = &cd->visible;
					cd->Y = y * 16;
					cd->chunk = chunk;
					/* will only contain blockId + data, no skylight or blocklight */
					cd->cdFlags = CDFLAG_NOLIGHT;
					cd->blockIds = blocks;
				}
			}
		}
	}
	return brush;
}

/* destructor */
void selectionFreeBrush(Map brush)
{
	Chunk chunk;
	int   count;
	if (! brush->sharedBanks)
		renderFreeMesh(brush, False);
	/* clear tile entites per chunk */
	for (chunk = brush->chunks, count = ((brush->size[VX] + 15) >> 4) * ((brush->size[VZ] + 15) >> 4); count > 0; count --, chunk ++)
		if (chunk->tileEntities) free(chunk->tileEntities);
	free(brush);
}

/* dialog to manipulate selection clone */
void selectionEditBrush(Bool simplified)
{
	TEXT buffer[32];
	SIT_Widget diag = selection.editBrush = SIT_CreateWidget("editbrush.mc", SIT_DIALOG, globals.app,
		SIT_DialogStyles,  SITV_Plain,
		SIT_Left,          SITV_AttachForm, NULL, SITV_Em(0.5),
		SIT_TopAttachment, SITV_AttachPosition, SITV_AttachPos(50), SITV_OffsetCenter,
		NULL
	);
	sprintf(buffer, "<pchar src=roll%s.png> Roll", selection.ext[globals.direction]);
	SIT_CreateWidgets(diag,
		"<label name=brotate title=R:>"
		"<button name=rotate.act title='<xchar src=rotate.png> Rotate' left=WIDGET,brotate,0.3em>"
		"<label name=broll title=T: left=WIDGET,rotate,1em>"
		"<button maxWidth=rotate name=roll.act title=", buffer, "left=WIDGET,broll,0.3em>"
		"<label name=bflip maxWidth=brotate title=L:>"
		"<button name=flip.act title='<pchar src=flip.png> Flip' maxWidth=roll top=WIDGET,broll,0.5em left=WIDGET,bflip,0.3em>"
		"<label name=bmirror maxWidth=broll title=M: left=WIDGET,flip,1em>"
		"<button name=mirror.act title='<xchar src=mirror.png> Mirror' maxWidth=flip top=WIDGET,broll,0.5em left=WIDGET,bmirror,0.3em>"

		"<button name=nudge title=Nudge nextCtrl=NONE right=FORM maxWidth=mirror>"
	);

	if (! simplified)
	{
		SIT_CreateWidgets(diag,
			"<label name=xlab title=X:><editbox name=xcoord curValue=", selection.cloneOff, "editType=", SITV_Integer,
			" right=WIDGET,nudge,1em left=WIDGET,xlab,0.3em top=WIDGET,mirror,1em>"
			"<label name=ylab title=Y: maxWidth=xlab><editbox name=ycoord curValue=", selection.cloneOff+1, "editType=", SITV_Integer,
			" right=WIDGET,nudge,1em left=WIDGET,ylab,0.3em top=WIDGET,xcoord,0.5em>"
			"<label name=zlab title=Z: maxWidth=ylab><editbox name=zcoord curValue=", selection.cloneOff+2, "editType=", SITV_Integer,
			" right=WIDGET,nudge,1em left=WIDGET,zlab,0.3em top=WIDGET,ycoord,0.5em>"

			"<label name=tlab title=... maxWidth=zlab>"
			"<editbox name=repeat curValue=", &selection.cloneRepeat, "editType=", SITV_Integer, "left=OPPOSITE,zcoord minValue=1 maxValue=128"
			" right=OPPOSITE,zcoord top=WIDGET,zcoord,1em>"
			"<label name=brep title=(Repeat) top=MIDDLE,repeat left=WIDGET,repeat,1em>"
		);
		SIT_SetAttributes(diag,
			"<xlab top=MIDDLE,xcoord><ylab top=MIDDLE,ycoord><zlab top=MIDDLE,zcoord>"
			"<nudge top=MIDDLE,ycoord><tlab top=MIDDLE,repeat>"
		);
	}
	else /* user selected an object from library.c: there are no extended selection in this case */
	{
		SIT_CreateWidgets(diag, "<label name=size top=WIDGET,nudge,1em left=", SITV_AttachPosition, SITV_AttachPos(50), SITV_OffsetCenter, ">");
		SIT_SetAttributes(diag, "<nudge left=", SITV_AttachPosition, SITV_AttachPos(50), SITV_OffsetCenter, "right=NONE top=WIDGET,mirror,0.5em>");
		selection.brushSize = SIT_GetById(diag, "size");
		selectionSetSize();
	}

	SIT_CreateWidgets(diag,
		"<button name=copyair title='Copy air'    curValue=", &selection.copyAir,    "top=WIDGET,repeat,1em    buttonType=", SITV_CheckBox, ">"
		"<button name=copywat title='Copy water'  curValue=", &selection.copyWater,  "top=WIDGET,copyair,0.5em buttonType=", SITV_CheckBox, ">"
		"<button name=copyent title='Copy entity' curValue=", &selection.copyEntity, "top=WIDGET,copywat,0.5em buttonType=", SITV_CheckBox, ">"

		"<button name=ko.act title=Cancel right=FORM top=WIDGET,copyent,1em>"
		"<button name=ok.act title=Clone  right=WIDGET,ko,0.5em top=OPPOSITE,ko buttonType=", SITV_DefaultButton, ">"
	);
	SIT_SetAttributes(diag,
		"<brotate top=MIDDLE,rotate><broll top=MIDDLE,roll><bflip top=MIDDLE,flip><bmirror top=MIDDLE,mirror>"
	);
	if (simplified) SIT_SetAttributes(diag, "<copyair topObject=size>");
	int i;
	for (i = 0; i < 3; i ++)
	{
		static STRPTR editBoxes[] = {"xcoord", "ycoord", "zcoord"};
		SIT_AddCallback(selection.brushOff[i] = SIT_GetById(diag, editBoxes[i]), SITE_OnChange, selectionChangeCoord, (APTR) i);
	}
	SIT_AddCallback(SIT_GetById(diag, "nudge"), SITE_OnClick, selectionNudge, (APTR) 4);
	SIT_AddCallback(SIT_GetById(diag, "repeat"), SITE_OnChange, selectionRepeat, NULL);
	SIT_AddCallback(SIT_GetById(diag, "ok"), SITE_OnActivate, selectionCopyBlocks, NULL);
	SIT_AddCallback(SIT_GetById(diag, "ko"), SITE_OnActivate, selectionCancelClone, NULL);
	SIT_AddCallback(SIT_GetById(diag, "rotate"), SITE_OnActivate, selectionTransform, NULL);
	SIT_AddCallback(SIT_GetById(diag, "roll"), SITE_OnActivate, selectionTransform, (APTR) 1);
	SIT_AddCallback(SIT_GetById(diag, "flip"), SITE_OnActivate, selectionTransform, (APTR) 2);
	SIT_AddCallback(SIT_GetById(diag, "mirror"), SITE_OnActivate, selectionTransform, (APTR) 3);
	SIT_ManageWidget(diag);
}

/* copy selected blocks into a mini-map */
Map selectionClone(vec4 pos, int side, Bool genMesh)
{
	if (globals.selPoints != 3)
		return NULL;

	Map map = globals.level;

	if (pos)
	{
		selection.cloneSize[VX] = fabsf(selection.firstPt[VX] - selection.secondPt[VX]) + 1;
		selection.cloneSize[VY] = fabsf(selection.firstPt[VY] - selection.secondPt[VY]) + 1;
		selection.cloneSize[VZ] = fabsf(selection.firstPt[VZ] - selection.secondPt[VZ]) + 1;

		selectionSetRect(SEL_POINT_CLONE);

		if (! selection.editBrush)
			selectionEditBrush(False);
	}

	if (selection.brush)
	{
		if (pos) selectionSetClonePt(pos, side);
		return selection.brush;
	}

	uint16_t sizes[] = {
		fabsf(selection.firstPt[VX] - selection.secondPt[VX]) + 3,
		fabsf(selection.firstPt[VY] - selection.secondPt[VY]) + 3,
		fabsf(selection.firstPt[VZ] - selection.secondPt[VZ]) + 3
	};

	/* the only data structure that can be relocated in the chunk grid */
	Map brush = selectionAllocBrush(sizes);

	if (brush)
	{
		/* copy blocks from map to the brush */
		vec4 srcPos = {
			fminf(selection.firstPt[VX], selection.secondPt[VX]),
			fminf(selection.firstPt[VY], selection.secondPt[VY]),
			fminf(selection.firstPt[VZ], selection.secondPt[VZ])
		};
		struct BlockIter_t src;
		struct BlockIter_t dst;
		Chunk chunk;
		int   x, y, z;
		mapInitIter(map, &src, srcPos, False);
		mapInitIterOffset(&dst, brush->firstVisible, 256+16+1);
		dst.nbor = brush->chunkOffsets;
		sizes[VX] -= 2;
		sizes[VY] -= 2;
		sizes[VZ] -= 2;
		/* note: we have to add a 1 block layer all around the brush to prevent face culling at the edge of chunk */
		for (y = 1; y <= sizes[VY]; y ++, mapIter(&src, 0, 1, -sizes[VZ]), mapIter(&dst, 0, 1, -sizes[VZ]))
		{
			for (z = 1; z <= sizes[VZ]; z ++, mapIter(&src, -sizes[VX], 0, 1), mapIter(&dst, -sizes[VX], 0, 1))
			{
				for (x = 1; x <= sizes[VX]; x ++, mapIter(&src, 1, 0, 0), mapIter(&dst, 1, 0, 0))
				{
					/* would be nice if we could use memcpy(), but DATA_OFFSET table is packed by 2 voxels per byte :-/ */
					uint8_t data = src.blockIds[DATA_OFFSET + (src.offset >> 1)];
					dst.blockIds[dst.offset] = src.blockIds[src.offset];
					if (src.offset & 1) data >>= 4; else data &= 15;
					dst.blockIds[DATA_OFFSET + (dst.offset>>1)] |= dst.offset & 1 ? data << 4 : data;

					/* also need to copy tile entities */
					DATA8 tile = chunkGetTileEntity(src.ref, (int[3]) {src.x, src.yabs, src.z});
					if (tile) chunkAddIterTE(dst, tile = NBT_Copy(tile));
				}
			}
		}

		if (genMesh)
		{
			/* convert all chunks into meshes */
			int chunksZ = (sizes[VZ] + 15) >> 4;
			int chunksX = (sizes[VX] + 15) >> 4;
			for (z = 0, chunk = brush->chunks; z < chunksZ; z ++)
			{
				for (x = 0; x < chunksX; x ++, chunk ++)
				{
					for (y = 0; y < chunk->maxy; y ++)
					{
						chunkUpdate(chunk, chunkAir, brush->chunkOffsets, y);
						/* transfer chunk to the GPU */
						renderFinishMesh(brush, True);
					}
				}
			}
			renderAllocCmdBuffer(brush);
		}
	}
	if (pos)
	{
		selection.brush = brush;
		globals.selPoints |= 8;
		selectionSetClonePt(pos, side);
	}
	return brush;
}

/* copy selection to library */
Map selectionCopy(void)
{
	Map brush;
 	if (selection.brush)
 	{
		Map map = selection.brush;

		/* copy blocks, but reuse mesh */
		brush = selectionAllocBrush(map->size);
		ChunkData src, dst;

		for (src = map->firstVisible, dst = brush->firstVisible; src && dst; src = src->visible, dst = dst->visible)
		{
			memcpy(dst->blockIds, src->blockIds, SKYLIGHT_OFFSET);
			dst->glBank  = src->glBank;
			dst->glSize  = src->glSize;
			dst->glAlpha = src->glAlpha;
			dst->glSlot  = src->glSlot;
		}

		brush->gpuBanks = map->gpuBanks;
		brush->sharedBanks = 1;
	}
	else /* create on the fly */
	{
		brush = selectionClone(NULL, 0, True);
	}
	return brush;
}

Map selectionCopyShallow(void)
{
	if (selection.brush)
		return selection.brush;

	Map temp = selectionClone(NULL, 0, False);
	/* need to be marked as needed to be free()'d, because we won't keep any ref here */
	temp->GPUMaxChunk = 0;
	return temp;
}

/* copy library brush to selection */
void selectionUseBrush(Map lib, Bool dup)
{
	Map brush;
	if (dup)
	{
		brush = selectionAllocBrush(lib->size);
		if (brush == NULL) return;

		ChunkData src, dst;
		for (src = lib->firstVisible, dst = brush->firstVisible; src && dst; src = src->visible, dst = dst->visible)
			memcpy(dst->blockIds, src->blockIds, SKYLIGHT_OFFSET);
	}
	else brush = lib;

	Chunk dstChunk;
	Chunk srcChunk;
	int chunksZ = (brush->size[VZ] + 15) >> 4;
	int chunksX = (brush->size[VX] + 15) >> 4;
	int x, y, z;
	for (z = 0, dstChunk = brush->chunks, srcChunk = lib->chunks; z < chunksZ; z ++)
	{
		for (x = 0; x < chunksX; x ++, dstChunk ++, srcChunk ++)
		{
			for (y = 0; y < dstChunk->maxy; y ++)
			{
				if (dup)
				{
					DATA8 tile;
					int   XYZ[3], offset;
					for (offset = 0; (tile = chunkIterTileEntity(srcChunk, &offset, XYZ)); )
						chunkAddTileEntity(dstChunk, XYZ, NBT_Copy(tile));
				}
				chunkUpdate(dstChunk, chunkAir, brush->chunkOffsets, y);
				renderFinishMesh(brush, True);
			}
		}
	}
	renderAllocCmdBuffer(brush);

	/* cancel previous brush if any */
	selectionCancel();

	/* make the new one active */
	selection.brush = brush;

	selection.cloneSize[VX] = lib->size[VX] - 2;
	selection.cloneSize[VY] = lib->size[VY] - 2;
	selection.cloneSize[VZ] = lib->size[VZ] - 2;

	globals.selPoints |= 1 << SEL_POINT_CLONE;

	/* green rectangle around brush */
	selectionSetRect(SEL_POINT_CLONE);
	/* simplified edit window */
	selectionEditBrush(True);
	renderSetSelectionPoint(RENDER_SEL_AUTOMOVE);
}


/* remove everything related to cloned selection */
int selectionCancelClone(SIT_Widget w, APTR cd, APTR ud)
{
	int ret = 0;
	if (selection.brush)
	{
		selectionFreeBrush(selection.brush);
		selection.brush = NULL;
		ret = 1;
	}
	if (selection.editBrush)
	{
		SIT_CloseDialog(selection.editBrush);
		selection.editBrush = NULL;
		selection.brushSize = NULL;
		ret = 1;
	}
	globals.selPoints &= ~8;
	return ret;
}

/*
 * brush manipulation: rotate, flip, mirror, roll
 */

#define mapUpdateData(iterptr, data) \
{ \
	DATA8 dataTbl = (iterptr)->blockIds + DATA_OFFSET + ((iterptr)->offset >> 1); \
	if ((iterptr)->offset & 1) \
		dataTbl[0] = (dataTbl[0] & 15) | ((data) << 4); \
	else \
		dataTbl[0] = (dataTbl[0] & 0xf0) | (data); \
}

/* rotate brush along Y axis by 90deg CW */
static void selectionBrushRotate(void)
{
	/* we can rotate the brush Y layer per Y layer */
	Map brush = selection.brush;
	int chunkX = (brush->size[VX] + 15) >> 4;
	int chunkZ = (brush->size[VZ] + 15) >> 4;
	int size = chunkX * chunkZ;
	int dx, dy, dz, x, y, z;

	DATA8 layer = size <= 6 ? alloca(size * 384) : malloc(size * 384);
	memset(layer, 0, size * 384);

	struct BlockIter_t iter;
	Chunk c;
	mapInitIterOffset(&iter, brush->firstVisible, 256+16+1);
	iter.nbor = brush->chunkOffsets;

	dx = brush->size[VX] - 2;
	dy = brush->size[VY] - 2;
	dz = brush->size[VZ] - 2;
	/* relocate blocks + data */
	for (y = 1; y <= dy; y ++, mapIter(&iter, 0, 1, -dz))
	{
		for (z = 1; z <= dz; z ++, mapIter(&iter, -dx, 0, 1))
		{
			for (x = 1; x <= dx; x ++, mapIter(&iter, 1, 0, 0))
			{
				/* these 2 assignments will do the 90deg CW rotation */
				int x2 = dz - z + 1;
				int z2 = x;
				int off = 0;
				DATA8 tile = chunkDeleteIterTE(iter, True);
				if (tile)
				{
					struct BlockIter_t te = iter;
					mapIter(&te, x2 - x, 0, z2 - z);
					chunkAddIterTE(te, tile);
				}
				if (x2 > 15) off += 384 * (x2 >> 4), x2 &= 15;
				if (z2 > 15) off += 384 * chunkX * (z2 >> 4), z2 &= 15;
				off += z2*16 + x2;
				int blockId = blockRotateY90(getBlockId(&iter));
				layer[off] = blockId >> 4; blockId &= 15;
				layer[256 + (off >> 1)] |= off & 1 ? blockId << 4 : blockId;
			}
		}
		/* copy temp layer into brush */
		DATA8 blocks;
		for (z = 0, blocks = layer, c = brush->chunks; z < chunkZ; z ++)
		{
			for (x = 0; x < chunkX; x ++, blocks += 384, c ++)
			{
				DATA8 dest = c->layer[y>>4]->blockIds;
				memcpy(dest + iter.y * 256, blocks, 256);
				memcpy(dest + iter.y * 128 + DATA_OFFSET, blocks + 256, 128);
				/* cleanup for next layer */
				memset(blocks, 0, 384);
			}
		}
	}
	if (size > 6) free(layer);
	brush->size[VX] = dz+2;
	brush->size[VZ] = dx+2;

	/* relocate chunk coord and regenerate mesh */
	swap(chunkX, chunkZ);
	float diff = (selection.cloneSize[VX] - selection.cloneSize[VZ]) * 0.5f;
	selection.clonePt[VX] += roundf(diff);
	selection.clonePt[VZ] += roundf(-diff);
	/* chunk grid will be the same size */
	for (z = 0, c = brush->chunks; z < chunkZ; z ++)
	{
		for (x = 0; x < chunkX; x ++, c ++)
		{
			uint8_t missing = 0;
			if (x == 0) missing |= 1 << SIDE_WEST;
			if (z == 0) missing |= 1 << SIDE_NORTH;
			if (x == chunkX-1) missing |= 1 << SIDE_EAST;
			if (z == chunkZ-1) missing |= 1 << SIDE_SOUTH;
			c->noChunks = missing;
			c->X = x * 16;
			c->Z = z * 16;

			for (dy = 0; dy < c->maxy; dy ++)
			{
				chunkUpdate(c, chunkAir, brush->chunkOffsets, dy);
				/* transfer chunk to the GPU */
				renderFinishMesh(brush, True);
			}
		}
	}
	renderAllocCmdBuffer(brush);

	/* swap sizes for X and Z axis */
	vec   sz  = selection.cloneSize;
	float tmp = sz[VX];
	sz[VX] = sz[VZ];
	sz[VZ] = tmp;

	selectionSetRect(SEL_POINT_CLONE);
	selectionSetClonePt(NULL, -1);
}


/* roll: rotate brush on X or Z axis */
static void selectionBrushRoll(void)
{
	Map brush = selection.brush;
	int dx, dy, dz, x, y, z;

	struct BlockIter_t src;
	struct BlockIter_t dst;
	mapInitIterOffset(&src, brush->firstVisible, 256+16+1);
	src.nbor = brush->chunkOffsets;

	dx = brush->size[VX] - 2;
	dy = brush->size[VY] - 2;
	dz = brush->size[VZ] - 2;

	if (dx > BUILD_HEIGHT) return;

	/* way too many things to relocate when doing in place modifications: WAY TOO MANY */
	Map roll;
	if (globals.direction & 1)
		roll = selectionAllocBrush((uint16_t[3]) {dx+2, dz+2, dy+2});
	else
		roll = selectionAllocBrush((uint16_t[3]) {dy+2, dx+2, dz+2});
	if (! roll) return;
	mapInitIterOffset(&dst, roll->firstVisible, 256+16+1);
	dst.nbor = roll->chunkOffsets;

	/* relocate blocks + data */
	if (globals.direction & 1)
	{
		int oz = 1, oy = 1;
		/* rotate along X axis */
		for (x = 1; x <= dx; x ++, mapIter(&src, 1, -dy, 0), mapIter(&dst, 1, 0, 0))
		{
			for (y = 1; y <= dy; y ++, mapIter(&src, 0, 1, -dz))
			{
				for (z = 1; z <= dz; z ++, mapIter(&src, 0, 0, 1))
				{
					int z2 = dy - y + 1;
					int y2 = z;
					int blockId = blockRotateX90(&src);
					mapIter(&dst, 0, y2 - oy, z2 - oz);
					oy = y2; oz = z2;
					dst.blockIds[dst.offset] = blockId >> 4;
					mapUpdateData(&dst, blockId & 15);

					DATA8 tile = chunkDeleteIterTE(src, True);
					if (tile) chunkAddIterTE(dst, tile);
				}
			}
		}
		/* center the new brush in the center of the old */
		float diff = (selection.cloneSize[VZ] - selection.cloneSize[VY]) * 0.5f;
		selection.clonePt[VZ] += roundf(diff);
		selection.clonePt[VY] += roundf(-diff);
		diff = selection.cloneSize[VZ];
		selection.cloneSize[VZ] = selection.cloneSize[VY];
		selection.cloneSize[VY] = diff;
	}
	else /* rotate along Z axis */
	{
		int ox = 1, oy = 1;
		/* rotate along X axis: camera viewing direction = axis of rotation */
		for (z = 1; z <= dz; z ++, mapIter(&src, 0, -dy, 1), mapIter(&dst, 0, 0, 1))
		{
			for (y = 1; y <= dy; y ++, mapIter(&src, -dx, 1, 0))
			{
				for (x = 1; x <= dx; x ++, mapIter(&src, 1, 0, 0))
				{
					int x2 = dy - y + 1;
					int y2 = x;
					int blockId = blockRotateZ90(&src);
					mapIter(&dst, x2 - ox, y2 - oy, 0);
					oy = y2; ox = x2;
					dst.blockIds[dst.offset] = blockId >> 4;
					mapUpdateData(&dst, blockId & 15);

					DATA8 tile = chunkDeleteIterTE(src, True);
					if (tile) chunkAddIterTE(dst, tile);
				}
			}
		}
		float diff = (selection.cloneSize[VX] - selection.cloneSize[VY]) * 0.5f;
		selection.clonePt[VX] += roundf(diff);
		selection.clonePt[VY] += roundf(-diff);
		diff = selection.cloneSize[VX];
		selection.cloneSize[VX] = selection.cloneSize[VY];
		selection.cloneSize[VY] = diff;
	}

	/* not needed anymore */
	selectionFreeBrush(brush);
	selection.brush = roll;

	/* regen mesh */
	Chunk chunk;
	dz = (roll->size[VZ] + 15) >> 4;
	dx = (roll->size[VX] + 15) >> 4;
	for (z = 0, chunk = roll->chunks; z < dz; z ++)
	{
		for (x = 0; x < dx; x ++, chunk ++)
		{
			for (y = 0; y < chunk->maxy; y ++)
			{
				chunkUpdate(chunk, chunkAir, roll->chunkOffsets, y);
				/* transfer chunk to the GPU */
				renderFinishMesh(roll, True);
			}
		}
	}
	renderAllocCmdBuffer(roll);

	selectionSetRect(SEL_POINT_CLONE);
	selectionSetClonePt(NULL, -1);
}


/* flip (or mirror) brush on Y axis */
static void selectionBrushFlip(void)
{
	Map brush = selection.brush;
	int dx, dy, dz, x, y, z;

	dx = brush->size[VX] - 2;
	dy = brush->size[VY] - 2;
	dz = brush->size[VZ] - 2;

	struct BlockIter_t iterHI;
	struct BlockIter_t iterLO;
	mapInitIterOffset(&iterLO, brush->firstVisible, 256+16+1);
	iterLO.nbor = brush->chunkOffsets;
	iterHI = iterLO;
	mapIter(&iterHI, 0, dy - 1, 0);

	/* relocate blocks + data */
	for (y = 1; iterLO.yabs < iterHI.yabs; y ++, mapIter(&iterLO, 0, 1, -dz), mapIter(&iterHI, 0, -1, -dz))
	{
		for (z = 1; z <= dz; z ++, mapIter(&iterLO, -dx, 0, 1), mapIter(&iterHI, -dx, 0, 1))
		{
			for (x = 1; x <= dx; x ++, mapIter(&iterLO, 1, 0, 0), mapIter(&iterHI, 1, 0, 0))
			{
				/* exchange LO & HI blockId, flipping them in the process */
				int idLO = blockMirrorY(&iterHI);
				int idHI = blockMirrorY(&iterLO);

				iterHI.blockIds[iterHI.offset] = idHI >> 4;
				iterLO.blockIds[iterLO.offset] = idLO >> 4;
				DATA8 tileLO = chunkDeleteIterTE(iterHI, True);
				DATA8 tileHI = chunkDeleteIterTE(iterLO, True);
				mapUpdateData(&iterLO, idLO & 15);
				mapUpdateData(&iterHI, idHI & 15);
				if (tileLO) chunkAddIterTE(iterLO, tileLO);
				if (tileHI) chunkAddIterTE(iterHI, tileHI);
			}
		}
	}
	if (iterLO.yabs == iterHI.yabs)
	{
		/* odd high brush: need to flip one layer alone */
		for (z = 1; z <= dz; z ++, mapIter(&iterLO, -dx, 0, 1))
		{
			for (x = 1; x <= dx; x ++, mapIter(&iterLO, 1, 0, 0))
			{
				/* tile entities won't be moved, no need to check for them */
				int id = blockMirrorY(&iterLO);
				iterLO.blockIds[iterLO.offset] = id >> 4;
				mapUpdateData(&iterLO, id&15);
			}
		}
	}

	/* regenerate mesh of brush */
	Chunk c;
	dx = (brush->size[VX] + 15) >> 4;
	dz = (brush->size[VZ] + 15) >> 4;
	for (z = 0, c = brush->chunks; z < dz; z ++)
	{
		for (x = 0; x < dx; x ++, c ++)
		{
			for (y = 0, dy = c->maxy; y < dy; y ++)
			{
				chunkUpdate(c, chunkAir, brush->chunkOffsets, y);
				/* transfer chunk to the GPU */
				renderFinishMesh(brush, True);
			}
		}
	}
	renderAllocCmdBuffer(brush);
}

/* mirror brush on X or Z axis */
typedef int (*BlockTransform_t)(BlockIter);

static void selectionBrushMirror(void)
{
	Map brush = selection.brush;
	int dx, dy, dz, x, y, z, axisEW;
	int iterX, iterZ;

	dx = brush->size[VX] - 2;
	dy = brush->size[VY] - 2;
	dz = brush->size[VZ] - 2;
	/* mirror will be done perdendicular to camera viewing direction */
	axisEW = (globals.direction & 1) == 0;

	struct BlockIter_t iterMIN;
	struct BlockIter_t iterMAX;
	BlockTransform_t   trans;
	int8_t             dirX, dirZ;
	mapInitIterOffset(&iterMIN, brush->firstVisible, 256+16+1);
	iterMIN.nbor = brush->chunkOffsets;
	iterMAX = iterMIN;
	iterZ   = dz;
	iterX   = dx;
	if (axisEW) mapIter(&iterMAX, dx - 1, 0, 0), trans = blockMirrorX, iterX >>= 1, dirX = -1, dirZ =  1;
	else        mapIter(&iterMAX, 0, 0, dz - 1), trans = blockMirrorZ, iterZ >>= 1, dirX =  1, dirZ = -1;


	/* relocate blocks + data */
	for (y = 1; y <= dy; y ++, mapIter(&iterMIN, 0, 1, -iterZ), mapIter(&iterMAX, 0, 1, -iterZ * dirZ))
	{
		for (z = 1; z <= iterZ; z ++, mapIter(&iterMIN, -iterX, 0, 1), mapIter(&iterMAX, -iterX * dirX, 0, dirZ))
		{
			for (x = 1; x <= iterX; x ++, mapIter(&iterMIN, 1, 0, 0), mapIter(&iterMAX, dirX, 0, 0))
			{
				/* exchange LO & HI blockId, mirroring them in the process */
				int idMIN = trans(&iterMAX);
				int idMAX = trans(&iterMIN);

				iterMAX.blockIds[iterMAX.offset] = idMAX >> 4;
				iterMIN.blockIds[iterMIN.offset] = idMIN >> 4;
				mapUpdateData(&iterMIN, idMIN & 15);
				mapUpdateData(&iterMAX, idMAX & 15);
				DATA8 tileMIN = chunkDeleteIterTE(iterMAX, True);
				DATA8 tileMAX = chunkDeleteIterTE(iterMIN, True);
				if (tileMIN) chunkAddIterTE(iterMIN, tileMIN);
				if (tileMAX) chunkAddIterTE(iterMAX, tileMAX);
			}
			if (axisEW && iterMIN.x == iterMAX.x)
			{
				/* odd width: need to transform a single block */
				int id = trans(&iterMIN);
				iterMIN.blockIds[iterMIN.offset] = id >> 4;
				mapUpdateData(&iterMIN, id & 15);
			}
		}
		if (axisEW == 0 && iterMIN.z == iterMAX.z)
		{
			/* odd length */
			for (x = 1; x <= iterX; x ++, mapIter(&iterMIN, 1, 0, 0))
			{
				int id = trans(&iterMIN);
				iterMIN.blockIds[iterMIN.offset] = id >> 4;
				mapUpdateData(&iterMIN, id & 15);
			}
			mapIter(&iterMIN, -iterX, 0, 0);
		}
	}

	/* regenerate mesh of brush */
	Chunk c;
	dx = (brush->size[VX] + 15) >> 4;
	dz = (brush->size[VZ] + 15) >> 4;
	for (z = 0, c = brush->chunks; z < dz; z ++)
	{
		for (x = 0; x < dx; x ++, c ++)
		{
			for (y = 0, dy = c->maxy; y < dy; y ++)
			{
				chunkUpdate(c, chunkAir, brush->chunkOffsets, y);
				/* transfer chunk to the GPU */
				renderFinishMesh(brush, True);
			}
		}
	}
	renderAllocCmdBuffer(brush);
}

/*
 * select similar blocks
 */
void selectionAutoSelect(vec4 pos, float scale)
{
	DATA8  visited = calloc(1, 4096); /* bitfield for a 32x32x32 area */
	int8_t minMax[8];

	/* work is done in mapUpdate.c because of that ring buffer */
	mapUpdateFloodFill(globals.level, pos, visited, minMax);
	free(visited);

	vec4 pt1 = {pos[VX] + minMax[VX],   pos[VY] + minMax[VY],   pos[VZ] + minMax[VZ]};
	vec4 pt2 = {pos[VX] + minMax[VX+4], pos[VY] + minMax[VY+4], pos[VZ] + minMax[VZ+4]};
	selectionSetPoint(scale, pt1, SEL_POINT_1);
	selectionSetPoint(scale, pt2, SEL_POINT_2);
}

/*
 * selection manipulation : fill / replace / geometric brushes
 */
static struct
{
	DATA32 progress;
	int    blockId;
	int    side;
	int    facing;
	int    replId;
	int    similar;
	char   cancel;
	vec4   size;
}	selectionAsync;

/* globals.direction only look at S,E,N,W: this one check for S,E,N,W,T,B */
static int extendedDir(void)
{
	float pitch = globals.yawPitch[1];

	if (pitch > M_PI_4f)
		return SIDE_BOTTOM;
	else if (pitch < - M_PI_4f)
		return SIDE_TOP;
	else
		return selectionAsync.facing;
}

static int selectionAdjustOrient(int blockId, int orient, int dx, int dy, int dz)
{
	switch (orient) {
	case ORIENT_LOG:
		if (dy == 1)
		{
			if (dz == 1 && dx > 1) blockId |= 4; /* E/W beam */
			if (dx == 1 && dz > 1) blockId |= 8; /* N/S */
		}
		/* else upward beam */
		break;
	case ORIENT_RAILS:
		if (dy == 1)
		{
			if (dz == 1 && dx > 1) blockId |= 1; /* E/W */
			/* else N/S is 0 */
		}
		break;
	case ORIENT_NSWE: /* ladder, furnace, jack-o-lantern ... */
		{
			static uint8_t dir2nswe[] = {2, 4, 3, 5};
			blockId |= dir2nswe[selectionAsync.facing];
		}
		break;
	case ORIENT_STAIRS:
		{
			static uint8_t dir2stairs[] = {2, 0, 3, 1};
			blockId |= dir2stairs[selectionAsync.facing];
		}
		break;
	case ORIENT_FULL: /* piston */
		{
			static uint8_t dir2full[] = {2, 4, 3, 5, 1, 0};
			blockId &= ~15;
			blockId |= dir2full[extendedDir()];
		}
		break;
	case ORIENT_SWNE:
		{
			static uint8_t dir2swne[] = {2, 1, 0, 3};
			blockId |= dir2swne[selectionAsync.facing];
		}
		break;
	}
	return blockId;
}

/* thread to process fill command */
void selectionProcessFill(void * unused)
{
	struct BlockIter_t iter;
	Map  map;
	vec4 pos;
	int  dx, dy, dz, z, x, blockId, yinc;
	pos[VX] = MIN(selection.firstPt[VX], selection.secondPt[VX]);
	pos[VY] = MIN(selection.firstPt[VY], selection.secondPt[VY]);
	pos[VZ] = MIN(selection.firstPt[VZ], selection.secondPt[VZ]);
	dx = selection.regionSize[VX];
	dy = selection.regionSize[VY];
	dz = selection.regionSize[VZ];

	map = globals.level;
	blockId = selectionAsync.blockId;
	mapInitIter(map, &iter, pos, blockId > 0);

	/* updated in the order XZY (first to last) */
	mapUpdateInit(&iter);

	/* lock the mutex for main thread to know when we are finished here */
	MutexEnter(selection.wait);

	Block b = &blockIds[blockId>>4];

	if (b->opacSky < MAXSKY)
	{
		/* transparent to skylight: cheaper to start from top */
		mapIter(&iter, 0, dy-1, 0);
		yinc = -1;
	}
	else yinc = 1;

	if ((b->special == BLOCK_HALF || b->special == BLOCK_STAIRS) && selectionAsync.side > 0 /* top/bottom option */)
		blockId |= 8;

	blockId = selectionAdjustOrient(blockId, b->orientHint, dx, dy, dz);

	if (dy == 1 && dx > 2 && dz > 2 && b->special == BLOCK_STAIRS)
	{
		/* only build the outline of a rectangle with this block (typical use case: roof) */
		blockId |= 2;
		for (x = dx; x > 0; x--, mapIter(&iter, 1, 0, 0))
			mapUpdate(map, NULL, blockId, NULL, UPDATE_SILENT);

		blockId |= 3;
		mapIter(&iter, -dx, 0, dz-1);
		for (x = dx; x > 0; x--, mapIter(&iter, 1, 0, 0))
			mapUpdate(map, NULL, blockId, NULL, UPDATE_SILENT);

		blockId &= ~3;
		mapIter(&iter, -dx, 0, -dz+2);
		for (z = dz-2; z > 0; z--, mapIter(&iter, 0, 0, 1))
			mapUpdate(map, NULL, blockId, NULL, UPDATE_SILENT);

		blockId |= 1;
		mapIter(&iter, dx-1, 0, -dz+2);
		for (z = dz-2; z > 0; z--, mapIter(&iter, 0, 0, 1))
			mapUpdate(map, NULL, blockId, NULL, UPDATE_SILENT);

		/* no need to check if the operation is cancelled: this type of operation should be very fast */
		selectionAsync.progress[0] = dz*dx;
	}
	else while (dy > 0)
	{
		for (z = dz; z > 0; z --, mapIter(&iter, -dx, 0, 1))
		{
			for (x = dx; x > 0; x --, mapIter(&iter, 1, 0, 0))
			{
				/* DEBUG: slow down processing */
				// ThreadPause(500);
				mapUpdate(map, NULL, blockId, NULL, UPDATE_SILENT);
			}

			/* O(n^3) complexity functions better have a way to be cancelled */
			if (selectionAsync.cancel) goto break_all;
			selectionAsync.progress[0] += dx;
		}
		mapIter(&iter, 0, yinc, -dz);
		dy --;
	}
	/* note: mapUpdateEnd() will regen mesh, must no be called from here */
	break_all:
	MutexLeave(selection.wait);
}

/* this only starts selection processing */
int selectionFill(DATA32 progress, int blockId, int side, int direction)
{
	selectionAsync.progress = progress;
	selectionAsync.blockId  = blockId;
	selectionAsync.side     = side;
	selectionAsync.facing   = direction;
	selectionAsync.cancel   = 0;

	/* have to be careful with thread: don't call any opengl or SITGL function in them */
	ThreadCreate(selectionProcessFill, NULL);

	return (int) selection.regionSize[VX] *
	       (int) selection.regionSize[VY] *
	       (int) selection.regionSize[VZ];
}

extern char * strcasestr(const char * hayStack, const char * needle);

/* find block, stairs and slab variant of block <blockId> */
static void selectionFindVariant(int variant[3], int blockId)
{
	/* try not to rely too much on hardcoded id: not perfect, but better than look-up tables */
	BlockState state = blockGetById(blockId);
	/* note: techName is only meaningful at block level, we need BlockState */

	/* indentify material */
	STRPTR material = STRDUPA(state->name);
	STRPTR match = strrchr(material, '(');
	uint8_t flags;

	if (match == NULL)
	{
		int len = strlen(material);
		match = material;
		if (len > 6 && strcasecmp(material + len - 6, " Block") == 0)
			material[len-6] = 0;
		if (len > 7 && strcasecmp(material + len - 7, " Stairs") == 0)
			material[len-7] = 0;
		if (len > 5 && strcasecmp(material + len - 5, " Slab") == 0)
			material[len-5] = 0;
	}
	else
	{
		match ++;
		material = strchr(match, ')');
		if (material) *material = 0;
	}

	variant[0] = blockId;
	variant[1] = 0;
	variant[2] = 0;

	/* scan the table for compatible material */
	for (state = blockGetById(ID(1,0)), flags = 0; flags != 3 && state < blockLast; state ++)
	{
		if (state->special == BLOCK_STAIRS && variant[1] == 0 && strcasestr(state->name, match))
			variant[1] = state->id, flags |= 1;

		if (state->special == BLOCK_HALF && variant[2] == 0 && strcasestr(state->name, match))
			variant[2] = state->id, flags |= 2;
	}
//	fprintf(stderr, "variant of %d:%d = %d:%d (stairs) %d:%d (slab)\n", blockId>>4, blockId&15,
//		variant[1]>>4, variant[1]&15, variant[2]>>4, variant[2]&15);
}


/* thread that will process block replace */
static void selectionProcessReplace(void * unsued)
{
	struct BlockIter_t iter;
	Map  map;
	vec4 pos;
	int  dx, dy, dz, z, x, blockId, replId;
	int  variant[6];
	pos[VX] = MIN(selection.firstPt[VX], selection.secondPt[VX]);
	pos[VY] = MIN(selection.firstPt[VY], selection.secondPt[VY]);
	pos[VZ] = MIN(selection.firstPt[VZ], selection.secondPt[VZ]);
	dx = selection.regionSize[VX];
	dy = selection.regionSize[VY];
	dz = selection.regionSize[VZ];

	MutexEnter(selection.wait);

	map = globals.level;
	replId = selectionAsync.replId;
	blockId = selectionAsync.blockId;
	mapInitIter(map, &iter, pos, blockId > 0);
	mapUpdateInit(&iter);

	Block b = &blockIds[replId>>4];

	if (selectionAsync.similar)
	{
		/* find block, stairs and slab variant of each block type */
		selectionFindVariant(variant,   blockId);
		selectionFindVariant(variant+3, replId);
		variant[1] >>= 4;
		while (dy > 0)
		{
			for (z = dz; z > 0; z --, mapIter(&iter, -dx, 0, 1))
			{
				for (x = dx; x > 0; x --, mapIter(&iter, 1, 0, 0))
				{
					int srcId = iter.blockIds ? getBlockId(&iter) : 0;

					if (srcId == blockId)
						/* replace full blocks */
						mapUpdate(map, NULL, variant[3], NULL, UPDATE_SILENT);
					else if ((srcId >> 4) == variant[1])
						/* replace stairs */
						mapUpdate(map, NULL, variant[4] | (srcId&15), NULL, UPDATE_SILENT);
					else if ((srcId & ~8) == variant[2])
						/* replace slabs */
						mapUpdate(map, NULL, variant[5] | (srcId&8), NULL, UPDATE_SILENT);
				}
				/* emergency exit */
				if (selectionAsync.cancel) goto break_all;
				selectionAsync.progress[0] += dx;
			}
			mapIter(&iter, 0, 1, -dz);
			dy --;
		}
	}
	else /* only replace <blockId> */
	{
		if ((b->special == BLOCK_HALF || b->special == BLOCK_STAIRS) && selectionAsync.side > 0)
			replId |= 8;

		while (dy > 0)
		{
			for (z = dz; z > 0; z --, mapIter(&iter, -dx, 0, 1))
			{
				for (x = dx; x > 0; x --, mapIter(&iter, 1, 0, 0))
				{
					int srcId = iter.blockIds ? getBlockId(&iter) : 0;

					if (srcId == blockId)
						mapUpdate(map, NULL, replId, NULL, UPDATE_SILENT);
				}
				if (selectionAsync.cancel) goto break_all;
				selectionAsync.progress[0] += dx;
			}
			mapIter(&iter, 0, 1, -dz);
			dy --;
		}
	}
	break_all:
	MutexLeave(selection.wait);
}

/* change one type of block with another */
int selectionReplace(DATA32 progress, int blockId, int replId, int side, Bool doSimilar)
{
	selectionAsync.progress = progress;
	selectionAsync.blockId  = blockId;
	selectionAsync.similar  = doSimilar;
	selectionAsync.replId   = replId;
	selectionAsync.side     = side;
	selectionAsync.cancel   = 0;

	ThreadCreate(selectionProcessReplace, NULL);

	return (int) selection.regionSize[VX] *
	       (int) selection.regionSize[VY] *
	       (int) selection.regionSize[VZ];
}


/*
 * fill selection with a geometric brush
 */
static Bool isInInnerShape(int shape, vec4 voxelPos, vec4 sqRxyz)
{
	vec4 voxel;
	int  i, axis1, axis2;
	if (shape == SHAPE_CYLINDER)
	{
		axis1 = sqRxyz[VT];
		axis2 = (axis1 >> 2) & 3;
		axis1 &= 3;
	}
	else axis1 = axis2 = 0;
	for (i = 0; i < 3; i ++)
	{
		if (voxelPos[i] == 0) continue;
		memcpy(voxel, voxelPos, 12);
		if (voxel[i] < 0) voxel[i] --;
		else              voxel[i] ++;
		/*
		 * <voxel> is the vector from sphere center to voxel center: check in the vector direction
		 * if there is a farther voxel that would hide this one
		 */
		switch (shape) {
		case SHAPE_SPHERE:
			if (voxel[VX]*voxel[VX]*sqRxyz[VX] + voxel[VY]*voxel[VY]*sqRxyz[VY] + voxel[VZ]*voxel[VZ]*sqRxyz[VZ] >= 1)
				return False;
			break;
		case SHAPE_CYLINDER:
			if (voxel[axis1]*voxel[axis1]*sqRxyz[axis1] + voxel[axis2]*voxel[axis2]*sqRxyz[axis2] >= 1)
				return False;
			break;
		case SHAPE_DIAMOND:
			if (fabsf(voxel[VX])*sqRxyz[VX] + fabsf(voxel[VY])*sqRxyz[VY] + fabsf(voxel[VZ])*sqRxyz[VZ] - EPSILON >= 1)
				return False;
		}
	}
	/* hidden on 3 sides: don't place that voxel */
	return True;
}

/* get the axis (W, L or H) perdenticular to the disk of the cylinder */
int selectionCylinderAxis(vec4 size, int direction)
{
	vec4 ratio = {
		fabsf(1 - size[1] / size[2]),
		fabsf(1 - size[0] / size[2]),
		fabsf(1 - size[0] / size[1])
	};
	/* get the axis closest to 0 */
	int axis = 0;
	if (ratio[0] == ratio[1]) return 2;
	if (ratio[0] > ratio[1]) axis = 1;
	if (ratio[axis] > ratio[2]) axis = 2;
	return axis;
}

static void selectionProcessShape(void * unused)
{
	vec4 pos = {
		MIN(selection.firstPt[VX], selection.secondPt[VX]),
		MIN(selection.firstPt[VY], selection.secondPt[VY]),
		MIN(selection.firstPt[VZ], selection.secondPt[VZ])
	};

	/* size can be bigger than selection to create half-sphere or arches */
	int selSize[] = {selectionAsync.size[0], selectionAsync.size[2], selectionAsync.size[1]};
	if (selectionAsync.facing & 1)
		swap(selSize[VX], selSize[VZ]);

	vec4 center = {
		pos[VX] + selSize[VX] * 0.5f,
		pos[VY] + selSize[VY] * 0.5f,
		pos[VZ] + selSize[VZ] * 0.5f,
	};

	int   x, y, z;
	int   flags = selectionAsync.similar;
	Block block = &blockIds[selectionAsync.blockId>>4];
	char  shape = flags & 15;
	int   blockId = selectionAdjustOrient(selectionAsync.blockId, block->orientHint, selSize[VX], selSize[VY], selSize[VZ]);

	struct BlockIter_t iter;
	MutexEnter(selection.wait);
	mapInitIter(globals.level, &iter, pos, selectionAsync.blockId > 0);
	mapUpdateInit(&iter);

	switch (shape) {
	case SHAPE_SPHERE:
	case SHAPE_CYLINDER:
		/* equation of a 3d ellipse is (x - center[VX])� / Rx� + (y - center[VY])� / Ry� + (z - center[VZ])� / Rz� <= 1 */
		pos[VX] = 1 / (selSize[VX] * selSize[VX] * 0.25f); /* == 1 / Rx.y.z� (selSize == diameter) */
		pos[VY] = 1 / (selSize[VY] * selSize[VY] * 0.25f);
		pos[VZ] = 1 / (selSize[VZ] * selSize[VZ] * 0.25f);
		break;
	case SHAPE_DIAMOND:
		/* equation of a 3d diamond: |x - center[VX]| / Rx + |y - center[VY]| / Ry + |z - center[VZ]| / Rz <= 1 */
		pos[VX] = 2. / selSize[VX];
		pos[VY] = 2. / selSize[VY];
		pos[VZ] = 2. / selSize[VZ];
	}

	selSize[VX] = fabsf(selection.firstPt[VX] - selection.secondPt[VX]) + 1;
	selSize[VY] = fabsf(selection.firstPt[VY] - selection.secondPt[VY]) + 1;
	selSize[VZ] = fabsf(selection.firstPt[VZ] - selection.secondPt[VZ]) + 1;
	float   yoffset = selSize[VY] - selectionAsync.size[2];
	uint8_t outer = flags & SHAPE_OUTER;
	uint8_t hollow = flags & SHAPE_HOLLOW;
	uint8_t axis1 = 0;
	uint8_t axis2 = 0;
	uint8_t axisS = 0;
	uint8_t slab  = block->orientHint == ORIENT_SLAB;
	if (shape == SHAPE_CYLINDER)
	{
		/* get the 2 axis where the disk of the cylinder will be located */
		uint8_t axis[] = {0, 2};
		if (selectionAsync.facing & 1) axis[0] = 2, axis[1] = 0;
		if (flags & SHAPE_AXIS_H) axis1 = axis[0], axis2 = axis[1]; else
		if (flags & SHAPE_AXIS_L) axis1 = axis[0], axis2 = 1;
		else                      axis1 = axis[1], axis2 = 1;
		pos[VT] = axis1 | (axis2 << 2);
	}
	if (slab) hollow = 0;
	if (selectionAsync.facing & 1) axisS = 2;
	flags &= 15;

	for (y = 0; y < selSize[VY]; y ++, mapIter(&iter, 0, 1, -selSize[VZ]))
	{
		for (z = 0; z < selSize[VZ]; z ++, mapIter(&iter, -selSize[VX], 0, 1))
		{
			for (x = 0; x < selSize[VX]; x ++, mapIter(&iter, 1, 0, 0))
			{
				/*
				 * XXX yeah, this is a gcc extension :-/ alternatives are:
				 * - functions with way too many parameters
				 * - macro hell
				 * good luck to anyone trying to port this for another compiler
				 */
				int isInShape(vec4 vox) /* this function can access all local vars declared up to this point */
				{
					/*
					 * add a voxel if its center is within the object (this is also what was done in MCEdit v1)
					 * this might not rasterize the object as perfectly as it should, but the result is kind
					 * of esthetically pleasing, which is all what matter.
					 */
					switch (shape) {
					case SHAPE_SPHERE:
						vox[VT] = vox[VX]*vox[VX]*pos[VX] + vox[VY]*vox[VY]*pos[VY] + vox[VZ]*vox[VZ]*pos[VZ];
						break;
					case SHAPE_CYLINDER:
						vox[VT] = vox[axis1]*vox[axis1]*pos[axis1] + vox[axis2]*vox[axis2]*pos[axis2];
						break;
					case SHAPE_DIAMOND:
						vox[VT] = fabsf(vox[VX])*pos[VX] + fabsf(vox[VY])*pos[VY] + fabsf(vox[VZ])*pos[VZ] - EPSILON;
					}
					if (outer)
					{
						/* don't care about hollow */
						if (vox[VT] < 1) return 0;
					}
					else
					{
						if (vox[VT] >= 1) return 0;
						if (hollow && isInInnerShape(shape, vox, pos)) return 2;
					}
					return 1;
				}

				if (slab)
				{
					vec4 vox = {
						iter.ref->X + iter.x + 0.5f - center[VX],
						iter.yabs + 0.25f - center[VY] - yoffset,
						iter.ref->Z + iter.z + 0.5f - center[VZ]
					};
					uint8_t quadrant;
					/* hmmm: using 0.25 and 0.75 as center for axisS looks slightly off: uses 0.3 and 0.7 instead :-/ */
					vox[axisS] -= 0.2f; quadrant  = isInShape(vox);
					vox[axisS] += 0.4f; quadrant |= isInShape(vox) << 1;
					vox[VY] += 0.5f;
					vox[axisS] -= 0.4f; quadrant |= isInShape(vox) << 2;
					vox[axisS] += 0.4f; quadrant |= isInShape(vox) << 3;
					int id = blockId;
					switch (quadrant) {
					default: continue;
					case 3:  case 3+4: case 3+8: break;
					case 12: case 12+1: case 12+2: id |= 8; break; /* top slab */
					case 15: id -= 16; break; /* double slab */
					}
					mapUpdate(globals.level, NULL, id, NULL, UPDATE_SILENT);
				}
				else
				{
					vec4 vox = {
						iter.ref->X + iter.x + 0.5f - center[VX],
						iter.yabs + 0.5f - center[VY] - yoffset,
						iter.ref->Z + iter.z + 0.5f - center[VZ]
					};
					switch (isInShape(vox)) {
					case 1: mapUpdate(globals.level, NULL, blockId, NULL, UPDATE_SILENT); break;
					case 2: mapUpdate(globals.level, NULL, 0, NULL, UPDATE_SILENT);
					}
				}

				/* DEBUG: slow processing down */
				// ThreadPause(500);
			}

			if (selectionAsync.cancel) goto break_all;
			selectionAsync.progress[0] += selSize[VX];
		}
	}
	break_all:
	MutexLeave(selection.wait);
}

/* start the thread that will do the job */
int selectionFillWithShape(DATA32 progress, int blockId, int flags, vec4 size, int direction)
{
	selectionAsync.progress = progress;
	selectionAsync.blockId  = blockId;
	selectionAsync.similar  = flags;
	selectionAsync.facing   = direction;
	selectionAsync.cancel   = 0;

	memcpy(selectionAsync.size, size, 12);

	ThreadCreate(selectionProcessShape, NULL);

	return (int) selection.regionSize[VX] *
	       (int) selection.regionSize[VY] *
	       (int) selection.regionSize[VZ];
}

/* need to wait for thread to exit first */
void selectionCancelOperation(void)
{
	selectionAsync.cancel = 1;
	/* wait for thread to finish */
	MutexEnter(selection.wait);
	MutexLeave(selection.wait);
}
