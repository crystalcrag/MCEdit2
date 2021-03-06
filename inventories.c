/*
 * inventories.c: generic inventory interface management: move/drag/split/draw items between inventories
 *                with mouse, keyboard or block update (like hopper).
 *
 * written by T.Pierron, oct 2020.
 */

#define MCUI_IMPL
#include <malloc.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include "nanovg.h"
#include "SIT.h"
#include "interface.h"
#include "inventories.h"
#include "globals.h"

struct /* keep track of all inventories currently opened in a given interface */
{
	SIT_CallProc transfer;
	SIT_Widget   toolTip;
	MCInventory  groups[10];      /* well, at most 10 actually: ought to be enough(TM) */
	uint8_t      groupCount;
	uint8_t      groupIdStart;
	uint8_t      groupOther;
	uint8_t      selCount;
	uint8_t      dragOneItem;
	uint8_t      maxItemSize;
	int          padding[4];
	int          itemSz, cellSz;
	Item_t       dragSplit;
	Item_t       drag;

}	inventories;


int * inventoryReset(void)
{
	inventories.selCount = 0;
	inventories.groupCount = 0;
	inventories.groupOther = 0;
	inventories.transfer = NULL;
	inventories.toolTip = NULL;
	return &inventories.itemSz;
}

Item mcuiAddItemToRender(void);

Item inventoryDraggedItem(void)
{
	return &inventories.drag;
}

static int inventoryRender(SIT_Widget w, APTR cd, APTR ud)
{
	MCInventory inv = ud;
	float       x, y, curX, curY, width;
	int         i, j, sz = inventories.cellSz;
	uint8_t     select = inv->movable & INV_SELECT_ONLY;
	Item        item = inv->items + inv->top;
	int         max = inv->itemsNb - inv->top;
	int *       cols = alloca(sizeof *cols * (inv->invCol+1));

	SIT_GetValues(w, SIT_AbsX, &x, SIT_AbsY, &y, SIT_Width, &width, NULL);
	curX = inv->curX;
	curY = inv->curY;
	inv->width = width;
	/*
	 * canvas can be a bit wider than what we asked, due to messages from translation widening the
	 * interface by quite a bit: simply enlarge the cell and center the item in it
	 */
	for (i = 0; i <= inv->invCol; i ++)
		cols[i] = i * width / inv->invCol;

	for (j = 0; j < inv->invRow; j ++)
	{
		for (i = 0; i < inv->invCol; i ++)
		{
			int y2 = j * sz;
			int x2 = cols[i];
			int szx = cols[i+1] - x2;
			if (select && item->added)
			{
				/* selection underlay */
				nvgBeginPath(globals.nvgCtx);
				nvgRect(globals.nvgCtx, x+x2, y+y2, szx, sz);
				nvgFillColorRGBA8(globals.nvgCtx, "\x20\xff\x20\x7f");
				nvgFill(globals.nvgCtx);
			}
			if ((i == curX && j == curY) || (max > 0 && item->slot > 0))
			{
				/* cursor underlay */
				nvgBeginPath(globals.nvgCtx);
				nvgRect(globals.nvgCtx, x+x2, y+y2, szx, sz);
				nvgFillColorRGBA8(globals.nvgCtx, "\xff\xff\xff\x7f");
				nvgFill(globals.nvgCtx);
			}
			SIT_SetValues(inv->cell, SIT_X, x2, SIT_Y, y2, SIT_Width, szx, SIT_Height, sz, NULL);
			SIT_RenderNode(inv->cell);
			/* grab item to render */
			if (max > 0)
			{
				if (item->id == 0xffff)
				{
					/* custom item draw */
					inv->customDraw(w, (int[4]){x+x2, y+y2, szx, sz}, item);
				}
				else if (item->id > 0)
				{
					Item render = mcuiAddItemToRender();
					render[0] = item[0];
					render->x = x + x2 + ((inventories.padding[0] + szx - sz) >> 1);
					render->y = globals.height - (y + y2 + (inventories.padding[1] >> 1)) - inventories.itemSz;
				}
				item ++;
				max --;
			}
		}
	}
	return 0;
}

/*
 * fill tooltip text when hovering an item
 */
