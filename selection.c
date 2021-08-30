/*
 * selection.c: handle extended selection and operations that can be done with it.
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
#include "player.h"
#include "render.h"
#include "SIT.h"

struct Selection_t selection;
extern uint8_t bboxIndices[]; /* from blocks.c */
extern uint8_t texCoord[];

/*
 * selection render / nudge
 */

/* init VBO and VAO */
void selectionInitStatic(int shader, DATA8 direction)
{
	selection.shader = shader;
	selection.infoLoc = glGetUniformLocation(shader, "info");
	selection.direction = direction;
	selection.cloneRepeat = 1;
	selection.copyAir = 1;
	selection.copyWater = 1;

	/* will use selection.vsh and indexed rendering */
	glGenBuffers(2, &selection.vboVertex);
	glBindBuffer(GL_ARRAY_BUFFER, selection.vboVertex);
	glBufferData(GL_ARRAY_BUFFER, MAX_VERTEX * 20, NULL, GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, selection.vboIndex);
	glBufferData(GL_ARRAY_BUFFER, MAX_INDEX * 2, NULL, GL_STATIC_DRAW);

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
	glBindVertexArray(0);
}

void selectionSetSize(void)
{
	if (selection.nudgeSize)
	{
		TEXT buffer[32];
		int size[] = {
			(int) fabsf(selection.firstPt[VX] - selection.secondPt[VX]) + 1,
			(int) fabsf(selection.firstPt[VZ] - selection.secondPt[VZ]) + 1,
			(int) fabsf(selection.firstPt[VY] - selection.secondPt[VY]) + 1
		};
		if (renderGetFacingDirection() & 1)
			swap(size[0], size[1]);
		sprintf(buffer, "%dW x %dL x %dH", size[0], size[1], size[2]);
		SIT_SetValues(selection.nudgeSize, SIT_Title, buffer, NULL);
	}
}

static void selectionSetRect(int pointId)
{
	int i;
	if (pointId == 2)
	{
		for (i = 0; i < 3; i ++)
		{
			float pt1 = selection.firstPt[i];
			float pt2 = selection.secondPt[i];
			selection.regionPt[i] = (pt1 < pt2 ? pt1 : pt2) - 0.005;
			selection.regionSize[i] = fabsf(pt2 - pt1) + 0.01 + 1;
		}
	}

	glBindBuffer(GL_ARRAY_BUFFER, selection.vboVertex);
	vec vtx = (vec) glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
	vec size;

	switch (pointId) {
	case 2: size = selection.regionSize; vtx += (8*5) * 2; break;
	case 3: size = selection.cloneSize;  vtx += (8*2 + 36 + 24) * 5;
	}

	/* build a box big enough to cover the whole region */
	for (i = 0; i < 24; i ++, vtx += 5)
	{
		static uint8_t coordU[] = {0, 2, 0, 2, 0, 0};
		static uint8_t coordV[] = {1, 1, 1, 1, 2, 2};
		DATA8 p  = vertex + cubeIndices[i];
		DATA8 uv = texCoord + (i & 3) * 2;
		vtx[0] = p[0] * size[0];
		vtx[1] = p[1] * size[1];
		vtx[2] = p[2] * size[2];
		vtx[3] = uv[0] * size[coordU[i>>2]]; if (vtx[3] > 0) vtx[3] -= 0.01;
		vtx[4] = uv[1] * size[coordV[i>>2]]; if (vtx[4] > 0) vtx[4] -= 0.01;
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
		DATA8 p = vertex + bboxIndices[i] * 3;
		vtx[0] = p[0] * size[0];
		vtx[1] = p[1] * size[1];
		vtx[2] = p[2] * size[2];
		vtx[3] = (31*16+8) / 512.;
		vtx[4] = 8 / 1024.;
	}
	/* lines around the edges */
	glUnmapBuffer(GL_ARRAY_BUFFER);

	selectionSetSize();
}

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
		}
		break;
	case SITOM_ButtonReleased:
		//SIT_SetValues(w, SIT_CheckState, False, NULL);
		selection.nudgePoint = 0;
		break;
	}
	return 1;
}

