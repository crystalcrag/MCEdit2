/*
 * library.c : handle temporary list of brush (clone + copy) and schematics library.
 *
 * Written by T.Pierron, Sep 2021.
 */


#define MCLIBRARY_IMPL
#include <glad.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <malloc.h>
#include "render.h"
#include "SIT.h"
#include "selection.h"
#include "library.h"
#include "globals.h"

static struct MCLibrary_t library;

/* SITE_OnActivate on "Save" button */
static int librarySaveCopy(SIT_Widget w, APTR cd, APTR ud)
{
	return 1;
}

/* SITE_OnActivate on "Use" button */
static int libraryUseCopy(SIT_Widget w, APTR cd, APTR ud)
{
	LibBrush brush;
	int      nth;
	SIT_GetValues(ud, SIT_SelectedIndex, &nth, NULL);
	SIT_GetValues(ud, SIT_RowTag(nth), &brush, NULL);
	selectionUseBrush(brush->data);
	return 1;
}

/* SITE_OnActivate on "Delete" button */
static int libraryDelCopy(SIT_Widget w, APTR cd, APTR ud)
{
	LibBrush brush;
	int sel;
	SIT_GetValues(ud, SIT_SelectedIndex, &sel, SIT_RowTag(-1), &brush, NULL);
	if (sel >= 0)
	{
		if (brush) libraryDelBrush(brush);
		if (library.nbBrushes == 0)
		{
			SIT_CloseDialog(library.copyWnd);
			renderSetCompassOffset(0);
			library.copyWnd = NULL;
		}
		else SIT_ListDeleteRow(ud, sel);
	}
	return 1;
}

/* select an item in the list of brush */
static int librarySelItem(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_SetValues(library.save, SIT_Enabled, True, NULL);
	SIT_SetValues(library.use,  SIT_Enabled, True, NULL);
	SIT_SetValues(library.del,  SIT_Enabled, True, NULL);
	return 1;
}

static void libraryGenThumb(LibBrush lib)
{
	Map  brush = lib->data;
	mat4 view;
	vec4 center = {
		brush->size[VX] * 0.5,
		brush->size[VY] * 0.5,
		brush->size[VZ] * 0.5,
	};
	vec4 camera;

	camera[VY] = center[VY] * 1.3;
	camera[VT] = 1;
	int surface[] = {
		brush->size[VX] * brush->size[VY],
		brush->size[VX] * brush->size[VZ],
		brush->size[VZ] * brush->size[VY]
	};
	int axis = VX;
	if (surface[VY] > surface[VX])   axis = VY;
	if (surface[VZ] > surface[axis]) axis = VZ;
	/* point the camera to the axis with the biggest surface area */

	switch (axis) {
	case VX: camera[VX] = center[VX] * 1.1;
	         camera[VY] = center[VY] * 1.3;
	         camera[VZ] = center[VZ] + (globals.direction == 0 ? - center[VX] : center[VX]) * 1.5; break;
	case VZ: camera[VZ] = center[VZ] * 1.1;
	         camera[VY] = center[VY] * 1.3;
	         camera[VX] = center[VX] + (globals.direction == 1 ? - center[VZ] : center[VZ]) * 1.5; break;
	case VY: camera[VX] = center[VX] * 1.1;
	         camera[VY] = center[VY] + MAX(brush->size[VX], brush->size[VZ]);
	         camera[VX] = center[VZ] * 1.1;
	}

	matLookAt(view, camera, center, (float[3]){0, 1, 0});
	glBindBuffer(GL_UNIFORM_BUFFER, library.uboShader);
	glBufferSubData(GL_UNIFORM_BUFFER, UBO_CAMERA_OFFFSET, sizeof (vec4), camera);
	glBufferSubData(GL_UNIFORM_BUFFER, UBO_MVMATRIX_OFFSET, sizeof (mat4), view);
	glBindBuffer(GL_UNIFORM_BUFFER, 0);

	nvgluBindFramebuffer(lib->nvgFBO);
	glViewport(0, 0, lib->thumbSz, lib->thumbSz);
	glClearColor(0.3, 0.3, 0.8, 1);
	glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);

	/* this will override the default render.uboShader */
	glBindBufferBase(GL_UNIFORM_BUFFER, UBO_BUFFER_INDEX, library.uboShader);

	renderDrawMap(brush);

	nvgluBindFramebuffer(NULL);

	/* not needed anyore */
	if (! brush->sharedBanks)
		renderFreeMesh(brush, True);
	else
		ListNew(&brush->gpuBanks);
}

