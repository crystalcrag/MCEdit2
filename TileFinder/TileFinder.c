/*
 * Tilefinder.c : Find tile position in a minecraft block texture.
 *
 * Drag'n drop a texture file onto the canvas.
 * Point the mouse on a tile and left click to map texture to current side of cube.
 * Order is: south face, east, north, west, top, bottom.
 * Right click to remove last entry.
 * 'R' key to rotate 90deg the texture of current side.
 *
 * Written by T.Pierron, Jan 2020.
 */

#include <GL/gl.h>
#include <GL/glu.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <malloc.h>
#include <math.h>
#include "SIT.h"
#include "graphics.h"
#include "ViewImage.h"
#include "TileFinder.h"

#define BGCOLOR     RGB(0x88, 0x88, 0x88)
#define CELLSZ      16

MainCtrl ctrl;

static int drawCube(SIT_Widget canvas, APTR cd, APTR ud);
static STRPTR parseBlock(STRPTR fmt);
static void blockEdit(Block * b);

static char texCoord[] = {
	0,0,    0,1,    1,1,    1,0,
	0,1,    1,1,    1,0,    0,0,
	1,1,    1,0,    0,0,    0,1,
	1,0,    0,0,    0,1,    1,1,
};

static uint16_t vertex[] = {
	0, 0, 1,  1, 0, 1,  1, 1, 1,  0, 1, 1,
	0, 0, 0,  1, 0, 0,  1, 1, 0,  0, 1, 0,
};
static uint8_t cubeIndices[6*4] = { /* face (quad) of cube: S, E, N, W, T, B */
	9, 0, 3, 6,    6, 3, 15, 18,     18, 15, 12, 21,     21, 12, 0, 9,    21, 9, 6, 18,      15, 3, 0, 12
};

static uint8_t cubeLines[] = {
	0, 1, 1, 2, 2, 3, 3, 0,  4, 5, 5, 6, 6, 7, 7, 4, 3, 7, 2, 6, 1, 5, 0, 4
};

static STRPTR rot90Names[] = {
	"", "90\xC2\xB0", "180\xC2\xB0", "270\xC2\xB0"
};

static void blockResetVertices(Block * b)
{
	float * v;
	float   trans[3];
	int     i, nbRot = 0, nbRotCas = 0;
	mat4    rotation, rot90, rotCascade, tmp;
	Block * prev;
	matIdent(rotCascade);
	for (prev = ctrl.primitives; prev <= b; prev ++)
	{
		for (i = 0; i < 3; i ++)
		{
			if (prev->rotCascade[i] != 0)
			{
				matRotate(tmp, prev->rotCascade[i] * M_PI / 180, i);
				matMult(rotCascade, rotCascade, tmp);
				nbRotCas ++;
			}
		}
	}
	if (b->rotate[0] != 0)
		matRotate(rotation, b->rotate[0] * M_PI / 180, 0), nbRot ++;

	if (b->rotate[1] != 0)
	{
		matRotate(tmp, b->rotate[1] * M_PI / 180, 1), nbRot ++;
		if (nbRot == 1)
			memcpy(rotation, tmp, sizeof tmp);
		else
			matMult(rotation, rotation, tmp);
	}

	if (b->rotate[2] != 0)
	{
		matRotate(tmp, b->rotate[2] * M_PI / 180, 2), nbRot ++;
		if (nbRot == 1)
			memcpy(rotation, tmp, sizeof tmp);
		else
			matMult(rotation, rotation, tmp);
	}
	switch (ctrl.rot90) {
	case 1: matRotate(rot90, M_PI_2, 1); break;
	case 2: matRotate(rot90, M_PI, 1); break;
	case 3: matRotate(rot90, M_PI+M_PI_2, 1);
	}
	trans[0] = b->trans[0] / 16 - 0.5;
	trans[1] = b->trans[1] / 16 - 0.5;
	trans[2] = b->trans[2] / 16 - 0.5;
	for (i = 0, v = b->vertex; i < DIM(cubeIndices); i ++, v += 3)
	{
		DATA16 p = vertex + cubeIndices[i];
		v[0] = p[0] * b->size[0]/16;
		v[1] = p[1] * b->size[1]/16;
		v[2] = p[2] * b->size[2]/16;
		if (nbRot > 0)
		{
			float tr[3] = {b->size[0]/32, b->size[1]/32, b->size[2]/32};
			vecSub(v, v, tr);
			matMultByVec3(v, rotation, v);
			vecAdd(v, v, tr);
		}
		v[0] += trans[0];
		v[1] += trans[1];
		v[2] += trans[2];
		if (nbRotCas > 0)
			matMultByVec3(v, rotCascade, v);
		if (ctrl.rot90 > 0)
			matMultByVec3(v, rot90, v);
	}
}

static Block * blockAdd(float szx, float szy, float szz)
{
	Block * b = ctrl.primitives + ctrl.nbBlocks;
	DATA16  p;
	int     i, j;

	if (ctrl.nbBlocks == PRIMITIVES)
		return NULL;

	memset(b, 0, sizeof *b);
	b->size[0] = szx;
	b->size[1] = szy;
	b->size[2] = szz;
	b->faces = 63;
	b->vtxCount = DIM(cubeIndices) * 3;
	if (ctrl.nbBlocks == 0)
		b->detailMode = ctrl.detailSel;
	else
		b->detailMode = ctrl.primitives[0].detailMode;
	blockResetVertices(b);
	for (i = 0, p = b->texUV; i < DIM(b->texUV); )
	{
		for (j = 0; j < 8; j += 2, i += 2, p += 2)
		{
			p[0] = (ctrl.defU + texCoord[j])   * CELLSZ;
			p[1] = (ctrl.defV + texCoord[j+1]) * CELLSZ;
		}
	}
	ctrl.nbBlocks ++;

	return b;
}

/* add to list */
static void blockAddItem(Block * b, Bool reset)
{
	TEXT size[64];
	sprintf(size, "%g, %g, %g", b->size[0], b->size[1], b->size[2]);

	SIT_ListInsertItem(ctrl.list, -1, NULL, NULL, "Box", size, NULL);

	/* reset tex coord of unused faces */
	DATA16 tex;
	int    i, j, faces;

	/* note: when loading previous block from disk ctrl.defU and ctrl.defV was not setup */
	if (ctrl.detailSel && reset)
	{
		for (i = 0, faces = b->faces, tex = b->texUV + i * 8; i < 6 && faces; faces >>= 1, i ++, tex += 8)
		{
			if ((faces & 1) == 0)
			for (j = 0; j < 8; j += 2)
			{
				tex[j]   = (ctrl.defU + texCoord[j])   * CELLSZ;
				tex[j+1] = (ctrl.defV + texCoord[j+1]) * CELLSZ;
			}
		}
	}
}

static void updateTexCoord(void)
{
	Block * b;
	DATA16  tex;
	TEXT    coord[256];
	int     i;

	coord[0] = 0;
	b = ctrl.primitives + ctrl.editBlock;

	if (ctrl.editBlock >= 0)
	{
		STRPTR p = coord;
		int faces = b->faces;
		for (i = 0; i < 6 && ((faces&1) == 0 || b->texTrans[i] >= 0x80); faces >>= 1, i ++);
		faces = b->faces;

		if (i == 6)
		{
			int rot = 0;
			/* simplified form */
			for (i = 0, tex = b->texUV; i < 6; i ++)
			{
				if (i > 0) *p++ = ',';
				int8_t u, v, j, val;
				for (j = 1, u = tex[0]/16, v = tex[1]/16, tex += 2; j < 4; j ++, tex += 2)
				{
					val = tex[0] / 16; if (u > val) u = val;
					val = tex[1] / 16; if (v > val) v = val;
				}
				if (u == ctrl.defU && v == ctrl.defV) {
					if (i > 0) p[-1] = 0;
					break;
				}
				p += sprintf(p, "%2d,%2d", u, v);
				rot |= (b->texTrans[i] & 3) << i*2;
			}
			if (rot > 0) sprintf(p, ",  %d", rot);
		}
		else /* full list */
		{
			int j;
			for (i = 0, tex = b->texUV; faces; faces >>= 1)
			{
				if (faces & 1)
				{
					for (j = 0; j < 4; j ++, tex += 2)
					{
						if (p > coord) *p++ = ',';
						p += sprintf(p, "%d,%d", tex[0], tex[1]);
					}
					i ++;
				}
				else tex += 8;
			}
		}
	}
	SIT_SetValues(ctrl.tex, SIT_Title, coord, NULL);
}

static void setImage(Image img)
{
	if (! img)
		return;
	if (ctrl.back)
		GFX_FreeImage(ctrl.back);
	ctrl.back = img;

	DATA8 image = malloc(img->width * img->height * 4);
	DATA8 s;
	int   i;

	memcpy(image, img->bitmap, img->stride * img->height);

	/* BGR => RGB */
	for (s = image, i = img->width * img->height; i > 0; i --, s += 4)
	{
		uint8_t tmp = s[2];
		s[2] = s[0];
		s[0] = tmp;
	}

	if (img->bpp > 24)
		GFX_FlattenImage(img, BGCOLOR);

	int height, zoom = 1;
	int width = img->width;
	SIT_GetValues(ctrl.app, SIT_ScreenHeight, &height, NULL);
	if (width < 1024) width = 1024;

	SIT_SetValues(ctrl.canvas, SIT_MinWidth, width * zoom, SIT_MinHeight, img->height * zoom, NULL);
	SIT_SetValues(ctrl.canvas, VIT_Image, img, VIT_Factor, (double) zoom, NULL);

	ctrl.defU = img->width  / 16 - 1;
	ctrl.defV = img->height / 16 - 1;

	if (! ctrl.texId)
		glGenTextures(1, &ctrl.texId);

	glBindTexture(GL_TEXTURE_2D, ctrl.texId);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, img->width, img->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	free(image);
}

