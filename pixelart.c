/*
 * pixelart.c : interface to generate pixel art from blocks or maps
 *
 * Written by T.Pierron, oct 2021.
 */

#define MCUI_IMPL
#include <glad.h>
#include <malloc.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "nanovg.h"
#include "nanovg_gl_utils.h"
#include "SIT.h"
#include "entities.h"
#include "interface.h"
#include "selection.h"
#include "cartograph.h"
#include "globals.h"

struct
{
	SIT_Widget image;
	SIT_Widget palette;
	SIT_Widget icon;
	SIT_Widget selinfo;
	Item       allItems;
	uint8_t    axis1, axis2, axisMin;
	uint8_t    side;
	int        rasterizeWith;
	int        dither, stretch;
	int        sizeX, sizeY;
	int        itemsNb;

} pixArt = {
	/* default values */
	.dither = 1
};

/* from interface.c */
void mcuiResetScrollbar(MCInventory);
void mcuiReplaceFillItems(SIT_Widget, MCInventory);
void mcuiInitInventory(SIT_Widget, MCInventory);
int  mcuiExitWnd(SIT_Widget, APTR cd, APTR ud);

/* from cartograph.c */
extern uint8_t mapRGB[];
extern uint8_t mapShading[];

#define MAP_SIZEPX  128

#define UPTO(x)     (x|0x8000)
static uint16_t palettes[] = {
	100, /* full blocks */
	16, UPTO(22), 32, 48, UPTO(50), 64, 80, UPTO(85), 112, 192, 193, 208, 224, 240, 256, 272,
	UPTO(275), 284, UPTO(287), 304, 305, 336, 352, 371, 384, UPTO(386), 400, 465, 529, 560, UPTO(575), 656,
	672, 696, 697, 720, 736, 752, 768, 784, 896, 912, 928, 960, 979, 1168, 1264, 1280,
	1296, 1312, 1344, 1376, 1380, 1392, 1408, 1424, 1456, 1568, UPTO(1571), 1598, 1614, 1648, 1760, 1792,
	1936, 1968, 2064, 2128, 2432, 2448, 2480, UPTO(2482), 2529, 2544, UPTO(2559), 2592, 2593, 2604, 2605, 2688,
	UPTO(2690), 2704, 2720, 2752, 2768, 2784, 2864, UPTO(2866), 2904, 3216, 3232, 3264, 3296, 3328, 3408, 3424,
	3440, 3456, 4016, UPTO(4047),

	2, /* wool */
	560, UPTO(575),

	2, /* terracotta/hardened clay */
	2544, UPTO(2559),

	2, /* concrete */
	4017, UPTO(4047),

	9, /* flowers */
	96, UPTO(101), 498, 512, 592, 608, UPTO(616), 624, 640,

	2, /* black&white */
	560, 575,
};

static STRPTR palNames[] = {
	"Full blocks",
	"Wool",
	"Terracotta",
	"Concrete",
	"Flowers",
	"Black&white"
};

static STRPTR palNamesMap[] = {
	"1.12",
	"1.13+",
	"Black&white"
};

static uint16_t palettesMap[] = {
	2,
	0, UPTO(51),
	2,
	0, UPTO(61),
	2,
	29, 8,
};

static int pixArtSelInfo(SIT_Widget w, APTR cd, APTR ud)
{
	MCInventory inv = ud;
	TEXT buffer[64];
	STRPTR plane;
	if (pixArt.rasterizeWith == 0)
	{
		/* blocks: visible from 2 sides */
		switch (pixArt.axisMin) {
		case VX: plane = "east-west"; break;
		case VZ: plane = "north-south"; break;
		default: plane = "floor";
		}
		sprintf(buffer, "%d x %d blocks, %s plane", pixArt.sizeX, pixArt.sizeY, plane);
	}
	else /* maps: will be only visible from one side */
	{
		static STRPTR sides[] = {"south", "east", "north", "west", "top", "bottom"};
		sprintf(buffer, "%d x %dpx, %s face", pixArt.sizeX * MAP_SIZEPX, pixArt.sizeY * MAP_SIZEPX, sides[pixArt.side]);
	}
	SIT_SetValues(pixArt.selinfo, SIT_Title, buffer, NULL);

	/* change combobox content */
	DATA16  palette;
	uint8_t i;
	SIT_SetValues(pixArt.palette, SIT_InitialValues, NULL, NULL);
	if (pixArt.rasterizeWith == 0)
	{
		for (i = 0, palette = palettes; i < DIM(palNames); i ++, palette += palette[0] + 1)
			SIT_ComboInsertItem(pixArt.palette, -1, palNames[i], palette + 1);
		/* and items in inventory */
		inv->items = pixArt.allItems;
		inv->itemsNb = pixArt.itemsNb;
	}
	else /* use map art color entries */
	{
		for (i = 0, palette = palettesMap; i < DIM(palNamesMap); i ++, palette += palette[0] + 1)
			SIT_ComboInsertItem(pixArt.palette, -1, palNamesMap[i], palette + 1);
		inv->items = pixArt.allItems + pixArt.itemsNb;
		inv->itemsNb = 62;
	}
	mcuiResetScrollbar(inv);
	SIT_SetValues(pixArt.palette, SIT_SelectedIndex, 0, NULL);

	return 1;
}

