/*
 * pixelart.c : interface to generate pixel art from blocks or maps.
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
#include "inventories.h"
#include "selection.h"
#include "cartograph.h"
#include "mapUpdate.h"
#include "mcedit.h"
#include "globals.h"

struct
{
	SIT_Widget image;
	SIT_Widget palette;
	SIT_Widget icon, fill, info;
	SIT_Widget selinfo, cmapSz;
	Item       allItems;
	uint8_t    axis1, axis2, axisMin;
	uint8_t    side;                    /* S, E, N, W, T, B */
	int        rasterizeWith;
	int        fillAir, stretch;
	int        sizeX, sizeY;
	uint16_t   itemsNb;
	uint16_t   itemSel;
	int        selPalette;
	TEXT       defImage[128];

} pixArt = {.selPalette = 5};

/* from interface.c */
void mcuiReplaceFillItems(SIT_Widget, MCInventory);
int  mcuiExitWnd(SIT_Widget, APTR cd, APTR ud);

/* from cartograph.c */
extern uint8_t mapRGB[];
extern uint8_t mapShading[];

enum /* rasterizeWith */
{
	PIXART_BLOCKS,
	PIXART_MAPS
};

#define MAP_SIZEPX  128

static uint8_t minAxis[] = {0, 0, 1, 1, 0, 1};

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
	DLANG("Full blocks"),
	DLANG("Wool"),
	DLANG("Terracotta"),
	DLANG("Concrete"),
	DLANG("Flowers"),
	DLANG("Black&white")
};