/* drop files on canvas */
static int changeImage(SIT_Widget w, APTR cd, APTR ud)
{
	STRPTR * list = cd;
	Image    img  = GFX_LoadImage(list[0]);

	if (img)
	{
		setImage(img);
		ctrl.curCX = -1;
	}
	else SIT_Log(w, SIT_ERROR, "Fail to load image '%s'", list[0]);

	return 1;
}

static void showTexCoord(int face)
{
	Block * b    = ctrl.primitives + ctrl.editBlock;
	Rect    rect = {.x = 1e6, .y = 1e6};
	DATA16  tex  = b->texUV + face * 8;
	int     i;

	if ((tex[0] >= ctrl.defU * CELLSZ && tex[1] >= ctrl.defV * CELLSZ) || face >= 6)
	{
		rect.x = rect.y = -1;
	}
	else for (i = 0; i < 4; i ++, tex += 2)
	{
		if (rect.x > tex[0]) rect.x = tex[0];
		if (rect.y > tex[1]) rect.y = tex[1];
		if (rect.width  < tex[0]) rect.width  = tex[0];
		if (rect.height < tex[1]) rect.height = tex[1];
	}
	//fprintf(stderr, "rect = %d, %d - %d, %d\n", rect.x, rect.y, rect.width, rect.height);
	SIT_SetValues(ctrl.canvas, VIT_MarqueeRect, &rect, NULL);
}

static void editFace(int dir)
{
	int id;
	Block * b = ctrl.primitives + ctrl.editBlock;

	if (ctrl.detailSel)
		for (id = ctrl.faceEdit + dir; 0 <= id && id < 6 && (b->faces & (1<<id)) == 0; id += dir);
	else
		id = ctrl.faceEdit + dir;
	if (0 <= id && id <= 6)
	{
		ctrl.faceEdit = id;
		showTexCoord(id);
		SIT_SetValues(ctrl.faces[id], SIT_CheckState, 1, NULL);
	}
}

static int mouse(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_OnMouse * msg = cd;

	double csz, fact;
	int    cx, cy, x, y;

	switch (msg->state) {
	case SITOM_Move:
		SIT_GetValues(ctrl.canvas, VIT_ZoomX, &x, VIT_ZoomY, &y, VIT_Factor, &fact, NULL);
		csz = CELLSZ * fact;
		if (msg->y >= y && msg->x >= x)
		{
			cx = (msg->x - x) / csz;
			cy = (msg->y - y) / csz;
			if (cx > ctrl.defU || cy > ctrl.defV)
				cx = cy = -1;
		}
		else cx = cy = -1;
		if (ctrl.back && (cx != ctrl.curCX || cy != ctrl.curCY))
		{
			if (ctrl.curCX >= 0)
			{
				ViewImageInvalidate(ctrl.canvas, ctrl.curCX*CELLSZ, ctrl.curCY*CELLSZ, CELLSZ, CELLSZ);
			}
			SIT_SetValues(ctrl.label, SIT_Title|XfMt, "%d, %d", cx, cy, NULL);
			ctrl.curCX = cx;
			ctrl.curCY = cy;
			if (cx >= 0 && cy >= 0)
			{
				ViewImageInvalidate(ctrl.canvas, ctrl.curCX*CELLSZ, ctrl.curCY*CELLSZ, CELLSZ, CELLSZ);
			}
		}
		break;
	case SITOM_ButtonPressed:
	case SITOM_DoubleClick:
		if (ctrl.editBlock >= 0 && (! ctrl.detailSel || msg->button == 1))
		{
			Block * b = ctrl.primitives + ctrl.editBlock;
			DATA16  p = b->texUV + ctrl.faceEdit * 8;
			int     j, texU, texV;
			j = ctrl.faceEdit;
			switch (msg->button) {
			case 2: /* MMB: select whole 16x16 texture */
				if (j == ctrl.lastFaceSet)
				{
					editFace(1);
					j = ctrl.faceEdit;
				}
				if (j == 6) return 0;
				p = b->texUV + j * 8;
				b->texTrans[j] = 0x80;
				texU = ctrl.curCX * CELLSZ;
				texV = ctrl.curCY * CELLSZ;
				ctrl.lastFaceSet = j;
				break;
			case 1: /* delete texture from last face */
				if (j == 0) return 0;
				ctrl.lastFaceSet = j-1;
				texU = ctrl.defU * CELLSZ;
				texV = ctrl.defV * CELLSZ;
				b->texTrans[j] = 0;
				b->detailFaces &= ~(1 << j);
				if (j > 0)
					editFace(-1);
				break;
			default:
				return 0;
			}
			for (j = 0; j < 8; j += 2, p += 2)
			{
				p[0] = texU + texCoord[j] * CELLSZ;
				p[1] = texV + texCoord[j+1] * CELLSZ;
			}
			updateTexCoord();
			drawCube(ctrl.cube, NULL, NULL);
		}
	}
	return 0;
}

/* select a tex using marquee */
static int selTex(SIT_Widget w, APTR cd, APTR ud)
{
	ViewImageOnChange msg = cd;

	if (ctrl.editBlock < 0) return 0;
	if (msg->type == VIT_Marquee)
	{
		Block * b = ctrl.primitives + ctrl.editBlock;
		DATA16  tex = b->texUV + ctrl.faceEdit * 8;
		int *   rect = &msg->rect.x;

		//fprintf(stderr, "tex coord = %d, %d - %d, %d\n", rect[0], rect[1], rect[2], rect[3]);
		b->texTrans[ctrl.faceEdit] = 0;
		b->detailFaces |= (1 << ctrl.faceEdit);

		tex[0] = rect[0]; tex[2] = rect[0]; tex[4] = rect[2]; tex[6] = rect[2];
		tex[1] = rect[1]; tex[3] = rect[3]; tex[5] = rect[3]; tex[7] = rect[1];

		updateTexCoord();
		drawCube(ctrl.cube, NULL, NULL);
		ctrl.oldSize[0] = 0;
	}
	else if (msg->type == VIT_MarqueeNotif)
	{
		TEXT  txt[32];
		int * rect = &msg->rect.x;
		sprintf(txt, "%dx%d", rect[2] - rect[0], rect[3] - rect[1]);
		if (strcmp(ctrl.oldSize, txt))
		{
			strcpy(ctrl.oldSize, txt);
			SIT_SetValues(ctrl.label, SIT_Title, txt, NULL);
		}
	}
	return 1;
}

static DATA16 getUVTex(float vertex[12])
{
	static uint8_t Ucoord[] = {0, 2, 0, 2, 0, 0};
	static uint8_t Vcoord[] = {1, 1, 1, 1, 2, 2};
	static uint8_t revers[] = {0, 1, 1, 0, 2, 0};
	static uint8_t norm2face[] = {1, 3, 4, 5, 0, 2};
	static uint16_t texCoord[8];
	DATA16 tex = ctrl.primitives->texUV, p;
	vec4   v1  = {vertex[3] - vertex[0], vertex[4] - vertex[1], vertex[5] - vertex[2], 1};
	vec4   v2  = {vertex[6] - vertex[0], vertex[7] - vertex[1], vertex[8] - vertex[2], 1};
	vec4   norm;
	int    dir, i, U, V;

	vecCrossProduct(norm, v1, v2);

	dir = 0; v1[0] = norm[0];
	if (fabsf(v1[0]) < fabsf(norm[VY])) dir = 2, v1[0] = norm[VY];
	if (fabsf(v1[0]) < fabsf(norm[VZ])) dir = 4, v1[0] = norm[VZ];
	if (v1[0] < 0) dir ++;

	dir  = norm2face[dir];
	tex += dir * 8;
	U    = Ucoord[dir];
	V    = Vcoord[dir];

	for (p = texCoord, i = 0; i < 4; i ++, p += 2, vertex += 3)
	{
		float val = vertex[V] + 0.5;
		if (revers[dir] & 2) val = 1 - val;
		float pt1[] = {tex[2] + (tex[0]-tex[2]) * val, tex[3] + (tex[1] - tex[3]) * val};
		float pt2[] = {tex[4] + (tex[6]-tex[4]) * val, tex[5] + (tex[7] - tex[5]) * val};

		val = vertex[U] + 0.5;
		if (revers[dir] & 1) val = 1 - val;
		p[0] = roundf(pt1[0] + (pt2[0] - pt1[0]) * val);
		p[1] = roundf(pt1[1] + (pt2[1] - pt1[1]) * val);
	}

	return texCoord;
}

