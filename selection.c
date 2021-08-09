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
#include "blocks.h"
#include "selection.h"
#include "player.h"
#include "SIT.h"

struct Selection_t selection;
extern uint8_t bboxIndices[]; /* from blocks.c */
extern uint8_t texCoord[];

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
}


static int selectionNudge(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_OnMouse * msg = cd;
	switch (msg->state) {
	case SITOM_ButtonPressed:
		if (msg->button == SITOM_ButtonLeft)
		{
			selection.nudgePoint = (int) ud;
			return 2;
		}
		break;
	case SITOM_ButtonReleased:
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
		selectionSetRect();

		SIT_Widget diag = selection.nudgeDiag = SIT_CreateWidget("selection", SIT_DIALOG, sitRoot,
			SIT_DialogStyles,  SITV_Plain,
			SIT_Bottom,        SITV_AttachForm, NULL, (int) (24 * scale),
			SIT_TopAttachment, SITV_AttachNone,
			NULL
		);
		SIT_CreateWidgets(diag,
			"<button name=whole title=Nudge left=", SITV_AttachPosition, SITV_AttachPos(50), SITV_OffsetCenter, ">"
			"<label name=size title='8W x 5L x 3H' top=WIDGET,whole,0.3em left=FORM right=FORM style='text-align: center; color: white'>"
			"<button name=first title=Nudge top=WIDGET,size,0.3em>"
			"<button name=second title=Nudge top=OPPOSITE,first left=WIDGET,first,0.5em>"
		);
		SIT_AddCallback(SIT_GetById(diag, "whole"),  SITE_OnClick, selectionNudge, (APTR) 3);
		SIT_AddCallback(SIT_GetById(diag, "first"),  SITE_OnClick, selectionNudge, (APTR) 1);
		SIT_AddCallback(SIT_GetById(diag, "second"), SITE_OnClick, selectionNudge, (APTR) 2);
		SIT_ManageWidget(diag);
	}
}

void selectionClear(void)
{
	if (selection.nudgeDiag)
	{
		SIT_CloseDialog(selection.nudgeDiag);
		selection.nudgeDiag = NULL;
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
	setShaderValue(selection.shader, "info", 4, loc);
	if (offset == 2) glDrawArrays(GL_TRIANGLES, 8*2, 36);
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

		selectionDrawPoint(selection.firstPt, 0);
		if (selection.hasPoint & 2)
		{
			selectionDrawPoint(selection.secondPt, 1);
			selectionDrawPoint(selection.regionPt, 2);
		}

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
		glEnable(GL_DEPTH_TEST);
	}
}