Bool selectionProcessKey(int key, int mod)
{
	if (selection.nudgePoint > 0)
	{
		static int8_t axisSENW[] = {2,0,2,0};
		static int8_t axisMain[] = {1,1,-1,-1};
		static int8_t axisRot[]  = {1,-1,-1,1};
		int8_t axis = 0;
		int8_t dir  = selection.direction[0]; /* S,E,N,W */
		switch (key) {
		case FORWARD:  axis = axisSENW[dir];   dir =  axisMain[dir]; break;
		case BACKWARD: axis = axisSENW[dir];   dir = -axisMain[dir]; break;
		case LEFT:     axis = 2-axisSENW[dir]; dir =  axisRot[dir]; break;
		case RIGHT:    axis = 2-axisSENW[dir]; dir = -axisRot[dir]; break;
		case 'q':      axis = 1; dir =  1; break;
		case 'z':      axis = 1; dir = -1; break;
		default:       return False;
		}
		dir *= selection.nudgeStep;
		if (selection.nudgePoint & 1)
			selection.firstPt[axis] += dir;
		if (selection.nudgePoint & 2)
			selection.secondPt[axis] += dir;
		if (selection.nudgePoint & 4)
			selection.clonePt[axis] += dir,
			selectionSetClonePt(NULL, -1);
		selectionSetRect(2);
		return True;
	}
	return False;
}

/* set the position of one of the 2 extended selection point */
void selectionSetPoint(APTR sitRoot, float scale, vec4 pos, int point)
{
	memcpy(point ? selection.secondPt : selection.firstPt, pos, 16);

	selection.hasPoint |= 1<<point;

	if (selection.hasPoint == 3)
	{
		if (selection.nudgeDiag == NULL)
		{
			SIT_Widget diag = selection.nudgeDiag = SIT_CreateWidget("selection.mc", SIT_DIALOG, sitRoot,
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
			selectionSetRect(2);
			SIT_ManageWidget(diag);
		}
		else selectionSetRect(2);
	}
}

void selectionClear(void)
{
	if (selection.nudgeDiag)
	{
		SIT_CloseDialog(selection.nudgeDiag);
		selection.nudgeDiag = NULL;
		selection.nudgeSize = NULL;
	}
	selection.hasPoint = 0;
}

vec selectionGetPoints(void)
{
	return selection.firstPt;
}

/* draw selection point/boxes */
static void selectionDrawPoint(vec4 point, int pointId)
{
	/* first: draw fllled box */
	vec4 loc;
	memcpy(loc, point, sizeof loc);
	loc[3] = pointId*4+4+1;
	glEnable(GL_DEPTH_TEST);
	glProgramUniform4fv(selection.shader, selection.infoLoc, 1, loc);
	switch (pointId) {
	case 0: loc[3] --;   glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_SHORT, 0); break; /* first pt */
	case 1: loc[3] --;   glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_SHORT, (APTR) ((24+36)*2)); break; /* first pt */
	case 2: loc[3] = 0;  glDrawArrays(GL_TRIANGLES, 8*2, 36); break; /* box that cover 1st and 2nd point */
	case 3: loc[3] = 20; glDrawArrays(GL_TRIANGLES, 8*2+36+24, 36);  /* box that cover cloned selection */
	}

	glDisable(GL_DEPTH_TEST);
	glProgramUniform4fv(selection.shader, selection.infoLoc, 1, loc);
	switch (pointId) {
	case 0: glDrawElements(GL_LINES, 24, GL_UNSIGNED_SHORT, (APTR) (36 * 2)); break;
	case 1: glDrawElements(GL_LINES, 24, GL_UNSIGNED_SHORT, (APTR) ((24+36*2)*2)); break;
	case 2: glDrawArrays(GL_LINES, 8*2+36, 24); break;
	case 3: glDrawArrays(GL_LINES, 8*2+36*2+24, 24);
	}
}