void inventorySetTooltip(SIT_Widget toolTip, Item item, STRPTR extra)
{
	TEXT title[256];
	TEXT id[16];
	int  tag = NBT_FindNodeFromStream(item->tile, 0, "/tag.ench");
	int  index = 0;
	int  itemNum;
	int  metaData;

	title[0] = 0;
	if (tag >= 0)
		index = StrCat(title, sizeof title, 0, "<b>");
	if (isBlockId(item->id))
	{
		BlockState state = blockGetById(item->id);
		if (state->id > 0)
		{
			index = StrCat(title, sizeof title, index,
				STATEFLAG(state, TRIMNAME) ? blockIds[item->id >> 4].name : state->name
			);
			itemNum  = state->id >> 4;
			metaData = state->id & 15;
		}
		else
		{
			/* a block that shouldn't be in a inventory :-/ */
			SIT_SetValues(toolTip, SIT_Visible, False, NULL);
			return;
		}
	}
	else
	{
		ItemDesc desc = itemGetById(item->id);
		if (desc == NULL)
		{
			SIT_SetValues(toolTip, SIT_Visible, False, NULL);
			return;
		}
		itemNum  = ITEMNUM(item->id);
		metaData = ITEMMETA(item->id);
		index    = StrCat(title, sizeof title, index, desc->name);
	}
	if (tag >= 0)
		index = StrCat(title, sizeof title, index, "</b>");

	/* add id */
	if (itemNum != 255 /* dummy block for extended inventory bar */)
	{
		STRPTR p = id + sprintf(id, " (#%04d", itemNum);
		if (metaData > 0) p += sprintf(p, "/%d", metaData);
		p += sprintf(p, ")");
		index = StrCat(title, sizeof title, index, id);

		/* add enchant if any */
		if (tag >= 0)
			itemDecodeEnchants(item->tile + tag, title, sizeof title);

		index = StrCat(title, sizeof title, index, "<br><dim>");

		/* check if this is an item container */
		int inventory = NBT_FindNodeFromStream(item->tile, 0, "/Items");

		if (inventory >= 0)
		{
			inventory = ((NBTHdr)(item->tile + inventory))->count;
			sprintf(id, "+%d ", inventory);
			index = StrCat(title, sizeof title, index, id);
			index = StrCat(title, sizeof title, index, inventory > 1 ? "Items" : "Item");
			index = StrCat(title, sizeof title, index, "<br>");
		}

		/* and technical name */
		itemGetTechName(item->id, title + index, sizeof title - index, True);
		index = StrCat(title, sizeof title, index, "</dim>");
	}

	if (extra)
		StrCat(title, sizeof title, index, extra);

	SIT_SetValues(toolTip, SIT_Visible, True, SIT_Title, title, SIT_DisplayTime, SITV_ResetTime, NULL);
}

/* show info about block hovered in a tooltip */
static void inventoryRefreshTooltip(MCInventory inv)
{
	int index = inv->top + inv->curX + inv->curY * inv->invCol;
	if (index >= inv->itemsNb || inv->items[index].id == 0xffff)
	{
		SIT_SetValues(inventories.toolTip, SIT_Visible, False, NULL);
		return;
	}
	inventorySetTooltip(inventories.toolTip, inv->items + index, NULL);
}

static int inventoryDragItem(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_OnMouse * msg = cd;
	switch (msg->state) {
	case SITOM_CaptureMove:
		inventories.drag.x = msg->x;
		inventories.drag.y = globals.height - msg->y - inventories.itemSz;
		SIT_ForceRefresh();
		break;
	case SITOM_ButtonPressed:
		inventories.drag.id = 0;
		SIT_InitDrag(NULL);
		SIT_ForceRefresh();
	default: break;
	}
	return 1;
}

static void inventorySplitItems(Item addCell)
{
	if (addCell->slot > 0 || (addCell->id > 0 && addCell->id != inventories.dragSplit.id))
		return;

	/* items need to be filled in the order they were drawed */
	DATA8 slots, group;
	Item  list, eof;
	int   count, split, i;

	if (addCell->id == 0)
	{
		addCell[0] = inventories.dragSplit;
		addCell->count = addCell->added = 0;
	}
	addCell->slot = ++ inventories.selCount;
	count = inventories.dragSplit.count;
	split = inventories.dragOneItem ? 1 : count / inventories.selCount;
	if (split < 1) split = 1;

	slots = alloca(inventories.selCount * 2);
	group = slots + inventories.selCount;

	/* get all slots affected so far in a linear buffer */
	for (i = 0; i < inventories.groupCount; i ++)
	{
		MCInventory inv = inventories.groups[i];
		int j;
		if (inv->groupId != inventories.groupIdStart)
			continue;
		for (list = inv->items, eof = list + inv->itemsNb, j = 0; list < eof; list ++, j ++)
		{
			if (list->slot == 0) continue;
			uint8_t slot = list->slot-1;
			slots[slot] = j;
			group[slot] = i;
		}
	}

	/* split items between all slots hovered */
	for (i = 0; i < inventories.selCount; i ++)
	{
		MCInventory inv = inventories.groups[group[i]];
		list = inv->items + slots[i];
		list->count -= list->added;
		list->added = 0;
		int left = MIN(split, count);
		count -= left - itemAddCount(list, left);
		if (list->count == 0)
			list->id = 0;
	}

	/* if there are any leftovers, drag them */
	if (count > 0)
	{
		i = SIT_InitDrag(inventoryDragItem);
		inventories.drag = inventories.dragSplit;
		inventories.drag.count = count;
		inventories.drag.x = i & 0xffff;
		inventories.drag.y = globals.height - (i >> 16) - inventories.itemSz;
	}
	else inventories.drag.id = 0, SIT_InitDrag(NULL);
	SIT_ForceRefresh();
}

/* check if we double-clicked on an item: gather all the same item into one slot (up to stack limit) */
static void inventoryGrabAllItems(MCInventory inv, int index)
{
	static double lastClick;
	static int    lastSlot;

	if (inv->movable & INV_PICK_ONLY)
		return;

	double timeMS = FrameGetTime();
	if (lastSlot == index && timeMS - lastClick < 500)
	{
		uint8_t groupId = inv->groupId;
		uint8_t i;
		for (i = 0; i < inventories.groupCount; i ++)
		{
			Item item, end;
			inv = inventories.groups[i];
			if (inv->groupId != groupId) continue;
			for (item = inv->items, end = item + inv->itemsNb; item < end; item ++)
			{
				if (inventories.drag.id != item->id) continue;
				item->count = itemAddCount(&inventories.drag, item->count);
				if (item->count == 0) item->id = 0;
				else goto break_all; /* stack full */
			}
		}
	}
	break_all:
	lastSlot = index;
	lastClick = timeMS;
}