static STRPTR palNamesMap[] = {
	"1.12",
	"1.13+",
	DLANG("Black&white")
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
	if (pixArt.rasterizeWith == PIXART_BLOCKS)
	{
		/* blocks: visible from 2 sides */
		switch (pixArt.axisMin) {
		case VX: plane = LANG("east-west"); break;
		case VZ: plane = LANG("north-south"); break;
		default: plane = LANG("floor");
		}
		sprintf(buffer, LANG("%d x %d blocks, %s plane"), pixArt.sizeX, pixArt.sizeY, plane);
	}
	else /* maps: will be visible from only one side */
	{
		static STRPTR sides[] = {DLANG("south"), DLANG("east"), DLANG("north"), DLANG("west"), DLANG("top"), DLANG("bottom")};
		sprintf(buffer, LANG("%d x %dpx, %s face"), pixArt.sizeX * MAP_SIZEPX, pixArt.sizeY * MAP_SIZEPX, LANG(sides[pixArt.side]));
	}
	strcat(buffer, ", ");
	SIT_SetValues(pixArt.selinfo, SIT_Title, buffer, NULL);

	/* change combobox content */
	DATA16  palette;
	uint8_t i;
	pixArt.selPalette = 0;
	SIT_SetValues(pixArt.palette, SIT_InitialValues, NULL, NULL);
	if (pixArt.rasterizeWith == PIXART_BLOCKS)
	{
		for (i = 0, palette = palettes; i < DIM(palNames); i ++, palette += palette[0] + 1)
			SIT_ComboInsertItem(pixArt.palette, -1, LANG(palNames[i]), -1, palette + 1);
		/* and items in inventory */
		inv->items = pixArt.allItems;
		inv->itemsNb = pixArt.itemsNb;
	}
	else /* use map art color entries */
	{
		for (i = 0, palette = palettesMap; i < DIM(palNamesMap); i ++, palette += palette[0] + 1)
			SIT_ComboInsertItem(pixArt.palette, -1, LANG(palNamesMap[i]), -1, palette + 1);
		inv->items = pixArt.allItems + pixArt.itemsNb;
		inv->itemsNb = 62;
	}
	inventoryResetScrollbar(inv);
	SIT_SetValues(pixArt.palette, SIT_SelectedIndex, 0, NULL);
	SIT_SetValues(SIT_GetById(w, "../fillair"), SIT_Enabled, pixArt.rasterizeWith == PIXART_BLOCKS, NULL);

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

/* SITE_OnChange on palette */
static int pixArtGetColorCount(SIT_Widget w, APTR cd, APTR ud)
{
	Item item, eof;
	int  count;
	TEXT buffer[32];
	if (pixArt.rasterizeWith == PIXART_BLOCKS)
		item = pixArt.allItems, eof = item + pixArt.itemsNb;
	else
		item = pixArt.allItems + pixArt.itemsNb, eof = item + 62;
	for (count = 0; item < eof; count += item->added, item ++);
	/* XXX confusing to see more colors than what is selected :-/ */
	// if (pixArt.rasterizeWith == PIXART_MAPS)
	//	count *= 4;
	if (pixArt.itemSel != count)
	{
		switch (count) {
		case 0:  CopyString(buffer, LANG("no colors"), sizeof buffer); break;
		case 1:  CopyString(buffer, LANG("1 color"),   sizeof buffer); break;
		default: snprintf(buffer, sizeof buffer, LANG("%d colors"), count);
		}
		if (pixArt.itemSel >= 0 && (pixArt.itemSel < 2) != (count < 2))
			SIT_SetValues(pixArt.cmapSz, SIT_Style, count < 2 ? "color: red" : "", NULL);
		SIT_SetValues(pixArt.cmapSz, SIT_Title, buffer, NULL);
		pixArt.itemSel = count;
	}
	return 1;
}

/* SITE_OnChange on palette combobox */
static int pixArtChangePalette(SIT_Widget w, APTR cd, APTR ud)
{
	MCInventory inv = ud;
	DATA16 palette = SIT_ComboGetRowTag(w, (int) cd, NULL);
	Item   item, eof;
	int    itemId, i, nb;

	pixArt.selPalette = (int) cd;
	for (item = inv->items, eof = item + inv->itemsNb; item < eof; item->added = 0, item ++);
	for (i = 0, nb = palette[-1], itemId = palette[0], item = inv->items; ; )
	{
		if (pixArt.rasterizeWith == PIXART_BLOCKS)
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
	pixArtGetColorCount(NULL, NULL, NULL);
	return 1;
}

/* map art: items will be colormap of maps */
static int pixArtDrawMapColor(SIT_Widget w, APTR cd, APTR ud)
{
	NVGCTX vg = globals.nvgCtx;
	int *  rect = cd;
	Item   item = ud;

	nvgBeginPath(vg);
	int sz = rect[3] >> 1;
	int offX = (rect[2] - sz) >> 1;
	int offY = (rect[3] - sz) >> 1;
	nvgStrokeWidth(vg, 2);
	nvgFillColorRGBA8(vg, item->tile);
	nvgRect(vg, rect[0]+offX, rect[1]+offY, sz, sz);
	nvgFill(vg);
	int x = rect[0]+offX;
	int y = rect[1]+offY;
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

static int pixArtClearRef(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_Widget * ptr = ud;
	*ptr = NULL;
	return 1;
}

static void pixArtSetIcon(STRPTR path)
{
	TEXT styles[128];
	int  image;
	snprintf(styles, sizeof styles, "background: #8b8b8b url(%s) 50%% 50%% no-repeat; background-size: %s",
		path, pixArt.stretch ? "100% 100%" : "contain");
	SIT_SetValues(pixArt.icon, SIT_Style, styles, NULL);
	if (path != pixArt.defImage)
	{
		/* check if image was successfully loaded */
		if (SIT_GetCSSValue(pixArt.icon, "background-image", &image))
		{
			SIT_SetValues(pixArt.fill, SIT_Enabled, True, NULL);
			CopyString(pixArt.defImage, path, sizeof pixArt.defImage);
		}
		else SIT_Log(SIT_INFO, LANG("Failed to load image %s: unsupported format"), path);
	}
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
		SIT_AddCallback(file, SITE_OnFinalize, pixArtClearRef, &file);
	}

	if (SIT_ManageWidget(file))
	{
		STRPTR path;
		SIT_GetValues(file, SIT_SelPath, &path, NULL);
		pixArtSetIcon(path);
	}
	return 1;
}

#define SPP      4 /* samples per pixel */

/* convert a RGBA image into a palette 8bpp image */
static int pixArtToPalette(DATA8 pixels, int width, int height, DATA8 cmapRGB)
{
	/*
	 * use a floyd-steinberg error diffusion matrix (3x2):
	 *      X  7
	 *   3  5  1
	 */
	DATA8 s, d, data, cmapEOF;
	int   stride = width * SPP;
	int   i, j;
	char  withMaps = pixArt.rasterizeWith == PIXART_MAPS;
	Item  item;

	if (withMaps)
	{
		cmapRGB = alloca(256*4);
		/* mapRGB[] only contains 64 entries: we need 256 */
		for (j = 0, d = cmapRGB; j < 4; j ++)
		{
			uint8_t shade = mapShading[j];
			for (s = mapRGB, i = 0, item = pixArt.allItems + pixArt.itemsNb; i < 64; i ++, s += 4, item ++)
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
	}
	else /* blocks: build color map from main terrain texture */
	{
		DATA8 cmap = cmapRGB + 32 * 32 * 4;
		DATA8 blockId = cmap + 32 * 32 * 4 - 2;
		for (item = pixArt.allItems, i = pixArt.itemsNb; i > 0; i --, item ++)
		{
			if (item->added == 0) continue;
			BlockState state = blockGetById(item->id);
			DATA8 texUV = &state->nzU + pixArt.side * 2;
			if (texUV[1] >= 32) continue; // XXX glass pane
			DATA8 color = cmapRGB + (texUV[0] + texUV[1] * 32) * 4;
			if (color[3] == 0) continue; /* color already used */
			memcpy(cmap, color, 4);
			color[3] = 0;
			blockId[0] = item->id >> 8;
			blockId[1] = item->id;
			blockId -= 2;
			cmap += 4;
		}
		cmapRGB += 32*32*4;
		cmapEOF = cmap;
	}
	if (cmapRGB + 4 >= cmapEOF)
	{
		/* not enough colors selected (0 or 1): show a tooltip of the problem */
		SIT_SetValues(pixArt.info,
			SIT_Visible, True,
			SIT_Title,   LANG("Not enough colors selected"),
			NULL
		);
		return 0;
	}

	/* perform dithering with a fixed colormap */
	for (j = height, d = data = pixels; j > 0; j --, data += stride)
	{
		for (i = width, s = data; i > 0; i --, s += SPP)
		{
			DATA8 cmap, best;
			short r, g, b;
			int   minDist;
			r = s[0];
			g = s[1];
			b = s[2];
			if (withMaps == 0 && s[3] < 64)
			{
				/* pixel is almost transparent: use air block for this */
				d[0] = d[1] = 0;
				continue;
			}
			/* find nearest color from colormap that matches RGBA component pointed by <s> */
			for (cmap = cmapRGB, best = NULL, minDist = 1e6; cmap < cmapEOF; cmap += 4)
			{
				/* find shortest euclidean distance */
				int dist = (cmap[0] - r) * (cmap[0] - r) +
				           (cmap[1] - g) * (cmap[1] - g) +
				           (cmap[2] - b) * (cmap[2] - b);
				if (minDist > dist)
					best = cmap, minDist = dist;
			}

			/* diffuse error to nearby pixels */
			r -= best[0];
			g -= best[1];
			b -= best[2];

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
			if (! withMaps)
			{
				/* 16bpp */
				DATA8 blockId = cmapRGB + 32*32*4-2 - ((best - cmapRGB) >> 1);
				d[0] = blockId[0];
				d[1] = blockId[1];
				d += 2;
			}
			else *d++ = best[3]; /* 8bpp */
		}
	}
	return 1;
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
	/* <bitmap> is bottom-up, but map is stored top-down */
	cmap += (dstHeight - 1) * MAP_SIZEPX;

	for (j = 0; j < dstHeight; j ++)
	{
		memcpy(cmap, bitmap, dstWidth);
		cmap -= MAP_SIZEPX;
		bitmap += width;
	}

	tileX = cartoSaveMap(nbt.mem, nbt.usage);
	NBT_Free(&nbt);
	return tileX;
}

/* <data> is final image rasterized using map colormap: split it up into individual maps */
static void pixArtGenerateMaps(DATA8 data, int width, int height)
{
	vec points = selectionGetPoints();
	int size[] = {
		fabsf(points[VX] - points[VX+4]) + 1,
		fabsf(points[VY] - points[VY+4]) + 1,
		fabsf(points[VZ] - points[VZ+4]) + 1
	};
	uint8_t  axis1 = pixArt.axisMin;
	uint8_t  axis2 = pixArt.axis2;
	uint8_t  i, j;
	ItemID_t itemFrame = itemGetByName("item_frame", False);
	ItemID_t fillMap = itemGetByName("filled_map", False);
	vec4     pos;

	pos[axis1] = minAxis[pixArt.side] ? points[axis1] : points[axis1+4];
	axis1 = pixArt.axis1;
	pos[axis2] = fminf(points[axis2], points[axis2+4]);
	pos[3]     = fminf(points[axis1], points[axis1+4]);
	for (j = 0; j < size[axis2]; j ++, pos[axis2] ++)
	{
		pos[axis1] = pos[3];
		for (i = 0; i < size[axis1]; i ++, pos[axis1] ++)
		{
			int entityId = worldItemCreate(globals.level, itemFrame, pos, pixArt.side);
			int mapId = pixArtCreateMap(data, width, height, i, j);
			worldItemUseItemOn(globals.level, entityId, fillMap | mapId, pos);
		}
	}
}

/* generate pixel art with blocks */
static void pixArtGenerateBlocks(DATA8 data, int width, int height)
{
	vec points = selectionGetPoints();
	int size[] = {
		fabsf(points[VX] - points[VX+4]) + 1,
		fabsf(points[VY] - points[VY+4]) + 1,
		fabsf(points[VZ] - points[VZ+4]) + 1
	};
	uint8_t axis1 = pixArt.axisMin;
	uint8_t axis2 = pixArt.axis2;
	int     dir2[3] = {0, 0, 0};
	int     dir1[3] = {0, 0, 0};
	vec4    pos;
	int     i, j;

	pos[axis1] = minAxis[pixArt.side] ? points[axis1] : points[axis1+4];
	axis1 = pixArt.axis1;
	pos[axis1] = fminf(points[axis1], points[axis1+4]);
	pos[axis2] = fminf(points[axis2], points[axis2+4]);
	dir2[axis2] = 1;
	dir2[axis1] = - size[axis1];
	dir1[axis1] = 1;

	/* be sure the picture remains oriented the way it is displayed in the 3d space */
	i = pixArt.side >= SIDE_TOP ? globals.direction : opp[pixArt.side];
	if (i == 0 || i == 3)
		pos[axis1] += size[axis1]-1, dir1[axis1] = -1, dir2[axis1] = size[axis1];
	if (pixArt.side >= SIDE_TOP && globals.direction >= SIDE_NORTH)
		pos[axis2] += size[axis2]-1, dir2[axis2] = -1;

	struct BlockIter_t iter;
	mapInitIter(globals.level, &iter, pos, True);
	mapUpdateInit(&iter);

	for (j = size[axis2]; j > 0; j --, mapIter(&iter, dir2[0], dir2[1], dir2[2]))
	{
		for (i = size[axis1]; i > 0; i --, mapIter(&iter, dir1[0], dir1[1], dir1[2]), data += 2)
		{
			uint16_t blockId = (data[0] << 8) | data[1];
			if (blockId > 0 || pixArt.fillAir)
				mapUpdate(globals.level, NULL, blockId, NULL, UPDATE_SILENT);
		}
	}
	mapUpdateEnd(globals.level);
}

typedef struct NVGLUframebuffer *  NVGFBO;

static int pixArtGenerate(SIT_Widget w, APTR cd, APTR ud)
{
	/* first: resize image using OpenGL command (main image is already fully transfered to GPU anyway) */
	NVGCTX vg;
	NVGFBO fbo;
	int    dstWidth, dstHeight, image;
	int    srcWidth, srcHeight, done;

	if (! SIT_GetCSSValue(pixArt.icon, "background-image", &image)) /* NVG image handle */
		return 0;

	vg = globals.nvgCtx;
	nvgImageSize(vg, image, &srcWidth, &srcHeight);
	dstWidth = pixArt.sizeX;
	dstHeight = pixArt.sizeY;
	if (pixArt.rasterizeWith == PIXART_MAPS)
		dstWidth *= MAP_SIZEPX, dstHeight *= MAP_SIZEPX;
	if (pixArt.stretch == 0)
	{
		/* keep image aspect ratio */
		if (srcWidth * dstHeight > srcHeight * dstWidth)
			dstHeight = dstWidth * srcHeight / srcWidth;
		else
			dstWidth = dstHeight * srcWidth / srcHeight;
	}

	done = 0;
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
		if (pixArt.rasterizeWith == PIXART_MAPS)
		{
			done = pixArtToPalette(data, dstWidth, dstHeight, NULL);
			if (done)
				pixArtGenerateMaps(data, dstWidth, dstHeight);
		}
		else /* using blocks */
		{
			int size[2], level, texId;
			DATA8 cmap;
			renderGetTerrain(size, &texId);
			for (level = 0; size[0] > 32; size[0] >>= 1, size[1] >>= 1, level ++);
			cmap = malloc(size[0] * size[1] * 4);
			/* the last mipmap actually contains the colormap of terrain texture */
			glBindTexture(GL_TEXTURE_2D, texId);
			glGetTexImage(GL_TEXTURE_2D, level, GL_RGBA, GL_UNSIGNED_BYTE, cmap);

			done = pixArtToPalette(data, dstWidth, dstHeight, cmap);
			free(cmap);
			if (done > 0)
				pixArtGenerateBlocks(data, dstWidth, dstHeight);
		}
		free(data);
	}
	if (done)
		SIT_Exit(EXIT_LOOP);
	return 1;
}

static void pixArtFillMapColors(MCInventory inv)
{
	Item item;
	int  i;

	for (item = inv->items + inv->itemsNb, i = 0; i < 64; i ++, item ++)
	{
		item->id = 0xffff;
		item->tile = mapRGB + i * 4;
	}
}

/* main interface for pixel art editor */
void mcuiShowPixelArt(vec4 playerPos)
{
	static struct MCInventory_t mcinv = {.invRow = 6, .invCol = MAXCOLINV, .movable = INV_SELECT_ONLY, .customDraw = pixArtDrawMapColor};

	SIT_Widget diag = SIT_CreateWidget("pixelart.bg", SIT_DIALOG + SIT_EXTRA((blockLast - blockStates + 64) * sizeof (struct Item_t)), globals.app,
		SIT_DialogStyles, SITV_Plain | SITV_Modal,
		SIT_Style,        "padding-top: 0.2em",
		NULL
	);

	int sz = SIT_EmToReal(diag, SITV_Em(11));

	TEXT saveMsg[64];
	snprintf(saveMsg, sizeof saveMsg, "(<a href=#>%s</a>)", LANG("Save"));

	SIT_CreateWidgets(diag,
		"<label name=dlgtitle.big title=", LANG("Pixel art editor"), "left=", SITV_AttachCenter, ">"
		"<label name=icon#table top=WIDGET,dlgtitle,0.5em labelSize=", SITV_LabelSize(sz,sz), ">"
		"<label name=msg title=", LANG("Rasterize with:"), "left=WIDGET,icon,1em top=WIDGET,dlgtitle,0.5em>"
		"<button name=blocks curValue=", &pixArt.rasterizeWith, "title=", LANG("Blocks"), "buttonType=", SITV_RadioButton,
		" top=WIDGET,msg,0.5em left=WIDGET,icon,1em>"
		"<button name=maps curValue=", &pixArt.rasterizeWith, "title=", LANG("Maps tiles"), "buttonType=", SITV_RadioButton,
		" top=WIDGET,blocks,0.5em left=WIDGET,icon,1em maxWidth=blocks>"
		"<button name=fillair title=", LANG("Fill with air"), "curValue=", &pixArt.fillAir, "buttonType=", SITV_CheckBox,
		" checkState=1 top=OPPOSITE,blocks left=WIDGET,blocks,1.5em>"
		"<button name=stretch title=", LANG("Stretch"), "curValue=", &pixArt.stretch, "buttonType=", SITV_CheckBox,
		" top=WIDGET,fillair,0.5em left=OPPOSITE,fillair>"
		"<label name=msg2 title=", LANG("Palette:"), "left=WIDGET,icon,1em top=WIDGET,maps,1em>"
		"<combobox name=palette top=WIDGET,msg2,0.5em left=OPPOSITE,msg2>"
		"<label name=save.big title=", saveMsg, "bottom=OPPOSITE,msg2 right=OPPOSITE,palette>"
		"<label name=msg3.big title=", LANG("Selection:"), "top=WIDGET,icon,0.5em>"
		"<label name=selinfo top=OPPOSITE,msg3 left=WIDGET,msg3,0.3em>"
		"<label name=cmapsz top=OPPOSITE,msg3 left=WIDGET,selinfo>"
		"<canvas composited=1 name=inv.inv top=WIDGET,msg3,0.5em nextCtrl=LAST/>"
		"<button name=load title=", LANG("Load image"), "top=WIDGET,inv,0.5em>"
		"<button name=ko title=", LANG("Cancel"), "buttonType=", SITV_CancelButton, "top=OPPOSITE,load right=FORM>"
		"<button name=ok title=", LANG("Fill"), "enabled=0 top=OPPOSITE,ko right=WIDGET,ko,0.5em buttonType=", SITV_DefaultButton, ">"
		"<scrollbar width=1.2em name=scroll.inv wheelMult=1 top=OPPOSITE,inv,0 bottom=OPPOSITE,inv,0 right=FORM>"
		"<tooltip name=info delayTime=", SITV_TooltipManualTrigger, "displayTime=10000 toolTipAnchor=", SITV_TooltipFollowMouse, ">"
	);
	SIT_SetAttributes(diag, "<inv right=WIDGET,scroll,0.2em left=FORM>");

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

	if (size[VX] == size[VY] && size[VY] == size[VZ])
	{
		/* cube block selected: opposite of viewing direction */
		axis1 = globals.direction & 1 ? VX : VZ;
	}
	else
	{
		axis1 = size[VX] < size[VY] ? VX : VY;
		if (size[axis1] > size[VZ]) axis1 = VZ;
	}
	switch (pixArt.axisMin = axis1) {
	case VX: /* extend in YZ plane: visible from east and/or west */
		axis1 = VZ; axis2 = VY;
		pixArt.side =
			vecDistSquare(playerPos, (vec4){points[VX],   center[VY], center[VZ]}) <
			vecDistSquare(playerPos, (vec4){points[VX]+1, center[VY], center[VZ]}) ? SIDE_WEST : SIDE_EAST;
		break;
	case VY: /* extend in XZ plane: visible from top/bottom */
		if (globals.direction & 1)
			axis1 = VZ, axis2 = VX;
		else
			axis1 = VX, axis2 = VZ;
		pixArt.side =
			vecDistSquare(playerPos, (vec4){center[VX], points[VY],   center[VZ]}) <
			vecDistSquare(playerPos, (vec4){center[VX], points[VY]+1, center[VZ]}) ? SIDE_TOP : SIDE_BOTTOM;
		break;
	case VZ: /* extend in XY plane: visible from south and/or north */
		axis1 = VX; axis2 = VY;
		pixArt.side =
			vecDistSquare(playerPos, (vec4){center[VX], center[VY], points[VZ]})   <
			vecDistSquare(playerPos, (vec4){center[VX], center[VY], points[VZ]+1}) ? SIDE_NORTH : SIDE_SOUTH;
	}

	SIT_GetValues(diag, SIT_UserData, &pixArt.allItems, NULL);
	pixArt.palette = SIT_GetById(diag, "palette");
	pixArt.selinfo = SIT_GetById(diag, "selinfo");
	pixArt.cmapSz  = SIT_GetById(diag, "cmapsz");
	pixArt.icon    = SIT_GetById(diag, "icon");
	pixArt.info    = SIT_GetById(diag, "info");
	pixArt.fill    = SIT_GetById(diag, "ok");
	pixArt.axis1   = axis1;
	pixArt.axis2   = axis2;
	pixArt.sizeX   = size[axis1];
	pixArt.sizeY   = size[axis2];
	pixArt.itemSel = -1;

	int old = pixArt.selPalette;
	SIT_AddCallback(pixArt.palette, SITE_OnChange,   pixArtChangePalette, &mcinv);
	SIT_AddCallback(pixArt.fill,    SITE_OnActivate, pixArtGenerate,      NULL);

	SIT_Widget inv = SIT_GetById(diag, "inv");
	mcuiReplaceFillItems(diag, &mcinv);
	pixArtFillMapColors(&mcinv);
	inventoryInit(&mcinv, inv, 1);
	inventoryResetScrollbar(&mcinv);
	SIT_AddCallback(inv, SITE_OnChange, pixArtGetColorCount, NULL);

	SIT_AddCallback(SIT_GetById(diag, "blocks"), SITE_OnActivate, pixArtSelInfo, &mcinv);
	SIT_AddCallback(SIT_GetById(diag, "maps"),   SITE_OnActivate, pixArtSelInfo, &mcinv);
	SIT_AddCallback(SIT_GetById(diag, "save"),   SITE_OnActivate, pixArtSavePal, &mcinv);
	SIT_AddCallback(SIT_GetById(diag, "load"),   SITE_OnActivate, pixArtLoadImg, NULL);
	SIT_AddCallback(SIT_GetById(diag, "ko"),     SITE_OnActivate, mcuiExitWnd, NULL);

	/* restore last image selected */
	if (pixArt.defImage[0])
	{
		pixArtSetIcon(pixArt.defImage);
		SIT_SetValues(pixArt.fill, SIT_Enabled, True, NULL);
	}

	pixArt.itemsNb = mcinv.itemsNb;
	pixArtSelInfo(NULL, NULL, &mcinv);
	SIT_SetValues(pixArt.palette, SIT_SelectedIndex, old, NULL);

	SIT_ManageWidget(diag);
}