/* draw the cube, textured using opengl */
static int drawCube(SIT_Widget canvas, APTR cd, APTR ud)
{
	int w, h, i, j;
	if (! ctrl.back) return 0;
	SIT_GetValues(canvas, SIT_Width, &w, SIT_Height, &h, NULL);
	glViewport(0, 0, w, h);
	glClearColor(0.8, 0.8, 0.8, 1);
	glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

	if (ctrl.cullFace) glEnable(GL_CULL_FACE);
	else               glDisable(GL_CULL_FACE);
	glEnable(GL_DEPTH_TEST);
//	glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	glFrontFace(GL_CCW);
	glEnable(GL_TEXTURE_2D);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	/* to pick quad face using mouse */
    glEnable(GL_STENCIL_TEST);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    glStencilMask(0xFFFF);
    glStencilFunc(GL_ALWAYS, 1, 0xFFFF);


	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
//	glOrtho(-2, 2, -2, 2, 1, 10);
	gluPerspective(70, w / (float) h, 1, 10);
	//glOrtho(-1, 1, -1, 1, 1, 10);
	gluLookAt(0, 0, 2, 0, 0, 0, 0, 1, 0);
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glTranslatef(ctrl.trans[0], ctrl.trans[1], 0);
	glScalef(ctrl.scale, ctrl.scale, ctrl.scale);
	glMultMatrixf(ctrl.rotation);

	glBindTexture(GL_TEXTURE_2D, ctrl.texId);
	glBegin(GL_QUADS);
	float texNormW = 1 / (float) ctrl.back->width;
	float texNormH = 1 / (float) ctrl.back->height;
	int   stencil = 1;
	int   detail  = 1;
	for (i = 0; i < ctrl.nbBlocks; i ++)
	{
		Block * b = ctrl.primitives + i;
		DATA16  p;
		int     faces = b->faces;
		int     detailFaces = b->detailFaces;

		if (i == 0) detail = b->detailMode;

		for (j = 0, p = b->texUV; j < b->vtxCount; faces >>= 1, detailFaces >>= 1, p += 8)
		{
			/* fake lighting */
			static float shades[] = {0.9, 0.7, 0.9, 0.7, 1, 0.6};
			float shade = shades[j/12];
			int k;

			if (faces & 1)
			{
				DATA16 tex;
				if (stencil > 1) glEnd();
				glStencilFunc(GL_ALWAYS, stencil, 0xFFFF);
				glBegin(GL_QUADS);
				glColor3f(shade, shade, shade);
				stencil ++;
				tex = (detail == 0 && (detailFaces & 1) == 0 ? getUVTex(b->vertex + j) : p);
				for (k = 0; k < 4; k ++, tex += 2, j += 3)
				{
					glTexCoord2f(tex[0] * texNormW, tex[1] * texNormH);
					glVertex3fv(b->vertex + j);
				}
			}
			else j += 12;
		}
	}

	glEnd();
	glDisable(GL_TEXTURE_2D);
	glDisable(GL_STENCIL_TEST);

	/* 3d axes: red = +X, green = +Y, blue = +Z */
	glDisable(GL_DEPTH_TEST);
	glBegin(GL_LINES);
	glColor3f(1, 0, 0);
	glVertex3f(0, 0, 0);
	glVertex3f(1, 0, 0);
	glColor3f(0, 1, 0);
	glVertex3f(0, 0, 0);
	glVertex3f(0, 1, 0);
	glColor3f(0, 0, 1);
	glVertex3f(0, 0, 0);
	glVertex3f(0, 0, 1);
	glEnd();

	if (ctrl.showBBox)
	{
		glEnable(GL_DEPTH_TEST);
		glColor3f(1, 1, 1);
		glBegin(GL_LINES);
		for (i = 0; i < DIM(cubeLines); i ++)
		{
			DATA16 v = vertex + cubeLines[i] * 3;
			glVertex3f(v[0] - 0.5, v[1] - 0.5, v[2] - 0.5);
		}
		glEnd();
	}

	glPopMatrix();

	GFX_SwapBuffers(ctrl.cube);
	return 1;
}

static void set3dfrom2d(int x, int y)
{
	static double ref[3];
	static float oldMat[16];
	double proj[16];
	double model[16];
	double pt[4];
	int    vp[4], height;

	SIT_GetValues(ctrl.cube, SIT_Height, &height, NULL);
	glGetDoublev(GL_MODELVIEW_MATRIX, model);
	glGetDoublev(GL_PROJECTION_MATRIX, proj);
	glGetIntegerv(GL_VIEWPORT, vp);

	if (gluUnProject(x, height - y, 0, model, proj, vp, pt, pt+1, pt+2))
	{
		if (ctrl.line[0] == 0)
		{
			memcpy(ref, pt, sizeof ref);
			memcpy(oldMat, ctrl.rotation, sizeof oldMat);
		}

		/* get X and Y angles formed by vector <reF> and <line> */
		ctrl.line[0] = pt[0];
		ctrl.line[1] = pt[1];
		ctrl.line[2] = pt[2];

		/* rotation on X axis */
		double dot = ref[1]*ctrl.line[1] + ref[2]*ctrl.line[2];
		double det = ref[1]*ctrl.line[2] - ref[2]*ctrl.line[1];
		double anglev = atan2(det, dot);

		dot = cos(anglev);
		det = sin(anglev);
		mat4 mat = {1,0,0,0, 0,dot,det,0, 0,-det,dot,0, 0,0,0,1};

		/* rotation on Y axis */
		dot = ref[0]*ctrl.line[0] + ref[2]*ctrl.line[2];
		det = ref[0]*ctrl.line[2] - ref[2]*ctrl.line[0];
		double angleh = -atan2(det, dot);
		dot = cos(angleh);
		det = sin(angleh);

		//fprintf(stderr, "anglev = %f, angleh = %f\n", anglev * 180 / M_PI, angleh * 180 / M_PI);

		mat4 mat2 = {dot,0,-det,0, 0,1,0,0, det,0,dot,0, 0,0,0,1};

		/* combine both rotation */
		matMult(mat, mat, mat2);

		/* final view matrix: the multiplication order will prevent both gimbal lock and control inversion when one of the angle is > 180deg */
		matMult(ctrl.rotation, mat, oldMat);
	}
}

/* select object and face */
static void selectFace(int id)
{
	int i, j, face;
	for (i = 0, face = 1; i < ctrl.nbBlocks; i ++)
	{
		Block * b = ctrl.primitives + i;
		int     faces = b->faces;
		int     max = b->vtxCount/12;

		for (j = 0; j < max; j ++, faces >>= 1)
		{
			if ((faces & 1) == 0)
				continue;

			if (face == id)
			{
				if (ctrl.editBlock != i)
				{
					SIT_SetValues(ctrl.list, SIT_SelectedIndex, i, NULL);
					ctrl.editBlock = i;
					blockEdit(b);
				}
				ctrl.faceEdit = j;
				showTexCoord(j);
				SIT_SetValues(ctrl.faces[j], SIT_CheckState, True, NULL);
			}
			face ++;
		}
	}
}

static int rotateCube(SIT_Widget w, APTR cd, APTR ud)
{
	static int translate = 0, pt[2];
	static float origPt[2];
	SIT_OnMouse * msg = cd;

	if (msg->button == 3)
	{
		float v = ctrl.scale + (msg->state < 0 ? -0.2 : 0.2);
		if (v < 0.2) v = 0.2;
		if (v > 4)   v = 4;
		if (v != ctrl.scale)
		{
			ctrl.scale = v;
			drawCube(ctrl.cube, NULL, NULL);
		}
	}
	else switch (msg->state) {
	case SITOM_ButtonPressed:
		if ((msg->flags & SITK_FlagShift) && msg->button == 0)
			msg->button = 2;
		switch (msg->button) {
		case 2: /* MMB: translate view */
			pt[0] = msg->x;
			pt[1] = msg->y;
			memcpy(origPt, ctrl.trans, sizeof origPt);
			translate = 1;
			return 1;

		case 0: /* LMB: rotate view */
			set3dfrom2d(msg->x, msg->y);
			return 1;

		case 1: /* RMB: select face/block */
		{	GLint id = 0xdeadbeef;
			int height;
			SIT_GetValues(ctrl.cube, SIT_Height, &height, NULL);
			glReadPixels(msg->x, height - msg->y, 1, 1, GL_STENCIL_INDEX, GL_INT, &id);
			selectFace(id);
			return 0;
		} }
		break;
	case SITOM_CaptureMove:
		if (translate)
		{
			ctrl.trans[0] = origPt[0] - (pt[0] - msg->x) / (ctrl.swapView ? 200. : 100.);
			ctrl.trans[1] = origPt[1] + (pt[1] - msg->y) / (ctrl.swapView ? 200. : 100.);
			drawCube(w, NULL, NULL);
		}
		else
		{
			set3dfrom2d(msg->x, msg->y);
			drawCube(w, NULL, NULL);
		}
		break;
	case SITOM_ButtonReleased:
		translate = 0;
		memset(ctrl.line, 0, sizeof ctrl.line);
	}

	return 1;
}

/* SITE_OnGeometrySet */
static int fixHeight(SIT_Widget w, APTR cd, APTR ud)
{
	int * sz = cd;
	if (sz[2] == 1 && ctrl.swapView == (int) ud)
	{
		int width;
		SIT_GetValues(w, SIT_Width, &width, NULL);
		if (sz[1] != width)
		{
			sz[1] = width;
			return 1;
		}
	}
	return 0;
}

/* Ctrl+C or copy button */
static int copyBlock(SIT_Widget w, APTR cd, APTR ud)
{
	if (ctrl.nbBlocks > 0)
	{
		TEXT block[512];
		APTR clip = SIT_AllocClipboardObject(NULL);
		int  i, j, detail = ctrl.primitives[0].detailMode;
		for (i = 0; i < ctrl.nbBlocks; )
		{
			DATA16  tex;
			Block * b = ctrl.primitives + i;
			int     faces;
			STRPTR  p = block; i ++;
			/* faces: faces:0-5, inv normals:6, cubeMap:7, continue:8, rot90:9-10, detailFaces:11-16, incFaceId:17 */
			faces = b->faces | (!b->detailMode << 7) | ((i < ctrl.nbBlocks) << 8) | (ctrl.rot90 << 9);
			if (! detail && b->detailMode) faces |= b->detailFaces<<11;
			p += sprintf(p, "%d", faces);
			p += sprintf(p, ",%g,%g,%g", b->size[0], b->size[1], b->size[2]);
			p += sprintf(p, ",%g,%g,%g", b->trans[0], b->trans[1], b->trans[2]);
			p += sprintf(p, ",%g,%g,%g", b->rotate[0], b->rotate[1], b->rotate[2]);
			p += sprintf(p, ",%g,%g,%g", b->rotCascade[0], b->rotCascade[1], b->rotCascade[2]);
			faces &= 63;
			if (b->detailMode)
			{
				if (! detail) faces = b->detailFaces;
				for (tex = b->texUV, faces &= 63; faces; faces >>= 1)
				{
					if (faces & 1)
					{
						for (j = 0; j < 8; j += 2, tex += 2)
							p += sprintf(p, ",%d", tex[0] + tex[1] * 513);
					}
					else tex += 8;
				}
			}
			else if (i == 1)
			{
				for (tex = b->texUV, faces = 0; faces < 6; faces ++)
					for (j = 0; j < 8; j += 2, tex += 2)
						p += sprintf(p, ",%d", tex[0] + tex[1] * 513);
			}
			strcpy(p, ",\n");
			clip = SIT_AddTextToClipboard(clip, block, -1);
		}
		SIT_SetClipboardData(w, "TEXT", clip, NULL);
	}
	return 1;
}