/* click on "save" link */
static int pixArtSavePal(SIT_Widget w, APTR cd, APTR ud)
{
	MCInventory inv = ud;
	Item        item;
	int         nb, old, run, total;

	if (w == cd) return 0; /* click on label instead of embedded <a> */
	for (item = inv->items, total = run = old = 0, nb = inv->itemsNb; nb > 0; nb --, item ++)
	{
		if (! item->added) continue; total ++;
		if (old == item->id - run - 1) { run ++; continue; }
		if (run > 0)
		{
			if (run > 1) fprintf(stderr, "UPTO(%d), ", old+run);
			else         fprintf(stderr, "%d, ", old+run);
		}
		fprintf(stderr, "%d, ", item->id);
		old = item->id;
		run = 0;
	}
	if (run > 0)
	{
		if (run > 1) fprintf(stderr, "UPTO(%d), ", old+run);
		else         fprintf(stderr, "%d, ", old+run);
	}
	fputc('\n', stderr);
	return 1;
}

/* SITE_OnChange on palette combobox */
static int pixArtChangePalette(SIT_Widget w, APTR cd, APTR ud)
{
	MCInventory inv = ud;
	DATA16 palette = SIT_ComboGetRowTag(w, (int) cd, NULL);
	Item   item, eof;
	int    itemId, i, nb;

	for (item = inv->items, eof = item + inv->itemsNb; item < eof; item->added = 0, item ++);
	for (i = 0, nb = palette[-1], itemId = palette[0], item = inv->items; ; )
	{
		if (pixArt.rasterizeWith == 0)
		{
			while (item < eof && item->id != itemId) item ++;
			if (item < eof) item->added = 1; else break;
		}
		else item[itemId].added = 1;
		if ((palette[0] & 0x8000) == 0 || itemId >= (palette[0] & 0x7fff))
		{
			palette ++; i ++;
			if (i >= nb) break;
			if (palette[0] & 0x8000)
				itemId ++;
			else
				itemId = palette[0];
		}
		else itemId ++;
	}
	return 1;
}

/* map art: items will be colormap of maps */
static int pixArtDrawMapColor(SIT_Widget w, APTR cd, APTR ud)
{
	NVGCTX vg = globals.nvgCtx;
	int *  rect = cd;
	Item   item = ud;

	nvgBeginPath(vg);
	int sz = rect[2] * 3 / 4;
	int off = (rect[2] - sz) >> 1;
	nvgStrokeWidth(vg, 2);
	nvgFillColorRGBA8(vg, item->extra);
	nvgRect(vg, rect[0]+off, rect[1]+off, sz, sz);
	nvgFill(vg);
	int x = rect[0]+off;
	int y = rect[1]+off;
	nvgBeginPath(vg);
	nvgStrokeColorRGBA8(vg, "\xff\xff\xff\x7f");
	nvgMoveTo(vg, x, y + sz);
	nvgLineTo(vg, x, y);
	nvgLineTo(vg, x + sz, y);
	nvgStroke(vg);
	nvgBeginPath(vg);

	nvgStrokeColorRGBA8(vg, "\0\0\0\x7f");
	nvgMoveTo(vg, x, y + sz);
	nvgLineTo(vg, x + sz, y + sz);
	nvgLineTo(vg, x + sz, y);
	nvgStroke(vg);

	return 1;
}

