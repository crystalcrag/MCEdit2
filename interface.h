/*
 * interface.h: public functions for creating/managing all user interface of MCEdit.
 *
 * Written by T.Pierron, oct 2020
 */


#ifndef MCUI_H
#define MCUI_H

#include "render.h"
#include "player.h"

void mcuiTakeSnapshot(int width, int height);
void mcuiResize(void);
void mcuiCreateInventory(Inventory);
void mcuiEditChestInventory(Inventory, Item items, int count, Block type);
void mcuiCreateSignEdit(vec4 pos, int blockId);
void mcuiGoto(vec4 pos);
void mcuiInitDrawItems(void);
void mcuiDrawItems(void);
void mcuiAnalyze(void);
void mcuiFillOrReplace(Bool fillWithBrush);
void mcuiDeleteAll(void);
void mcuiDeletePartial(void);
void mcuiShowPaintings(void);
void mcuiShowPixelArt(vec4 pos);
void mcuiWorldInfo(void);

#ifdef MCUI_IMPL

struct MCInterface_t
{
	SIT_Widget   toolTip;
	SIT_Widget   curDialog;
	SIT_CallProc resize;
	uint8_t      curTab;
	uint8_t      clipItems;
	int          clipRect[4];
	int          width, height;
	int          glBack, nvgImage;
	int          itemRender;
	int *        itemSize;
	Item         allItems;
	ItemBuf      items[128];
	vec4         signPos;
	Chunk        signChunk;
};

#endif
#endif