static int pasteBlock(SIT_Widget w, APTR cd, APTR ud)
{
	int     size  = 0;
	STRPTR  clip  = SIT_GetFromClipboard("TEXT", &size);
	int     count = ctrl.nbBlocks;
	int     edit  = ctrl.editBlock;
	int     sel   = ctrl.detailSel;
	int     rot90 = ctrl.rot90;
	Block * old   = NULL;
	TEXT    extract[20];

	if (size > 16)
	{
		CopyString(extract, clip, 16);
		strcat(extract, "...");
	}
	else if (size > 0)
	{
		strcpy(extract, clip);
	}

	if (count > 0)
	{
		old = alloca(count * sizeof *old);
		memcpy(old, ctrl.primitives, count * sizeof *old);
	}

	ctrl.nbBlocks = ctrl.editBlock = 0;
	if (IsDef(clip))
		ctrl.detailSel = (atoi(clip) & 128) == 0;
	while (IsDef(clip))
	{
		while (isspace(*clip)) clip ++;
		if (*clip == 0) break;
		clip = parseBlock(clip);
	}
	if (clip && ctrl.nbBlocks > 0)
	{
		SIT_SetValues(ctrl.lab90, SIT_Title, rot90Names[ctrl.rot90], NULL);
		SIT_ListDeleteRow(ctrl.list, DeleteAllRow);
		for (edit = 0; edit < ctrl.nbBlocks; edit ++)
			blockAddItem(ctrl.primitives + edit, False);
		SIT_SetValues(ctrl.list, SIT_SelectedIndex, ctrl.editBlock, NULL);
		blockEdit(ctrl.primitives);
		drawCube(ctrl.cube, NULL, NULL);
	}
	else /* restore previous */
	{
		ctrl.nbBlocks  = count;
		ctrl.editBlock = edit;
		ctrl.detailSel = sel;
		ctrl.rot90     = rot90;
		memcpy(ctrl.primitives, old, count * sizeof *old);
		for (clip = extract; *clip; clip ++)
			if (*clip == '\t') *clip = ' ';
		if (size == 0)
			SIT_Log(w, SIT_INFO, "Clipboard does not contain a block model");
		else
			SIT_Log(w, SIT_INFO, "Clipboard does not contain a block model:\n\n%s", extract);
	}
	return 1;
}

static int getEditableFace(Block *  b, int face)
{
	if (face <  0) return -1;
	if (face == 6) return 5;
	face *= 8;
	if (b->texUV[face]   >= (ctrl.defU<<4) &&
	    b->texUV[face+1] >= (ctrl.defV<<4))
	{
		if (face == 0) return -1;
		face -= 8;
		if (b->texUV[face]   >= (ctrl.defU<<4) &&
		    b->texUV[face+1] >= (ctrl.defV<<4))
		    return -1;
	}
	return face >> 3;
}

/* SITE_OnMenu */
static int menuHandler(SIT_Widget w, APTR cd, APTR ud)
{
	int type;
	switch ((int) cd) {
	case MENU_COPY: /* Ctrl+C */
		SIT_GetValues(SIT_GetFocus(), SIT_CtrlType, &type, NULL);
		/* don't hi-jack Ctlr+C from text box */
		if (type == SIT_EDITBOX) return 0;
		copyBlock(w, NULL, NULL);
		break;
	case MENU_PASTE: /* Ctrl+V */
		SIT_GetValues(SIT_GetFocus(), SIT_CtrlType, &type, NULL);
		/* don't hi-jack Ctlr+V from text box */
		if (type == SIT_EDITBOX) return 0;
		pasteBlock(w, NULL, NULL);
		break;
	case MENU_RESETVIEW: /* F1: reset view matrix */
		ctrl.scale = 1;
		ctrl.trans[0] = ctrl.trans[1] = 0;
		matIdent(ctrl.rotation);
		drawCube(ctrl.cube, NULL, NULL);
		break;
	case MENU_RESETTEX: /* Del: clear textures */
		if (ctrl.editBlock >= 0)
		{
			Block * b = ctrl.primitives + ctrl.editBlock;
			int     i, j;
			DATA16   p;
			for (i = 0, p = b->texUV; i < DIM(b->texUV); )
			{
				for (j = 0; j < 8; j += 2, i += 2, p += 2)
				{
					p[0] = (ctrl.defU + texCoord[j])   * CELLSZ;
					p[1] = (ctrl.defV + texCoord[j+1]) * CELLSZ;
				}
			}
			memset(b->texTrans, 0, sizeof b->texTrans);
			ctrl.faceEdit = 0;
			ctrl.lastFaceSet = -1;
			showTexCoord(0);
			SIT_SetValues(ctrl.faces[0], SIT_CheckState, True, NULL);
			updateTexCoord();
			drawCube(ctrl.cube, NULL, NULL);
		}
		break;
	case MENU_ROT90TEX: /* rotate last texture */
		if (ctrl.editBlock >= 0)
		{
			DATA16  tex;
			Block * b = ctrl.primitives + ctrl.editBlock;
			int     face = getEditableFace(b, ctrl.faceEdit);
			DATA8   p = b->texTrans + face;
			if (face >= 0)
			{
				uint8_t trans = p[0] & 3;
				p[0] &= ~3;
				if (trans < 3)
					p[0] |= trans+1;
				tex = b->texUV + face * 8;
				uint16_t tmp[2];
				memcpy(tmp, tex, 4);
				memmove(tex, tex + 2, sizeof *tex * 6);
				memcpy(tex + 6, tmp, sizeof tmp);
				updateTexCoord();
				drawCube(ctrl.cube, NULL, NULL);
			}
		}
		break;
	case MENU_MIRRORTEX: /* M: mirror last tex */
		if (ctrl.editBlock >= 0)
		{
			Block * b = ctrl.primitives + ctrl.editBlock;
			int face = getEditableFace(b, ctrl.faceEdit);
			if (face >= 0)
			{
				uint16_t tmp;
				DATA16   tex = b->texUV + face * 8;
				if (tex[0] != tex[6])
				{
					swap_tmp(tex[0], tex[6], tmp);
					swap_tmp(tex[2], tex[4], tmp);
				} else {
					swap_tmp(tex[1], tex[7], tmp);
					swap_tmp(tex[3], tex[5], tmp);
				}
				updateTexCoord();
				drawCube(ctrl.cube, NULL, NULL);
			}
		}
		break;
	case MENU_COPYTEX: /* C: copy last text */
		if (ctrl.editBlock >= 0)
		{
			Block * b = ctrl.primitives + ctrl.editBlock;
			int face = ctrl.faceEdit - 1;
			int faces = b->faces;
			while (face >= 0 && (faces & (1<<face)) == 0) face --;
			if (face >= 0)
			{
				DATA16 tex = b->texUV + face * 8;
				memcpy(tex + 8, tex, 8 * sizeof *tex);
				editFace(1);
				updateTexCoord();
				drawCube(ctrl.cube, NULL, NULL);
			}
		}
		break;
	case MENU_SWITCHSEL: /* F3: switch sel mode */
		SIT_ApplyCallback((&ctrl.full)[1-ctrl.detailSel], (APTR) (1-ctrl.detailSel), SITE_OnActivate);
		break;
	case MENU_SWAPVIEW: /* F2: swap texture and 3d views */
		ctrl.swapView = ! ctrl.swapView;
		if (ctrl.swapView)
		{
			int width, height;
			SIT_GetValues(ctrl.canvas, SIT_MinWidth, &width, SIT_MinHeight, &height, NULL);
			SIT_SetAttributes(ctrl.dialog,
				"<img left=OPPOSITE,addbox top=WIDGET,Copy,0.5em bottom=NONE right=FORM width=0 height=0 minWidth=0 minHeight=0>"
				"<addbox left=WIDGET,preview,0.5em top=OPPOSITE,preview>"
				"<preview right=NONE left=FORM bottom=FORM top=WIDGET,full,0.5em height=100 minWidth=", width, "minHeight=", height, ">"
				"<bbox top=WIDGET,img,0.5em>"
			);
		}
		else
		{
			int width, height;
			SIT_GetValues(ctrl.cube, SIT_MinWidth, &width, SIT_MinHeight, &height, NULL);
			SIT_SetAttributes(ctrl.dialog,
				"<img right=NONE left=FORM bottom=FORM top=WIDGET,full,0.5em height=100 minWidth=", width, "minHeight=", height, ">"
				"<addbox left=WIDGET,img,0.5em top=OPPOSITE,img>"
				"<preview left=OPPOSITE,addbox top=WIDGET,Copy,0.5em bottom=NONE right=FORM width=0 height=0 minWidth=10 minHeight=10>"
				"<bbox top=WIDGET,preview,0.5em>"
			);
		}
		break;
	case MENU_NEXTFACE:
		SIT_GetValues(SIT_GetFocus(), SIT_CtrlType, &type, NULL);
		/* don't hi-jack Tab if inside a text box */
		if (type == SIT_EDITBOX) return 0;
		editFace(1);
		break;
	case MENU_PREVFACE: /* backspace: edit prev face */
		SIT_GetValues(SIT_GetFocus(), SIT_CtrlType, &type, NULL);
		/* don't hi-jack BackSpace from text box */
		if (type == SIT_EDITBOX) return 0;
		editFace(-1);
		break;
	case MENU_ABOUT:
		SIT_Log(w, SIT_INFO,
			"TileFinder 1.2\n"
			"Written by T.Pierron, Feb 2020.\n"
			"Free software under BSD license.\n"
		);
		break;
	case MENU_EXIT:
		SIT_Exit(0);
	}
	return 1;
}

