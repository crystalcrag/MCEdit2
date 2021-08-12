/*
 * interface.h: public functions for creating/managing all user interface of MCEdit.
 *
 * Written by T.Pierron, oct 2020
 */


#ifndef MCUI_H
#define MCUI_H

#include "SIT.h"
#include "render.h"
#include "player.h"

void mcuiTakeSnapshot(SIT_Widget app, int width, int height);
void mcuiCreateInventory(Inventory);
void mcuiEditChestInventory(Inventory, Item items, int count);
void mcuiCreateSignEdit(Map map, vec4 pos, int blockId, int * exit);
void mcuiGoto(SIT_Widget parent, vec4 pos);
void mcuiInitDrawItems(void);
void mcuiDrawItems(void);
void mcuiAnalyze(SIT_Widget app, Map map);
void mcuiReplace(SIT_Widget parent);

#ifdef MCUI_IMPL

typedef struct MCInventory_t *       MCInventory;

struct MCInterface_t
{
	SIT_Widget   app;
	SIT_Widget   scroll;
	SIT_Widget   toolTip;
	SIT_CallProc cb;
	MCInventory  groups[10];
	int          groupCount;
	int          groupIdStart;
	APTR         nvgCtx;
	int          cellSz;
	int          itemSz;
	int          width, height;
	int          glBack, nvgImage;
	int          itemRender, curTab;
	int          padding[4];
	Item         allItems;
	uint8_t      selCount;
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
	uint8_t    groupId;
	Item       items;
	int        itemsNb, top;
};


#endif
#endif