/* highlight cell hovered */
static int inventoryMouse(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_OnMouse * msg = cd;
	MCInventory   inv = ud;
	Item          old;

	if (inv->width == 0) return 0; /* OnPaint not received yet... */
	int cellx = msg->x * inv->invCol / inv->width;
	int celly = msg->y / inventories.cellSz;
	switch (msg->state) {
	case SITOM_CaptureMove:
		if (cellx < 0 || cellx >= inv->invCol || celly < 0 || celly >= inv->invRow)
			return 0;
		if (inventories.selCount > 0)
		{
			inventorySplitItems(inv->items + inv->top + cellx + celly * inv->invCol);
		}
		else if (inv->movable & INV_SELECT_ONLY)
		{
			/* drag selection */
			if (inv->curX != cellx || inv->curY != celly)
			{
				inv->curX = cellx;
				inv->curY = celly;
				old = inv->items + inv->top + cellx + celly * inv->invCol;
				if (old->added != inventories.dragOneItem)
				{
					old->added = inventories.dragOneItem;
					SIT_ApplyCallback(w, NULL, SITE_OnChange);
				}
				SIT_ForceRefresh();
			}
		}
		break;
	case SITOM_Move:
		if (inv->curX != cellx || inv->curY != celly)
		{
			/* item being dragged, but this inventory is not part of the group */
			if (inventories.groupIdStart > 0 && inv->groupId != inventories.groupIdStart)
				return 0;
			inv->curX = cellx;
			inv->curY = celly;
			if (inventories.selCount == 0)
			{
				if (inventories.drag.id == 0)
					inventoryRefreshTooltip(inv);
				SIT_ForceRefresh();
			}
			else inventorySplitItems(inv->items + inv->top + cellx + celly * inv->invCol);
		}
		break;
	case SITOM_ButtonReleased:
		if (inventories.selCount > 0)
		{
			/* clear slots */
			int i;
			for (i = 0; i < inventories.groupCount; i ++)
			{
				Item list, eof;
				inv = inventories.groups[i];
				for (list = inv->items, eof = list + inv->itemsNb; list < eof; list->slot = 0, list->added = 0, list ++);
			}
			inventories.selCount = 0;
			inventories.groupIdStart = 0;
			SIT_ForceRefresh();
			if (inventories.drag.id == 0)
				SIT_InitDrag(NULL);
		}
		break;
	case SITOM_ButtonPressed:
		cellx = inv->top + inv->curX + inv->curY * inv->invCol;
		switch (msg->button) {
		case SITOM_ButtonWheelDown:
		case SITOM_ButtonWheelUp:
			/* scroll content of inventory if applicable */
			SIT_ApplyCallback(inv->scroll, cd, SITE_OnClick);
			break;
		case SITOM_ButtonMiddle:
			if (inv->movable & INV_SELECT_ONLY) return 0;
			/* grab an entire stack (no matter how many items there are in inventory) */
			if (inv->items[cellx].id > 0)
			{
				grab_stack:
				inventories.drag = inv->items[cellx];
				/* will be clamped to max stack */
				itemAddCount(&inventories.drag, 64);
				cellx = SIT_InitDrag(inventoryDragItem);
				inventories.drag.x = cellx & 0xffff;
				inventories.drag.y = globals.height - (cellx >> 16) - inventories.itemSz;
				SIT_ForceRefresh();
				return -1;
			}
			break;
		case SITOM_ButtonRight:
			if (inv->movable & INV_SELECT_ONLY) return 0;
			if (inv->groupId && inventories.drag.id == 0 && inv->items[cellx].count > 0)
			{
				/* grab half the stack */
				Item cur = inv->items + cellx;
				int  cnt = (cur->count + 1) >> 1;

				cur->count -= cnt;
				inventories.drag = *cur;
				inventories.drag.count = cnt;
				if (cur->count == 0)
				{
					memset(cur, 0, sizeof *cur);
					SIT_ApplyCallback(w, 0, SITE_OnChange);
				}
				cellx = SIT_InitDrag(inventoryDragItem);
				inventories.drag.x = cellx & 0xffff;
				inventories.drag.y = globals.height - (cellx >> 16) - inventories.itemSz;
				return -1;
			}
			else if ((inv->movable & INV_PICK_ONLY) == 0 && inventories.drag.id > 0)
			{
				/* initiate drag, but only distribute 1 item at time */
				celly = 1;
				inventories.dragOneItem = 1;
				old = &inv->items[cellx];
				if (old->id == 0 || old->id == inventories.drag.id)
					goto init_drag;
			}
			else inventories.drag.id = 0;
			break;
		case SITOM_ButtonLeft:
			if (inv->movable & INV_SELECT_ONLY)
			{
				/* toggle selected flag for item hovered */
				old = &inv->items[cellx];
				if (old->id > 0)
				{
					old->added ^= 1;
					inventories.dragOneItem = old->added;
					SIT_ApplyCallback(w, NULL, SITE_OnChange);
					SIT_ForceRefresh();
					return 2;
				}
			}
			else if (msg->flags & SITK_FlagShift)
			{
				if (inventories.transfer && (inv->movable & INV_TRANSFER))
				{
					/* transfer item to other inventory usually */
					if (inventories.transfer(w, inv, (APTR) cellx))
						SIT_ForceRefresh();
				}
				else if (inv->groupId)
				{
					/* clear slot */
					memset(inv->items + cellx, 0, sizeof (struct Item_t));
					SIT_ForceRefresh();
					SIT_ApplyCallback(w, 0, SITE_OnChange);
				}
				else goto grab_stack;
			}
			else if (inventories.drag.id > 0)
			{
				if ((inv->movable & INV_PICK_ONLY) == 0)
				{
					/* merge stack if same id */
					old = &inv->items[cellx];
					SIT_ApplyCallback(w, (APTR) (int) inventories.drag.id, SITE_OnChange);
					if (old->id == 0 || old->id == inventories.drag.id)
					{
						/* but only to stack limit */
						inventories.dragOneItem = 0;
						celly = inv->movable & INV_SINGLE_DROP ? 1 : inventories.drag.count;
						init_drag:
						inventories.dragSplit = inventories.drag;
						if (old->id == 0)
							*old = inventories.drag, old->count = old->added = celly, inventories.drag.count -= celly;
						else
							inventories.drag.count -= celly - itemAddCount(old, celly);
						/* start spliting items by drawing on inventory slots */
						inventories.groupIdStart = inv->groupId;
						if (inventories.drag.count == 0)
							inventories.drag.id = 0;
						inventories.selCount = 1;
						inv->items[cellx].slot = 1;
						SIT_ForceRefresh();
						return 2;
					}
					else if (old->id > 0)
					{
						/* click on a slot with different items in it: exchange with item being dragged */
						struct Item_t buf = inventories.drag;
						inventories.drag.id = old->id;
						inventories.drag.count = old->count;
						inventories.drag.uses = old->uses;
						inventories.drag.tile = old->tile;
						inventories.drag.extraF = old->extraF;
						*old = buf;
						if (inv->movable & INV_SINGLE_DROP)
							old->count = 1;
						SIT_ForceRefresh();
					}
					return -1;
				}
				else if (inventories.drag.id == inv->items[cellx].id)
				{
					/* click twice on the same block: add 1 to stack */
					if (itemAddCount(&inventories.drag, 1) == 0)
						SIT_ForceRefresh();

					return 1;
				}
				inventories.drag.id = 0;
				SIT_InitDrag(NULL);
				SIT_ForceRefresh();
			}
			else if (cellx < inv->itemsNb)
			{
				/* pick an item */
				inventories.drag = inv->items[cellx];
				if (inventories.drag.id == 0) break;
				if (inv->groupId)
				{
					memset(inv->items + cellx, 0, sizeof (struct Item_t));
					SIT_ApplyCallback(w, 0, SITE_OnChange);
				}
				cellx = SIT_InitDrag(inventoryDragItem);
				inventories.drag.x = cellx & 0xffff;
				inventories.drag.y = globals.height - (cellx >> 16) - inventories.itemSz;
				inventoryGrabAllItems(inv, cellx);
				SIT_ForceRefresh();
			}
			/* cancel capture move */
			return -1;
		default: break;
		}
	default: break;
	}
	return 1;
}