/* button activation */
static int resetView(SIT_Widget w, APTR cd, APTR ud)
{
	menuHandler(ctrl.dialog, (APTR) MENU_RESETVIEW, NULL);
	return 1;
}

static int cullFace(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_GetValues(w, SIT_CheckState, &ctrl.cullFace, NULL);
	drawCube(ctrl.cube, NULL, NULL);
	return 1;
}

/* change size of current box */
static int setSize(SIT_Widget w, APTR cd, APTR ud)
{
	if (ctrl.editBlock >= 0)
	{
		STRPTR value;
		Block * b = ctrl.primitives + ctrl.editBlock;

		SIT_GetValues(w, SIT_Title, &value, NULL);

		b->size[(int) ud] = strtod(value, NULL);

		blockResetVertices(b);

		TEXT size[64];
		sprintf(size, "%g, %g, %g", b->size[0], b->size[1], b->size[2]);

		SIT_ListSetCell(ctrl.list, ctrl.editBlock, 1, size, DontChangePtr, DontChangePtr);

		drawCube(ctrl.cube, NULL, NULL);
	}
	return 0;
}

/* translate value changed on one axis */
static int setTrans(SIT_Widget w, APTR cd, APTR ud)
{
	if (ctrl.editBlock >= 0)
	{
		STRPTR value;
		Block * b = ctrl.primitives + ctrl.editBlock;

		SIT_GetValues(w, SIT_Title, &value, NULL);

		b->trans[(int) ud] = strtod(value, NULL);

		blockResetVertices(b);

		drawCube(ctrl.cube, NULL, NULL);
	}
	return 0;
}

/* rotate on one axis */
static int setRotation(SIT_Widget w, APTR cd, APTR ud)
{
	if (ctrl.editBlock >= 0)
	{
		STRPTR value;
		Block * b = ctrl.primitives + ctrl.editBlock;

		SIT_GetValues(w, SIT_Title, &value, NULL);

		b->rotate[(int) ud] = strtod(value, NULL);

		blockResetVertices(b);

		drawCube(ctrl.cube, NULL, NULL);
	}
	return 0;
}

/* rotate by 90deg step */
static int rot90Block(SIT_Widget w, APTR cd, APTR ud)
{
	int i = ctrl.rot90 + (ud ? -1 : 1);
	if (i < 0) i = 3;
	if (i > 3) i = 0;
	ctrl.rot90 = i;

	for (i = 0; i < ctrl.nbBlocks; i ++)
		blockResetVertices(ctrl.primitives + i);

	drawCube(ctrl.cube, NULL, NULL);

	SIT_SetValues(ctrl.lab90, SIT_Title, rot90Names[ctrl.rot90], NULL);

	return 1;
}

/* scale box size on one axis */
static int setRotationCascade(SIT_Widget w, APTR cd, APTR ud)
{
	if (ctrl.editBlock >= 0)
	{
		STRPTR value;
		Block * b = ctrl.primitives + ctrl.editBlock;
		int     i;

		SIT_GetValues(w, SIT_Title, &value, NULL);

		b->rotCascade[(int) ud] = strtod(value, NULL);

		for (i = b - ctrl.primitives; i < ctrl.nbBlocks; i ++, b ++)
			blockResetVertices(b);

		drawCube(ctrl.cube, NULL, NULL);
	}
	return 0;
}

/* remove quads from box */
static int selFace(SIT_Widget w, APTR cd, APTR ud)
{
	int id = 1 << (int) ud;
	int state;

	SIT_GetValues(w, SIT_CheckState, &state, NULL);

	if (ctrl.editBlock >= 0)
	{
		Block * b = ctrl.primitives + ctrl.editBlock;

		if (state) b->faces |= id;
		else       b->faces &= ~id;

		id = ctrl.faceEdit;
		if (id == (int) ud)
		{
			while (id < 6 && (b->faces & (1<<id)) == 0) id ++;
			if (id == 6 && ctrl.faceEdit > 0)
			{
				for (id = ctrl.faceEdit-1; id >= 0 && (b->faces & (1<<id)) == 0; id --);
			}
			if (0 <= id && id < 6)
			{
				ctrl.faceEdit = id;
				showTexCoord(id);
				SIT_SetValues(ctrl.faces[id], SIT_CheckState, 1, NULL);
			}
		}

		drawCube(ctrl.cube, NULL, NULL);
	}
	return 1;
}

static int selFaceEdit(SIT_Widget w, APTR cd, APTR ud)
{
	ctrl.faceEdit = (int) ud;
	showTexCoord(ctrl.faceEdit);
	return 1;
}

static int paintCursor(SIT_Widget w, APTR gc, APTR ud)
{
	if (ctrl.detailSel || ctrl.curCX < 0) return 0;
	double csz, fact;
	int    cx, cy, x, y;

	SIT_GetValues(ctrl.canvas, VIT_ZoomX, &x, VIT_ZoomY, &y, VIT_Factor, &fact, NULL);
	csz = CELLSZ * fact;
	cx = ctrl.curCX*csz+x;
	cy = ctrl.curCY*csz+y;
	GFX_SetPenEx(gc, 1, RGB(255,255,255), GFX_PenSolid);
	GFX_DrawRect(gc, cx, cy, cx+csz-1, cy+csz-1);
	return 1;
}

/* highlight unit cube */
static int showBBox(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_GetValues(w, SIT_CheckState, &ctrl.showBBox, NULL);
	drawCube(ctrl.cube, NULL, NULL);
	return 1;
}

static int selMode(SIT_Widget w, APTR cd, APTR ud)
{
	int id = (int) ud;
	ctrl.detailSel = id;
	SIT_SetValues(w, SIT_CheckState, 1, NULL);
	SIT_SetValues((&ctrl.full)[1-id], SIT_CheckState, 0, NULL);
	SIT_SetValues(ctrl.canvas, VIT_Marquee, id, NULL);
	Block * b = ctrl.primitives + ctrl.editBlock;
	b->detailMode = id;
	if (b->detailMode == 0)
		b->detailFaces = 0;
	return 1;
}

/* SITE_OnCtxMenu */
static int showMenu(SIT_Widget w, APTR cd, APTR ud)
{
	if (SIT_GetFocus() == ctrl.dialog)
	{
		int * pos = cd;
		int   id  = SIT_PopupMenu(w, ud, pos[0], pos[1], w);
		if (id > 0) menuHandler(w, (APTR) id, NULL);
	}
	return 1;
}

/* SITE_OnClose */
static int saveChanges(SIT_Widget w, APTR cd, APTR ud)
{
	FILE * out = fopen("Block.txt", "wb");
	int    i, j, detail = ctrl.primitives[0].detailMode;
	fprintf(out, "# Settings\n");
	fprintf(out, "DetailMode=%d\n", ctrl.detailSel);
	fprintf(out, "ShowBBox=%d\n", ctrl.showBBox);
	fprintf(out, "SwapView=%d\n", ctrl.swapView);
	fprintf(out, "CullFace=%d\n", ctrl.cullFace);
	if (ctrl.nbBlocks > 0)
	{
		for (i = 0; i < ctrl.nbBlocks; )
		{
			DATA16  tex;
			Block * b = ctrl.primitives + i;
			int     faces;
			i ++;
			faces = b->faces | (!b->detailMode << 7) | ((i < ctrl.nbBlocks) << 8) | (ctrl.rot90 << 9);
			if (! detail && b->detailMode) faces |= b->detailFaces<<11;
			fprintf(out, "# Block %d description\n", i);
			fprintf(out, "Block=%d", faces);
			fprintf(out, ",%g,%g,%g", b->size[0], b->size[1], b->size[2]);
			fprintf(out, ",%g,%g,%g", b->trans[0], b->trans[1], b->trans[2]);
			fprintf(out, ",%g,%g,%g", b->rotate[0], b->rotate[1], b->rotate[2]);
			fprintf(out, ",%g,%g,%g", b->rotCascade[0], b->rotCascade[1], b->rotCascade[2]);
			faces &= 63;
			if (b->detailMode)
			{
				if (! detail) faces = b->detailFaces;
				for (tex = b->texUV, faces &= 63; faces; faces >>= 1)
				{
					if (faces & 1)
					{
						for (j = 0; j < 8; j += 2, tex += 2)
							fprintf(out, ",%d", tex[0] + tex[1] * 513);
					}
					else tex += 8;
				}
			}
			else if (i == 1)
			{
				for (tex = b->texUV, faces = 0; faces < 6; faces ++)
					for (j = 0; j < 8; j += 2, tex += 2)
						fprintf(out, ",%d", tex[0] + tex[1] * 513);
			}
			fputc('\n', out);
		}
	}
	fclose(out);

	return 1;
}

