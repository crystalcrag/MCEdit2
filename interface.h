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
void mcuiResizeInventories(void);

#ifdef MCUI_IMPL

typedef struct MCInventory_t *       MCInventory;

struct MCInterface_t
{
	SIT_Widget   toolTip;
	SIT_Widget   curDialog;
	SIT_CallProc transfer;
	SIT_CallProc resize;
	MCInventory  groups[10];
	uint8_t      groupCount;
	uint8_t      groupIdStart;
	uint8_t      groupOther;
	uint8_t      selCount;
	uint8_t      dragOneItem;
	uint8_t      curTab;
	uint8_t      maxItemSize;
	int          cellSz;
	int          itemSz;
	int          width, height;
	int          glBack, nvgImage;
	int          itemRender;
	int          padding[4];
	Item         allItems;
	ItemBuf      dragSplit;
	ItemBuf      drag;
	ItemBuf      items[128];
	vec4         signPos;
	Chunk        signChunk;
};

struct MCInventory_t
{
	SIT_Widget   cell;
	SIT_Widget   scroll;
	SIT_Widget   canvas;
	SIT_CallProc customDraw;
	int8_t       curX, curY;
	uint8_t      invCol, invRow;
	uint8_t      groupId, movable;
	Item         items;
	int          itemsNb, top;
};

enum /* possible flags for <movable> */
{
	INV_PICK_ONLY   = 1,          /* can only pickup block, not drop them */
	INV_SINGLE_DROP = 2,          /* can drop item, but only one at most */
	INV_SELECT_ONLY = 4,          /* cells can be select, but no item pickup */
};

#endif
#endif