static int inventoryMouseOut(SIT_Widget w, APTR cd, APTR ud)
{
	MCInventory inv = ud;
	inv->curX = -1;
	SIT_ForceRefresh();
	return 1;
}

void inventoryResetScrollbar(MCInventory inv)
{
	int lines = (inv->itemsNb + inv->invCol - 1) / inv->invCol;

	/* empty lines at bottom? */
	if (inv->top + inv->invRow > lines)
	{
		int top = lines - inv->invRow;
		if (top < 0) top = 0;
		inv->top = top;
	}

	if (lines < inv->invRow)
		SIT_SetValues(inv->scroll, SIT_MaxValue, 1, SIT_PageSize, 1, SIT_ScrollPos, inv->top, NULL);
	else
		SIT_SetValues(inv->scroll, SIT_MaxValue, lines, SIT_PageSize, inv->invRow, SIT_LineHeight, 1, SIT_ScrollPos, inv->top, NULL);
}

/* SITE_OnChange: scrollbar position changed */
static int inventorySetTop(SIT_Widget w, APTR cd, APTR ud)
{
	MCInventory inv = ud;
	inv->top = (int) cd * inv->invCol;
	int visible = 0;
	SIT_GetValues(inventories.toolTip, SIT_Visible, &visible, NULL);
	if (inv->curX >= 0 && visible) inventoryRefreshTooltip(inv);
	return 1;
}

/* add/remove items from inventory using the keyboard */
static void inventoryAddTo(MCInventory inv)
{
	int pos = inv->top + inv->curX + inv->curY * inv->invCol;

	if (pos < inv->itemsNb)
	{
		int id = inv->items[pos].id, i, j;
		Item free;
		if (id == 0) return;

		/* check first if this item is already in the inventory */
		for (i = inventories.groupCount - 1, free = NULL; i >= 0; i --)
		{
			MCInventory copyTo = inventories.groups[i];
			for (j = 0; j < copyTo->itemsNb; j ++)
			{
				Item cur = copyTo->items + j;
				if (cur->id == 0)
				{
					if (free == NULL)
						free = cur;
				}
				else if (cur->id == id)
				{
					ItemDesc desc = itemGetById(id);
					if (cur->count < (desc ? desc->stack : 64))
					{
						cur->count ++;
						SIT_ForceRefresh();
						return;
					}
				}
			}
		}
		/* use first free slot in inventory */
		if (free)
		{
			free[0] = inv->items[pos];
			SIT_ForceRefresh();
		}
	}
}

