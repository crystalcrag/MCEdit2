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

static void selectionSetRect(void)
{
	int i;
	for (i = 0; i < 3; i ++)
	{
		float pt1 = selection.firstPt[i];
		float pt2 = selection.secondPt[i];
		selection.regionPt[i] = (pt1 < pt2 ? pt1 : pt2) - 0.005;
		selection.regionSize[i] = fabsf(pt2 - pt1) + 0.01 + 1;
	}

	glBindBuffer(GL_ARRAY_BUFFER, selection.vboVertex);
	vec vtx = (vec) glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY) + (8*5) * 2;

	/* build a box big enough to cover the whole region */
	for (i = 0; i < 24; i ++, vtx += 5)
	{
		static uint8_t coordU[] = {0, 2, 0, 2, 0, 0};
		static uint8_t coordV[] = {1, 1, 1, 1, 2, 2};
		DATA8 p  = vertex + cubeIndices[i];
		DATA8 uv = texCoord + (i & 3) * 2;
		vtx[0] = p[0] * selection.regionSize[0];
		vtx[1] = p[1] * selection.regionSize[1];
		vtx[2] = p[2] * selection.regionSize[2];
		vtx[3] = uv[0] * selection.regionSize[coordU[i>>2]]; if (vtx[3] > 0) vtx[3] -= 0.01;
		vtx[4] = uv[1] * selection.regionSize[coordV[i>>2]]; if (vtx[4] > 0) vtx[4] -= 0.01;
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
		vtx[0] = p[0] * selection.regionSize[0];
		vtx[1] = p[1] * selection.regionSize[1];
		vtx[2] = p[2] * selection.regionSize[2];
		vtx[3] = (31*16+8) / 512.;
		vtx[4] = 8 / 1024.;
	}
	/* lines around the edges */
	glUnmapBuffer(GL_ARRAY_BUFFER);
	selection.extVtx = 36;

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
		selectionSetRect();
		return True;
	}
	return False;
}

void selectionSet(APTR sitRoot, float scale, vec4 pos, int point)
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
			selectionSetRect();
			SIT_ManageWidget(diag);
		}
		else selectionSetRect();
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


static void selectionDrawPoint(vec4 point, int offset)
{
	/* first: draw fllled box */
	vec4 loc;
	int  indices;
	memcpy(loc, point, sizeof loc);
	loc[3] = offset*4+4+1;
	indices = offset * (24+36) * 2;
	glEnable(GL_DEPTH_TEST);
	glProgramUniform4fv(selection.shader, selection.infoLoc, 1, loc);
	if (offset == 2) glDrawArrays(GL_TRIANGLES, 8*2, selection.extVtx);
	else glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_SHORT, (APTR) indices);
	indices += 36*2;

	if (offset == 2) loc[3] = 0;
	else loc[3] -= 1;
	glDisable(GL_DEPTH_TEST);
	glProgramUniform4fv(selection.shader, selection.infoLoc, 1, loc);
	if (offset == 2) glDrawArrays(GL_LINES, 8*2+36, 24);
	else glDrawElements(GL_LINES, 24, GL_UNSIGNED_SHORT, (APTR) indices);
}

void selectionRender(void)
{
	if (selection.hasPoint)
	{
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

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
		glEnable(GL_DEPTH_TEST);
	}
}

/*
 * selection manipulation : fill / replace
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
	case ORIENT_SWNE:
		break;
	}

	if (dy == 1 && dx > 2 && dz > 2 && b->special == BLOCK_STAIRS)
	{
		/* only build build the outline of a rectangle with this block (typical use case: roof) */
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
	fprintf(stderr, "varient of %d:%d = %d:%d (stairs) %d:%d (slab)\n", blockId>>4, blockId&15,
		variant[1]>>4, variant[1]&15, variant[2]>>4, variant[2]&15);
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


int selectionFillWithShape(Map map, DATA32 progress, int blockId, int shape, vec4 size, int direction)
{
	vec4 pos = {
		MIN(selection.firstPt[VX], selection.secondPt[VX]),
		MIN(selection.firstPt[VY], selection.secondPt[VY]),
		MIN(selection.firstPt[VZ], selection.secondPt[VZ])
	};

	/* size can be bigger than selection to create half-sphere or arches */
	int selSize[] = {size[0], size[2], size[1]};
	if (direction & 1)
		swap(selSize[VX], selSize[VZ]);

	vec4 center = {
		pos[VX] + selSize[VX] * 0.5,
		pos[VY] + selSize[VY] * 0.5,
		pos[VZ] + selSize[VZ] * 0.5,
	};

	int x, y, z;

	struct BlockIter_t iter;
	mapInitIter(map, &iter, pos, blockId > 0);
	mapUpdateInit(&iter);

	/* equation of an ellipse is (x - center[VX])� / Rx� + (y - center[VY])� / Ry� + (z - center[VZ])� / Rz� <= 1 */
	pos[VX] = 1 / (selSize[VX] * selSize[VX] * 0.25); /* == 1 / Rx.y.z� */
	pos[VY] = 1 / (selSize[VY] * selSize[VY] * 0.25);
	pos[VZ] = 1 / (selSize[VZ] * selSize[VZ] * 0.25);

	selSize[VX] = fabsf(selection.firstPt[VX] - selection.secondPt[VX]) + 1;
	selSize[VY] = fabsf(selection.firstPt[VY] - selection.secondPt[VY]) + 1;
	selSize[VZ] = fabsf(selection.firstPt[VZ] - selection.secondPt[VZ]) + 1;
	float   yoff = selSize[VY] - size[2];
	uint8_t outer = shape & SHAPE_OUTER;
	uint8_t hollow = shape & SHAPE_HOLLOW;
	uint8_t axis1 = 0;
	uint8_t axis2 = 0;
	if ((shape & 15) == SHAPE_CYLINDER)
	{
		/* get the 2 axis where the disk of the cylinder will be located */
		uint8_t axis[] = {0, 2};
		if (direction & 1) axis[0] = 2, axis[1] = 0;
		if (shape & SHAPE_AXIS_H) axis1 = axis[0], axis2 = axis[1]; else
		if (shape & SHAPE_AXIS_L) axis1 = axis[0], axis2 = 1;
		else                      axis1 = axis[1], axis2 = 1;
		pos[VT] = axis1 | (axis2 << 2);
	}
	shape &= 15;

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
				mapUpdate(map, NULL, blockId, NULL, UPDATE_SILENT);
			}
		}
	}

	mapUpdateEnd(map);

	return 0;
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

