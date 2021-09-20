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
	selectionUseBrush(brush->data);
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
	case VX: camera[VX] = center[VX] * 1.1;
	         camera[VY] = center[VY] * 1.3;
	         camera[VZ] = center[VZ] + (globals.direction == 0 ? - center[VX] : center[VX]) * 1.5; break;
	case VZ: camera[VZ] = center[VZ] * 1.1;
	         camera[VY] = center[VY] * 1.3;
	         camera[VX] = center[VX] + (globals.direction == 1 ? - center[VZ] : center[VZ]) * 1.5; break;
	case VY: camera[VX] = center[VX] * 1.1;
	         camera[VY] = center[VY] + MAX(brush->size[VX], brush->size[VZ]);
	         camera[VZ] = center[VZ] * 1.1;
	}

	if (library.uboShader == 0)
	{
		mat4 P;
		library.uboShader = renderInitUBO();
		glBindBuffer(GL_UNIFORM_BUFFER, library.uboShader);
		matPerspective(P, DEF_FOV, 1, NEAR_PLANE, 1000);
		glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof (mat4), P);
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
	lib->data = brush;
	return True;
}

static inline int topHalf(NBTFile nbt, int offset)
{
	STRPTR prop = NBT_Payload(nbt, NBT_FindNode(nbt, offset, "half"));
	return prop ? strcmp(prop, "top") == 0 : 0;
}

/* parse a structure file: this format is used by Minecraft to store objects to paste in worldgen */
static Bool libraryParseStructure(LibBrush lib, DATA16 size)
{
	/*
	 * what a retarded format that is: 52 bytes per voxel: that's 50 bytes of pure noise, or 2600%
	 * more data than it is necessary. Hopefully, those files usually store small objects.
	 */
	int palette = NBT_FindNode(&lib->nbt, 0, "palette");
	int blocks  = NBT_FindNode(&lib->nbt, 0, "blocks");

	if (palette >= 0 && blocks >= 0)
	{
		struct BlockIter_t brushIter;
		NBTIter_t iter;
		DATA8 pal;
		int max, i;

		/* need to parse palette first */
		NBT_InitIter(&lib->nbt, palette, &iter);
		if (iter.state <= 0) return False;
		max = iter.state;
		pal = alloca(max * 2);
		i = 0;
		while ((palette = NBT_Iter(&iter)) > 0)
		{
			static char variantSlabs[] = "stone,sandstone,wooden,cobblestone,brick,stone brick,nether brick,quartz";
			static char variantLogs[]  = "oak,spruce,birch,jungle";
			static char facingStairs[] = "east,west,south,north";
			DATA8 name = NBT_Payload(&lib->nbt, NBT_FindNode(&lib->nbt, palette, "Name"));
			if (name)
			{
				int block = strcmp(name, "stone_stairs") == 0 ? 67 /* legacy crap, obviously */ : itemGetByName(name, False);
				uint8_t data = 0;
				/* not complete by far */
				switch (blockIds[block].orientHint) {
				case ORIENT_STAIRS:
					if (topHalf(&lib->nbt, palette)) data = 8;
					data |= FindInList(facingStairs, NBT_Payload(&lib->nbt, NBT_FindNode(&lib->nbt, palette, "facing")), 0);
					break;
				case ORIENT_LOG:
					data = FindInList("y,x,z", NBT_Payload(&lib->nbt, NBT_FindNode(&lib->nbt, palette, "axis")), 0) * 4;
					data |= FindInList(variantLogs, NBT_Payload(&lib->nbt, NBT_FindNode(&lib->nbt, palette, "variant")), 0);
					break;
				case ORIENT_SLAB:
					if (topHalf(&lib->nbt, palette)) data = 8;
					data |= FindInList(variantSlabs, NBT_Payload(&lib->nbt, NBT_FindNode(&lib->nbt, palette, "variant")), 0);
				}
				pal[i] = block;
				pal[i+max] = data;
			}
			i ++;
		}

		/* now, we can fill the brush */
		Map brush = selectionAllocBrush((uint16_t[3]){size[VX]+2, size[VY]+2, size[VZ]+2});
		if (! brush) return False;
		mapInitIterOffset(&brushIter, brush->firstVisible, 256+16+1);
		brushIter.nbor = brush->chunkOffsets;

		NBT_InitIter(&lib->nbt, blocks, &iter);
		while ((blocks = NBT_Iter(&iter)) > 0)
		{
			struct BlockIter_t fill = brushIter;
			float pos[3] = {0, 0, 0};
			int index = NBT_ToInt(&lib->nbt, NBT_FindNode(&lib->nbt, blocks, "state"), -1);
			if (index < 0 || index >= max) continue;
			NBT_ToFloat(&lib->nbt, NBT_FindNode(&lib->nbt, blocks, "pos"), pos, 3);
			mapIter(&fill, pos[VX], pos[VY], pos[VZ]);
			fill.blockIds[fill.offset] = pal[index];
			uint8_t data = pal[index+max];
			fill.blockIds[DATA_OFFSET + (fill.offset>>1)] |= (fill.offset & 1 ? data << 4 : data);
		}
	}

	return False;
}