void selectionRender(void)
{
	if (selection.hasPoint)
	{
		glDepthMask(GL_FALSE);
		glUseProgram(selection.shader);
		glBindVertexArray(selection.vao);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, selection.vboIndex);

		switch (selection.hasPoint & 3) {
		case 1: selectionDrawPoint(selection.firstPt,  0); break;
		case 2: selectionDrawPoint(selection.secondPt, 1); break;
		case 3: selectionDrawPoint(selection.firstPt,  0);
		        selectionDrawPoint(selection.secondPt, 1);
		        selectionDrawPoint(selection.regionPt, 2);
		}
		if (selection.hasClone)
		{
			/* draw the brush */
			#if 0
			glDepthMask(GL_TRUE);
			vec4 clonePt = {selection.clonePt[VX]-1, selection.clonePt[VY]-1, selection.clonePt[VZ]-1, 1};
			renderDrawMap(selection.clone, clonePt);
			#endif

			/* box overlay */
			glDepthMask(GL_FALSE);
			glUseProgram(selection.shader);
			glBindVertexArray(selection.vao);
			selectionDrawPoint(selection.clonePt, 3);
		}
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
		glEnable(GL_DEPTH_TEST);
	}
}

/*
 * clone selection tool: create a mini-map from the selected blocks
 */
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

	DATA8 off = axis + side * 4;
	int i, j;

	if (side >= 0)
	{
		i = off[0]; selection.clonePt[i] = pos[i] - floorf(selection.cloneSize[i] * 0.5);
		i = off[1]; selection.clonePt[i] = pos[i] - floorf(selection.cloneSize[i] * 0.5);
		i = off[2]; selection.clonePt[i] = pos[i] + (off[3] ? -floorf(selection.cloneSize[i]) : 1);
	}

	/* offset from original selection */
	if (selection.editBrush)
	{
		for (i = 0; i < 3; i ++)
		{
			TEXT coord[32];
			sprintf(coord, "%d", (int) (selection.clonePt[i] - selection.regionPt[i] - 0.005));
			SIT_SetValues(selection.brushOff[i], SIT_Title, coord, NULL);
		}
	}

	/* relocate chunk->X+Z, renderPrepVisibleChunks() will need this */
	if (selection.clone)
	{
		Chunk c;
		Map   brush = selection.clone;
		int   x = (brush->size[VX] + 15) >> 4;
		int   z = (brush->size[VZ] + 15) >> 4;
		for (c = brush->chunks, pos = selection.clonePt, j = 0; j < z; j ++)
		{
			for (i = 0; i < x; i ++, c ++)
			{
				c->X = (int) pos[VX] + i * 16;
				c->Z = (int) pos[VZ] + j * 16;
			}
		}
	}
	fprintf(stderr, "clone pt = %g,%g,%g, side = %d\n", selection.clonePt[0], selection.clonePt[1], selection.clonePt[2], side);
}

/* SITE_OnChange on brush offset */
static int selectionChangeCoord(SIT_Widget w, APTR cd, APTR ud)
{
	int axis = (int) ud;
	selection.clonePt[axis] = selection.regionPt[axis] + 0.005 + atoi(cd);
	return 1;
}