static int pixArtLoadImg(SIT_Widget w, APTR cd, APTR ud)
{
	static SIT_Widget file;

	if (file == NULL)
	{
		file = SIT_CreateWidget("fileselect", SIT_FILESELECT, w,
			SIT_Filters,   "Any\t*",
			SIT_SelFilter, 0,
			SIT_DlgFlags,  SITV_FileMustExist,
			NULL
		);
	}

	if (SIT_ManageWidget(file))
	{
		STRPTR path;
		TEXT   styles[128];
		SIT_GetValues(file, SIT_SelPath, &path, NULL);
		snprintf(styles, sizeof styles, "background: #8b8b8b url(%s) 50%% 50%% no-repeat; background-size: %s",
			path, pixArt.stretch ? "100% 100%" : "contain");
		SIT_SetValues(pixArt.icon, SIT_Style, styles, NULL);
	}
	return 1;
}

#define SPP      4 /* samples per pixel */

/* convert a RGBA image into a palette 8bpp image */
static void pixArtToPalette(DATA8 pixels, int width, int height)
{
	/*
	 * use a floyd-steinberg error diffusion matrix (3x2):
	 *      X  7
	 *   3  5  1
	 */
	uint8_t cmapRGB[256*4];
	DATA8   s, d, data, cmapEOF;
	int     stride = width * SPP;
	int     i, j;
	Item    item;

	/* mapRGB[] only contains 64 entries: we need 256 */
	for (j = 0, d = cmapRGB; j < 4; j ++)
	{
		uint8_t shade = mapShading[j];
		for (s = mapRGB, i = 0, item = pixArt.allItems + pixArt.itemsNb; i < 64; i ++, s += 4)
		{
			/* alpha == 0 => invisible: don't care */
			if (item->added == 0 || s[3] == 0x00) continue;
			d[0] = s[0] * shade / 255;
			d[1] = s[1] * shade / 255;
			d[2] = s[2] * shade / 255;
			d[3] = (i << 2) | j; d += 4;
		}
	}
	cmapEOF = d;

	/* perform dithering with a fixed colormap */
	for (j = height, d = data = pixels; j > 0; j --, data += stride)
	{
		for (i = width, s = data; i > 0; i --, s += SPP, d ++)
		{
			DATA8 cmap, best;
			short r, g, b;
			int   minDist;
			r = s[0];
			g = s[1];
			b = s[2];
			/* find nearest color from colormap that matches RGBA component pointed by <s> */
			for (cmap = cmapRGB, best = NULL; cmap < cmapEOF; cmap += 4)
			{
				/* find shortest euclidean distance */
				int dist = (cmap[0] - r) * (cmap[0] - r) +
				           (cmap[1] - g) * (cmap[1] - g) +
				           (cmap[2] - b) * (cmap[2] - b);
				if (best == NULL || minDist > dist)
					best = cmap, minDist = dist;
			}

			/* diffuse error to nearby pixels */
			r -= best[0];
			g -= best[1];
			b -= best[2];

			//if (r > 0 || g > 0 || b > 0)
			//	puts("here");

			int16_t tmp;
			#define CLAMP(x)    (tmp = (x)) > 255 ? 255 : (tmp < 0 ? 0 : tmp)
			if (i > 1)
			{
				s[SPP+0] = CLAMP(s[SPP+0] + (7 * r >> 4));
				s[SPP+1] = CLAMP(s[SPP+1] + (7 * g >> 4));
				s[SPP+2] = CLAMP(s[SPP+2] + (7 * b >> 4));
			}
			if (j > 1)
			{
				DATA8 d2 = s + stride - SPP;
				if (s > data)
				{
					d2[0] = CLAMP(d2[0] + (3 * r >> 4));
					d2[1] = CLAMP(d2[1] + (3 * g >> 4));
					d2[2] = CLAMP(d2[2] + (3 * b >> 4));
				}
				d2 += SPP;
				d2[0] = CLAMP(d2[0] + (5 * r >> 4));
				d2[1] = CLAMP(d2[1] + (5 * g >> 4));
				d2[2] = CLAMP(d2[2] + (5 * b >> 4));

				d2 += SPP;
				if (i > 1)
				{
					d2[0] = CLAMP(d2[0] + (r >> 4));
					d2[1] = CLAMP(d2[1] + (g >> 4));
					d2[2] = CLAMP(d2[2] + (b >> 4));
				}
			}
			#undef CLAMP
			*d = best[3];
		}
	}
}

