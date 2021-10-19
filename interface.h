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
void mcuiEditChestInventory(Inventory, Item items, int count);
void mcuiCreateSignEdit(vec4 pos, int blockId, int * exit);
void mcuiGoto(vec4 pos);
void mcuiInitDrawItems(void);
void mcuiDrawItems(void);
void mcuiAnalyze(void);
void mcuiFillOrReplace(Bool fillWithBrush);
void mcuiDeleteAll(void);
void mcuiDeletePartial(void);
void mcuiShowPaintings(void);
void mcuiShowPixelArt(void);

#ifdef MCUI_IMPL

typedef struct MCInventory_t *       MCInventory;

struct MCInterface_t
{
	SIT_Widget   scroll;
	SIT_Widget   toolTip;
	SIT_CallProc cb;
	MCInventory  groups[10];
	int          groupCount;
	int          groupIdStart;
	int          cellSz;
	int          itemSz;
	int          width, height;
	int          glBack, nvgImage;
	int          itemRender, curTab;
	int          padding[4];
	Item         allItems;
	uint8_t      selCount;
	uint8_t      dragOneItem;
	ItemBuf      dragSplit;
	ItemBuf      drag;
	ItemBuf      items[128];
	int *        exitCode;
	vec4         signPos;
	Chunk        signChunk;
};

struct MCInventory_t
{
	SIT_Widget cell;
	int8_t     curX, curY;
	uint8_t    invCol, invRow;
	uint8_t    groupId, movable;
	Item       items;
	int        itemsNb, top;
};

enum /* possible flags for <movable> */
{
	INV_PICK_ONLY   = 1,          /* can only pickup block, not drop them */
	INV_SINGLE_DROP = 2,          /* can drop item, but only one at most */
	INV_SELECT      = 3,          /* cells can be select, but no item pickup */
};

#endif
#endif