/* try to move item into another inventory (delete if can't) */
static void inventoryTransferFrom(MCInventory inv)
{
	int pos = inv->top + inv->curX + inv->curY * inv->invCol;
	if (pos < inv->itemsNb)
	{
		/* check if there is another inventory group */
		Item transfer = NULL;
		int  i, j;
		for (i = 0; i < inventories.groupCount; i ++)
		{
			MCInventory dest = inventories.groups[i];
			if (dest->movable != INV_PICK_ONLY && dest->groupId != inv->groupId)
			{
				for (j = 0; j < dest->itemsNb && dest->items[j].id > 0; j ++);
				if (j < dest->itemsNb)
				{
					transfer = dest->items + j;
					break;
				}
			}
			dest = NULL;
		}
		Item item = inv->items + pos;
		if (item->count > 0)
		{
			if (transfer)
			{
				transfer[0] = item[0];
				memset(item, 0, sizeof *item);
			}
			else
			{
				item->count --;
				if (item->count == 0)
					item->id = 0;
			}
			SIT_ForceRefresh();
		}
	}
}

/* handle inventory selection using keyboard */
static int inventoryKeyboard(SIT_Widget w, APTR cd, APTR ud)
{
	MCInventory inv = ud;
	SIT_OnKey * msg = cd;

	if ((msg->flags & SITK_FlagUp) == 0)
	{
		int top = inv->top / inv->invCol;
		int x = inv->curX;
		int y = inv->curY + top;
		int max = (inv->itemsNb + inv->invCol + 1) / inv->invCol - 1;
		int row;
		if (max <= inv->invRow)
			max = inv->invRow - 1;
		switch (msg->keycode) {
		case SITK_Up:
			y --; if (y < 0) y = 0;
			reset_y:
			row = y * inv->invCol;
			if (row < inv->top)
				SIT_SetValues(inv->scroll, SIT_ScrollPos, y, NULL), y = 0, top = 0;
			else if (row >= inv->top + inv->invRow * inv->invCol)
				SIT_SetValues(inv->scroll, SIT_ScrollPos, y - inv->invRow + 1, NULL), y = inv->invRow - 1, top = 0;
			break;
		case SITK_Down:
			y ++; if (y > max) y = max;
			goto reset_y;
		case SITK_PrevPage:
			if (max < inv->invRow) return 0;
			y -= inv->invRow;
			goto reset_y;
		case SITK_NextPage:
			if (max < inv->invRow) return 0;
			y += inv->invRow;
			goto reset_y;
		case SITK_Left:
			x --; if (x < 0) x = 0;
			break;
		case SITK_Right:
			x ++; if (x >= inv->invCol) x = inv->invCol - 1;
			break;
		case SITK_Home:
			if (msg->flags & SITK_FlagCtrl)
			{
				if (inv->top > 0)
					SIT_SetValues(inv->scroll, SIT_ScrollPos, 0, NULL);
				y = x = top = 0;
			}
			else x = 0;
			break;
		case SITK_End:
			if (msg->flags & SITK_FlagCtrl)
			{
				if (max >= inv->invRow)
					SIT_SetValues(inv->scroll, SIT_ScrollPos, max - inv->invRow + 1, NULL);
				top = 0;
				x = inv->invCol - 1;
				y = inv->invRow - 1;
			}
			else x = inv->invCol - 1;
			break;
		case SITK_Space:
			switch (inv->movable) {
			case INV_PICK_ONLY:   inventoryAddTo(inv); break;
			case INV_SINGLE_DROP: inventoryTransferFrom(inv); break;
			case INV_SELECT_ONLY:
				x += y * inv->invCol;
				if (x < inv->itemsNb)
				{
					inv->items[x].added ^= 1;
					SIT_ForceRefresh();
					SIT_ApplyCallback(w, NULL, SITE_OnChange);
				}
			}
			return 0;
		default: return 0;
		}
		inv->curX = x;
		inv->curY = y - top;
		SIT_SetValues(inventories.toolTip, SIT_Visible, False, NULL);
		SIT_ForceRefresh();
	}
	return 0;
}

static int inventoryFocus(SIT_Widget w, APTR cd, APTR ud)
{
	MCInventory inv = ud;
	if (cd)
	{
		if (inv->curX < 0)
			inv->curX = inv->curY = 0, SIT_ForceRefresh();
	}
	else if (inv->curX >= 0)
	{
		inv->curX = -1;
		SIT_ForceRefresh();
	}
	return 0;
}

static void inventorySetCellSize(MCInventory inv, int max)
{
	/* this inventory will constraint the size of items displayed */
	SIT_GetValues(inv->cell, SIT_Padding, inventories.padding, NULL);
	/* same scale than player toolbar... */
	inventories.cellSz = roundf(globals.width * 17 * ITEMSCALE / (3 * 182.f));
	/* ... unless it doesn't fit within window's height */
	if (inventories.cellSz * max > globals.height)
		inventories.cellSz = globals.height / max;
	inventories.itemSz = inventories.cellSz - inventories.padding[0] - inventories.padding[2];
}