/* create map on disk from slice of picture */
static int pixArtCreateMap(DATA8 bitmap, int width, int height, int tileX, int tileY)
{
	struct NBTFile_t nbt = {.page = 1024};

	NBT_Add(&nbt,
		TAG_Compound, "data",
			TAG_Int,   "xCenter",   30000000,
			TAG_Int,   "zCenter",   30000000,
			TAG_Short, "width",     MAP_SIZEPX,
			TAG_Short, "height",    MAP_SIZEPX,
			TAG_Byte,  "dimension", 0,
			TAG_Byte,  "scale",     0,
			TAG_Byte_Array, "colors", MAP_SIZEPX*MAP_SIZEPX, 0,
		TAG_Compound_End
	);

	DATA8   cmap      = NBT_Payload(&nbt, NBT_FindNode(&nbt, 0, "colors"));
	uint8_t dstWidth  = MAP_SIZEPX;
	uint8_t dstHeight = MAP_SIZEPX;
	uint8_t j;

	tileX *= MAP_SIZEPX;
	tileY *= MAP_SIZEPX;

	if (tileX + dstWidth > width)
		dstWidth = width - tileX;
	if (tileY + dstHeight > height)
		dstHeight = height - tileY;

	bitmap += tileX + tileY * width;
	/* texture is upside down */
	cmap += (tileY + dstHeight - 1) * width;

	for (j = 0; j < dstHeight; j ++)
	{
		memcpy(cmap, bitmap, dstWidth);
		cmap -= MAP_SIZEPX;
		bitmap += dstWidth;
	}

	tileX = cartoSaveMap(nbt.mem, nbt.usage);
	NBT_Free(&nbt);
	return tileX;
}

static void pixArtGenerateMaps(DATA8 data, int width, int height)
{
	static uint8_t minAxis[] = {0, 0, 1, 1, 0, 1};
	vec points = selectionGetPoints();
	int size[] = {
		fabsf(points[VX] - points[VX+4]) + 1,
		fabsf(points[VY] - points[VY+4]) + 1,
		fabsf(points[VZ] - points[VZ+4]) + 1
	};
	uint8_t axis1 = pixArt.axisMin;
	uint8_t axis2 = pixArt.axis2;
	uint8_t i, j;
	vec4    pos;

	pos[axis1] = minAxis[pixArt.side] ? points[axis1] : points[axis1+4];
	axis1 = pixArt.axis1;
	pos[axis2] = points[axis2];
	for (j = 0; j < size[axis2]; j ++, pos[axis2] ++)
	{
		pos[axis1] = points[axis1];
		for (i = 0; i < size[axis1]; i ++, pos[axis1] ++)
		{
			int entityId = entityCreate(globals.level, ID(389, 0), pos, pixArt.side);
			int mapId = pixArtCreateMap(data, width, height, i, j);
			entityUseItemOn(globals.level, entityId, ID(358, mapId), pos);
		}
	}
}

typedef struct NVGLUframebuffer *  NVGFBO;

static int pixArtGenerate(SIT_Widget w, APTR cd, APTR ud)
{
	/* first: resize image using OpenGL command (main image is already fully transfered to GPU anyway) */
	NVGCTX vg;
	NVGFBO fbo;
	int    dstWidth, dstHeight, image;
	int    srcWidth, srcHeight;

	if (! SIT_GetCSSValue(pixArt.icon, "background-image", &image)) /* NVG image handle */
		return 0;

	vg = globals.nvgCtx;
	nvgImageSize(vg, image, &srcWidth, &srcHeight);
	dstWidth = pixArt.sizeX;
	dstHeight = pixArt.sizeY;
	if (pixArt.rasterizeWith == 1)
		dstWidth *= MAP_SIZEPX, dstHeight *= MAP_SIZEPX;
	if (pixArt.stretch == 0)
	{
		/* keep image aspect ratio */
		if (srcWidth * dstHeight > srcHeight * dstWidth)
			dstHeight = dstWidth * srcHeight / srcWidth;
		else
			dstWidth = dstHeight * srcWidth / srcHeight;
	}

	fbo = nvgluCreateFramebuffer(vg, dstWidth, dstHeight, 0);

	if (fbo)
	{
		nvgluBindFramebuffer(fbo);
		nvgBeginFrame(vg, dstWidth, dstHeight, 1);
		glViewport(0, 0, dstWidth, dstHeight);
		nvgBeginPath(vg);
		nvgRect(vg, 0, 0, dstWidth, dstHeight);
		nvgFillPaint(vg, nvgImagePattern(vg, 0, 0, dstWidth, dstHeight, 0, image, 1));
		nvgFill(vg);
		nvgEndFrame(vg);
		nvgluBindFramebuffer(NULL);
		DATA8 data = malloc(dstWidth * dstHeight * 4);
		glBindTexture(GL_TEXTURE_2D, fbo->texture);
		glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
		nvgluDeleteFramebuffer(fbo);

		/* second: convert the texture to palette using floyd-steinberg dithering */
		if (pixArt.rasterizeWith == 1)
		{
			pixArtToPalette(data, dstWidth, dstHeight);
			pixArtGenerateMaps(data, dstWidth, dstHeight);
		}
		free(data);
	}
	SIT_Exit(1);
	return 1;
}