/* copy selected blocks into a mini-map */
void selectionClone(APTR sitRoot, Map map, vec4 pos, int side)
{
	selection.cloneSize[VX] = fabsf(selection.firstPt[VX] - selection.secondPt[VX]) + 1 + 0.01;
	selection.cloneSize[VY] = fabsf(selection.firstPt[VY] - selection.secondPt[VY]) + 1 + 0.01;
	selection.cloneSize[VZ] = fabsf(selection.firstPt[VZ] - selection.secondPt[VZ]) + 1 + 0.01;

	if (! selection.hasClone)
	{
		selectionSetRect(3);
		selection.hasClone = 1;
	}

	if (! selection.editBrush)
	{
		SIT_Widget diag = selection.editBrush = SIT_CreateWidget("brush.mc", SIT_DIALOG, sitRoot,
			SIT_DialogStyles,  SITV_Plain,
			SIT_Left,          SITV_AttachForm, NULL, SITV_Em(0.5),
			SIT_TopAttachment, SITV_AttachPosition, SITV_AttachPos(50), SITV_OffsetCenter,
			NULL
		);
		SIT_CreateWidgets(diag,
			"<label name=brotate title=R:>"
			"<button name=rotate.act title=Rotate left=WIDGET,brotate,0.3em>"
			"<label name=broll title=T: left=WIDGET,rotate,1em>"
			"<button maxWidth=rotate name=roll.act title=Roll left=WIDGET,broll,0.3em>"
			"<label name=bflip maxWidth=brotate title=F:>"
			"<button name=flip.act title=Flip maxWidth=roll top=WIDGET,broll,0.5em left=WIDGET,bflip,0.3em>"
			"<label name=bmirror maxWidth=broll title=M: left=WIDGET,flip,1em>"
			"<button name=mirror.act title=Mirror maxWidth=flip top=WIDGET,broll,0.5em left=WIDGET,bmirror,0.3em>"

			"<button name=nudge title=Nudge nextCtrl=NONE right=FORM>"
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

			"<button name=copyair title='Copy air'   curValue=", &selection.copyAir,   "top=WIDGET,repeat,1em    buttonType=", SITV_CheckBox, ">"
			"<button name=copywat title='Copy water' curValue=", &selection.copyWater, "top=WIDGET,copyair,0.5em buttonType=", SITV_CheckBox, ">"
			"<button name=copybio title='Copy biome' curValue=", &selection.copyBiome, "top=WIDGET,copywat,0.5em buttonType=", SITV_CheckBox, ">"

			"<button name=ko.act title=Cancel right=FORM top=WIDGET,copybio,1em>"
			"<button name=ok.act title=Clone  right=WIDGET,ko,0.5em top=OPPOSITE,ko>"
		);
		SIT_SetAttributes(diag,
			"<brotate top=MIDDLE,rotate><broll top=MIDDLE,roll><bflip top=MIDDLE,flip><bmirror top=MIDDLE,mirror>"
			"<xlab top=MIDDLE,xcoord><ylab top=MIDDLE,ycoord><zlab top=MIDDLE,zcoord>"
			"<nudge top=MIDDLE,ycoord><tlab top=MIDDLE,repeat>"
		);
		int i;
		for (i = 0; i < 3; i ++)
		{
			static STRPTR editBoxes[] = {"xcoord", "ycoord", "zcoord"};
			SIT_AddCallback(selection.brushOff[i] = SIT_GetById(diag, editBoxes[i]), SITE_OnChange, selectionChangeCoord, (APTR) i);
		}
		SIT_AddCallback(SIT_GetById(diag, "nudge"), SITE_OnClick, selectionNudge, (APTR) 4);
		SIT_ManageWidget(diag);
	}

#if 0
	if (selection.clone)
		free(selection.clone);

	uint16_t sizes[] = {
		fabsf(selection.firstPt[VX] - selection.secondPt[VX]) + 3,
		fabsf(selection.firstPt[VY] - selection.secondPt[VY]) + 3,
		fabsf(selection.firstPt[VZ] - selection.secondPt[VZ]) + 3
	};
	uint16_t chunks[] = {
		(sizes[VX] + 15) >> 4,
		(sizes[VY] + 15) >> 4,
		(sizes[VZ] + 15) >> 4
	};

	int total = chunks[VX] * chunks[VZ] * (chunks[VY] * (4096 + 2048 + sizeof (struct ChunkData_t)) + sizeof (struct Chunk_t)) +
	       sizeof (struct Map_t);

	fprintf(stderr, "bytes for brush = %d\n", total);

	/* alloc everything in one mem chunk: the brush isn't going to be resized anyway */
	Map brush = selection.clone = calloc(1, total);

	if (brush)
	{
		ChunkData cd;
		Chunk     chunk;
		DATA8     blocks;
		int       chunkXZ = chunks[VX] * chunks[VZ];
		int       x, y, z;

		brush->chunkOffsets = (DATAS16) brush->path;
		brush->chunks = (Chunk) (brush + 1);
		brush->GPUMaxChunk = 512 * 1024;
		cd = (ChunkData) (brush->chunks + chunkXZ);
		blocks = (DATA8) (cd + chunkXZ * chunks[VY]);

		memcpy(brush->size, sizes, 6);

		/* does not matter: there will be no wrap around chunks */
		brush->mapArea = brush->maxDist = brush->mapSize = 1e6;
		brush->center  = brush->chunks;
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
				chunk->X = x * 16;
				chunk->Z = z * 16;
				chunk->maxy = chunks[VY];
				chunk->cflags |= CFLAG_GOTDATA;
				/* brush don't have lazy chunks all around */
				uint8_t missing = 0;
				if (x == 0) missing |= 1 << SIDE_WEST;
				if (z == 0) missing |= 1 << SIDE_NORTH;
				if (x == chunks[VX]-1) missing |= 1 << SIDE_EAST;
				if (z == chunks[VZ]-1) missing |= 1 << SIDE_SOUTH;
				chunk->noChunks = missing;
				for (y = 0; y < chunks[VY]; y ++, cd ++, blocks += 4096 + 2048)
				{
					chunk->layer[y] = cd;
					/* setup ChunkData */
					*first = cd;
					first = &cd->visible;
					cd->Y = y * 16;
					/* will only contain blockId + data, no skylight or blocklight */
					cd->cdflags = CDFLAG_NOLIGHT;
					cd->blockIds = blocks;
				}
			}
		}

		/* now we can copy blocks from map to the brush */
		vec4 pos = {
			fminf(selection.firstPt[VX], selection.secondPt[VX]),
			fminf(selection.firstPt[VY], selection.secondPt[VY]),
			fminf(selection.firstPt[VZ], selection.secondPt[VZ])
		};
		struct BlockIter_t iter;
		DATA8 blockIds;
		int   offset;
		mapInitIter(map, &iter, pos, False);
		/* note: we have to add a 1 block layer all around the brush to prevent face culling at the edge of chunk */
		for (y = 1, offset = 256, blockIds = cd->blockIds, cd = brush->firstVisible; y <= sizes[VY]; y ++, mapIter(&iter, 0, - sizes[VZ], 1))
		{
			for (z = 1, offset += 16; z < sizes[VZ]; z ++, mapIter(&iter, - sizes[VX], 1, 0))
			{
				for (x = 1, offset ++; x < sizes[VX]; x ++, mapIter(&iter, 1, 0, 0))
				{
					/* would be nice if we could use memcpy(), but DATA_OFFSET table is packed by 2 voxels per byte :-/ */
					uint8_t data = iter.blockIds[DATA_OFFSET + (iter.offset >> 1)];
					blockIds[offset] = iter.blockIds[iter.offset];
					if (iter.offset & 1) data >>= 4; else data &= 15;
					blockIds[DATA_OFFSET + (offset>>1)] |= (offset & 1) ? data << 4 : data;

					if ((x & 15) == 15)
					{
						cd += chunks[VY];
						offset &= ~15;
						blockIds = cd->blockIds;
					}
				}
				if ((z & 15) == 15)
				{
					cd += chunks[VX] * chunks[VY];
					offset &= ~(15<<4);
					blockIds = cd->blockIds;
				}
			}
			if ((y & 15) == 15)
			{
				cd ++;
				offset &= ~(15<<8);
				blockIds = cd->blockIds;
			}
		}

		/* still not done: need to convert all into meshes */
		for (z = 0, chunk = brush->chunks; z < chunks[VZ]; z ++)
		{
			for (x = 0; x < chunks[VX]; x ++, chunk ++)
			{
				for (y = 0; y < chunk->maxy; y ++)
				{
					chunkUpdate(chunk, chunkAir, map->chunkOffsets, y);
					/* transfer chunk to the GPU */
					renderFinishMesh(brush, True);
				}
			}
		}
	}