static STRPTR parseBlock(STRPTR fmt)
{
	int    faces, n, detail;
	float  sz[3];
	STRPTR p;

	/* remove C comment from fmt */
	for (p = fmt; *p; p ++)
	{
		if (p[0] == '/' && p[1] == '*')
		{
			STRPTR end;
			for (end = p + 2; *end && ! (end[0] == '*' && end[1] == '/'); end ++);
			if (*end) end += 2;
			memset(p, ' ', end - p);
			p = end-1;
		}
		else if (p[0] == '+' && p[1] == 'B')
		{
			/* remove constant */
			STRPTR end;
			for (end = p + 2; *end && *end != ','; end ++);
			if (*end) end ++;
			*p++ = ','; memset(p, ' ', end - p);
			p = end - 1;
		}
	}

	/* check for simplified form first */
	for (n = 0, p = fmt; n < 12; n ++, p ++)
	{
		faces = strtoul(p, &p, 10);
		if (faces > 31 || *p != ',') break;
	}
	if ((faces = strtoul(p, &p, 10)) < (1<<13) && p[0] == 0)
	{
		/* simplified form */
		Block * b = blockAdd(16, 16, 16);
		DATA16  tex;

		ctrl.rot90 = 0;
		ctrl.detailSel = 0;
		b->detailMode = 0;

		for (n = 0, tex = b->texUV; n < 6; n ++, faces >>= 2)
		{
			char * coord = texCoord + (faces & 3) * 8;
			int u = strtoul(fmt, &fmt, 10); fmt ++;
			int v = strtoul(fmt, &fmt, 10); fmt ++;
			int j;
			b->texTrans[n] = 0x80 | (faces&3);

			for (j = 0; j < 8; j += 2, tex += 2)
			{
				tex[0] = (u + coord[j])   * CELLSZ;
				tex[1] = (v + coord[j+1]) * CELLSZ;
			}
		}
		return p;
	}

	faces  = strtol(fmt, &fmt, 10);
	detail = ctrl.nbBlocks > 0 ? ctrl.primitives[0].detailMode : ctrl.detailSel;
	ctrl.rot90 = (faces >> 9) & 3;

	if (*fmt == ',' && sscanf(fmt + 1, "%f,%f,%f%n", sz, sz+1, sz+2, &n) >= 3)
	{
		Block * b = blockAdd(sz[0], sz[1], sz[2]);

		if (b == NULL) return NULL;

		fmt += n+2;
		b->faces = faces & 127;
		b->detailMode = (faces & 0x80) == 0;
		b->detailFaces = faces >> 11;

		if (sscanf(fmt, "%f,%f,%f%n", b->trans, b->trans+1, b->trans+2, &n) < 3)
		{
			ctrl.nbBlocks--;
			return NULL;
		}
		fmt += n+1;
		if (sscanf(fmt, "%f,%f,%f%n", b->rotate, b->rotate+1, b->rotate+2, &n) < 3)
		{
			ctrl.nbBlocks--;
			return NULL;
		}
		fmt += n+1;
		if (sscanf(fmt, "%f,%f,%f%n", b->rotCascade, b->rotCascade+1, b->rotCascade+2, &n) < 3)
		{
			ctrl.nbBlocks--;
			return NULL;
		}
		blockResetVertices(b);

		fmt += n+1;
		faces &= 63;
		DATA16 tex;
		if (b->detailMode == 0)
		{
			/* first row define all sides */
			if (ctrl.nbBlocks > 1) return fmt;
			faces = 63;
		}
		else if (! detail)
		{
			faces = b->detailFaces;
		}

		for (n = 0, tex = b->texUV; faces; faces >>= 1, tex += 8, n ++)
		{
			if (faces & 1)
			{
				int i, minU = 0, minV = 0, min = -1;
				for (i = 0; i < 8; i += 2)
				{
					int U = strtoul(fmt, &fmt, 10);
					int V = U / 513; U %= 513;
					if (*fmt == ',') fmt ++;
					tex[i] = U;
					tex[i+1] = V;
					if (min < 0 || (U < minU && V < minV))
						min = i, minU = U, minV = V;
				}
				b->texTrans[n] = 3-min/2;
				if (abs(tex[0]-tex[6]) == 16 && abs(tex[1]-tex[3]) == 16)
				{
					b->texTrans[n] |= 0x80;
				}
			}
		}
		return fmt;
	}
	return NULL;
}

static void loadPrefs(void)
{
	FILE * in = fopen("Block.txt", "rb");

	if (in)
	{
		TEXT line[512];
		while (fgets(line, sizeof line, in))
		{
			if (line[0] == '#') continue;

			STRPTR sep = strchr(line, '=');
			if (! sep) continue; *sep ++ = 0;
			switch (FindInList("DetailMode,ShowBBox,SwapView,CullFace,Block", line, 0)) {
			case 0: ctrl.detailSel = atoi(sep); break;
			case 1: ctrl.showBBox  = atoi(sep); break;
			case 2: ctrl.swapView  = atoi(sep); break;
			case 3: ctrl.cullFace  = atoi(sep); break;
			case 4: parseBlock(sep);
			}
		}
		fclose(in);
	}
}

static void blockEdit(Block * b)
{
	TEXT sz[128];
	sprintf(sz,    "%g", b->size[0]);
	sprintf(sz+16, "%g", b->size[1]);
	sprintf(sz+32, "%g", b->size[2]);
	SIT_SetAttributes(ctrl.dialog,
		"<szx title=", sz, "><szy title=", sz+16, "><szz title=", sz+32, ">"
		"<faceS checkState=", (b->faces & 1)  > 0, ">"
		"<faceE checkState=", (b->faces & 2)  > 0, ">"
		"<faceN checkState=", (b->faces & 4)  > 0, ">"
		"<faceW checkState=", (b->faces & 8)  > 0, ">"
		"<faceT checkState=", (b->faces & 16) > 0, ">"
		"<faceB checkState=", (b->faces & 32) > 0, ">"
		"<INV   checkState=", (b->faces & 64) > 0, ">"
	);
	sprintf(sz, "<trx  title=%g><try  title=%g><trz  title=%g>", b->trans[0], b->trans[1], b->trans[2]); SIT_SetAttributes(ctrl.dialog, sz);
	sprintf(sz, "<rezx title=%g><rezy title=%g><rezz title=%g>", b->rotCascade[0], b->rotCascade[1], b->rotCascade[2]); SIT_SetAttributes(ctrl.dialog, sz);
	sprintf(sz, "<rotx title=%g><roty title=%g><rotz title=%g>", b->rotate[0], b->rotate[1], b->rotate[2]); SIT_SetAttributes(ctrl.dialog, sz);
	updateTexCoord();

	SIT_ApplyCallback((&ctrl.full)[b->detailMode], (APTR) (int) b->detailMode, SITE_OnActivate);

	int id = ctrl.faceEdit;
	while (id < 6 && (b->faces & (1<<id)) == 0) id ++;
	if (id == 6 && ctrl.faceEdit > 0)
	{
		for (id = ctrl.faceEdit-1; id >= 0 && (b->faces & (1<<id)) == 0; id --);
	}
	if (0 <= id && id < 6 && id != ctrl.faceEdit)
	{
		ctrl.faceEdit = id;
		showTexCoord(id);
		SIT_SetValues(ctrl.faces[id], SIT_CheckState, 1, NULL);
	}
}

/* add a new box to the list */
static int addBox(SIT_Widget w, APTR cd, APTR ud)
{
	Block * b = blockAdd(16,16,16);

	if (b)
	{
		ctrl.editBlock = b - ctrl.primitives;

		blockAddItem(b, False);
		blockEdit(b);
		SIT_SetValues(ctrl.list, SIT_SelectedIndex, ctrl.editBlock, NULL);

		drawCube(ctrl.cube, NULL, NULL);
	}
	return 1;
}

/* delete one box from view */
static int delBox(SIT_Widget w, APTR cd, APTR ud)
{
	if (ctrl.editBlock >= 0)
	{
		Block * b = ctrl.primitives + ctrl.editBlock;

		SIT_ListDeleteRow(ctrl.list, ctrl.editBlock);

		if (ctrl.editBlock < ctrl.nbBlocks-1)
			memcpy(b, b + 1, (ctrl.nbBlocks - ctrl.editBlock - 1) * sizeof *b);
		else
			ctrl.editBlock --;

		ctrl.nbBlocks --;
		SIT_SetValues(ctrl.list, SIT_SelectedIndex, ctrl.editBlock, NULL);
		blockEdit(b);
		drawCube(ctrl.cube, NULL, NULL);
	}
	return 1;
}

/* restart from scratch */
static int resetBox(SIT_Widget w, APTR cd, APTR ud)
{
	if (SIT_Ask(w, "Confirm", SITV_YesNo, "Are you sure you want to delete everything?"))
	{
		ctrl.editBlock = ctrl.nbBlocks = ctrl.rot90 = 0;
		SIT_ListDeleteRow(ctrl.list, DeleteAllRow);
		blockAdd(16, 16, 16);
		blockAddItem(ctrl.primitives, False);
		blockEdit(ctrl.primitives);
		ctrl.faceEdit = 0;
		showTexCoord(0);
		SIT_SetValues(ctrl.faces[0], SIT_CheckState, True, NULL);
		SIT_SetValues(ctrl.list, SIT_SelectedIndex, 0, NULL);
		drawCube(ctrl.cube, NULL, NULL);
	}
	return 1;
}

