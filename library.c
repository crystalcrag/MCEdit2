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
#include "MCEdit.h"
#include "globals.h"
#include "NBT2.h"

static struct MCLibrary_t library;

/* SITE_OnActivate on "Save" button */
static int librarySaveCopy(SIT_Widget w, APTR cd, APTR ud)
{
	library.saveFromLib = True;
	mceditUIOverlay(MCUI_OVERLAY_SAVESEL);
	return 1;
}

/* SITE_OnActivate on "Use" button */
static int libraryUseCopy(SIT_Widget w, APTR cd, APTR ud)
{
	LibBrush brush;
	int      nth;
	SIT_GetValues(ud, SIT_SelectedIndex, &nth, NULL);
	SIT_GetValues(ud, SIT_RowTag(nth), &brush, NULL);
	selectionUseBrush(brush->data, True);
	return 1;
}

/* SITE_OnActivate on "Delete" button */
static int libraryDelCopy(SIT_Widget w, APTR cd, APTR ud)
{
	int sel, count;
	SIT_GetValues(ud, SIT_SelectedIndex, &sel, SIT_ItemCount, &count, NULL);
	if (sel >= 0)
	{
		LibBrush brush;
		SIT_GetValues(ud, SIT_RowTag(sel), &brush, NULL);
		if (brush)
		{
			library.nbBrushes --;
			ListRemove(&library.brushes, &brush->node);
			libraryFreeBrush(brush);
		}
		if (library.nbBrushes == 0)
		{
			SIT_CloseDialog(library.copyWnd);
			renderSetCompassOffset(0);
			library.copyWnd = NULL;
		}
		else
		{
			SIT_ListDeleteRow(ud, sel);
			/* renumber following rows */
			for (count --; sel < count; sel ++)
			{
				SIT_Widget td = SIT_ListInsertControlIntoCell(ud, sel, 0);
				TEXT num[10];
				sprintf(num, "#%d", sel + 1);
				SIT_SetValues(SIT_GetById(td, "num"), SIT_Title, num, NULL);
				SIT_ListFinishInsertControl(ud);
			}
		}
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
	case VX: camera[VX] = center[VX] * 1.1f;
	         camera[VY] = center[VY] * 1.3f;
	         camera[VZ] = center[VZ] + (globals.direction == 0 ? - center[VX] : center[VX]) * 1.5f; break;
	case VZ: camera[VZ] = center[VZ] * 1.1f;
	         camera[VY] = center[VY] * 1.3f;
	         camera[VX] = center[VX] + (globals.direction == 1 ? - center[VZ] : center[VZ]) * 1.5f; break;
	case VY: camera[VX] = center[VX] * 1.1f;
	         camera[VY] = center[VY] + MAX(brush->size[VX], brush->size[VZ]);
	         camera[VZ] = center[VZ] * 1.1f;
	}

	if (library.uboShader == 0)
	{
		mat4 P;
		library.uboShader = renderInitUBO();
		glBindBuffer(GL_UNIFORM_BUFFER, library.uboShader);
		matPerspective(P, globals.fieldOfVision, 1, NEAR_PLANE, 1000);
		glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof (mat4), P);
	}

	matLookAt(view, camera, center, (float[3]){0, 1, 0});
	glBindBuffer(GL_UNIFORM_BUFFER, library.uboShader);
	glBufferSubData(GL_UNIFORM_BUFFER, UBO_CAMERA_OFFSET, sizeof (vec4), camera);
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
	glViewport(0, 0, globals.width, globals.height);

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
		lib->nvgFBO = nvgluCreateFramebuffer(globals.nvgCtx, lib->thumbSz, lib->thumbSz, NVG_IMAGE_DEPTH);
		brush->cx = brush->cy = brush->cz = 0;

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

/* SITE_OnResize on copyWnd */
static int libraryGetOffset(SIT_Widget w, APTR cd, APTR ud)
{
	float x = 0;
	SIT_GetValues(w, SIT_X, &x, NULL);
	renderSetCompassOffset(x);
	return 1;
}