#endif
	selectionSetClonePt(pos, side);
}

/* remove everything related to cloned selection */
void selectionCancelClone(void)
{
	if (selection.clone)
	{
		free(selection.clone);
		selection.clone = NULL;
	}
	if (selection.editBrush)
	{
		SIT_CloseDialog(selection.editBrush);
		selection.editBrush = NULL;
	}
	selection.hasClone = 0;
}

/*
 * select similar blocks
 */
void selectionAutoSelect(Map map, vec4 pos, APTR sitRoot, float scale)
{
	uint8_t visited[512];
	int8_t  minMax[8];

	memset(visited, 0, sizeof visited);
	/* work is done in that function because of that ring buffer */
	mapUpdateFloodFill(map, pos, visited, minMax);

	vec4 pt1 = {pos[VX] + minMax[VX],   pos[VY] + minMax[VY],   pos[VZ] + minMax[VZ]};
	vec4 pt2 = {pos[VX] + minMax[VX+4], pos[VY] + minMax[VY+4], pos[VZ] + minMax[VZ+4]};
	selectionSetPoint(sitRoot, scale, pt1, 0);
	selectionSetPoint(sitRoot, scale, pt2, 1);
}

/*
 * selection manipulation : fill / replace / geometric brushes
 */
static struct
{
	DATA32 progress;
	Map    map;
	int    blockId;
	int    side;
	int    facing;
	int    replId;
	int    similar;
	char   cancel;
	vec4   size;
	Mutex  wait;
}	selectionAsync;

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

	map = selectionAsync.map;
	blockId = selectionAsync.blockId;
	mapInitIter(map, &iter, pos, blockId > 0);

	/* updated in the order XZY (first to last) */
	mapUpdateInit(&iter);

	/* lock the mutex for main thread to know when we are finished here */
	MutexEnter(selectionAsync.wait);

	Block b = &blockIds[blockId>>4];

	if (b->opacSky < MAXSKY)
	{
		/* transparent to skylight: cheaper to start from top */
		mapIter(&iter, 0, dy-1, 0);
		yinc = -1;
	}
	else yinc = 1;

	if ((b->special == BLOCK_HALF || b->special == BLOCK_STAIRS) && selectionAsync.side > 0)
		blockId |= 8;

	switch (b->orientHint) {
	case ORIENT_LOG:
		if (dy == 1)
		{
			if (dz == 1 && dx > 1) blockId |= 4; /* E/W beam */
			if (dx == 1 && dz > 1) blockId |= 8; /* N/S */
		}
		/* else upward beam */
		break;
	case ORIENT_SE: /* rails */
		if (dy == 1)
		{
			if (dz == 1 && dx > 1) blockId |= 1; /* E/W */
			/* else N/S is 0 */
		}
		break;
	case ORIENT_SENW: /* ladder, furnace, jack-o-lantern ... */
		{
			static uint8_t dir2data[] = {2, 4, 3, 5};
			blockId |= dir2data[selectionAsync.facing];
		}
		break;
	case ORIENT_STAIRS:
		{
			static uint8_t dir2stairs[] = {2, 0, 3, 1};
			blockId |= dir2stairs[selectionAsync.facing];
		}
		break;
	case ORIENT_SWNE:
		break;
	}

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

			/* emergency exit */
			if (selectionAsync.cancel) goto break_all;
			selectionAsync.progress[0] += dx;
		}
		mapIter(&iter, 0, yinc, -dz);
		dy --;
	}
	/* note: mapUpdateEnd() will regen mesh, must no be called from here */
	break_all:
	MutexLeave(selectionAsync.wait);
}