static void pixArtFillMapColors(MCInventory inv)
{
	Item item;
	int  i;

	for (item = inv->items + inv->itemsNb, i = 0; i < 64; i ++, item ++)
	{
		item->id = 0xffff;
		item->extra = mapRGB + i * 4;
	}
}

/* main interface for pixel art editor */
void mcuiShowPixelArt(vec4 pos)
{
	static struct MCInventory_t mcinv = {.invRow = 6, .invCol = MAXCOLINV, .movable = INV_SELECT_ONLY, .customDraw = pixArtDrawMapColor};

	SIT_Widget diag = SIT_CreateWidget("pixelart.bg", SIT_DIALOG + SIT_EXTRA((blockLast - blockStates + 64) * sizeof (ItemBuf)), globals.app,
		SIT_DialogStyles, SITV_Plain | SITV_Modal,
		SIT_Style,        "padding-top: 0.2em",
		NULL
	);

	int sz = SIT_EmToReal(diag, SITV_Em(11));

	SIT_CreateWidgets(diag,
		"<label name=dlgtitle.big title=", "Pixel art editor", "left=", SITV_AttachPosition, SITV_AttachPos(50), SITV_OffsetCenter, ">"
		"<label name=icon#table top=WIDGET,dlgtitle,0.5em labelSize=", SITV_LabelSize(sz,sz), ">"
		"<label name=msg title='Rasterize with:' left=WIDGET,icon,1em top=WIDGET,dlgtitle,0.5em>"
		"<button name=blocks curValue=", &pixArt.rasterizeWith, "title=Blocks buttonType=", SITV_RadioButton, "top=WIDGET,msg,0.5em left=WIDGET,icon,1em>"
		"<button name=maps curValue=", &pixArt.rasterizeWith, "title=Maps buttonType=", SITV_RadioButton, "top=WIDGET,blocks,0.5em left=WIDGET,icon,1em>"
		"<button name=dither title=Dither curValue=", &pixArt.dither, "buttonType=", SITV_CheckBox, "checkState=1 top=OPPOSITE,blocks left=WIDGET,blocks,2em>"
		"<button name=stretch title=Stretch curValue=", &pixArt.stretch, "buttonType=", SITV_CheckBox, "top=WIDGET,dither,0.5em left=WIDGET,blocks,2em>"
		"<label name=msg2 title='Palette:' left=WIDGET,icon,1em top=WIDGET,maps,1em>"
		"<combobox name=palette top=WIDGET,msg2,0.5em left=OPPOSITE,msg2>"
		"<label name=save.big title='(<a href=#>Save</a>)' bottom=OPPOSITE,msg2 right=OPPOSITE,palette>"
		"<label name=msg3.big title=Selection: top=WIDGET,icon,0.5em>"
		"<label name=selinfo top=OPPOSITE,msg3 left=WIDGET,msg3,0.3em>"
		"<canvas composited=1 name=inv.inv top=WIDGET,msg3,0.5em nextCtrl=LAST/>"
		"<button name=load title='Load image' top=WIDGET,inv,0.5em>"
		"<button name=ko title=Cancel buttonType=", SITV_CancelButton, "top=OPPOSITE,load right=FORM>"
		"<button name=ok title=Fill top=OPPOSITE,ko right=WIDGET,ko,0.5em>"
		"<scrollbar width=1.2em name=scroll.inv wheelMult=1 top=OPPOSITE,inv,0 bottom=OPPOSITE,inv,0 right=FORM>"
		"<tooltip name=info delayTime=", SITV_TooltipManualTrigger, "displayTime=10000 toolTipAnchor=", SITV_TooltipFollowMouse, ">"
	);
	SIT_SetAttributes(diag, "<inv right=WIDGET,scroll,0.2em>");

	/* show selection info */
	vec points = selectionGetPoints();
	int size[] = {
		fabsf(points[VX] - points[VX+4]) + 1,
		fabsf(points[VY] - points[VY+4]) + 1,
		fabsf(points[VZ] - points[VZ+4]) + 1
	};
	float center[] = {
		(points[VX] + points[VX+4] + 1) * 0.5f,
		(points[VY] + points[VY+4] + 1) * 0.5f,
		(points[VZ] + points[VZ+4] + 1) * 0.5f
	};
	uint8_t axis1, axis2;

	if (size[VX] == 1 && size[VY] == 1 && size[VZ] == 1)
	{
		/* 1 block selected: opposite of viewing direction */
		axis1 = globals.direction & 1 ? VX : VZ;
	}
	else
	{
		axis1 = size[VX] < size[VY] ? 0 : 1;
		if (size[axis1] < size[VZ]) axis2 = 2;
	}
	switch (pixArt.axisMin = axis1) {
	case VX: /* extend in YZ plane: visible from east and/or west */
		axis1 = VY; axis2 = VZ;
		pixArt.side =
			vecDistSquare(pos, (vec4){points[VX],   center[VY], center[VZ]}) <
			vecDistSquare(pos, (vec4){points[VX]+1, center[VY], center[VZ]}) ? SIDE_WEST : SIDE_EAST;
		break;
	case VY: /* extend in XZ plane: visible from top/bottom */
		axis1 = VX; axis2 = VZ;
		pixArt.side =
			vecDistSquare(pos, (vec4){center[VX], points[VY],   center[VZ]}) <
			vecDistSquare(pos, (vec4){center[VX], points[VY+1], center[VZ]}) ? SIDE_TOP : SIDE_BOTTOM;
		break;
	case VZ: /* extend in XY plane: visible from south and/or north */
		axis1 = VX; axis2 = VY;
		pixArt.side =
			vecDistSquare(pos, (vec4){center[VX], center[VY], points[VZ]})   <
			vecDistSquare(pos, (vec4){center[VX], center[VY], points[VZ+1]}) ? SIDE_NORTH : SIDE_SOUTH;
	}

	SIT_GetValues(diag, SIT_UserData, &pixArt.allItems, NULL);
	pixArt.palette = SIT_GetById(diag, "palette");
	pixArt.selinfo = SIT_GetById(diag, "selinfo");
	pixArt.icon    = SIT_GetById(diag, "icon");
	pixArt.axis1   = axis1;
	pixArt.axis2   = axis2;
	pixArt.sizeX   = size[axis1];
	pixArt.sizeY   = size[axis2];

	SIT_AddCallback(pixArt.palette, SITE_OnChange, pixArtChangePalette, &mcinv);

	mcuiReplaceFillItems(diag, &mcinv);
	mcuiSetItemSize(mcinv.cell, 0);
	pixArtFillMapColors(&mcinv);
	mcuiInitInventory(SIT_GetById(diag, "inv"), &mcinv);
	mcuiResetScrollbar(&mcinv);

	SIT_AddCallback(SIT_GetById(diag, "blocks"), SITE_OnActivate, pixArtSelInfo, &mcinv);
	SIT_AddCallback(SIT_GetById(diag, "maps"),   SITE_OnActivate, pixArtSelInfo, &mcinv);
	SIT_AddCallback(SIT_GetById(diag, "save"),   SITE_OnActivate, pixArtSavePal, &mcinv);
	SIT_AddCallback(SIT_GetById(diag, "load"),   SITE_OnActivate, pixArtLoadImg, NULL);
	SIT_AddCallback(SIT_GetById(diag, "ko"),     SITE_OnActivate, mcuiExitWnd, NULL);
	SIT_AddCallback(SIT_GetById(diag, "ok"),     SITE_OnActivate, pixArtGenerate, NULL);

	pixArt.itemsNb = mcinv.itemsNb;
	pixArtSelInfo(NULL, NULL, &mcinv);

	SIT_ManageWidget(diag);
}