/* user just hit Ctrl+C in world editor */
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

void libraryFreeBrush(LibBrush lib)
{
	if (lib->data) selectionFreeBrush(lib->data);
	if (lib->nvgFBO) nvgluDeleteFramebuffer(lib->nvgFBO);
	if (lib->nbt.mem) free(lib->nbt.mem);
	if (lib->staticStruct == 0) free(lib);
}

static void libraryGenMesh(LibBrush lib)
{
	Chunk chunk;
	Map   brush = lib->data;
	int   chunkZ = (brush->size[VZ] + 15) >> 4;
	int   chunkX = (brush->size[VX] + 15) >> 4;
	int   x, y, z;

	for (z = 0, chunk = brush->chunks; z < chunkZ; z ++)
	{
		for (x = 0; x < chunkX; x ++, chunk ++)
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

static Bool GetTilePosition(int * XYZ, DATA8 tile)
{
	NBTFile_t nbt = {.mem = tile};
	NBTIter_t iter;
	uint8_t   flags = 0;
	int       i;
	NBT_IterCompound(&iter, tile);
	while ((i = NBT_Iter(&iter)) >= 0 && flags != 7)
	{
		int n = FindInList("X,Y,Z", iter.name, 0);
		if (n >= 0) XYZ[n] = NBT_GetInt(&nbt, i, 0), flags |= 1 << n;
	}
	return flags == 7;
}

/* parse an MCEdit v1 schematics: it is a simple dump of BlockIds and Data table */
static Bool libraryParseSchematics(LibBrush lib, DATA16 size)
{
	int x, y, z;
	DATA8 block = NBT_ArrayStart(&lib->nbt, NBT_FindNode(&lib->nbt, 0, "Blocks"), &z);
	DATA8 data  = NBT_ArrayStart(&lib->nbt, NBT_FindNode(&lib->nbt, 0, "Data"),   &y);

	if (! block || ! data || y < z || z < size[VX] * size[VY] * size[VZ]) return False;
	Map brush = selectionAllocBrush((uint16_t[3]){size[VX]+2, size[VY]+2, size[VZ]+2});
	if (! brush) return False;

	/* pretty straitforward forward */
	struct BlockIter_t iter;
	mapInitIterOffset(&iter, brush->firstVisible, 256+16+1);
	iter.nbor = brush->chunkOffsets;
	for (y = size[VY]; y > 0; y --, mapIter(&iter, 0, 1, -size[VZ]))
	{
		for (z = size[VZ]; z > 0; z --, mapIter(&iter, -size[VX], 0, 1))
		{
			for (x = size[VX]; x > 0; x --, block ++, data ++, mapIter(&iter, 1, 0, 0))
			{
				uint8_t state = data[0];
				iter.blockIds[iter.offset] = block[0];
				iter.blockIds[DATA_OFFSET + (iter.offset>>1)] |= iter.offset & 1 ? state << 4 : state;
			}
		}
	}

	x = NBT_FindNode(&lib->nbt, 0, "TileEntities");
	if (x > 0)
	{
		NBTIter_t list;
		NBT_InitIter(&lib->nbt, x, &list);
		while ((x = NBT_Iter(&list)) > 0)
		{
			int XYZ[3];
			GetTilePosition(XYZ, lib->nbt.mem + x);
			mapInitIterOffset(&iter, brush->firstVisible, 256+16+1);
			if (0 <= XYZ[VX] && XYZ[VX] < size[VX] && 0 <= XYZ[VZ] && XYZ[VZ] <= size[VZ])
			{
				mapIter(&iter, XYZ[0], XYZ[1], XYZ[2]);
				if (iter.cd) /* Y check (XYZ[VY]) */
					chunkAddTileEntity(iter.ref, (int[3]){iter.x-1, iter.yabs-1, iter.z-1}, NBT_Copy(lib->nbt.mem + x));
			}
		}
	}

	lib->data = brush;
	return True;
}

/* save brush as a MCEdit v1 schematic file */
static Bool librarySaveSchematics(Map brush, STRPTR path)
{
	NBTFile_t nbt = {.page = 511};
	int size[] = {brush->size[VX] - 2,
	              brush->size[VY] - 2,
	              brush->size[VZ] - 2};
	int bytes = size[VX] * size[VY] * size[VZ];
	int tileNb = 0;
	NBT_Add(&nbt,
		/* MCEdit v1 saved biome information too, here it is omitted */
		TAG_Compound, "Schematic",
			TAG_Short, "Width", size[VX],
			TAG_Short, "Length", size[VZ],
			TAG_Short, "Height", size[VY],
			TAG_String, "Materials", "Alpha",
			TAG_Byte_Array, "Blocks", bytes, 0,
			TAG_Byte_Array, "Data", bytes, 0,
			TAG_List_Compound, "TileEntities", 0, /* count will be filled later */
			TAG_End
	);
	int TE = NBT_FindNode(&nbt, 0, "TileEntities");

	/* Blocks and Data table are empty: need to copy them from brush to NBT */
	struct BlockIter_t iter;
	int x, y, z, blocks, data;
	mapInitIterOffset(&iter, brush->firstVisible, 256+16+1);
	blocks = (DATA8) NBT_Payload(&nbt, NBT_FindNode(&nbt, 0, "Blocks")) - nbt.mem;
	data   = (DATA8) NBT_Payload(&nbt, NBT_FindNode(&nbt, 0, "Data")) - nbt.mem;

	/* stored XZY, like chunks */
	for (y = size[VY]; y > 0; y --, mapIter(&iter, 0, 1, -size[VZ]))
	{
		for (z = size[VZ]; z > 0; z --, mapIter(&iter, -size[VX], 0, 1))
		{
			for (x = size[VX]; x > 0; x --, mapIter(&iter, 1, 0, 0), blocks ++, data ++)
			{
				uint8_t state = iter.blockIds[DATA_OFFSET + (iter.offset >> 1)];
				nbt.mem[blocks] = iter.blockIds[iter.offset];
				nbt.mem[data]   = iter.offset & 1 ? state >> 4 : state & 15;
				DATA8 tile = chunkGetTileEntity(iter.ref, (int[3]){iter.x, iter.yabs, iter.z});
				if (tile)
				{
					NBTIter_t iterTE;
					NBT_IterCompound(&iterTE, tile);
					for (tileNb ++; (bytes = NBT_Iter(&iterTE)) >= 0; )
						NBT_Add(&nbt, TAG_Raw_Data, NBT_HdrSize(tile+bytes), tile+bytes, TAG_End);
					NBT_Add(&nbt, TAG_Compound_End);
				}
			}
		}
	}
	/* end of TileEntities list */
	NBT_Hdr(&nbt, TE)->count = tileNb;
	NBT_Add(&nbt, TAG_List_Compound, "Entities", 0, TAG_Compound_End);

	//NBT_Dump(&nbt, 0, 0, 0);

	bytes = NBT_Save(&nbt, path, NULL, NULL);
	NBT_Free(&nbt);

	return bytes > 0;
}

/*
 * user's library interface
 */


static Bool libraryExtractThumb(LibBrush lib, STRPTR path, DATA16 size)
{
	if (NBT_Parse(&lib->nbt, path))
	{
		/* seems to be a valid NBT, check if it is a schematics */
		size[VY] = NBT_GetInt(&lib->nbt, NBT_FindNode(&lib->nbt, 0, "Height"), 0);
		size[VZ] = NBT_GetInt(&lib->nbt, NBT_FindNode(&lib->nbt, 0, "Length"), 0);
		size[VX] = NBT_GetInt(&lib->nbt, NBT_FindNode(&lib->nbt, 0, "Width"), 0);
		if (size[VY] > 0 && size[VZ] > 0 && size[VX] > 0)
		{
			lib->nvgFBO = nvgluCreateFramebuffer(globals.nvgCtx, lib->thumbSz, lib->thumbSz, NVG_IMAGE_DEPTH);

			if (libraryParseSchematics(lib, size))
			{
				libraryGenMesh(lib);
				libraryGenThumb(lib);
			}
		}
		/* not needed anymore */
		NBT_Free(&lib->nbt);
		memset(&lib->nbt, 0, sizeof lib->nbt);
	}
	return lib->nvgFBO > 0;
}

#include "extra.h"
#include "FSView.c"

/* generate a preview from schematic */
static int libraryGenPreview(SIT_Widget w, APTR cd, APTR ud)
{
	FSItem item = ud;
	if (item->haspreview == 0)
	{
		if (FrameGetTime() - globals.curTimeUI > 100)
		{
			SIT_ForceRefresh();
			return 0;
		}

		LibBrush lib;
		STRPTR   path;
		uint16_t size[3];
		int      thumbSz;
		TEXT     bg[10];

		item->haspreview = 1;
		SIT_ForceRefresh();

		path = strrchr(item->name, '.');
		SIT_GetValues(w, SIT_LabelSize, &thumbSz, SIT_UserData, &lib, NULL);

		/* this format is too retarded to generate a preview from it */
		if (path && strcasecmp(path, ".nbt") == 0)
			goto unsupported;

		path = (STRPTR) lib->node.ln_Prev;
		path = strcpy(alloca(strlen(path) + strlen(item->name) + 2), path);
		AddPart(path, item->name, 1e6);
		lib->thumbSz = thumbSz & 0xfff;
		if (lib->thumbSz == 0) return 0;
		memset(size, 0, sizeof size);
		if (libraryExtractThumb(lib, path, size))
		{
			sprintf(bg, "id(%d)", lib->nvgFBO->image);
			SIT_SetValues(w, SIT_ImagePath, bg, NULL);
			STRPTR title;
			TEXT   fullsz[64];
			w = (APTR) lib->node.ln_Next;
			SIT_GetValues(w, SIT_Title, &title, NULL);
			sprintf(fullsz, "%s - %dW x %dL x %dH", title, size[VX], size[VZ], size[VY]);
			SIT_SetValues(w, SIT_Title, fullsz, NULL);
		}
		else /* set the icon to show it is unsupported */
		{
			TEXT styles[128];
			int  szTex[2], tex;
			unsupported:
			tex = renderGetTerrain(szTex, NULL);
			thumbSz &= 0xfff;
			/* use unknown entity texture */
			sprintf(styles, "background: transparent id(%d) %dpx %dpx; background-size: %dpx %dpx",
				tex, -496 * thumbSz >> 4, -208 * thumbSz >> 4, szTex[0] * thumbSz >> 4, szTex[1] * thumbSz >> 4);
			SIT_SetValues(w, SIT_Style, styles, NULL);
			return 0;
		}
	}
	return 0;
}

static int libraryFreePreview(SIT_Widget w, APTR cd, APTR ud)
{
	LibBrush lib;
	SIT_GetValues(w, SIT_UserData, &lib, NULL);
	lib->staticStruct = 1;
	libraryFreeBrush(lib);
	return 1;
}

/* callback for FSView to create an item in the list view */
static int libraryCreateItem(SIT_Widget td, APTR curDir, APTR ud)
{
	TEXT display[64];
	TEXT size[20];
	FSItem item = ud;

	if (item->type == 0)
		FormatNumber(size, sizeof size, "%d Kb", (item->size + 1023) >> 10);
	else
		strcpy(size, "(Directory)");

	int len = strlen(item->name);
	int max = len >= sizeof display-1 ? sizeof display-1 : len;
	int thumbSz = SIT_EmToReal(td, SITV_Em(4));
	memcpy(display, item->name, max);
	display[max] = 0;
	/* this will cut long file name */
	if (max < len) memcpy(display + max - 3, "...", 3);
	if (item->type == 0)
	{
		SIT_CreateWidgets(td,
			"<label name=icon extra=", sizeof (struct LibBrush_t), "labelSize=", SITV_LabelSize(thumbSz, thumbSz), ">"
			"<label name=wname title=", display, "left=WIDGET,icon,0.5em top=FORM,,1em>"
			"<label name=size#dim title=", size, "left=OPPOSITE,wname top=WIDGET,wname,0.1em>"
		);
		LibBrush lib;
		SIT_Widget label = SIT_GetById(td, "icon");
		SIT_GetValues(label, SIT_UserData, &lib, NULL);
		/* those fields not used */
		lib->node.ln_Next = (APTR) SIT_GetById(td, "size");
		lib->node.ln_Prev = (APTR) curDir;
		item->preview = label;
		SIT_AddCallback(label, SITE_OnPaint,    libraryGenPreview,  item);
		SIT_AddCallback(label, SITE_OnFinalize, libraryFreePreview, item);
	}
	else
	{
		SIT_CreateWidgets(td,
			"<label name=dicon imagePath=folder.png width=", thumbSz, "height=", thumbSz, ">"
			"<label name=wname title=", display, "left=WIDGET,dicon top=FORM,,1em>"
			"<label name=size#dim title=", size, "left=OPPOSITE,wname top=WIDGET,wname>"
		);
	}
	return 1;
}

/* user confirmed its selection for FSView */
static int librarySelectName(SIT_Widget w, APTR cd, APTR ud)
{
	LibBrush lib;
	FSView   view = cd;
	int      sel;
	if (library.saveBrush)
	{
		Map brush;
		if (library.saveFromLib)
		{
			/* use "Save" on brush library */
			SIT_GetValues(library.copyList, SIT_SelectedIndex, &sel, NULL);
			SIT_GetValues(library.copyList, SIT_RowTag(sel), &lib, NULL);
			brush = lib->data;
		}
		else brush = selectionCopyShallow(); /* from toolbar */
		/* any warning about overwriting files should have been displayed by now */
		if (brush && ! librarySaveSchematics(brush, ud))
		{
			TEXT error[256];
			snprintf(error, sizeof error, "Failed to save '%s': %s\n", (STRPTR) ud, GetError());
			FSYesNo(view, error, NULL, False);
		}
		else SIT_Exit(1);
		/* no mesh allocated: only a temporary brush */
		if (brush->GPUMaxChunk == 0)
			selectionFreeBrush(brush);
	}
	else /* use brush */
	{
		FSItem item;
		SIT_GetValues(view->list, SIT_SelectedIndex, &sel, NULL);
		SIT_GetValues(view->list, SIT_RowTag(sel), &item, NULL);
		if (item && item->preview)
		{
			SIT_GetValues(item->preview, SIT_UserData, &lib, NULL);
			if (lib->data)
			{
				selectionUseBrush(lib->data, False);
				renderSaveRestoreState(True);
				lib->data = NULL;
				SIT_Exit(1);
			}
		}
		/* else not yet generated */
	}
	//fprintf(stderr, "name = %s\n", (STRPTR) ud);
	return 1;
}

static int libraryExitWnd(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_CloseDialog(w);
	SIT_Exit(1);
	return 1;
}

/*
 * interface for library schematics
 */
void libraryShow(int type)
{
	TEXT defPath[256];
	GetDefaultPath(FOLDER_MYDOCUMENTS, defPath, sizeof defPath);
	AddPart(defPath, "MCEdit/Schematics", sizeof defPath);
	library.saveBrush = type == MCUI_OVERLAY_SAVESEL;
	uint8_t flags = FSVIEW_HASDELETE | FSVIEW_HASMAKEDIR | FSVIEW_HASRENAME;
	if (library.saveBrush)
	{
		flags |= FSVIEW_SAVE;
		StrCat(defPath, sizeof defPath, 0, "\tschematic");
	}
	/* the interface is actually handled by a FSView widget */
	SIT_Widget diag = FSInit(globals.app, defPath, flags, libraryCreateItem, librarySelectName);

	/* need a special exit code */
	SIT_AddCallback(SIT_GetById(diag, "exit"), SITE_OnActivate, libraryExitWnd, NULL);
}