/* this only starts selection processing */
int selectionFill(Map map, DATA32 progress, int blockId, int side, int direction)
{
	selectionAsync.progress = progress;
	selectionAsync.blockId  = blockId;
	selectionAsync.side     = side;
	selectionAsync.facing   = direction;
	selectionAsync.map      = map;
	selectionAsync.cancel   = 0;

	/* wait for thread to finish */
	if (! selectionAsync.wait)
		selectionAsync.wait = MutexCreate();

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

	MutexEnter(selectionAsync.wait);

	map = selectionAsync.map;
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
	else
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
	MutexLeave(selectionAsync.wait);
}

/* change one type of block with another */
int selectionReplace(Map map, DATA32 progress, int blockId, int replId, int side, Bool doSimilar)
{
	selectionAsync.progress = progress;
	selectionAsync.blockId  = blockId;
	selectionAsync.similar  = doSimilar;
	selectionAsync.replId   = replId;
	selectionAsync.side     = side;
	selectionAsync.map      = map;
	selectionAsync.cancel   = 0;

	/* wait for thread to finish */
	if (! selectionAsync.wait)
		selectionAsync.wait = MutexCreate();

	ThreadCreate(selectionProcessReplace, NULL);

	return (int) selection.regionSize[VX] *
	       (int) selection.regionSize[VY] *
	       (int) selection.regionSize[VZ];
}


/*
 * fill selection with a geometric brush
 */