static int inventoryTransfer(SIT_Widget w, APTR cd, APTR ud)
{
	MCInventory inv = cd;
	MCInventory target;
	Item        source = inv->items + (int) ud;
	Item        dest, dump;
	int         canMove = (inv->movable & INV_PICK_ONLY) == 0;
	int         id = inv->groupId;
	int         i, slot, max;

	for (i = 0; i < inventories.groupCount && inventories.groups[i]->groupId == id; i ++);
	next_target:
	target = inventories.groups[i];

	/* first: dump items from <source> into stack of same item id of <target> inventory */
	for (slot = 0, max = target->itemsNb, dest = target->items, dump = NULL; slot < max; slot ++, dest ++)
	{
		/* find first free slot */
		if (dump == NULL && dest->id == 0)
			dump = dest;

		if (dest->id == source->id && canMove)
		{
			source->count = itemAddCount(dest, source->count);

			if (source->count == 0)
			{
				/* everything transfered */
				memset(source, 0, sizeof *source);
				return 1;
			}
		}
	}

	if (dump == NULL && i < inventories.groupCount)
	{
		/* check if there is another inventory we can dump this into */
		for (i ++; i < inventories.groupCount; i ++)
		{
			if (inventories.groups[i]->groupId != id)
			{
				int enabled = 1;
				SIT_GetValues(inventories.groups[i]->canvas, SIT_Enabled, &enabled, NULL);
				if (enabled) goto next_target;
			}
		}
	}

	if (source->count > 0 && dump)
	{
		/* free slot available: transfer all what's remaining here */
		dump[0] = source[0];
		if (canMove)
			memset(source, 0, sizeof *source);
	}

	return 1;
}

void inventoryInit(MCInventory inv, SIT_Widget canvas, int max)
{
	inv->cell = SIT_CreateWidget("td", SIT_HTMLTAG, canvas, SIT_Visible, False, NULL);
	inv->canvas = canvas;
	inv->curX = -1;
	inv->top  = 0;

	if (max > 0)
	{
		inventorySetCellSize(inv, max);
		inventories.maxItemSize = max;
	}

	SIT_AddCallback(canvas, SITE_OnPaint,     inventoryRender,   inv);
	SIT_AddCallback(canvas, SITE_OnClickMove, inventoryMouse,    inv);
	SIT_AddCallback(canvas, SITE_OnMouseOut,  inventoryMouseOut, inv);
	SIT_AddCallback(canvas, SITE_OnRawKey,    inventoryKeyboard, inv);
	SIT_AddCallback(canvas, SITE_OnFocus,     inventoryFocus,    inv);
	SIT_AddCallback(canvas, SITE_OnBlur,      inventoryFocus,    inv);

	SIT_SetValues(canvas, SIT_Width, inv->invCol * inventories.cellSz, SIT_Height, inv->invRow * inventories.cellSz, NULL);

	if (inv->scroll)
		SIT_AddCallback(inv->scroll, SITE_OnChange, inventorySetTop, inv);

	SIT_Widget tip = SIT_GetById(canvas, "/info");
	if (tip)
		inventories.toolTip = tip;

	if (inv->movable & INV_TRANSFER)
		inventories.transfer = inventoryTransfer;

	if (inv->groupId == 0)
	{
		/* annonymous group : insert at end */
		inventories.groupOther ++;
		inventories.groups[10 - inventories.groupOther] = inv;
	}
	else inventories.groups[inventories.groupCount++] = inv;
}

/* screen resized: manually change some hardcoded values */
void inventoryResize(void)
{
	int i, total = inventories.groupCount + inventories.groupOther;
	if (total > 0)
		inventorySetCellSize(inventories.groups[9], inventories.maxItemSize);
	for (i = 0; i < total; i ++)
	{
		MCInventory inv = inventories.groups[i >= inventories.groupCount ? 9 - i + inventories.groupCount : i];
		SIT_SetValues(inv->canvas, SIT_Width, inv->invCol * inventories.cellSz, SIT_Height, inv->invRow * inventories.cellSz, NULL);
	}
}

/*
 * container manipulation (mostly needed by hoppers)
 */

/* old save file (<1.8, I think), saved items in numeric format (as TAG_Short): convert these to strings */
STRPTR inventoryItemName(NBTFile nbt, int offset, TEXT itemId[16])
{
	NBTHdr hdr = NBT_Hdr(nbt, offset);
	if (hdr->type != TAG_String)
	{
		sprintf(itemId, "%d", NBT_GetInt(nbt, offset, 0));
		return itemId;
	}
	return NBT_Payload(nbt, offset);
}