/* remove all texture information */
static int clearTex(SIT_Widget w, APTR cd, APTR ud)
{
	Block * b;
	int     i, j, face;
	for (b = ctrl.primitives, i = 0; i < ctrl.nbBlocks; i ++, b ++)
	{
		DATA16 p;
		for (face = 0, p = b->texUV; face < 6; face ++)
		{
			for (j = 0; j < 8; j += 2, p += 2)
			{
				p[0] = (ctrl.defU + texCoord[j])   * CELLSZ;
				p[1] = (ctrl.defV + texCoord[j+1]) * CELLSZ;
			}
		}
	}
	ctrl.faceEdit = 0;
	ctrl.lastFaceSet = -1;
	SIT_SetValues(ctrl.faces[0], SIT_CheckState, 1, NULL);
	drawCube(ctrl.cube, NULL, NULL);
	return 1;
}

/* object list selection change */
static int selectBox(SIT_Widget w, APTR cd, APTR ud)
{
	if ((int) cd >= 0)
	{
		ctrl.editBlock = (int) cd;
		blockEdit(ctrl.primitives + ctrl.editBlock);
	}
	else SIT_SetValues(ctrl.list, SIT_SelectedIndex, ctrl.editBlock, NULL);
	return 1;
}

int my_main(int nb, char * argv[])
{
	static SIT_MenuStruct menu[] = {
		{1, "&Tiles"},
			{2, "Copy block",      "C",     0, MENU_COPY},
			{2, "Paste block",     "V",     0, MENU_PASTE},
			{2, "Reset 3d view",   "F1",    0, MENU_RESETVIEW},
			{2, "Reset all tex",   "Del",   0, MENU_RESETTEX},
			{2, "Rotate tex",      "\tR",   0, MENU_ROT90TEX,  'R'},
			{2, "Mirror tex",      "\tM",   0, MENU_MIRRORTEX, 'M'},
			{2, "Copy tex face",   "\tC",   0, MENU_COPYTEX,   'C'},
			{2, "Switch sel mode", "F3",    0, MENU_SWITCHSEL},
			{2, "Swap view",       "F2",    0, MENU_SWAPVIEW},
			{2, "Edit next face",  "Tab",   0, MENU_NEXTFACE},
			{2, "Edit prev face",  "Back",  0, MENU_PREVFACE, SITK_BackSpace},
			{2, SITM_SEPARATOR},
			{2, "About...",       NULL,     0, MENU_ABOUT},
			{2, "Exit",            "Q",     0, MENU_EXIT},
		{0}
	};

	loadPrefs();

	ctrl.app = SIT_CreateWidget("TileFinder", SIT_APP, NULL, NULL);

	SIT_Widget dialog = SIT_CreateWidget("MainWnd", SIT_DIALOG, ctrl.app,
		SIT_Title,        "Tile Finder",
		SIT_Styles,       SITV_NoResize,
		SIT_Margins,      8, 8, 8, 8,
		SIT_FocusOnClick, True,
		SIT_Menu,         menu,
		SIT_MenuVisible,  False, // Too few entries to be worth displaying - but shortcuts will work
		NULL
	);

	SIT_Widget max = NULL;
	SIT_CreateWidgets(dialog,
		"<label name=txt title='Tile : ' font=System/Bold>"
		"<label name=coord title='' left=WIDGET,txt,0.5em width=10em resizePolicy=", SITV_Fixed, ">"
		"<label name=select title='Tex selection:' left=WIDGET,coord,1em font=System/bold>"
		"<button name=full title='Full block' buttonType=", SITV_ToggleButton, "checkState=", ctrl.detailSel == 0, "left=WIDGET,select,0.5em>"
		"<button name=detail title=Detail buttonType=", SITV_ToggleButton, "checkState=", ctrl.detailSel == 1, "left=WIDGET,full,0.1em>"
		"<label name=help title='Mouse wheel to zoom, left to drag image, middle to select, right to cancel' right=FORM"
		" top=MIDDLE,detail foreground=", RGB(0x66,0x66,0x66), ">"
		"<canvas name=img top=WIDGET,full,0.5em bottom=FORM minWidth=800 minHeight=600 background=", BGCOLOR, "/>"
		/* toolbar */
		"<button name=addbox title='Add box' margins=0,8,0,8 left=WIDGET,img,0.5em top=OPPOSITE,img>"
		"<button name=delbox title=Del margins=0,8,0,8 left=WIDGET,addbox,0.5em top=OPPOSITE,addbox>"
		"<button name=clear  title='Del all' margins=0,8,0,8 left=WIDGET,delbox,0.5em top=OPPOSITE,addbox>"
		"<button name=clstex title='Clear tex' margins=0,8,0,8 left=WIDGET,clear,0.5em top=OPPOSITE,addbox>"
		"<listbox name=objects minWidth=10em height=10em top=WIDGET,addbox,0.5em left=OPPOSITE,addbox right=FORM"
		" columnNames='Primitive\tSize' columnWidths='*\t*' listBoxFlags=", SITV_FullRowSelect|SITV_NoSort, ">"

		"<label name=X><label name=Y><label name=Z>"
		"<editbox name=szx width=5em title=16 editType=", SITV_Integer, "minValue=0 maxValue=32 buddyLabel=", "SIZE:", &max, "top=WIDGET,objects,1.5em>"
		"<editbox name=szy width=5em title=16 editType=", SITV_Integer, "minValue=0 maxValue=32 top=OPPOSITE,szx left=WIDGET,szx,0.2em>"
		"<editbox name=szz width=5em title=16 editType=", SITV_Integer, "minValue=0 maxValue=32 top=OPPOSITE,szx left=WIDGET,szy,0.2em>"

		"<editbox name=trx width=5em title=0 editType=", SITV_Integer, "minValue=-8 maxValue=24 buddyLabel=", "TR:", &max, "top=WIDGET,szx,0.5em>"
		"<editbox name=try width=5em title=0 editType=", SITV_Integer, "minValue=-8 maxValue=24 top=OPPOSITE,trx left=WIDGET,trx,0.2em>"
		"<editbox name=trz width=5em title=0 editType=", SITV_Integer, "minValue=-8 maxValue=24 top=OPPOSITE,trx left=WIDGET,try,0.2em>"

		"<editbox name=rotx width=5em title=0 editType=", SITV_Integer, "minValue=-180 maxValue=180 buddyLabel=", "ROT:", &max, "top=WIDGET,trx,0.5em>"
		"<editbox name=roty width=5em title=0 editType=", SITV_Integer, "minValue=-180 maxValue=180 top=OPPOSITE,rotx left=WIDGET,rotx,0.2em>"
		"<editbox name=rotz width=5em title=0 editType=", SITV_Integer, "minValue=-180 maxValue=180 top=OPPOSITE,rotx left=WIDGET,roty,0.2em>"

		"<editbox name=tex title='' buddyLabel=", "TEX:", &max, "top=WIDGET,rotx,0.5em right=FORM>"

		"<button name=faceS title='S' buddyLabel=", "FACES:", &max, "buttonType=", SITV_ToggleButton, "checkState=1 margins=0,8,0,8 top=WIDGET,tex,0.5em>"
		"<button name=faceE title='E' buttonType=", SITV_ToggleButton, "checkState=1 top=OPPOSITE,faceS margins=0,8,0,8 left=WIDGET,faceS>"
		"<button name=faceN title='N' buttonType=", SITV_ToggleButton, "checkState=1 top=OPPOSITE,faceS margins=0,8,0,8 left=WIDGET,faceE>"
		"<button name=faceW title='W' buttonType=", SITV_ToggleButton, "checkState=1 top=OPPOSITE,faceS margins=0,8,0,8 left=WIDGET,faceN>"
		"<button name=faceT title='T' buttonType=", SITV_ToggleButton, "checkState=1 top=OPPOSITE,faceS margins=0,8,0,8 left=WIDGET,faceW>"
		"<button name=faceB title='B' buttonType=", SITV_ToggleButton, "checkState=1 top=OPPOSITE,faceS margins=0,8,0,8 left=WIDGET,faceT>"
		"<button name=INV   title='I' buttonType=", SITV_ToggleButton, "checkState=0 top=OPPOSITE,faceS margins=0,8,0,8 left=WIDGET,faceB tooltip='Invert normals'>"

		"<label  name=beditS title='EDIT:' left=OPPOSITE,addbox font=System/Bold maxWidth=bfaceS alignHoriz=", SITV_AlignRight, ">"
		"<button name=editS title='' buttonType=", SITV_RadioButton, "checkState=1 top=WIDGET,faceS,0.5em left=MIDDLE,faceS>"
		"<button name=editE title='' buttonType=", SITV_RadioButton, "top=OPPOSITE,editS left=MIDDLE,faceE>"
		"<button name=editN title='' buttonType=", SITV_RadioButton, "top=OPPOSITE,editS left=MIDDLE,faceN>"
		"<button name=editW title='' buttonType=", SITV_RadioButton, "top=OPPOSITE,editS left=MIDDLE,faceW>"
		"<button name=editT title='' buttonType=", SITV_RadioButton, "top=OPPOSITE,editS left=MIDDLE,faceT>"
		"<button name=editB title='' buttonType=", SITV_RadioButton, "top=OPPOSITE,editS left=MIDDLE,faceB>"
		"<button name=editH title='' buttonType=", SITV_RadioButton, "visible=0>"

		"<frame name=sep left=OPPOSITE,addbox top=WIDGET,editS,0.5em title='=== Global ===' right=FORM>"

		"  <button name=rotm90 title='-90' buddyLabel=", "ORIENT:", &max, "margins=0,8,0,8>"
		"  <button name=rot90 title='+90' margins=0,8,0,8 top=OPPOSITE,rotm90 left=WIDGET,rotm90,0.5em>"
		"  <label name=brot90 title=", rot90Names[ctrl.rot90], " left=WIDGET,rot90,0.5em right=FORM top=MIDDLE,rot90 resizePolicy=", SITV_Fixed, ">"

		"  <editbox name=rezx width=5em title=0 editType=", SITV_Integer, "minValue=-180 maxValue=180 buddyLabel=", "ROT:", &max, "top=WIDGET,rotm90,0.5em>"
		"  <editbox name=rezy width=5em title=0 editType=", SITV_Integer, "minValue=-180 maxValue=180 top=OPPOSITE,rezx left=WIDGET,rezx,0.2em>"
		"  <editbox name=rezz width=5em title=0 editType=", SITV_Integer, "minValue=-180 maxValue=180 top=OPPOSITE,rezx left=WIDGET,rezy,0.2em>"

		"</frame>"

		"<button name=Copy margins=0,8,0,8 top=WIDGET,sep,0.5em left=OPPOSITE,addbox>"
		"<button name=Paste margins=0,8,0,8 top=OPPOSITE,Copy left=WIDGET,Copy,1em>"

		"<canvas name=preview left=OPPOSITE,addbox top=WIDGET,Copy,0.5em right=FORM height=100 background=", RGB(248,248,248), "/>"

		"<button name=bbox title='Show unit bbox' buttonType=", SITV_ToggleButton, "checkState=", ctrl.showBBox, "top=WIDGET,preview,0.5em"
		" left=OPPOSITE,addbox margins=0,8,0,8>"
		"<button name=cull title='Cull face' buttonType=", SITV_ToggleButton, "checkState=", ctrl.cullFace, "top=OPPOSITE,bbox"
		" left=WIDGET,bbox,0.5em margins=0,8,0,8>"
		"<button name=reset title='Reset view' top=OPPOSITE,bbox left=WIDGET,cull,0.5em margins=0,8,0,8>"
	);
	SIT_SetAttributes(dialog,
		"<bszx left=OPPOSITE,addbox font=System/bold>"
		"<btrx left=OPPOSITE,addbox font=System/bold>"
		"<brotx left=OPPOSITE,addbox font=System/bold>"
		"<brezx font=System/bold>"
		"<btex left=OPPOSITE,addbox font=System/bold>"
		"<bfaceS left=OPPOSITE,addbox font=System/bold>"
		"<brotm90 font=System/bold>"
		"<beditS top=MIDDLE,editS>"
		"<X left=MIDDLE,szx bottom=WIDGET,szx,0.1em>"
		"<Y left=MIDDLE,szy bottom=WIDGET,szy,0.1em>"
		"<Z left=MIDDLE,szz bottom=WIDGET,szz,0.1em>"
		"<select top=MIDDLE,full>"
		"<coord top=MIDDLE,full>"
		"<txt top=MIDDLE,full>"
	);
	ctrl.dialog = dialog;
	ctrl.canvas = SIT_GetById(dialog, "img");
	ctrl.label  = SIT_GetById(dialog, "coord");
	ctrl.coords = SIT_GetById(dialog, "coords");
	ctrl.cube   = SIT_GetById(dialog, "preview");
	ctrl.list   = SIT_GetById(dialog, "objects");
	ctrl.full   = SIT_GetById(dialog, "full");
	ctrl.detail = SIT_GetById(dialog, "detail");
	ctrl.tex    = SIT_GetById(dialog, "tex");
	ctrl.lab90  = SIT_GetById(dialog, "brot90");
	ctrl.scale  = 1;
	ctrl.lastFaceSet = -1;
	matIdent(ctrl.rotation);
	ViewImageInit(ctrl.canvas, NULL);
	SIT_SetValues(ctrl.canvas, VIT_Overlay, paintCursor, VIT_Marquee, ctrl.detailSel, VIT_MiniMap, 0, NULL);
	SIT_AddCallback(ctrl.full,   SITE_OnActivate, selMode, NULL);
	SIT_AddCallback(ctrl.detail, SITE_OnActivate, selMode, (APTR) 1);

	GFX_EnableGL(ctrl.cube);

	setImage(GFX_LoadImage(nb > 1 ? argv[1] : "Terrain.png"));
	if (ctrl.nbBlocks == 0)
		blockAdd(16, 16, 16);

	if (ctrl.swapView)
	{
		ctrl.swapView = 0;
		menuHandler(ctrl.dialog, (APTR) MENU_SWAPVIEW, NULL);
	}

	for (nb = 0; nb < ctrl.nbBlocks; nb ++)
		blockAddItem(ctrl.primitives + nb, True);
	blockEdit(ctrl.primitives);
	SIT_SetValues(ctrl.list, SIT_SelectedIndex, 0, NULL);

	static STRPTR faceNames[] = {"editS", "editE", "editN", "editW", "editT", "editB", "editH"};

	for (nb = 0; nb < DIM(faceNames); nb ++)
	{
		ctrl.faces[nb] = SIT_GetById(dialog, faceNames[nb]);
		SIT_AddCallback(ctrl.faces[nb], SITE_OnActivate, selFaceEdit, (APTR) nb);
	}

	SIT_AddCallback(ctrl.dialog, SITE_OnDropFiles,   changeImage, NULL);
	SIT_AddCallback(ctrl.dialog, SITE_OnClose,       saveChanges, NULL);
	SIT_AddCallback(ctrl.dialog, SITE_OnCtxMenu,     showMenu, menu);
	SIT_AddCallback(ctrl.dialog, SITE_OnMenu,        menuHandler, NULL);
	SIT_AddCallback(ctrl.canvas, SITE_OnClickMove,   mouse, NULL);
	SIT_AddCallback(ctrl.canvas, SITE_OnChange,      selTex, NULL);
	SIT_AddCallback(ctrl.canvas, SITE_OnGeometrySet, fixHeight, (APTR) 1);
	SIT_AddCallback(ctrl.cube,   SITE_OnGeometrySet, fixHeight, NULL);
	SIT_AddCallback(ctrl.cube,   SITE_OnClickMove,   rotateCube, NULL);
	SIT_AddCallback(ctrl.cube,   SITE_OnPaint,       drawCube, NULL);
	SIT_AddCallback(ctrl.list,   SITE_OnChange,      selectBox, NULL);
	SIT_AddCallback(SIT_GetById(dialog, "addbox"), SITE_OnActivate, addBox, NULL);
	SIT_AddCallback(SIT_GetById(dialog, "delbox"), SITE_OnActivate, delBox, NULL);
	SIT_AddCallback(SIT_GetById(dialog, "clear"),  SITE_OnActivate, resetBox, NULL);
	SIT_AddCallback(SIT_GetById(dialog, "clstex"), SITE_OnActivate, clearTex, NULL);
	SIT_AddCallback(SIT_GetById(dialog, "szx"), SITE_OnChanged, setSize, NULL);
	SIT_AddCallback(SIT_GetById(dialog, "szy"), SITE_OnChanged, setSize, (APTR) 1);
	SIT_AddCallback(SIT_GetById(dialog, "szz"), SITE_OnChanged, setSize, (APTR) 2);
	SIT_AddCallback(SIT_GetById(dialog, "trx"), SITE_OnChanged, setTrans, NULL);
	SIT_AddCallback(SIT_GetById(dialog, "try"), SITE_OnChanged, setTrans, (APTR) 1);
	SIT_AddCallback(SIT_GetById(dialog, "trz"), SITE_OnChanged, setTrans, (APTR) 2);
	SIT_AddCallback(SIT_GetById(dialog, "rotx"), SITE_OnChanged, setRotation, NULL);
	SIT_AddCallback(SIT_GetById(dialog, "roty"), SITE_OnChanged, setRotation, (APTR) 1);
	SIT_AddCallback(SIT_GetById(dialog, "rotz"), SITE_OnChanged, setRotation, (APTR) 2);
	SIT_AddCallback(SIT_GetById(dialog, "rezx"), SITE_OnChanged, setRotationCascade, NULL);
	SIT_AddCallback(SIT_GetById(dialog, "rezy"), SITE_OnChanged, setRotationCascade, (APTR) 1);
	SIT_AddCallback(SIT_GetById(dialog, "rezz"), SITE_OnChanged, setRotationCascade, (APTR) 2);
	SIT_AddCallback(SIT_GetById(dialog, "bbox"),  SITE_OnActivate, showBBox, NULL);
	SIT_AddCallback(SIT_GetById(dialog, "reset"), SITE_OnActivate, resetView, NULL);
	SIT_AddCallback(SIT_GetById(dialog, "cull"),  SITE_OnActivate, cullFace, NULL);
	SIT_AddCallback(SIT_GetById(dialog, "faceS"), SITE_OnActivate, selFace, NULL);
	SIT_AddCallback(SIT_GetById(dialog, "faceE"), SITE_OnActivate, selFace, (APTR) 1);
	SIT_AddCallback(SIT_GetById(dialog, "faceN"), SITE_OnActivate, selFace, (APTR) 2);
	SIT_AddCallback(SIT_GetById(dialog, "faceW"), SITE_OnActivate, selFace, (APTR) 3);
	SIT_AddCallback(SIT_GetById(dialog, "faceT"), SITE_OnActivate, selFace, (APTR) 4);
	SIT_AddCallback(SIT_GetById(dialog, "faceB"), SITE_OnActivate, selFace, (APTR) 5);
	SIT_AddCallback(SIT_GetById(dialog, "INV"),   SITE_OnActivate, selFace, (APTR) 6);
	SIT_AddCallback(SIT_GetById(dialog, "Copy"),  SITE_OnActivate, copyBlock, NULL);
	SIT_AddCallback(SIT_GetById(dialog, "Paste"), SITE_OnActivate, pasteBlock, NULL);
	SIT_AddCallback(SIT_GetById(dialog, "rot90"),  SITE_OnActivate, rot90Block, NULL);
	SIT_AddCallback(SIT_GetById(dialog, "rotm90"), SITE_OnActivate, rot90Block, (APTR) 1);

	SIT_ManageWidget(dialog);

	return SIT_Main();
}