static Bool isInsideShape(int shape, vec4 voxelPos, vec4 sqRxyz)
{
	vec4 voxel;
	int  i, axis1, axis2;
	if (shape == SHAPE_CYLINDER)
	{
		axis1 = sqRxyz[VT];
		axis2 = (axis1 >> 2) & 3;
		axis1 &= 3;
	}
	for (i = 0; i < 3; i ++)
	{
		if (voxelPos[i] == 0) continue;
		memcpy(voxel, voxelPos, 12);
		if (voxel[i] < 0) voxel[i] --;
		else              voxel[i] ++;
		/*
		 * <voxel> is the vector from sphere center to voxel center: check in the vector direction
		 * if there is a farther voxel that would this one
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
//Map map, DATA32 progress, int blockId, int flags, vec4 size, int direction)
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
		pos[VX] + selSize[VX] * 0.5,
		pos[VY] + selSize[VY] * 0.5,
		pos[VZ] + selSize[VZ] * 0.5,
	};

	int x, y, z;
	int flags = selectionAsync.similar;
	uint8_t shape = flags & 15;

	struct BlockIter_t iter;
	MutexEnter(selectionAsync.wait);
	mapInitIter(selectionAsync.map, &iter, pos, selectionAsync.blockId > 0);
	mapUpdateInit(&iter);

	switch (shape) {
	case SHAPE_SPHERE:
	case SHAPE_CYLINDER:
		/* equation of an ellipse is (x - center[VX])� / Rx� + (y - center[VY])� / Ry� + (z - center[VZ])� / Rz� <= 1 */
		pos[VX] = 1 / (selSize[VX] * selSize[VX] * 0.25); /* == 1 / Rx.y.z� */
		pos[VY] = 1 / (selSize[VY] * selSize[VY] * 0.25);
		pos[VZ] = 1 / (selSize[VZ] * selSize[VZ] * 0.25);
		break;
	case SHAPE_DIAMOND:
		/* equation of a diamond: |x - center[VX]| / Rx + |y - center[VY]| / Ry + |z - center[VZ]| / Rz <= 1 */
		pos[VX] = 2. / selSize[VX];
		pos[VY] = 2. / selSize[VY];
		pos[VZ] = 2. / selSize[VZ];
	}

	selSize[VX] = fabsf(selection.firstPt[VX] - selection.secondPt[VX]) + 1;
	selSize[VY] = fabsf(selection.firstPt[VY] - selection.secondPt[VY]) + 1;
	selSize[VZ] = fabsf(selection.firstPt[VZ] - selection.secondPt[VZ]) + 1;
	float   yoff = selSize[VY] - selectionAsync.size[2];
	uint8_t outer = flags & SHAPE_OUTER;
	uint8_t hollow = flags & SHAPE_HOLLOW;
	uint8_t axis1 = 0;
	uint8_t axis2 = 0;
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
	flags &= 15;

	for (y = 0; y < selSize[VY]; y ++, mapIter(&iter, 0, 1, -selSize[VZ]))
	{
		for (z = 0; z < selSize[VZ]; z ++, mapIter(&iter, -selSize[VX], 0, 1))
		{
			for (x = 0; x < selSize[VX]; x ++, mapIter(&iter, 1, 0, 0))
			{
				/*
				 * add a voxel if its center is within the sphere (this is also what was done in MCEdit v1)
				 * this might not rasterize a sphere as perfectly as it should, but the result is kind
				 * of esthetically pleasing, which is all what matter.
				 */
				vec4 vox = {
					iter.ref->X + iter.x + 0.5 - center[VX],
					iter.yabs + 0.5 - center[VY] - yoff,
					iter.ref->Z + iter.z + 0.5 - center[VZ]
				};
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
					if (vox[VT] < 1) continue;
				}
				else
				{
					if (vox[VT] >= 1) continue;
					if (hollow && isInsideShape(shape, vox, pos)) continue;
				}
				mapUpdate(selectionAsync.map, NULL, selectionAsync.blockId, NULL, UPDATE_SILENT);
				/* DEBUG: slow processing down */
				// ThreadPause(500);
			}

			if (selectionAsync.cancel) goto break_all;
			selectionAsync.progress[0] += selSize[VX];
		}
	}
	break_all:
	MutexLeave(selectionAsync.wait);
}

/* start the thread that will do the job */
int selectionFillWithShape(Map map, DATA32 progress, int blockId, int flags, vec4 size, int direction)
{
	selectionAsync.progress = progress;
	selectionAsync.blockId  = blockId;
	selectionAsync.similar  = flags;
	selectionAsync.facing   = direction;
	selectionAsync.map      = map;
	selectionAsync.cancel   = 0;

	memcpy(selectionAsync.size, size, 12);

	/* wait for thread to finish */
	if (! selectionAsync.wait)
		selectionAsync.wait = MutexCreate();

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
	if (selectionAsync.wait)
	{
		MutexEnter(selectionAsync.wait);
		MutexLeave(selectionAsync.wait);
	}
}
