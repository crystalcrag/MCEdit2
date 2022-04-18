/*
 * inventories.h: public function for managing user interface of inventories of items.
 *
 * written by T.Pierron, oct 2020
 */


#ifndef MC_INVENTORIES_H
#define MC_INVENTORIES_H

#include "SIT.h"

typedef struct MCInventory_t *       MCInventory;

int *  inventoryReset(void);
void   inventoryResize(void);
Item   inventoryDraggedItem(void);
void   inventorySetTooltip(SIT_Widget toolTip, Item item, STRPTR extra);
void   inventoryInit(MCInventory, SIT_Widget canvas, int max);
void   inventoryResetScrollbar(MCInventory);
void   inventoryDecodeItems(Item container, int count, NBTHdr hdrItems);
STRPTR inventoryItemName(NBTFile nbt, int offset, TEXT itemId[16]);
Bool   inventorySerializeItems(ChunkData, int offset, STRPTR listName, Item items, int itemCount, NBTFile ret);
int    inventoryPushItem(BlockIter from, BlockIter to);


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
	int          itemsNb, top, width;
};

enum /* possible flags for <movable> */
{
	INV_PICK_ONLY   = 1,          /* can only pickup block, not drop them */
	INV_SINGLE_DROP = 2,          /* can drop item, but only one at most */
	INV_SELECT_ONLY = 4,          /* cells can be selected, but no item pickup */
	INV_TRANSFER    = 8,          /* transfert items to/from player inventory */
};

#endif