/* save brush as a MCEdit v1 schematic file */
static Bool librarySaveSchematics(Map brush, STRPTR path)
{
	NBTFile_t nbt = {.page = 512};
	int size[] = {brush->size[VX] - 2,
	              brush->size[VY] - 2,
	              brush->size[VZ] - 2};
	int bytes = size[VX] * size[VY] * size[VZ];
	NBT_Add(&nbt,
		/* MCEdit v1 saved biome information too, here it is omitted */
		TAG_Compound, "Schematic",
			TAG_Short, "Width", size[VX],
			TAG_Short, "Length", size[VZ],
			TAG_Short, "Height", size[VY],
			TAG_String, "Materials", "Alpha",
			TAG_Byte_Array, "Blocks", bytes, 0,
			TAG_Byte_Array, "Data", bytes, 0,
			TAG_List_Compound, "Entities", 0,
			TAG_List_Compound, "TileEntities", 0,
		TAG_Compound_End
	);

	/* Blocks and Data table are empty: need to copy them from brush to NBT */
	struct BlockIter_t iter;
	int x, y, z;
	DATA8 blocks, data;
	mapInitIterOffset(&iter, brush->firstVisible, 256+16+1);
	blocks = NBT_Payload(&nbt, NBT_FindNode(&nbt, 0, "Blocks"));
	data   = NBT_Payload(&nbt, NBT_FindNode(&nbt, 0, "Data"));

	/* stored XZY, like chunks */
	for (y = size[VY]; y > 0; y --, mapIter(&iter, 0, 1, -size[VZ]))
	{
		for (z = size[VZ]; z > 0; z --, mapIter(&iter, -size[VX], 0, 1))
		{
			for (x = size[VX]; x > 0; x --, mapIter(&iter, 1, 0, 0), blocks ++, data ++)
			{
				uint8_t state = iter.blockIds[DATA_OFFSET + (iter.offset >> 1)];
				blocks[0] = iter.blockIds[iter.offset];
				data[0] = iter.offset & 1 ? state >> 4 : state & 15;
			}
		}
	}

	bytes = NBT_Save(&nbt, path, NULL, NULL);
	NBT_Free(&nbt);

	return bytes > 0;
}

/*
 * user's library interface
 */


static void libraryExtractThumb(LibBrush lib, STRPTR path, DATA16 size)
{
	if (NBT_Parse(&lib->nbt, path))
	{
		int offset;
		/* seems to be a valid NBT, check if it is a schematics */
		size[VY] = NBT_ToInt(&lib->nbt, NBT_FindNode(&lib->nbt, 0, "Height"), 0);
		size[VZ] = NBT_ToInt(&lib->nbt, NBT_FindNode(&lib->nbt, 0, "Length"), 0);
		size[VX] = NBT_ToInt(&lib->nbt, NBT_FindNode(&lib->nbt, 0, "Width"), 0);
		if (size[VY] > 0 && size[VZ] > 0 && size[VX] > 0)
		{
			//fprintf(stderr, "parsing %s\n", path);
			lib->nvgFBO = nvgluCreateFramebuffer(globals.nvgCtx, lib->thumbSz, lib->thumbSz, NVG_IMAGE_DEPTH);

			if (libraryParseSchematics(lib, size))
			{
				libraryGenMesh(lib);
				libraryGenThumb(lib);
			}
		}
		else if ((offset = NBT_FindNode(&lib->nbt, 0, "size")) >= 0)
		{
			float array[] = {0, 0, 0};
			NBT_ToFloat(&lib->nbt, offset, array, DIM(array));
			size[VX] = array[VX];
			size[VY] = array[VY];
			size[VZ] = array[VZ];
			if (size[VX] >= 1 && size[VY] >= 1 && size[VZ] >= 1)
			{
			    if (libraryParseStructure(lib, size))
					libraryGenThumb(lib);
			}
		}
		/* not needed anymore */
		NBT_Free(&lib->nbt);
		memset(&lib->nbt, 0, sizeof lib->nbt);
	}
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

		/* this format is too retarded to generate a preview from it */
		if (path && strcasecmp(path, ".nbt") == 0)
			return 0;

		SIT_GetValues(w, SIT_LabelSize, &thumbSz, SIT_UserData, &lib, NULL);
		path = (STRPTR) lib->node.ln_Prev;
		path = strcpy(alloca(strlen(path) + strlen(item->name) + 2), path);
		AddPart(path, item->name, 1e6);
		lib->thumbSz = thumbSz & 0xfff;
		lib->staticStruct = 1;
		if (lib->thumbSz == 0) return 0;
		libraryExtractThumb(lib, path, size);
		if (lib->nvgFBO)
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
	}
	return 0;
}

static int libraryFreePreview(SIT_Widget w, APTR cd, APTR ud)
{
	LibBrush lib;
	SIT_GetValues(w, SIT_UserData, &lib, NULL);
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
	{
		FormatNumber((item->size + 1023) >> 10, size, sizeof size);
		strcat(size, " Kb");
	}
	else strcpy(size, "(Directory)");

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
		SIT_GetValues(library.copyList, SIT_SelectedIndex, &sel, NULL);
		SIT_GetValues(library.copyList, SIT_RowTag(sel), &lib, NULL);
		/* any warning about overwriting files should have been displayed by now */
		if (! librarySaveSchematics(lib->data, ud))
		{
			TEXT error[256];
			snprintf(error, sizeof error, "Failed to save '%s': %s\n", (STRPTR) ud, GetError());
			FSYesNo(view, error, NULL, False);
		}
		else SIT_Exit(1);
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
				selectionUseBrush(lib->data);
				renderSaveRestoreState(True);
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
	SIT_Widget diag = FSInit(globals.app, defPath, flags, libraryCreateItem, librarySelectName);

	/* need a special exit code */
	SIT_AddCallback(SIT_GetById(diag, "exit"), SITE_OnActivate, libraryExitWnd, NULL);
}