/* read TileEntities.Items from a container */
void inventoryDecodeItems(Item container, int count, NBTHdr hdrItems)
{
	DATA8 mem;
	int   index, max;

	memset(container, 0, sizeof *container * count);

	if (hdrItems)
	for (index = 0, max = hdrItems->count, mem = NBT_MemPayload(hdrItems); index < max; index ++)
	{
		NBTIter_t properties;
		NBTFile_t nbt = {.mem = mem};
		TEXT      itemId[16];
		Item_t    item;
		int       off;
		memset(&item, 0, sizeof item);
		NBT_IterCompound(&properties, nbt.mem);
		item.tile = nbt.mem;
		item.x = index;
		while ((off = NBT_Iter(&properties)) >= 0)
		{
			switch (FindInList("id,Slot,Count,Damage", properties.name, 0)) {
			case 0:  item.id = itemGetByName(inventoryItemName(&nbt, off, itemId), True); break;
			case 1:  item.slot = NBT_GetInt(&nbt, off, 255); break;
			case 2:  item.count = NBT_GetInt(&nbt, off, 1); break;
			case 3:  item.uses = NBT_GetInt(&nbt, off, 0); break;
			default: item.extraF = 1;
			}
		}
		if (isBlockId(item.id))
		{
			/* select a state with an inventory model */
			BlockState state = blockGetById(item.id);
			if (state->invId == 0)
			{
				Block b = &blockIds[item.id >> 4];
				if (b->special == BLOCK_TALLFLOWER)
					/* really weird state values :-/ */
					item.id += 10;
				else
					item.id = (item.id & ~15) | b->invState;
			}
		}
		if (item.uses > 0 && itemMaxDurability(item.id) < 0)
			/* damage means meta-data from these items :-/ */
			item.id += item.uses, item.uses = 0;
		if (item.slot < count)
		{
			off = item.slot;
			item.slot = 0;
			container[off] = item;
		}
		mem += properties.offset;
	}
}

static void inventoryItemToNBT(NBTFile ret, Item item, int slot)
{
	TEXT     itemId[128];
	ItemID_t id = item->id;
	uint16_t data;

	/* data (or damage) is only to get a specific inventory model: not needed in NBT */
	if (isBlockId(id))
	{
		Block b = &blockIds[id>>4];
		data = id & 15;
		if (b->invState == data)
			data = 0;
		else if (b->special == BLOCK_TALLFLOWER)
			data -= 10;
	}
	else data = ITEMMETA(id);

	NBT_Add(ret,
		TAG_String, "id",     itemGetTechName(id, itemId, sizeof itemId, False),
		TAG_Byte,   "Slot",   slot,
		TAG_Short,  "Damage", itemMaxDurability(item->id) > 0 ? item->uses : data,
		TAG_Byte,   "Count",  item->count,
		TAG_End
	);
	if (item->extraF)
	{
		/* merge entries we didn't care about */
		NBTIter_t iter;
		DATA8     mem;
		int       off;
		NBT_IterCompound(&iter, mem = item->tile);
		while ((off = NBT_Iter(&iter)) >= 0)
		{
			if (FindInList("id,Slot,Count,Damage", iter.name, 0) >= 0)
				/* these one already are */
				continue;

			NBT_Add(ret, TAG_Raw_Data, NBT_HdrSize(mem+off), mem + off, TAG_End);
		}
	}
	NBT_Add(ret, TAG_Compound_End);
}

/* convert a list of <items> into an NBT stream */
Bool inventorySerializeItems(ChunkData cd, int offset, STRPTR listName, Item items, int itemCount, NBTFile ret)
{
	TEXT itemId[128];
	int  i;

	memset(ret, 0, sizeof *ret);
	ret->page = 511;

	if (cd)
	{
		Chunk c = cd->chunk;
		DATA8 tile = chunkGetTileEntity(cd, offset);

		if (tile)
		{
			/* quote tags from original tile entity */
			NBTIter_t iter;
			NBT_IterCompound(&iter, tile);
			while ((i = NBT_Iter(&iter)) >= 0)
			{
				if (strcasecmp(iter.name, listName))
					NBT_Add(ret, TAG_Raw_Data, NBT_HdrSize(tile+i), tile + i, TAG_End);
			}
		}
		else
		{
			/* not yet created: add required fields */
			NBT_Add(ret,
				TAG_String, "id", itemGetTechName(ID(cd->blockIds[offset], 0), itemId, sizeof itemId, False),
				TAG_Int,    "x",  (offset & 15) + c->X,
				TAG_Int,    "y",  (offset >> 8) + cd->Y,
				TAG_Int,    "z",  ((offset >> 4) & 15) + c->Z,
				TAG_End
			);
		}
	}

	int count;
	for (i = 0, count = 0; i < itemCount; count += items[i].id > 0, i ++);

	NBT_Add(ret,
		TAG_List_Compound, listName, count,
		TAG_End
	);

	for (i = 0; i < itemCount; i ++, items ++)
	{
		if (items->id > 0)
			inventoryItemToNBT(ret, items, i);
	}
	if (cd)
		NBT_Add(ret, TAG_List_End, TAG_Compound_End);

	return True;
}

enum
{
	ACT_NOCHANGES,
	ACT_ADDITEM,
	ACT_DELITEM,
	ACT_CHGCOUNT
};