/* add a brush into the <copyWnd> list */
static void libraryAddBrush(Map brush)
{
	LibBrush lib = calloc(sizeof *lib, 1);

	if (lib)
	{
		lib->thumbSz = roundf(SIT_EmToReal(library.copyList, SITV_Em(4)));
		lib->data = brush;
		lib->size = brush->maxDist;
		lib->nvgFBO = nvgluCreateFramebuffer(library.nvgCtx, lib->thumbSz, lib->thumbSz, NVG_IMAGE_DEPTH);
		brush->cx = brush->cy = brush->cz = 0;

		if (library.uboShader == 0)
		{
			mat4 P;
			library.uboShader = renderInitUBO();
			glBindBuffer(GL_UNIFORM_BUFFER, library.uboShader);
			matPerspective(P, DEF_FOV, 1, NEAR_PLANE, 1000);
			glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof (mat4), P);
		}

		ListAddTail(&library.brushes, &lib->node);
		library.nbBrushes ++;
		libraryGenThumb(lib);

		SIT_Widget list = library.copyList;
		SIT_Widget td = SIT_ListInsertControlIntoCell(list, SIT_ListInsertItem(list, -1, lib, SITV_TDSubChild), 0);

		TEXT num[10], path[10], size[64];
		sprintf(num, "#%d", library.nbBrushes);
		sprintf(path, "id(%d)", lib->nvgFBO->image);
		sprintf(size, "<b>%d x %d x %d</b><br><dim>(%d kb)</dim>",
			brush->size[VX] - 2, brush->size[VZ] - 2, brush->size[VY] - 2, (lib->size + 1023) >> 10
		);
		SIT_CreateWidgets(td,
			"<label name=num title=", num, " style='vertical-align: middle'>"
			"<label name=icon imagePath=", path, "width=", lib->thumbSz, "height=", lib->thumbSz, "left=WIDGET,num,0.5em>"
			"<label name=wname title=", size, "left=WIDGET,icon,0.5em top=OPPOSITE,icon bottom=OPPOSITE,icon style='vertical-align: middle'>"
		);
		SIT_SetAttributes(td, "<num top=OPPOSITE,icon bottom=OPPOSITE,icon>");
		SIT_ListFinishInsertControl(library.copyList);
	}
}

static int libraryGetOffset(SIT_Widget w, APTR cd, APTR ud)
{
	float x = 0;
	SIT_GetValues(w, SIT_X, &x, NULL);
	renderSetCompassOffset(x);
	return 1;
}

/* user just hit Ctrl+C */
void libraryCopySelection(Map brush)
{
	if (! library.copyWnd)
	{
		SIT_Widget diag = library.copyWnd = SIT_CreateWidget("selcopy.mc", SIT_DIALOG, globals.app,
			SIT_DialogStyles,   SITV_Plain,
			SIT_Right,          SITV_AttachForm, NULL, SITV_Em(0.5),
			SIT_Top,            SITV_AttachForm, NULL, SITV_Em(0.5),
			SIT_LeftAttachment, SITV_AttachNone,
			NULL
		);
		SIT_CreateWidgets(diag,
			"<button name=save.act title=Save enabled=0>"
			"<button name=use.act title=Use enabled=0 left=WIDGET,save,0.5em>"
			"<button name=ko.act title=Delete enabled=0 left=WIDGET,use,0.5em>"
			"<listbox columnNames=X name=list left=FORM right=FORM listBoxFlags=", SITV_NoHeaders, "top=WIDGET,save,0.5em rowMaxVisible=4>"
		);
		library.copyList = SIT_GetById(diag, "list");
		library.save     = SIT_GetById(diag, "save");
		library.use      = SIT_GetById(diag, "use");
		library.del      = SIT_GetById(diag, "ko");
		SIT_GetValues(globals.app, SIT_NVGcontext, &library.nvgCtx, NULL);
		SIT_AddCallback(library.save,     SITE_OnActivate, librarySaveCopy, library.copyList);
		SIT_AddCallback(library.use,      SITE_OnActivate, libraryUseCopy,  library.copyList);
		SIT_AddCallback(library.del,      SITE_OnActivate, libraryDelCopy,  library.copyList);
		SIT_AddCallback(library.copyList, SITE_OnActivate, libraryUseCopy,  library.copyList);
		SIT_AddCallback(library.copyList, SITE_OnChange,   librarySelItem,  NULL);
		SIT_AddCallback(library.copyWnd,  SITE_OnResize,   libraryGetOffset, NULL);
		SIT_ManageWidget(diag);
	}

	libraryAddBrush(brush);
}

void libraryDelBrush(LibBrush lib)
{
	ListRemove(&library.brushes, &lib->node);
	library.nbBrushes --;
	if (lib->data) selectionFreeBrush(lib->data);
	if (lib->nvgFBO) nvgluDeleteFramebuffer(lib->nvgFBO);
	if (lib->nbt.mem) free(lib->nbt.mem);
	free(lib);
}