/* rewrite NBT record with minimal modification of the raw NBT stream */
static void inventoryUpdate(BlockIter iter, Item items, int count, int action, DATA8 listtile, Item item)
{
	NBTFile_t nbt;

	Chunk c = iter->cd->chunk;
	DATA8 tile = chunkUpdateTileEntity(iter->cd, iter->offset);

	if (tile && ! (c->nbt.mem <= tile && tile <= c->nbt.mem + c->nbt.max))
	{
		/* modify existing tile directly */
		nbt.mem = tile;
		nbt.page = 511;
		nbt.alloc = 0;
		nbt.usage = NBT_Size(tile) + 4;
		nbt.max = (nbt.usage + 511) & ~511;
		switch (action) {
		case ACT_ADDITEM:
			{
				struct NBTFile_t sub = {.page = 127};
				inventoryItemToNBT(&sub, item, item - items);
				NBT_Insert(&nbt, "Items", TAG_InsertAtEnd, &sub);
				NBT_Free(&sub);
			}
			break;
		case ACT_DELITEM:
			NBT_Delete(&nbt, listtile - tile, item->x);
			break;
		case ACT_CHGCOUNT:
			nbt.mem = item->tile;
			NBT_SetInt(&nbt, NBT_FindNode(&nbt, 0, "Count"), item->count);
			nbt.mem = tile;
		}
	}
	else /* tile still points to raw NBT stream */
	{
		inventorySerializeItems(iter->cd, iter->offset, "Items", items, count, &nbt);
	}
	chunkAddTileEntity(iter->cd, iter->offset, nbt.mem);
}


DATA8 inventoryLocateItems(BlockIter iter)
{
	DATA8 tile = chunkGetTileEntity(iter->cd, iter->offset);

	if (tile)
	{
		NBTFile_t nbt = {.mem = tile};
		int offset = NBT_FindNode(&nbt, 0, "Items");
		if (offset >= 0)
			return tile + offset;
	}
	return NULL;
}

int inventoryTryTransfer(Item inventory, Item grab, int max)
{
	Item item;
	int  i;
	for (i = 0, item = inventory; i < max; i ++, item ++)
	{
		ItemDesc desc;
		if (item->id > 0 && item->count < ((desc = itemGetById(item->id)) ? desc->stack : 64))
		{
			if (grab->id == item->id)
			{
				item->count ++;
				grab->count --;
				if (grab->count == 0)
				{
					grab->id = 0;
					return ACT_DELITEM | (ACT_CHGCOUNT << 12) | (i << 4);
				}
				else return ACT_CHGCOUNT | (ACT_CHGCOUNT << 12) | (i << 4);
			}
		}
	}
	/* can't increase the stack size: check for a free slot */
	for (i = 0, item = inventory; i < max; i ++, item ++)
	{
		if (item->id == 0)
		{
			item[0] = grab[0];
			item->count = 1;
			grab->count --;
			if (grab->count == 0)
			{
				grab->id = 0;
				return ACT_DELITEM | (ACT_ADDITEM << 12) | (i << 4);
			}
			else return ACT_CHGCOUNT | (ACT_ADDITEM << 12) | (i << 4);
		}
	}
	return ACT_NOCHANGES;
}

/* try to grab one item from <rc> and store it in <dst> (ie: hopper logic) */
Bool inventoryPushItem(BlockIter src, BlockIter dst)
{
	DATA8 order, srcTile, dstTile;
	int   srcSlot, dstSlot;
	Item  srcInv,  dstInv, item, grab;
	int   i, j, total, action;

	srcSlot = blockIds[src->blockIds[src->offset]].containerSize;
	dstSlot = blockIds[dst->blockIds[dst->offset]].containerSize;

	/* XXX need to filter out ender_chest and furnace */
	if (srcSlot <= 0 || dstSlot <= 0)
		return False;

	action = ACT_NOCHANGES;
	srcInv = alloca(srcSlot * sizeof *srcInv);
	dstInv = alloca(dstSlot * sizeof *dstInv);
	order  = alloca(srcSlot);

	inventoryDecodeItems(srcInv, srcSlot, (NBTHdr) (srcTile = inventoryLocateItems(src)));

	/* is there an item in source inventory? */
	for (i = 0, item = srcInv; i < srcSlot && item->id == 0; i ++, item ++);
	if (i == srcSlot) return 0;

	inventoryDecodeItems(dstInv, dstSlot, (NBTHdr) (dstTile = inventoryLocateItems(dst)));

	/* make item grabbing somewhat random */
	for (i = 0; i < srcSlot; order[i] = i, i ++);
	for (i = total = 0, item = srcInv; i < srcSlot; i ++, item ++)
	{
		int k;
		total += item->count;
		j = rand() % srcSlot;
		k = rand() % srcSlot;
		if (j != k)
			swap(order[j], order[k]);
	}

	for (j = 0; j < srcSlot; j ++)
	{
		/* try to push a random item */
		grab = srcInv + order[j];
		if (grab->id > 0)
		{
			action = inventoryTryTransfer(dstInv, grab, dstSlot);
			/* no free slot for this item, check the next one then */
			if (action != ACT_NOCHANGES)
				goto break_all;
		}
	}

	return 0;

	/* one item was transferred: update NBT records */
	break_all:

	inventoryUpdate(src, srcInv, srcSlot, action & 15,  srcTile, grab);
	inventoryUpdate(dst, dstInv, dstSlot, action >> 12, dstTile, dstInv + ((action >> 4) & 255));

	return True;
}

/* <dst> points to a hopper: used by logic to grab world item */
Bool inventoryPushWorldItem(BlockIter dst, Item item)
{
	Item_t inventory[5];
	DATA8  tile;

	inventoryDecodeItems(inventory, DIM(inventory), (NBTHdr) (tile = inventoryLocateItems(dst)));

	int action = inventoryTryTransfer(inventory, item, DIM(inventory));

	if (action > 0)
	{
		inventoryUpdate(dst, inventory, DIM(inventory), action >> 12, tile, inventory + ((action >> 4) & 255));
		return True;
	}
	return False;
}
