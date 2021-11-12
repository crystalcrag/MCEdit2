/*
 * interface.c: handle user interface for MCEdit (based on SITGL); contains code for following interfaces:
 *  - generic code for handling inventories
 *  - creative inventory editor
 *  - chest/furnace/dropper/dispenser editor
 *  - text sign editor
 *  - analyze block window
 *  - fill/replace blocks
 *  - geometric brush fill
 *  - partial delete
 *  - painting selection
 *  - world info
 *
 * Written by T.Pierron, oct 2020
 */

#define MCUI_IMPL
#include <glad.h>
#include <malloc.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include "nanovg.h"
#include "nanovg_gl_utils.h"
#include "SIT.h"
#include "entities.h"
#include "interface.h"
#include "selection.h"
#include "mapUpdate.h"
#include "render.h"
#include "player.h"
#include "sign.h"
#include "globals.h"

static struct MCInterface_t mcui;
static struct MCInventory_t selfinv = {.invRow = 3, .invCol = MAXCOLINV, .groupId = 1, .itemsNb = MAXCOLINV * 3};
static struct MCInventory_t toolbar = {.invRow = 1, .invCol = MAXCOLINV, .groupId = 1, .itemsNb = MAXCOLINV};
static int category[] = {BUILD, DECO, REDSTONE, CROPS, RAILS, 0};

/*
 * before displaying a user interface, take a snapshot of current framebuffer and
 * use this as a background: prevent from continuously rendering the same frame
 * (and will also completely disable any interaction with voxel space)
 */
void mcuiTakeSnapshot(int width, int height)
{
	if (mcui.glBack == 0)
		glGenTextures(1, &mcui.glBack);

	glBindTexture(GL_TEXTURE_2D, mcui.glBack);
	if (mcui.width != width || mcui.height != height)
	{
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
		if (mcui.nvgImage)
			nvgUpdateImage(globals.nvgCtx, mcui.nvgImage, NULL);
	}
	/* copy framebuffer content into texture */
	glBindTexture(GL_TEXTURE_2D, mcui.glBack);
	glReadBuffer(GL_FRONT);
	glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, 0, 0, width, height, 0);
	glGenerateMipmap(GL_TEXTURE_2D);

	if (mcui.nvgImage == 0)
		mcui.nvgImage = nvgCreateImage(globals.nvgCtx, (const char *) mcui.glBack, NVG_IMAGE_FLIPY | NVG_IMAGE_GLTEX);

	mcui.width = width;
	mcui.height = height;

	TEXT style[32];
	sprintf(style, "background: id(%d)", mcui.nvgImage);
	SIT_SetValues(globals.app, SIT_Style|XfMt, style, NULL);
}

/*
 * inventory interface management: move/drag/split/draw items between inventories
 */
static int mcuiInventoryRender(SIT_Widget w, APTR cd, APTR ud)
{
	MCInventory inv = ud;
	float       x, y, curX, curY;
	int         i, j, sz = mcui.cellSz;
	uint8_t     select = inv->movable & INV_SELECT_ONLY;
	Item        item = inv->items + inv->top;
	int         max = inv->itemsNb - inv->top;

	SIT_GetValues(w, SIT_AbsX, &x, SIT_AbsY, &y, NULL);
	curX = inv->curX;
	curY = inv->curY;
	for (j = 0; j < inv->invRow; j ++)
	{
		for (i = 0; i < inv->invCol; i ++)
		{
			int x2 = i * sz;
			int y2 = j * sz;
			if (select && item->added)
			{
				/* selection underlay */
				nvgBeginPath(globals.nvgCtx);
				nvgRect(globals.nvgCtx, x+x2, y+y2, sz, sz);
				nvgFillColorRGBA8(globals.nvgCtx, "\x20\xff\x20\x7f");
				nvgFill(globals.nvgCtx);
			}
			if ((i == curX && j == curY) || (max > 0 && item->slot > 0))
			{
				/* cursor underlay */
				nvgBeginPath(globals.nvgCtx);
				nvgRect(globals.nvgCtx, x+x2, y+y2, sz, sz);
				nvgFillColorRGBA8(globals.nvgCtx, "\xff\xff\xff\x7f");
				nvgFill(globals.nvgCtx);
			}
			SIT_SetValues(inv->cell, SIT_X, x2, SIT_Y, y2, SIT_Width, sz, SIT_Height, sz, NULL);
			SIT_RenderNode(inv->cell);
			/* grab item to render */
			if (max > 0)
			{
				if (item->id == 0xffff)
				{
					/* custom item draw */
					inv->customDraw(w, (int[3]){x+x2, y+y2, sz}, item);
				}
				else if (item->id > 0)
				{
					Item render = mcui.items + mcui.itemRender ++;
					render[0] = item[0];
					render->x = x + x2 + mcui.padding[0]/2;
					render->y = mcui.height - (y + y2 + mcui.padding[1]/2) - mcui.itemSz;
				}
				item ++;
				max --;
			}
		}
	}
	return 0;
}

void mcuiSetTooltip(SIT_Widget toolTip, Item item, STRPTR extra)
{
	TEXT title[256];
	TEXT id[16];
	int  tag = NBT_FindNodeFromStream(item->extra, 0, "/tag.ench");
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
			itemDecodeEnchants(item->extra + tag, title, sizeof title);

		index = StrCat(title, sizeof title, index, "<br><dim>");

		/* check if this is an item container */
		int inventory = NBT_FindNodeFromStream(item->extra, 0, "/Items");

		if (inventory >= 0)
		{
			inventory = ((NBTHdr)(item->extra + inventory))->count;
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
static void mcuiRefreshTooltip(MCInventory inv)
{
	int index = inv->top + inv->curX + inv->curY * inv->invCol;
	if (index >= inv->itemsNb || inv->items[index].id == 0xffff)
	{
		SIT_SetValues(mcui.toolTip, SIT_Visible, False, NULL);
		return;
	}
	mcuiSetTooltip(mcui.toolTip, inv->items + index, NULL);
}

static int mcuiDragItem(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_OnMouse * msg = cd;
	switch (msg->state) {
	case SITOM_CaptureMove:
		mcui.drag.x = msg->x;
		mcui.drag.y = mcui.height - msg->y - mcui.itemSz;
		SIT_ForceRefresh();
		break;
	case SITOM_ButtonPressed:
		mcui.drag.id = 0;
		SIT_InitDrag(NULL);
		SIT_ForceRefresh();
	default: break;
	}
	return 1;
}

static void mcuiSplitItems(Item addCell)
{
	if (addCell->slot > 0 || (addCell->id > 0 && addCell->id != mcui.dragSplit.id))
		return;

	/* items need to filled in the order they were drawn */
	DATA8 slots, group;
	Item  list, eof;
	int   count, split, i;

	if (addCell->id == 0)
	{
		addCell[0] = mcui.dragSplit;
		addCell->count = addCell->added = 0;
	}
	addCell->slot = ++ mcui.selCount;
	count = mcui.dragSplit.count;
	split = mcui.dragOneItem ? 1 : count / mcui.selCount;
	if (split < 1) split = 1;

	slots = alloca(mcui.selCount * 2);
	group = slots + mcui.selCount;

	/* get all slots affected so far in a linear buffer */
	for (i = 0; i < mcui.groupCount; i ++)
	{
		MCInventory inv = mcui.groups[i];
		int j;
		if (inv->groupId != mcui.groupIdStart)
			continue;
		for (list = inv->items, eof = list + inv->itemsNb, j = 0; list < eof; list ++, j ++)
		{
			if (list->slot == 0) continue;
			uint8_t slot = list->slot-1;
			slots[slot] = j;
			group[slot] = i;
		}
	}

	for (i = 0; i < mcui.selCount; i ++)
	{
		MCInventory inv = mcui.groups[group[i]];
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
		i = SIT_InitDrag(mcuiDragItem);
		mcui.drag = mcui.dragSplit;
		mcui.drag.count = count;
		mcui.drag.x = i & 0xffff;
		mcui.drag.y = mcui.height - (i >> 16) - mcui.itemSz;
	}
	else mcui.drag.id = 0, SIT_InitDrag(NULL);
	SIT_ForceRefresh();
}

/* check if we double-clicked on an item: gather all the same item into one slot (up to stack limit) */
static void mcuiGrabAllItems(MCInventory inv, int index)
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
		for (i = 0; i < mcui.groupCount; i ++)
		{
			Item item, end;
			inv = mcui.groups[i];
			if (inv->groupId != groupId) continue;
			for (item = inv->items, end = item + inv->itemsNb; item < end; item ++)
			{
				if (mcui.drag.id != item->id) continue;
				item->count = itemAddCount(&mcui.drag, item->count);
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
static int mcuiInventoryMouse(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_OnMouse * msg = cd;
	MCInventory   inv = ud;
	Item          old;

	int cellx = msg->x / mcui.cellSz;
	int celly = msg->y / mcui.cellSz;
	switch (msg->state) {
	case SITOM_CaptureMove:
		if (cellx < 0 || cellx >= inv->invCol || celly < 0 || celly >= inv->invRow)
			return 0;
		if (mcui.selCount > 0)
		{
			mcuiSplitItems(inv->items + inv->top + cellx + celly * inv->invCol);
		}
		else if (inv->movable & INV_SELECT_ONLY)
		{
			/* drag selection */
			if (inv->curX != cellx || inv->curY != celly)
			{
				inv->curX = cellx;
				inv->curY = celly;
				old = inv->items + inv->top + cellx + celly * inv->invCol;
				old->added = mcui.dragOneItem;
				SIT_ForceRefresh();
			}
		}
		break;
	case SITOM_Move:
		if (inv->curX != cellx || inv->curY != celly)
		{
			/* item being dragged, but this inventory is not part of the group */
			if (mcui.groupIdStart > 0 && inv->groupId != mcui.groupIdStart)
				return 0;
			inv->curX = cellx;
			inv->curY = celly;
			if (mcui.selCount == 0)
			{
				mcuiRefreshTooltip(inv);
				SIT_ForceRefresh();
			}
			else mcuiSplitItems(inv->items + inv->top + cellx + celly * inv->invCol);
		}
		break;
	case SITOM_ButtonReleased:
		if (mcui.selCount > 0)
		{
			/* clear slots */
			int i;
			for (i = 0; i < mcui.groupCount; i ++)
			{
				Item list, eof;
				inv = mcui.groups[i];
				for (list = inv->items, eof = list + inv->itemsNb; list < eof; list->slot = 0, list ++);
			}
			mcui.selCount = 0;
			mcui.groupIdStart = 0;
			SIT_ForceRefresh();
			if (mcui.drag.id == 0)
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
				mcui.drag = inv->items[cellx];
				/* will be clamped to max stack */
				itemAddCount(&mcui.drag, 64);
				cellx = SIT_InitDrag(mcuiDragItem);
				mcui.drag.x = cellx & 0xffff;
				mcui.drag.y = mcui.height - (cellx >> 16) - mcui.itemSz;
				SIT_ForceRefresh();
				return -1;
			}
			break;
		case SITOM_ButtonRight:
			if (inv->movable & INV_SELECT_ONLY) return 0;
			if (inv->groupId && mcui.drag.id == 0 && inv->items[cellx].count > 0)
			{
				/* grab half the stack */
				Item cur = inv->items + cellx;
				int  cnt = (cur->count + 1) >> 1;

				cur->count -= cnt;
				mcui.drag = *cur;
				mcui.drag.count = cnt;
				if (cur->count == 0)
				{
					memset(cur, 0, sizeof *cur);
					SIT_ApplyCallback(w, 0, SITE_OnChange);
				}
				cellx = SIT_InitDrag(mcuiDragItem);
				mcui.drag.x = cellx & 0xffff;
				mcui.drag.y = mcui.height - (cellx >> 16) - mcui.itemSz;
				return -1;
			}
			else if ((inv->movable & INV_PICK_ONLY) == 0 && mcui.drag.id > 0)
			{
				/* initiate drag, but only distribute 1 item at time */
				celly = 1;
				mcui.dragOneItem = 1;
				old = &inv->items[cellx];
				if (old->id == 0 || old->id == mcui.drag.id)
					goto init_drag;
			}
			else mcui.drag.id = 0;
			break;
		case SITOM_ButtonLeft:
			if (inv->movable & INV_SELECT_ONLY)
			{
				/* toggle selected flag for item hovered */
				old = &inv->items[cellx];
				if (old->id > 0)
				{
					old->added ^= 1;
					mcui.dragOneItem = old->added;
					SIT_ForceRefresh();
					return 2;
				}
			}
			else if (msg->flags & SITK_FlagShift)
			{
				if (mcui.cb)
				{
					/* transfer item to other inventory usually */
					if (mcui.cb(w, inv, (APTR) cellx))
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
			else if (mcui.drag.id > 0)
			{
				if ((inv->movable & INV_PICK_ONLY) == 0)
				{
					/* merge stack if same id */
					old = &inv->items[cellx];
					SIT_ApplyCallback(w, (APTR) (int) mcui.drag.id, SITE_OnChange);
					if (old->id == 0 || old->id == mcui.drag.id)
					{
						/* but only to stack limit */
						mcui.dragOneItem = 0;
						celly = inv->movable & INV_SINGLE_DROP ? 1 : mcui.drag.count;
						init_drag:
						mcui.dragSplit = mcui.drag;
						if (old->id == 0)
							*old = mcui.drag, old->count = old->added = celly, mcui.drag.count -= celly;
						else
							mcui.drag.count -= celly - itemAddCount(old, celly);
						/* start spliting items by drawing on inventory slots */
						mcui.groupIdStart = inv->groupId;
						if (mcui.drag.count == 0)
							mcui.drag.id = 0;
						mcui.selCount = 1;
						inv->items[cellx].slot = 1;
						SIT_ForceRefresh();
						return 2;
					}
					else if (old->id > 0)
					{
						/* click on a slot with different items in it: exchange with item being dragged */
						ItemBuf buf = mcui.drag;
						mcui.drag.id = old->id;
						mcui.drag.count = old->count;
						mcui.drag.uses = old->uses;
						mcui.drag.extra = old->extra;
						*old = buf;
						if (inv->movable & INV_SINGLE_DROP)
							old->count = 1;
						SIT_ForceRefresh();
					}
					return -1;
				}
				else if (mcui.drag.id == inv->items[cellx].id)
				{
					/* click twice on the same block: add 1 to stack */
					if (itemAddCount(&mcui.drag, 1) == 0)
						SIT_ForceRefresh();

					return 1;
				}
				mcui.drag.id = 0;
				SIT_InitDrag(NULL);
				SIT_ForceRefresh();
			}
			else if (cellx < inv->itemsNb)
			{
				/* pick an item */
				mcui.drag = inv->items[cellx];
				if (mcui.drag.id == 0) break;
				if (inv->groupId)
				{
					memset(inv->items + cellx, 0, sizeof (struct Item_t));
					SIT_ApplyCallback(w, 0, SITE_OnChange);
				}
				cellx = SIT_InitDrag(mcuiDragItem);
				mcui.drag.x = cellx & 0xffff;
				mcui.drag.y = mcui.height - (cellx >> 16) - mcui.itemSz;
				mcuiGrabAllItems(inv, cellx);
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

static int mcuiInventoryMouseOut(SIT_Widget w, APTR cd, APTR ud)
{
	MCInventory inv = ud;
	inv->curX = -1;
	SIT_ForceRefresh();
	return 1;
}

/* SITE_OnPaint handler for inventory items */
static int mcuiGrabItemCoord(SIT_Widget w, APTR cd, APTR ud)
{
	/* they need to be rendered by opengl in a separate pass: here, we just grab coord */
	SIT_OnPaint * paint = cd;
	Item          item;
	APTR          blockId;
	int           padding[4];

	SIT_GetValues(w, SIT_UserData, &blockId, SIT_Padding, padding, NULL);

	/* note: itemRender is set to 0 before rendering loop starts */
	item = mcui.items + mcui.itemRender ++;
	item->x = paint->x + padding[0]/2;
	item->y = mcui.height - (paint->y + padding[1]/2) - mcui.itemSz;
	item->id = (int) blockId;
	item->count = 1;
	return 0;
}

/* start of a refresh phase */
void mcuiInitDrawItems(void)
{
	mcui.itemRender = 0;
}

/* refresh phase done: render items */
void mcuiDrawItems(void)
{
	renderItems(mcui.items, mcui.itemRender, mcui.itemSz);
	if (mcui.drag.id > 0)
	{
		ItemBuf item = mcui.drag;
		item.x -= mcui.itemSz/2;
		item.y += mcui.itemSz/2;
		renderItems(&item, 1, mcui.itemSz);
	}
}

void mcuiResetScrollbar(MCInventory inv)
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

/* SITE_OnChange on tab: click on a different tab */
static int mcuiChangeTab(SIT_Widget w, APTR cd, APTR ud)
{
	MCInventory inv = ud;
	inv->top = 0;
	inv->itemsNb = itemGetInventoryByCat(inv->items, category[mcui.curTab = (int)cd]);
	mcuiResetScrollbar(inv);
	return 1;
}

/* SITE_OnScroll: scrollbar position changed */
static int mcuiSetTop(SIT_Widget w, APTR cd, APTR ud)
{
	MCInventory inv = ud;
	inv->top = (int) cd * inv->invCol;
	int visible = 0;
	SIT_GetValues(mcui.toolTip, SIT_Visible, &visible, NULL);
	if (inv->curX >= 0 && visible) mcuiRefreshTooltip(inv);
	return 1;
}

/* not defined by mingw... */
char * strcasestr(const char * hayStack, const char * needle)
{
	int length = strlen(needle);
	int max    = strlen(hayStack);

	if (length > max)
		return NULL;

	/* not the most optimized function ever, but on the other hand it doesn't have to be */
	for (max -= length-1; max > 0; max --, hayStack ++)
		if  (strncasecmp(hayStack, needle, length) == 0) return (char *) hayStack;

	return NULL;
}

/* SITE_OnChange on search: filter items based on user input */
static int mcuiFilterItems(SIT_Widget w, APTR cd, APTR ud)
{
	/* no matter what tab we are on, we'll scan all items */
	MCInventory inv   = ud;
	int         count = itemGetInventoryByCat(inv->items, 0), i;
	Item        items = inv->items;
	STRPTR      match = cd;

	if (*match == 0)
	{
		/* no text given: show original items from tab then */
		count = itemGetInventoryByCat(items, category[mcui.curTab]);
	}
	else for (i = 0; i < count; )
	{
		STRPTR name, tech;
		int    id = items[i].id;

		/* there is about 700 items: no need to implement something more complicated than a linear scan */
		if (isBlockId(id))
		{
			BlockState b = blockGetById(id);
			name = b->name;
			tech = blockIds[b->id>>4].tech;
		} else {
			ItemDesc item = itemGetById(id);
			name = item->name;
			tech = item->tech;
		}

		if (strcasestr(name, match) == NULL && strcasestr(tech, match) == NULL)
		{
			memmove(items + i, items + i + 1, (count - i - 1) * sizeof *items);
			count --;
		}
		else i ++;
	}

	inv->itemsNb = count;
	mcuiResetScrollbar(inv);

	return 1;
}

/* add/remove items from inventory using the keyboard */
static void mcuiAddToInventory(MCInventory inv)
{
	int pos = inv->top + inv->curX + inv->curY * inv->invCol;

	if (pos < inv->itemsNb)
	{
		int id = inv->items[pos].id, i, j;
		Item free;
		if (id == 0) return;

		/* check first if this item is already in the inventory */
		for (i = mcui.groupCount - 1, free = NULL; i >= 0; i --)
		{
			MCInventory copyTo = mcui.groups[i];
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
static void mcuiTransferFromInventory(MCInventory inv)
{
	int pos = inv->top + inv->curX + inv->curY * inv->invCol;
	if (pos < inv->itemsNb)
	{
		/* check if there is another inventory group */
		Item transfer = NULL;
		int  i, j;
		for (i = 0; i < mcui.groupCount; i ++)
		{
			MCInventory dest = mcui.groups[i];
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
static int mcuiInventoryKeyboard(SIT_Widget w, APTR cd, APTR ud)
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
			case INV_PICK_ONLY:   mcuiAddToInventory(inv); break;
			case INV_SINGLE_DROP: mcuiTransferFromInventory(inv); break;
			case INV_SELECT_ONLY:
				x += y * inv->invCol;
				if (x < inv->itemsNb) inv->items[x].added ^= 1, SIT_ForceRefresh();;
			}
			return 0;
		default: return 0;
		}
		inv->curX = x;
		inv->curY = y - top;
		SIT_SetValues(mcui.toolTip, SIT_Visible, False, NULL);
		SIT_ForceRefresh();
	}
	return 0;
}

static int mcuiInventoryFocus(SIT_Widget w, APTR cd, APTR ud)
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


void mcuiInitInventory(SIT_Widget canvas, MCInventory inv, int max)
{
	inv->cell = SIT_CreateWidget("td", SIT_HTMLTAG, canvas, SIT_Visible, False, NULL);
	inv->curX = -1;
	inv->top  = 0;

	if (max > 0)
	{
		/* this inventory will constraint the size of items displayed */
		SIT_GetValues(inv->cell, SIT_Padding, mcui.padding, NULL);
		/* same scale than player toolbar... */
		mcui.cellSz = roundf(mcui.width * 17 * ITEMSCALE / (3 * 182.f));
		/* ... unless it doesn't fit within window's height */
		if (mcui.cellSz * max > mcui.height)
			mcui.cellSz = mcui.height / max;
		mcui.itemSz = mcui.cellSz - mcui.padding[0] - mcui.padding[2];
	}

	SIT_AddCallback(canvas, SITE_OnPaint,     mcuiInventoryRender,   inv);
	SIT_AddCallback(canvas, SITE_OnClickMove, mcuiInventoryMouse,    inv);
	SIT_AddCallback(canvas, SITE_OnMouseOut,  mcuiInventoryMouseOut, inv);
	SIT_AddCallback(canvas, SITE_OnRawKey,    mcuiInventoryKeyboard, inv);
	SIT_AddCallback(canvas, SITE_OnFocus,     mcuiInventoryFocus,    inv);
	SIT_AddCallback(canvas, SITE_OnBlur,      mcuiInventoryFocus,    inv);

	SIT_SetValues(canvas, SIT_Width, inv->invCol * mcui.cellSz, SIT_Height, inv->invRow * mcui.cellSz, NULL);

	if (inv->scroll)
		SIT_AddCallback(inv->scroll, SITE_OnScroll, mcuiSetTop, inv);

	if (inv->groupId > 0)
		mcui.groups[mcui.groupCount++] = inv;
}

/* SITE_OnActivate on exch button */
static int mcuiExchangeLine(SIT_Widget w, APTR cd, APTR ud)
{
	Inventory player = ud;
	ItemBuf   items[MAXCOLINV];
	Item      line;
	STRPTR    name;

	SIT_GetValues(w, SIT_Name, &name, NULL);

	line = player->items + (name[4] - '0') * MAXCOLINV;

	memcpy(items, line, sizeof items);
	memcpy(line, player->items, sizeof items);
	memcpy(player->items, items, sizeof items);

	return 1;
}

/* SITE_OnActivate on clear button */
static int mcuiClearAll(SIT_Widget w, APTR cd, APTR ud)
{
	if (mcui.drag.id == 0)
	{
		Inventory player = ud;
		memset(player->items, 0, sizeof player->items);
	}
	else
	{
		mcui.drag.id = 0;
		SIT_InitDrag(NULL);
		SIT_ForceRefresh();
	}
	return 1;
}

/* SITE_OnClick on clear button */
static int mcuiCancelDrag(SIT_Widget w, APTR cd, APTR ud)
{
	/* will prevent from clearing the whole inventory if clicked with an item being dragged */
	return 1;
}

/*
 * creative inventory editor
 */
void mcuiCreateInventory(Inventory player)
{
	static TEXT tip[] = "Exchange row with toolbar";

	SIT_Widget diag = SIT_CreateWidget("inventory", SIT_DIALOG + SIT_EXTRA(itemGetInventoryByCat(NULL, 0) * sizeof (ItemBuf)), globals.app,
		SIT_DialogStyles, SITV_Plain | SITV_Modal,
		NULL
	);

	SIT_CreateWidgets(diag,
		"<tab name=items left=FORM right=FORM top=FORM bottom=FORM tabSpace=4 tabActive=", mcui.curTab, "tabStr='\t\t\t\t\t'>"
		" <editbox name=search right=FORM buddyLabel=", "Search:", NULL, ">"
		" <canvas composited=1 name=inv.inv left=FORM top=WIDGET,search,0.5em nextCtrl=LAST/>"
		" <scrollbar width=1.2em name=scroll.inv wheelMult=1 top=OPPOSITE,inv,0 bottom=OPPOSITE,inv,0 right=FORM>"
		" <label name=msg title='Player inventory:' top=WIDGET,inv,0.3em>"
		" <canvas composited=1 name=player.inv top=WIDGET,msg,0.3em  nextCtrl=LAST/>"
		" <canvas composited=1 name=tb.inv left=FORM top=WIDGET,player,0.5em  nextCtrl=LAST/>"
		" <button name=exch1.exch nextCtrl=NONE top=OPPOSITE,player right=FORM tooltip=", tip, "maxWidth=scroll>"
		" <button name=exch2.exch nextCtrl=NONE top=WIDGET,exch1 right=FORM tooltip=", tip, "maxWidth=exch1>"
		" <button name=exch3.exch nextCtrl=NONE top=WIDGET,exch2 right=FORM tooltip=", tip, "maxWidth=exch2>"
		" <button name=del.exch   nextCtrl=NONE top=OPPOSITE,tb right=FORM title=X tooltip='Clear inventory' maxWidth=exch3>"
		"</tab>"
		"<tooltip name=info delayTime=", SITV_TooltipManualTrigger, " displayTime=10000 toolTipAnchor=", SITV_TooltipFollowMouse, ">"
	);

	SIT_SetAttributes(diag, "<searchtxt top=MIDDLE,search><inv right=WIDGET,scroll,0.2em>");

	/* callbacks registration */
	mcui.toolTip = SIT_GetById(diag, "info");
	mcui.selCount = 0;
	mcui.groupCount = 0;
	mcui.cb = NULL;

	static struct MCInventory_t mcinv = {.invRow = 6, .invCol = MAXCOLINV, .movable = INV_PICK_ONLY};

	SIT_GetValues(diag, SIT_UserData, &mcui.allItems, NULL);
	mcinv.items   = mcui.allItems;
	mcinv.itemsNb = itemGetInventoryByCat(mcui.allItems, category[mcui.curTab]);
	mcinv.scroll  = SIT_GetById(diag, "scroll");
	selfinv.items = player->items + MAXCOLINV;
	toolbar.items = player->items;

	mcuiInitInventory(SIT_GetById(diag, "inv"),    &mcinv,   6 + 3 + 2 + 2 + 3);
	mcuiInitInventory(SIT_GetById(diag, "player"), &selfinv, 0);
	mcuiInitInventory(SIT_GetById(diag, "tb"),     &toolbar, 0);

	mcuiResetScrollbar(&mcinv);

	/* icons for tab: will be rendered as MC items */
	SIT_Widget tab  = SIT_GetById(diag, "items");
	SIT_Widget find = SIT_GetById(diag, "search");
	int i;
	for (i = 0; i < 6; i ++)
	{
		/* tab icons:           build     deco        redstone       crops          rails      search/all */
		static ItemID_t blockId[] = {ID(45,0), ID(175,15), ITEMID(331,0), ITEMID(260,0), ID(27, 0), ITEMID(345,0)};
		SIT_Widget w = SIT_TabGetNth(tab, i);

		SIT_SetValues(w, SIT_LabelSize, SITV_LabelSize(mcui.cellSz, mcui.cellSz), SIT_UserData, (APTR) blockId[i], NULL);
		SIT_AddCallback(w, SITE_OnPaint, mcuiGrabItemCoord, NULL);
	}

	SIT_SetAttributes(diag,
		"<exch1 height=", mcui.cellSz, ">"
		"<exch2 height=", mcui.cellSz, ">"
		"<exch3 height=", mcui.cellSz, ">"
		"<del   height=", mcui.cellSz, ">"
	);
	SIT_GetValues(mcinv.cell, SIT_Padding, mcui.padding, NULL);
	mcui.itemSz = mcui.cellSz - mcui.padding[0] - mcui.padding[2];

	SIT_SetFocus(find);

	SIT_AddCallback(SIT_GetById(diag, "exch1"), SITE_OnActivate, mcuiExchangeLine, player);
	SIT_AddCallback(SIT_GetById(diag, "exch2"), SITE_OnActivate, mcuiExchangeLine, player);
	SIT_AddCallback(SIT_GetById(diag, "exch3"), SITE_OnActivate, mcuiExchangeLine, player);
	SIT_AddCallback(SIT_GetById(diag, "del"),   SITE_OnActivate, mcuiClearAll,     player);
	SIT_AddCallback(SIT_GetById(diag, "del"),   SITE_OnClick,    mcuiCancelDrag,   player);

	SIT_AddCallback(tab,  SITE_OnChange, mcuiChangeTab,   &mcinv);
	SIT_AddCallback(find, SITE_OnChange, mcuiFilterItems, &mcinv);
	SIT_ManageWidget(diag);
}

/*
 * reading/writing chest inventory
 */
static int mcuiTransferItems(SIT_Widget w, APTR cd, APTR ud)
{
	MCInventory inv = cd;
	MCInventory target;
	Item        source = inv->items + (int) ud;
	Item        dest, dump;
	int         id = inv->groupId;
	int         i, slot, max;

	for (i = 0; i < mcui.groupCount && mcui.groups[i]->groupId == id; i ++);
	target = mcui.groups[i];

	/* first: dump items from <source> into stack of same item id of <target> inventory */
	for (slot = 0, max = target->itemsNb, dest = target->items, dump = NULL; slot < max; slot ++, dest ++)
	{
		/* find first free slot */
		if (dump == NULL && dest->id == 0)
			dump = dest;

		if (dest->id == source->id)
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

	if (source->count > 0 && dump)
	{
		/* free slot available: transfer all what's remaining here */
		dump[0] = source[0];
		memset(source, 0, sizeof *source);
	}

	return 1;
}

/*
 * single/double chest (ender, shulker or normal), dispenser, dropper inventory editor
 */
void mcuiEditChestInventory(Inventory player, Item items, int count, Block type)
{
	static struct MCInventory_t chest = {.invRow = 3, .invCol = MAXCOLINV, .groupId = 2};
	static struct MCInventory_t slot0 = {.invRow = 1, .invCol = 1, .groupId = 3, .itemsNb = 1};
	static struct MCInventory_t slot1 = {.invRow = 1, .invCol = 1, .groupId = 4, .itemsNb = 1};
	static struct MCInventory_t slot2 = {.invRow = 1, .invCol = 1, .groupId = 5, .itemsNb = 1};

	SIT_Widget diag = SIT_CreateWidget("container", SIT_DIALOG, globals.app,
		SIT_DialogStyles, SITV_Plain | SITV_Modal,
		NULL
	);
	mcui.groupCount = 0;

	if (strcmp(type->tech, "furnace") /* not a furnace type */)
	{
		STRPTR title = alloca(strlen(type->name) + 2);
		sprintf(title, "%s:", type->name);
		/* chest/dropper interface */
		SIT_CreateWidgets(diag,
			"<label name=msg title=", title, ">"
			"<canvas composited=1 name=inv.inv left=FORM top=WIDGET,msg,0.5em nextCtrl=LAST/>"
		);
		if (count > MAXCOLINV)
		{
			chest.invRow = count / MAXCOLINV;
			chest.invCol = MAXCOLINV;
		}
		else /* 3x3 instead of 9x1 */
		{
			/* center title and container */
			SIT_SetAttributes(diag,
				"<inv left=", SITV_AttachPosition, SITV_AttachPos(50), SITV_OffsetCenter, ">"
				"<msg left=", SITV_AttachPosition, SITV_AttachPos(50), SITV_OffsetCenter, ">"
			);
			chest.invRow = chest.invCol = 3;
		}
		chest.items   = items;
		chest.itemsNb = count;
		mcuiInitInventory(SIT_GetById(diag, "inv"), &chest, 3 + 3 + 2);
	}
	else /* furnace */
	{
		SIT_CreateWidgets(diag,
			/* fire should be between slot0 and slot1, but who cares? */
			"<label name=msg title=Furnace: left=", SITV_AttachPosition, SITV_AttachPos(50), SITV_OffsetCenter, ">"
			"<label name=furnace imagePath=furnace.png left=", SITV_AttachPosition, SITV_AttachPos(50), SITV_OffsetCenter, "top=WIDGET,msg,2em>"
			"<canvas composited=1 name=slot0.inv right=WIDGET,furnace,1em bottom=WIDGET,furnace,-0.5em nextCtrl=LAST/>"
			"<canvas composited=1 name=inv.inv right=WIDGET,furnace,1em top=WIDGET,furnace,-0.5em nextCtrl=LAST/>"
			"<canvas composited=1 name=slot2.inv left=WIDGET,furnace,1em top=MIDDLE,furnace nextCtrl=LAST/>"
		);
		slot0.items = items;
		slot1.items = items + 1;
		slot2.items = items + 2;
		mcuiInitInventory(SIT_GetById(diag, "slot0"), &slot0, 1);
		mcuiInitInventory(SIT_GetById(diag, "inv"),   &slot1, 0);
		mcuiInitInventory(SIT_GetById(diag, "slot2"), &slot2, 0);
		SIT_SetValues(SIT_GetById(diag, "furnace"), SIT_Height, mcui.cellSz/2, NULL);
	}

	SIT_CreateWidgets(diag,
		"<label name=msg2 title='Player inventory:' top=WIDGET,inv,0.3em>"
		"<canvas composited=1 name=player.inv top=WIDGET,msg2,0.3em nextCtrl=LAST/>"
		"<canvas composited=1 name=tb.inv left=FORM top=WIDGET,player,0.5em nextCtrl=LAST/>"
		"<tooltip name=info delayTime=", SITV_TooltipManualTrigger, "displayTime=10000 toolTipAnchor=", SITV_TooltipFollowMouse, ">"
	);

	mcui.toolTip = SIT_GetById(diag, "info");
	mcui.selCount = 0;
	mcui.cb = mcuiTransferItems;

	selfinv.items = player->items + MAXCOLINV;
	toolbar.items = player->items;

	mcuiInitInventory(SIT_GetById(diag, "tb"),     &toolbar, 0);
	mcuiInitInventory(SIT_GetById(diag, "player"), &selfinv, 0);

	SIT_ManageWidget(diag);
}


/*
 * interface to edit sign message
 */

static int mcuiSaveSign(SIT_Widget w, APTR cd, APTR ud)
{
	int    len = SIT_TextGetWithSoftline(ud, NULL, 0);
	STRPTR buffer = alloca(len);
	SIT_TextGetWithSoftline(ud, buffer, len);
	signSetText(mcui.signChunk, mcui.signPos, buffer);
	SIT_CloseDialog(w);
	SIT_Exit(1);
	return 1;
}

static int mcuiFontSize(SIT_Widget app, DATA8 text, int maxWidth, int fontSize)
{
	/* check if with current font size, sign text will fit in the sign */
	NVGcontext * vg;
	float textWidth;
	SIT_GetValues(app, SIT_NVGcontext, &vg, NULL);
	nvgFontSize(vg, fontSize);
	nvgTextAlign(vg, NVG_ALIGN_TOP|NVG_ALIGN_LEFT);
	/* size should be enough to accomodate at least about 15 'w' */
	textWidth = nvgTextBounds(vg, 0, 0, signMinText, signMinText+15, NULL);
	while (text)
	{
		DATA8 next;
		float sz;
		for (next = text; *next && *next != '\n'; next ++);
		sz = nvgTextBounds(vg, 0, 0, text, next, NULL);
		if (textWidth < sz)
			textWidth = sz;
		if (*next) text = next + 1;
		else break;
	}
	return fontSize * maxWidth / textWidth;
}

/* create interface for sign editing */
void mcuiCreateSignEdit(vec4 pos, int blockId)
{
	SIT_Widget diag = SIT_CreateWidget("sign", SIT_DIALOG, globals.app,
		SIT_DialogStyles, SITV_Plain | SITV_Modal,
		NULL
	);

	DATA8 signText;
	TEXT  styles[256];
	int   uv[4];

	signFillVertex(blockId, NULL, uv);
	signGetText(pos, styles, sizeof styles);
	signText = STRDUPA(styles);

	memcpy(mcui.signPos, pos, sizeof mcui.signPos);
	mcui.signChunk = mapGetChunk(globals.level, pos);

	if (uv[0] > uv[2]) swap(uv[0], uv[2]);
	if (uv[1] > uv[3]) swap(uv[1], uv[3]);

	int sz[2];
	int height = mcui.height / 4;
	int width  = height * (uv[2] - uv[0]) / (uv[3] - uv[1]);
	int image  = renderGetTerrain(sz, NULL);
	int fontsz = mcuiFontSize(globals.app, signText, width, (height - height / 10) / 4);
	height = (fontsz * 4) * 14 / 10 + 20;
	width += 20;
	int fullw  = sz[0] * width  / (uv[2] - uv[0]);
	int fullh  = sz[1] * height / (uv[3] - uv[1]);
	/* quote the texture for background plank from main terrain.png texture */
	sprintf(styles, "background: id(%d) %dpx %dpx; background-size: %dpx %dpx; padding: 10px; line-height: 1.3; font-size: %dpx",
		image, - fullw * uv[0] / sz[0] - 1, - fullh * uv[1] / sz[1] - 1, fullw, fullh, fontsz);

	SIT_CreateWidgets(diag,
		"<label name=msg title='Edit sign message:' left=", SITV_AttachPosition, SITV_AttachPos(50), SITV_OffsetCenter, ">"
		"<editbox name=signedit title=", signText, " wordWrap=", SITV_WWChar, "editType=", SITV_Multiline, "width=", width, "height=", height,
		" maxLines=4 tabStyle=", SITV_TabEditForbid, "style=", styles, "top=WIDGET,msg,4em>"
		"<button name=ok title=Done left=OPPOSITE,signedit top=WIDGET,signedit,4em left=", SITV_AttachPosition, SITV_AttachPos(50), SITV_OffsetCenter, ">"
	);
	SIT_Widget text = SIT_GetById(diag, "signedit");
	SIT_AddCallback(SIT_GetById(diag, "ok"), SITE_OnActivate, mcuiSaveSign, text);
	SIT_SetFocus(text);

	SIT_ManageWidget(diag);
}


/*
 * show a summary about all the blocks in the selection
 */

static int mcuiCopyAnalyze(SIT_Widget w, APTR cd, APTR ud)
{
	STRPTR bytes = malloc(256);
	int    max   = 256;
	int    i, nb, usage;

	/* copy info into clipboard */
	SIT_GetValues(ud, SIT_ItemCount, &nb, NULL);
	usage = sprintf(bytes, "Number,Type,ID\n");
	for (i = 0; i < nb; i ++)
	{
		STRPTR count = SIT_ListGetCellText(ud, 1, i);
		STRPTR name  = SIT_ListGetCellText(ud, 2, i);
		STRPTR id    = SIT_ListGetCellText(ud, 3, i);

		int len = strlen(count) + strlen(name) + strlen(id) + 4;

		if (usage + len > max)
		{
			max = (usage + len + 255) & ~255;
			bytes = realloc(bytes, max);
		}
		usage += sprintf(bytes + usage, "%s,%s,%s\n", count, name, id);
	}
	SIT_CopyToClipboard(bytes, usage);
	free(bytes);

	return 1;
}

/* only grab items to render */
static int mcuiGrabItem(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_OnCellPaint * ocp = cd;

	if ((ocp->rowColumn & 0xff) > 0) return 1;

	if (mcui.itemSz == 0)
		mcui.itemSz = ocp->LTWH[3] - 2;

	/* note: itemRender is set to 0 before rendering loop starts */
	Item item = mcui.items + mcui.itemRender ++;
	APTR rowTag;

	SIT_GetValues(w, SIT_RowTag(ocp->rowColumn>>8), &rowTag, NULL);

	item->x = ocp->LTWH[0];
	item->y = mcui.height - ocp->LTWH[1] - ocp->LTWH[3] + 1;
	item->id = (int) rowTag;
	item->count = 1;
	return 1;
}

int mcuiExitWnd(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_Exit(1);
	return 1;
}

void mcuiAnalyze(void)
{
	SIT_Widget diag = SIT_CreateWidget("analyze.bg", SIT_DIALOG, globals.app,
		SIT_DialogStyles, SITV_Plain | SITV_Modal | SITV_Movable,
		NULL
	);

	mcui.itemSz = 0;
	SIT_CreateWidgets(diag,
		"<label name=total>"
		"<listbox name=list columnNames='\xC2\xA0\xC2\xA0\xC2\xA0\xC2\xA0\tCount\tName\tID' width=20em height=15em top=WIDGET,total,0.5em"
		" composited=1 cellPaint=", mcuiGrabItem, ">"
		"<button name=ok title=Ok top=WIDGET,list,1em right=FORM buttonType=", SITV_DefaultButton, ">"
		"<button name=save title='Copy to clipboard' right=WIDGET,ok,0.5em top=OPPOSITE,ok>"
	);
	SIT_Widget w = SIT_GetById(diag, "list");
	SIT_AddCallback(SIT_GetById(diag, "ok"),   SITE_OnActivate, mcuiExitWnd, NULL);
	SIT_AddCallback(SIT_GetById(diag, "save"), SITE_OnActivate, mcuiCopyAnalyze, w);

	int  dx, dy, dz, i, j;
	vec  points = selectionGetPoints();
	vec4 pos = {
		fminf(points[VX], points[VX+4]),
		fminf(points[VY], points[VY+4]),
		fminf(points[VZ], points[VZ+4])
	};
	dx = fabsf(points[VX] - points[VX+4]) + 1;
	dy = fabsf(points[VY] - points[VY+4]) + 1;
	dz = fabsf(points[VZ] - points[VZ+4]) + 1;

	int * statistics = calloc(blockLast - blockStates, sizeof (int));
	struct BlockIter_t iter;
	for (mapInitIter(globals.level, &iter, pos, False); dy > 0; dy --)
	{
		for (j = 0; j < dz; j ++, mapIter(&iter, -dx, 0, 1))
		{
			for (i = 0; i < dx; i ++, mapIter(&iter, 1, 0, 0))
			{
				BlockState b = blockGetById(getBlockId(&iter));
				if (b->inventory == 0)
				{
					uint8_t data = b->id & 15;
					/* check if we can use alternative block state */
					switch (blockIds[iter.blockIds[iter.offset]].orientHint) {
					case ORIENT_LOG:
						if (4 <= data && data < 12) b -= data & ~3;
						break;
					case ORIENT_SLAB:
						b -= data & ~7;
						break;
					case ORIENT_SNOW:
						b -= data;
						break;
					case ORIENT_SWNE:
						b -= data & 3;
						break;
					case ORIENT_DOOR:
						if (data >= 8) continue;
						b -= data;
						break;
					case ORIENT_BED:
						b -= data;
					}
				}
				statistics[b - blockStates] ++;
			}
		}
		mapIter(&iter, 0, 1, -dz);
	}

	for (i = dx = 0, j = blockLast - blockStates; i < j; i ++)
	{
		if (statistics[i] == 0) continue;
		dx += statistics[i];
		STRPTR desc;
		TEXT   count[16];
		TEXT   id[16];
		int    itemId;
		BlockState b = blockStates + i;
		sprintf(count, "%d", statistics[i]);
		itemId = b->id;
		desc   = b->name;
		if (b->inventory == 0 && itemId > 0)
			/* check if there is an item that generate these type of block */
			itemId = itemCanCreateBlock(itemId, &desc);

		sprintf(id, "%d:%d", itemId >> 4, itemId & 15);
		SIT_ListInsertItem(w, -1, (APTR) itemId, "", count, desc, id);
	}
	free(statistics);
	SIT_ListReorgColumns(w, "**-*");
	dz *= (int) (fabsf(points[VX] - points[VX+4]) + 1) *
	      (int) (fabsf(points[VY] - points[VY+4]) + 1);
	SIT_SetValues(SIT_GetById(diag, "total"), SIT_Title|XfMt, "Non air block selected: <b>%d</b><br>Blocks in volume: <b>%d</b>", dx, dz, NULL);

	SIT_ManageWidget(diag);
}



/*
 * fill/replace selection with one type of block or with a geometric brush.
 */

static struct
{
	SIT_Widget  accept;
	SIT_Widget  search;
	SIT_Widget  replace;
	SIT_Widget  fill;
	SIT_Widget  similar;
	SIT_Widget  side0, side1;
	SIT_Widget  prog;
	SIT_Action  asyncCheck;
	MCInventory invFill;
	MCInventory invRepl;
	int         doReplace;
	int         doSimilar;
	int         side;
	int         hasSlab;
	uint32_t    processCurrent;
	uint32_t    processTotal;
	double      processStart;
	uint8_t     canUseSpecial[32];

	/* fill with geometric brush part */
	int         isHollow;
	int         shape;
	int         outerArea;
	int         fillAir;
	int         axisCylinder;
	vec4        size;
	SIT_Widget  XYZ[3];
}	mcuiRepWnd;

/* remove these blocks from inventory selection */
static uint8_t cannotFill[] = {
	BLOCK_CHEST,        /* need tile entity with neighbor location */
	BLOCK_DOOR,         /* 2 blocks tall */
	BLOCK_DOOR_TOP,     /* technical block, shouldn't be available in inventory */
	BLOCK_TALLFLOWER,   /* 2 blocks tall */
	BLOCK_SIGN,         /* need tile entity */
	BLOCK_BED,          /* 2 blocks wide */
};

/* SITE_OnChange on search: not exactly the same as mcuiFilterItems: don't want items here */
static int mcuiFilterBlocks(SIT_Widget w, APTR cd, APTR ud)
{
	MCInventory inv   = ud;
	BlockState  state = blockGetById(ID(1,0));
	Item        items = inv->items;
	STRPTR      match = cd;

	if (match[0] == 0)
		match = NULL;

	for ( ; state < blockLast; state ++)
	{
		if ((state->inventory & MODELFLAGS) == 0) continue;
		if (mcuiRepWnd.canUseSpecial[state->special & 31] == 0) continue;
		if (match && strcasestr(state->name, match) == NULL) continue;
		items->id = state->id;
		items ++;
	}

	inv->itemsNb = items - inv->items;
	mcuiResetScrollbar(inv);

	return 1;
}

/* timer: called every frame */
static int mcuiFillCheckProgress(SIT_Widget w, APTR cd, APTR ud)
{
	if (mcuiRepWnd.processTotal == mcuiRepWnd.processCurrent)
	{
		/* done */
		mapUpdateEnd(globals.level);
		mcuiRepWnd.asyncCheck = NULL;
		SIT_Exit(1);
		/* will cancel the timer */
		return -1;
	}
	double timeMS = FrameGetTime();
	if (timeMS - mcuiRepWnd.processStart > 250)
	{
		/* wait a bit before showing/updating progress bar */
		SIT_SetValues(mcuiRepWnd.prog, SIT_Visible, True, SIT_ProgressPos, (int)
			(uint64_t) mcuiRepWnd.processCurrent * 100 / mcuiRepWnd.processTotal, NULL);
		mcuiRepWnd.processStart = timeMS;
	}
	return 0;
}

/* OnActivate on fill button */
static int mcuiFillBlocks(SIT_Widget w, APTR cd, APTR ud)
{
	/* these functions will start a thread: processing the entire selection can be long, so don't hang the interface in the meantime */
	int block = mcuiRepWnd.invFill->items->id;
	mcuiRepWnd.processCurrent = 0;
	if (mcuiRepWnd.XYZ[0])
	{
		/* geometric brush fill */
		int shape = mcuiRepWnd.shape | (1 << (mcuiRepWnd.axisCylinder + 8));
		if (mcuiRepWnd.isHollow)  shape |= SHAPE_HOLLOW;
		if (mcuiRepWnd.fillAir)   shape |= SHAPE_FILLAIR;
		if (mcuiRepWnd.outerArea) shape |= SHAPE_OUTER;
		mcuiRepWnd.processTotal = selectionFillWithShape(&mcuiRepWnd.processCurrent, block, shape,
			mcuiRepWnd.size, globals.direction);
	}
	else if (! mcuiRepWnd.doReplace)
	{
		/* fill entire selection */
		mcuiRepWnd.processTotal = selectionFill(&mcuiRepWnd.processCurrent, block, mcuiRepWnd.side, globals.direction);
	}
	else mcuiRepWnd.processTotal = selectionReplace(&mcuiRepWnd.processCurrent, block, mcuiRepWnd.invRepl->items->id, mcuiRepWnd.side, mcuiRepWnd.doSimilar);

	/* better not to click twice on this button */
	SIT_SetValues(w, SIT_Enabled, False, NULL);

	/* this function will monitor the thread progress */
	mcuiRepWnd.asyncCheck = SIT_ActionAdd(w, mcuiRepWnd.processStart = globals.curTimeUI, globals.curTimeUI + 1e9, mcuiFillCheckProgress, NULL);

	renderAddModif();

	return 1;
}

static void mcuiFillSyncRadioSide(void)
{
	int ena = mcuiRepWnd.hasSlab & (mcuiRepWnd.doReplace ? 2 : 1);
	SIT_SetValues(mcuiRepWnd.side0, SIT_Enabled, ena, NULL);
	SIT_SetValues(mcuiRepWnd.side1, SIT_Enabled, ena, NULL);
}

/* activate top/bottom radio buttons */
static int mcuiCheckForSlab(SIT_Widget w, APTR cd, APTR slot)
{
	Block b = &blockIds[(int) cd>>4];
	int   flag = 1 << (int) slot;

	if (b->special == BLOCK_HALF || b->special == BLOCK_STAIRS)
		mcuiRepWnd.hasSlab |= flag;
	else
		mcuiRepWnd.hasSlab &= ~flag;

	mcuiFillSyncRadioSide();
	return 1;
}

/* OnActivate on checkbox replace item */
static int mcuiFillShowAction(SIT_Widget w, APTR cd, APTR ud)
{
	int checked;
	SIT_GetValues(w, SIT_CheckState, &checked, NULL);
	SIT_SetValues(mcuiRepWnd.replace, SIT_Enabled, checked, NULL);
	SIT_SetValues(mcuiRepWnd.similar, SIT_Enabled, checked, NULL);
	SIT_SetValues(mcuiRepWnd.accept, SIT_Title, checked ? "Replace" : "Fill", NULL);
	mcuiFillSyncRadioSide();
	return 0;
}

/* OnPaint on canvas replace item */
static int mcuiFillDisabled(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_OnPaint * paint = cd;
	int enabled;

	SIT_GetValues(w, SIT_Enabled, &enabled, NULL);
	if (enabled == 0)
	{
		/* draw a red cross to clearly show the slot as disabled */
		NVGcontext * vg = paint->nvg;
		float dist = floorf(paint->fontSize * 0.4f);
		float x1   = paint->x + dist, x2 = paint->x + paint->w - dist;
		float y1   = paint->y + dist, y2 = paint->y + paint->h - dist;
		nvgStrokeWidth(vg, dist * 0.6f);
		nvgStrokeColorRGBA8(vg, "\xff\x00\x00\xff");
		nvgBeginPath(vg);
		nvgMoveTo(vg, x1, y1); nvgLineTo(vg, x2, y2);
		nvgMoveTo(vg, x1, y2); nvgLineTo(vg, x2, y1);
		nvgStroke(vg);
		return 1;
	}
	return 0;
}

/* stop fill/replace operation */
static int mcuiFillStop(SIT_Widget w, APTR cd, APTR ud)
{
	int type;
	SIT_GetValues(w, SIT_CtrlType, &type, NULL);
	if (type == SIT_BUTTON)
		/* this callback can also be used by SIT_OnFinalize: don't change exit code in that case */
		SIT_Exit(1);
	if (mcuiRepWnd.asyncCheck)
	{
		selectionCancelOperation();
		SIT_ActionReschedule(mcuiRepWnd.asyncCheck, -1, -1);
		mcuiRepWnd.asyncCheck = NULL;
		/* show what's been modified */
		mapUpdateEnd(globals.level);
	}
	return 1;
}

void mcuiReplaceFillItems(SIT_Widget diag, MCInventory inv)
{
	/* will filter out some Block_t.special flag */
	int i;
	memset(mcuiRepWnd.canUseSpecial, 1, sizeof mcuiRepWnd.canUseSpecial);
	for (i = 0; i < DIM(cannotFill); i ++)
		mcuiRepWnd.canUseSpecial[cannotFill[i]] = 0;

	BlockState state;
	SIT_GetValues(diag, SIT_UserData, &mcui.allItems, NULL);
	for (state = blockGetById(ID(1, 0)), inv->itemsNb = 0; state < blockLast; state ++)
	{
		if ((state->inventory & MODELFLAGS) == 0) continue;
		if (mcuiRepWnd.canUseSpecial[state->special & 31] == 0) continue;
		Item item = mcui.allItems + inv->itemsNb;
		item->id = state->id;
		item->count = 1;
		item->uses = 0;
		inv->itemsNb ++;
	}

	inv->scroll = SIT_GetById(diag, "scroll");
	mcui.toolTip = SIT_GetById(diag, "info");
	mcui.selCount = 0;
	mcui.groupCount = 0;
	mcui.cb = NULL;
	inv->items = mcui.allItems;
}

void mcuiFillOrReplace(Bool fillWithBrush)
{
	static struct MCInventory_t mcinv   = {.invRow = 6, .invCol = MAXCOLINV, .movable = INV_PICK_ONLY};
	static struct MCInventory_t fillinv = {.invRow = 1, .invCol = 1, .groupId = 1, .itemsNb = 1, .movable = INV_SINGLE_DROP};
	static struct MCInventory_t replace = {.invRow = 1, .invCol = 1, .groupId = 2, .itemsNb = 1, .movable = INV_SINGLE_DROP};
	static struct Item_t fillReplace[2];

	/* if the interface is stopped early, we need to be notified */
	SIT_AddCallback(globals.app, SITE_OnFinalize, mcuiFillStop, NULL);

	SIT_Widget diag = SIT_CreateWidget("fillblock.bg", SIT_DIALOG + SIT_EXTRA((blockLast - blockStates) * sizeof (ItemBuf)), globals.app,
		SIT_DialogStyles, SITV_Plain | SITV_Modal | SITV_Movable,
		SIT_Style,        "padding-top: 0.2em",
		NULL
	);

	mcuiRepWnd.asyncCheck = NULL;

	SIT_CreateWidgets(diag,
		"<label name=dlgtitle.big title=", fillWithBrush ? "Geometric brush fill" : "Fill or replace selection",
		" left=", SITV_AttachPosition, SITV_AttachPos(50), SITV_OffsetCenter, ">"
		"<editbox name=search right=FORM top=WIDGET,dlgtitle,0.3em buddyLabel=", "Search:", NULL, ">"
		"<canvas composited=1 name=inv.inv left=FORM top=WIDGET,search,0.5em nextCtrl=LAST/>"
		"<scrollbar width=1.2em name=scroll.inv wheelMult=1 top=OPPOSITE,inv,0 bottom=OPPOSITE,inv,0 right=FORM>"
		"<label name=msg title='Fill:'>"
		"<canvas composited=1 name=fill.inv left=WIDGET,msg,0.5em top=WIDGET,inv,0.5em nextCtrl=LAST/>"
	);

	if (fillWithBrush)
	{
		/* interface to fill selection with a geometric brush */
		TEXT cylinder[32];
		vec points = selectionGetPoints();
		vec size   = mcuiRepWnd.size;
		size[0] = (int) fabsf(points[VX] - points[VX+4]) + 1;
		size[1] = (int) fabsf(points[VZ] - points[VZ+4]) + 1;
		size[2] = (int) fabsf(points[VY] - points[VY+4]) + 1;
		if (globals.direction & 1)
		{
			float tmp = size[0];
			size[0] = size[1];
			size[1] = tmp;
		}
		mcuiRepWnd.axisCylinder = selectionCylinderAxis(size, globals.direction);
		sprintf(cylinder, "Cylinder (%c)", "WLH"[mcuiRepWnd.axisCylinder]);

		SIT_CreateWidgets(diag,
			"<label name=label1 title=Shape: left=WIDGET,fill,0.5em top=MIDDLE,fill>"
			"<button name=shape1 title=Round checkState=1 curValue=", &mcuiRepWnd.shape, "buttonType=", SITV_RadioButton,
			" top=OPPOSITE,fill left=WIDGET,label1,0.5em>"
			"<button name=shape2 title=", cylinder, "curValue=", &mcuiRepWnd.shape, "buttonType=", SITV_RadioButton,
			" top=WIDGET,shape1,0.3em left=WIDGET,label1,0.5em maxWidth=shape1>"
			"<button name=shape3 title=Diamond curValue=", &mcuiRepWnd.shape, "buttonType=", SITV_RadioButton,
			" top=WIDGET,shape2,0.3em left=WIDGET,label1,0.5em maxWidth=shape2>"

			"<button name=outside title='Fill outer area' curValue=", &mcuiRepWnd.outerArea, "buttonType=", SITV_CheckBox,
			" left=WIDGET,shape1,1em top=OPPOSITE,fill>"
			"<button name=hollow curValue=", &mcuiRepWnd.isHollow, "buttonType=", SITV_CheckBox, "title=Hollow"
			" left=WIDGET,shape1,1em top=WIDGET,outside,0.3em>"
			"<button name=half title='Fill with air' tooltip='Only if hollow' curValue=", &mcuiRepWnd.fillAir, "buttonType=", SITV_CheckBox,
			" left=WIDGET,shape1,1em top=WIDGET,hollow,0.3em>"

			"<frame name=title2 title='<b>Size:</b> (clipped by selection)' left=FORM right=FORM top=WIDGET,shape3,0.5em/>"
			"<label name=label2.big title=W: right=WIDGET,fill,0.5em>"
			"<editbox name=xcoord roundTo=2 curValue=", size, "editType=", SITV_Float, "minValue=1"
			" right=", SITV_AttachPosition, SITV_AttachPos(32), 0, "left=OPPOSITE,fill top=WIDGET,title2,0.5em>"
			"<label name=label3.big title=L: left=WIDGET,xcoord,0.5em>"
			"<editbox name=zcoord roundTo=2 curValue=", size+1, "editType=", SITV_Float, "minValue=1"
			" right=", SITV_AttachPosition, SITV_AttachPos(64), 0, "left=WIDGET,label3,0.5em top=OPPOSITE,xcoord>"
			"<label name=label4.big title=H: left=WIDGET,zcoord,0.5em>"
			"<editbox name=ycoord roundTo=2 curValue=", size+2, "editType=", SITV_Float, "minValue=1"
			" right=FORM,,1em left=WIDGET,label4,0.5em top=OPPOSITE,zcoord>"

			"<button name=ok title=Fill>"
			"<button name=cancel title=Cancel buttonType=", SITV_CancelButton, "right=FORM top=WIDGET,xcoord,0.5em>"
			"<progress name=prog visible=0 title='%d%%' left=FORM right=WIDGET,ok,1em top=MIDDLE,ok>"
			"<tooltip name=info delayTime=", SITV_TooltipManualTrigger, "displayTime=10000 toolTipAnchor=", SITV_TooltipFollowMouse, ">"
		);
		SIT_SetAttributes(diag,
			"<label2 top=MIDDLE,xcoord><label3 top=MIDDLE,xcoord><label4 top=MIDDLE,xcoord>"
			"<label5 right=WIDGET,thick,0.5em top=MIDDLE,thick>"
			"<ok right=WIDGET,cancel,0.5em top=OPPOSITE,cancel buttonType=", SITV_DefaultButton, ">"
		);
		mcuiRepWnd.XYZ[0] = SIT_GetById(diag, "xcoord");
		mcuiRepWnd.XYZ[1] = SIT_GetById(diag, "zcoord");
		mcuiRepWnd.XYZ[2] = SIT_GetById(diag, "ycoord");
	}
	else /* fill/replace entire selection */
	{
		SIT_CreateWidgets(diag,
			"<button name=doreplace title='Replace by:' curValue=", &mcuiRepWnd.doReplace, "buttonType=", SITV_CheckBox,
			" left=WIDGET,fill,0.5em top=MIDDLE,fill>"
			"<canvas composited=1 enabled=", mcuiRepWnd.doReplace, "name=replace.inv left=WIDGET,doreplace,0.5em top=WIDGET,inv,0.5em/>"
			"<button name=side1 radioID=1 enabled=0 title=Top curValue=", &mcuiRepWnd.side, "buttonType=", SITV_RadioButton,
			" left=WIDGET,replace,0.5em top=MIDDLE,fill>"
			"<button name=side0 radioID=0 enabled=0 title=Bottom curValue=", &mcuiRepWnd.side, "buttonType=", SITV_RadioButton,
			" left=WIDGET,side1,0.5em top=MIDDLE,fill>"
			"<button name=similar enabled=", mcuiRepWnd.doReplace, "curValue=", &mcuiRepWnd.doSimilar, "title='Replace similar blocks (strairs, slabs)'"
			" buttonType=", SITV_CheckBox, "left=OPPOSITE,doreplace top=WIDGET,fill,0.5em>"
			"<button name=cancel title=Cancel right=FORM top=WIDGET,similar,1em>"
			"<button name=ok title=", mcuiRepWnd.doReplace ? "Replace" : "Fill", "top=OPPOSITE,cancel right=WIDGET,cancel,0.5em buttonType=", SITV_DefaultButton, ">"
			"<progress name=prog visible=0 title='%d%%' left=FORM right=WIDGET,ok,1em top=MIDDLE,ok>"
			"<tooltip name=info delayTime=", SITV_TooltipManualTrigger, "displayTime=10000 toolTipAnchor=", SITV_TooltipFollowMouse, ">"
		);
		mcuiRepWnd.XYZ[0] = NULL;
	}
	SIT_SetAttributes(diag, "<searchtxt top=MIDDLE,search><inv right=WIDGET,scroll,0.2em><msg top=MIDDLE,fill>");

	mcuiRepWnd.accept  = SIT_GetById(diag, "ok");
	mcuiRepWnd.search  = SIT_GetById(diag, "search");
	mcuiRepWnd.replace = SIT_GetById(diag, "replace");
	mcuiRepWnd.fill    = SIT_GetById(diag, "fill");
	mcuiRepWnd.similar = SIT_GetById(diag, "similar");
	mcuiRepWnd.side0   = SIT_GetById(diag, "side0");
	mcuiRepWnd.side1   = SIT_GetById(diag, "side1");
	mcuiRepWnd.prog    = SIT_GetById(diag, "prog");
	mcuiRepWnd.invFill = &fillinv;
	mcuiRepWnd.invRepl = &replace;
	mcuiRepWnd.processTotal = 0;

	mcuiFillSyncRadioSide();
	SIT_AddCallback(SIT_GetById(diag, "cancel"), SITE_OnActivate, mcuiFillStop, NULL);
	SIT_AddCallback(SIT_GetById(diag, "doreplace"), SITE_OnActivate, mcuiFillShowAction, NULL);
	SIT_AddCallback(mcuiRepWnd.accept,  SITE_OnActivate, mcuiFillBlocks, NULL);
	SIT_AddCallback(mcuiRepWnd.fill,    SITE_OnChange,   mcuiCheckForSlab, 0);
	SIT_AddCallback(mcuiRepWnd.replace, SITE_OnChange,   mcuiCheckForSlab, (APTR) 1);

	mcuiReplaceFillItems(diag, &mcinv);

	fillinv.items = fillReplace;
	replace.items = fillReplace + 1;

	mcuiInitInventory(SIT_GetById(diag, "inv"), &mcinv, 1);
	mcuiInitInventory(mcuiRepWnd.fill, &fillinv, 0);
	mcuiResetScrollbar(&mcinv);
	if (! fillWithBrush)
	{
		mcuiInitInventory(mcuiRepWnd.replace, &replace, 0);
		SIT_AddCallback(mcuiRepWnd.replace, SITE_OnPaint, mcuiFillDisabled, NULL);
	}

	SIT_GetValues(mcinv.cell, SIT_Padding, mcui.padding, NULL);
	mcui.itemSz = mcui.cellSz - mcui.padding[0] - mcui.padding[2];

	SIT_AddCallback(mcuiRepWnd.search, SITE_OnChange, mcuiFilterBlocks, &mcinv);
	SIT_AddCallback(mcinv.scroll, SITE_OnScroll, mcuiSetTop, &mcinv);
	SIT_SetFocus(mcuiRepWnd.search);

	SIT_ManageWidget(diag);
}

/*
 * delete all/selective from selection
 */

static struct
{
	SIT_Widget diag;
	int blocks;
	int entity;
	int tile;
}	mcuiDelWnd = {.blocks = False, .entity = True, .tile = True};

static int mcuiDeleteProgress(SIT_Widget w, APTR cd, APTR ud)
{
	if (mcuiRepWnd.processTotal == mcuiRepWnd.processCurrent)
	{
		/* done */
		mapUpdateEnd(globals.level);
		mcuiRepWnd.asyncCheck = NULL;
		SIT_Exit(1);
		/* will cancel the timer */
		return -1;
	}
	double timeMS = FrameGetTime();
	if (timeMS - mcuiRepWnd.processStart > 250)
	{
		if (mcuiRepWnd.replace == NULL)
		{
			SIT_Widget dialog = SIT_CreateWidget("delete.mc", SIT_DIALOG, w,
				SIT_DialogStyles, SITV_Plain,
				NULL
			);
			SIT_CreateWidgets(dialog,
				"<label name=title.big title='Delete in progress...' left=", SITV_AttachPosition, SITV_AttachPos(50), SITV_OffsetCenter, ">"
				"<progress name=prog title=%d%% width=15em top=WIDGET,title,0.5em>"
				"<button name=ko.act title=Cancel buttonType=", SITV_CancelButton, "left=WIDGET,prog,1em top=WIDGET,title,0.5em>"
			);
			mcuiRepWnd.replace = dialog;
			mcuiRepWnd.prog = SIT_GetById(dialog, "prog");
			SIT_AddCallback(SIT_GetById(dialog, "cancel"), SITE_OnActivate, mcuiFillStop, NULL);
			SIT_ManageWidget(dialog);
		}
		/* show a progress bar dialog and a way to cancel the action */
		SIT_SetValues(mcuiRepWnd.prog, SIT_Visible, True, SIT_ProgressPos, (int)
			(uint64_t) mcuiRepWnd.processCurrent * 100 / mcuiRepWnd.processTotal, NULL);
		mcuiRepWnd.processStart = timeMS;
	}
	return 0;
}

/* delete key pressed: auto-delete everything in selection */
void mcuiDeleteAll(void)
{
	/* delete all the blocks and entities in the selection */
	mcuiRepWnd.processCurrent = 0;
	mcuiRepWnd.replace = NULL;
	mcuiRepWnd.processTotal = selectionFill(&mcuiRepWnd.processCurrent, 0, 0, 0);
	renderAddModif();

	/* if the interface is stopped early, we need to be notified */
	SIT_AddCallback(globals.app, SITE_OnFinalize, mcuiFillStop, NULL);

	/* this function will monitor the thread progress */
	globals.curTimeUI = FrameGetTime();
	mcuiRepWnd.asyncCheck = SIT_ActionAdd(globals.app, mcuiRepWnd.processStart = globals.curTimeUI, globals.curTimeUI + 1e9, mcuiDeleteProgress, NULL);
}

static int mcuiAutoCheck(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_SetValues(ud, SIT_Enabled, ! mcuiDelWnd.blocks, NULL);
	return 1;
}

void mcuiIterTE(SIT_CallProc cb, APTR data)
{
	int dx, dz, i, j;
	vec points = selectionGetPoints();
	int pos[6], * p;
	for (i = 0, p = pos; i < 3; i ++, p ++)
	{
		p[0] = points[i];
		p[3] = points[i+4];
		if (p[0] > p[3]) swap(p[0], p[3]);
	}
	dx = pos[3] - pos[0] + 1;
	dz = pos[5] - pos[2] + 1;

	struct BlockIter_t iter;
	mapInitIter(globals.level, &iter, (float[3]){pos[0], pos[1], pos[2]}, False);
	for (j =  0; j < dz; j ++, mapIter(&iter, -i, 0, 16 - iter.z))
	{
		for (i = 0; i < dx; i += 16 - iter.x, mapIter(&iter, 16 - iter.x, 0, 0))
		{
			int offset = 0, XYZ[3];
			while ((iter.blockIds = chunkIterTileEntity(iter.ref, XYZ, &offset)))
			{
				if (pos[VX] <= XYZ[VX] && XYZ[VX] <= pos[VX+3] &&
				    pos[VY] <= XYZ[VY] && XYZ[VY] <= pos[VY+3] &&
				    pos[VZ] <= XYZ[VZ] && XYZ[VZ] <= pos[VZ+3])
				{
					cb((SIT_Widget)&iter, &offset, data);
				}
			}
		}
	}
}

static int mcuiDeleteTE(SIT_Widget w, APTR cd, APTR ud)
{
	BlockIter iter = (BlockIter) w;
	chunkDeleteTileEntity(iter->ref, (int[3]){iter->x, iter->yabs, iter->z}, False);
	chunkMarkForUpdate(iter->ref);
	((int *)cd)[0] --;
	((int *)ud)[0] ++;
	return 1;
}

static int mcuiDoDelete(SIT_Widget w, APTR cd, APTR ud)
{
	if (! mcuiDelWnd.blocks)
	{
		/* this doesn't need to be asynchronous */
		int total = 0;
		mcuiIterTE(mcuiDeleteTE, &total);
		if (total > 0) renderAddModif();
		SIT_Exit(1);
	}
	else if (! mcuiRepWnd.asyncCheck)
	{
		mcuiDeleteAll();
		mcuiRepWnd.replace = mcuiDelWnd.diag;
		SIT_SetValues(w, SIT_Enabled, False, NULL);
	}
	return 1;
}

static int mcuiCountTE(SIT_Widget w, APTR cd, APTR ud)
{
	((int *)ud)[0] ++;
	return 1;
}

void mcuiDeletePartial(void)
{
	SIT_Widget diag = SIT_CreateWidget("delete.bg", SIT_DIALOG, globals.app,
		SIT_DialogStyles, SITV_Plain | SITV_Modal | SITV_Movable,
		SIT_Style,        "padding-top: 0.2em",
		NULL
	);

	/* count the number of TileEntities */
	int tileEntities = 0;
	mcuiIterTE(mcuiCountTE, &tileEntities);

	SIT_CreateWidgets(diag,
		"<label name=dlgtitle.big title=", "Partial delete", "left=", SITV_AttachPosition, SITV_AttachPos(50), SITV_OffsetCenter, ">"
		"<label name=title title='Select the parts you want to delete:' top=WIDGET,dlgtitle,0.5em>"
		"<button name=blocks buttonType=", SITV_CheckBox, "curValue=", &mcuiDelWnd.blocks, "title=Blocks top=WIDGET,title,0.5em>"
		"<button name=entity buttonType=", SITV_CheckBox, "curValue=", &mcuiDelWnd.entity, "title='Entities (mobs, falling blocks, item frame, ...)'"
		" top=WIDGET,blocks,0.5em>"
	);
	if (tileEntities > 0)
	{
		TEXT title[64];
		sprintf(title, "Currently selected: %d", tileEntities);
		SIT_CreateWidgets(diag,
			"<button name=tile enabled=", ! mcuiDelWnd.blocks, "buttonType=", SITV_CheckBox, "curValue=", &mcuiDelWnd.tile,
			" title='Tile entities (chests inventories, ...)' top=WIDGET,entity,0.5em>"
			"<label name=tilenb style='font-weight:bold' title=", title, "left=FORM,,1.5em top=WIDGET,tile,0.3em>"
		);
	}
	SIT_CreateWidgets(diag,
		"<button name=cancel title=Cancel right=FORM top=WIDGET,#LAST,1em>"
		"<button name=ok title=Delete top=OPPOSITE,cancel right=WIDGET,cancel,0.5em>"
		"<progress name=prog title=%d%% visible=0 left=FORM right=WIDGET,ok,1em top=MIDDLE,ok>"
	);
	SIT_AddCallback(SIT_GetById(diag, "blocks"), SITE_OnActivate, mcuiAutoCheck, SIT_GetById(diag, "tile"));
	SIT_AddCallback(SIT_GetById(diag, "cancel"), SITE_OnActivate, mcuiExitWnd, NULL);
	SIT_AddCallback(SIT_GetById(diag, "ok"),     SITE_OnActivate, mcuiDoDelete, NULL);

	mcuiDelWnd.diag = diag;
	mcuiRepWnd.prog = SIT_GetById(diag, "prog");

	SIT_ManageWidget(diag);
}

/*
 * interface to select a painting
 */
static struct
{
	SIT_Widget view, name;
	DATA8      lastHover;
	float      scale;
	double     lastClick;

}	mcuiPaintings;

static int mcuiRenderPaintings(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_OnPaint * paint = cd;
	NVGcontext *  vg = paint->nvg;
	int size[2];
	int image = renderGetTerrain(size, NULL);
	float scale = mcuiPaintings.scale = (paint->w / (PAINTINGS_TILE_W * 16));

	nvgBeginPath(vg);
	nvgRect(vg, paint->x, paint->y, paint->w, paint->h);
	nvgFillPaint(vg, nvgImagePattern(vg, paint->x - (PAINTINGS_TILE_X * 16) * scale, paint->y - (PAINTINGS_TILE_Y * 16) * scale,
		size[0] * scale, size[1] * scale, 0, image, 1));
	nvgFill(vg);

	DATA8 hover = mcuiPaintings.lastHover;
	if (hover)
	{
		scale *= 16;
		nvgBeginPath(vg);
		nvgRect(vg, paint->x + hover[0] * scale, paint->y + hover[1] * scale, (hover[2] - hover[0]) * scale, (hover[3] - hover[1]) * scale);
		nvgStrokeColorRGBA8(vg, "\xff\xff\xff\xaf");
		nvgStrokeWidth(vg, 4);
		nvgStroke(vg);
	}

	return 1;
}

static DATA8 mcuiPaitingHovered(int x, int y)
{
	/* <scale> is a float, can't use /= operator (we want float div, not integer) */
	x = x / (mcuiPaintings.scale * 16);
	y = y / (mcuiPaintings.scale * 16);

	int i;
	for (i = 0; i < paintings.count; i ++)
	{
		DATA8 pos = paintings.location + i * 4;

		if (pos[0] <= x && x < pos[2] && pos[1] <= y && y < pos[3])
			return pos;
	}
	return NULL;
}

static void mcuiPaintingName(int id)
{
	STRPTR name, eof;
	if (id >= 0)
	{
		for (name = paintings.names; name && id > 0; id --, name = strchr(name+1, ','));
		if (name)
		{
			if (*name == ',') name ++;
			for (eof = name; *eof && *eof != ','; eof ++);
			id = eof - name + 1;
			eof = alloca(id);
			CopyString(eof, name, id);
			name = eof;
		}
		else name = "&lt;unknwown&gt;"; /* shouldn't happen */
	}
	else name = "";

	SIT_SetValues(mcuiPaintings.name, SIT_Title, name, NULL);
}

/* SITE_OnMouseMove over paintings */
static int mcuiSelectPaintings(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_OnMouse * msg = cd;
	DATA8 hover;
	switch (msg->state) {
	case SITOM_Move:
		hover = mcuiPaitingHovered(msg->x, msg->y);
		if (hover != mcuiPaintings.lastHover)
		{
			mcuiPaintings.lastHover = hover;
			mcuiPaintings.lastClick = 0;
			mcuiPaintingName(hover ? (hover - paintings.location) >> 2 : -1);
		}
		break;
	case SITOM_ButtonPressed:
		if (msg->button == SITOM_ButtonLeft && mcuiPaintings.lastHover)
		{
			double curTime = FrameGetTime();
			if ((curTime - mcuiPaintings.lastClick) < 750)
			{
				entityCreatePainting(globals.level, (mcuiPaintings.lastHover - paintings.location) >> 2);
				SIT_Exit(1);
			}
			else mcuiPaintings.lastClick = curTime;
		}
	default: break;
	}
	return 1;
}

void mcuiShowPaintings(void)
{
	SIT_Widget diag = SIT_CreateWidget("paintings.bg", SIT_DIALOG, globals.app,
		SIT_DialogStyles, SITV_Plain | SITV_Modal,
		SIT_Style,        "padding-top: 0.2em",
		NULL
	);

	int tiles = (globals.height >> 1) / PAINTINGS_TILE_H;

	SIT_CreateWidgets(diag,
		"<label name=dlgtitle.big title=", "Select painting", "left=", SITV_AttachPosition, SITV_AttachPos(50), SITV_OffsetCenter, ">"
		"<label name=title title='Double-click on the painting you want to add:' top=WIDGET,dlgtitle,0.5em>"
		"<label name=name left=WIDGET,title,0.5em right=FORM top=OPPOSITE,title>"
		"<canvas name=view#table left=FORM right=FORM top=WIDGET,title,0.5em height=", tiles * PAINTINGS_TILE_H, "width=", tiles * PAINTINGS_TILE_W, "/>"
		"<button name=ko title=Cancel top=WIDGET,view,0.5em right=FORM>"
	);

	SIT_Widget view = SIT_GetById(diag, "view");
	SIT_AddCallback(view, SITE_OnPaint,     mcuiRenderPaintings, NULL);
	SIT_AddCallback(view, SITE_OnClickMove, mcuiSelectPaintings, NULL);
	SIT_AddCallback(SIT_GetById(diag, "ko"), SITE_OnActivate, mcuiExitWnd, NULL);

	mcuiPaintings.name = SIT_GetById(diag, "name");
	mcuiPaintings.lastHover = NULL;
	mcuiPaintings.lastClick = 0;

	SIT_ManageWidget(diag);
}

/*
 * world info interface: quote some parameters from level.dat
 */
static struct
{
	int  mode, difficulty, dayCycle, keepInv, mobGrief;
	int  allowCmds, fireTick, hardcore;
	TEXT seed[32];
	int  days;
	TEXT time[16];
	TEXT name[256];
	TEXT folder[MAX_PATHLEN];

}	mcuiInfo;

/* get some a parameter from level.dat */
static void mcuiLevelDatParam(APTR buffer, int type, STRPTR key)
{
	NBTFile nbt = &globals.level->levelDat;
	STRPTR  sep = strchr(key, '.');
	int     off = 0;

	if (sep)
	{
		STRPTR sub = alloca(sep - key + 1);
		CopyString(sub, key, sep - key + 1);
		off = NBT_FindNode(nbt, 0, sub);
		key = sep + 1;
	}
	off = NBT_FindNode(nbt, off, key);

	switch (type & 15) {
	case TAG_Int:
		* (int *) buffer = NBT_ToInt(nbt, off, 0);
		break;
	case TAG_Byte:
		sep = NBT_Payload(nbt, off);
		* (int *) buffer = sep ? strcasecmp(sep, "true") == 0 : 0;
		break;
	case TAG_Long:
		sep = alloca(32);
		NBT_ToString(nbt, off, sep, 32);
		* (uint64_t *) buffer = strtoull(sep, NULL, 10);
		break;
	case TAG_String:
		NBT_ToString(nbt, off, buffer, type >> 8);
	}
}

static void TimeToStr(STRPTR dest, int time /* [0-23999] */)
{
	int h = time / 1000;
	int m = (time - h * 1000) / 20; /* there are only 50 "minutes" in one hour though :-/ */
	if (h > 12)
		sprintf(dest, "%d:%02d PM", h - 12, m);
	else
		sprintf(dest, "%d:%02d AM", h, m);
}

/* get total size of all items in the directory */
static uint64_t FolderSize(STRPTR path, int max)
{
	ScanDirData args;
	uint64_t    total = 0;

	if (ScanDirInit(&args, path))
	{
		do
		{
			if (args.isDir)
			{
				if (AddPart(path, args.name, max))
				{
					total += FolderSize(path, max);
					ParentDir(path);
				}
			}
			else total += args.size;
		}
		while (ScanDirNext(&args));
	}
	return total;
}

/* handler "folder" button */
static int mcuiInfoOpenFolder(SIT_Widget w, APTR cd, APTR ud)
{
	OpenDocument(mcuiInfo.folder);
	return 1;
}

/* OnActivate on "save" button */
static int mcuiInfoSave(SIT_Widget w, APTR cd, APTR ud)
{
	uint64_t time = mcuiInfo.days * 24000ULL;
	uint64_t seed = strtoull(mcuiInfo.seed, NULL, 10);
	int h = 0, m = 0;
	sscanf(mcuiInfo.time, "%d:%d", &h, &m);
	if (h > 12) h = 12; if (m > 50) m = 50;
	if (h < 0)  h = 0;  if (m < 0)  m = 0;
	STRPTR sep = strchr(mcuiInfo.time, ' ');
	if (sep && strcasecmp(sep + 1, "PM") == 0)
		h += 12;
	time += h * 1000 + m * 20;

	NBTFile nbt = &globals.level->levelDat;
	NBT_AddOrUpdateKey(nbt, "Data.LevelName",            TAG_String, mcuiInfo.name, 0);
	NBT_AddOrUpdateKey(nbt, "Data.RandomSeed",           TAG_Long, &seed, 0);
	NBT_AddOrUpdateKey(nbt, "Data.Time",                 TAG_Long, &time, 0);
	NBT_AddOrUpdateKey(nbt, "Data.DayTime",              TAG_Long, &time, 0);
	NBT_AddOrUpdateKey(nbt, "Data.allowCommands",        TAG_Int, &mcuiInfo.allowCmds, 0);
	NBT_AddOrUpdateKey(nbt, "Player.playerGameType",     TAG_Int, &mcuiInfo.mode, 0);
	NBT_AddOrUpdateKey(nbt, "Data.Difficulty",           TAG_Int, &mcuiInfo.difficulty, 0);
	NBT_AddOrUpdateKey(nbt, "Data.hardcore",             TAG_Int, &mcuiInfo.hardcore, 0);
	NBT_AddOrUpdateKey(nbt, "GameRules.doDayNightCycle", TAG_String, mcuiInfo.dayCycle ? "true" : "false", 0);
	NBT_AddOrUpdateKey(nbt, "GameRules.keepInventory",   TAG_String, mcuiInfo.keepInv  ? "true" : "false", 0);
	NBT_AddOrUpdateKey(nbt, "GameRules.mobGriefing",     TAG_String, mcuiInfo.mobGrief ? "true" : "false", 0);
	NBT_AddOrUpdateKey(nbt, "GameRules.doFireTick",      TAG_String, mcuiInfo.fireTick ? "true" : "false", 0);
	NBT_MarkForUpdate(nbt, 0, 1);

	renderAddModif();
	SIT_Exit(1);
	return 1;
}

/* use current framebuffer to generate an icon of the world */
static int mcuiInfoSetIcon(SIT_Widget w, APTR cd, APTR ud)
{
	/* generate a off-screen FBO for this */
	NVGCTX vg = globals.nvgCtx;

	struct NVGLUframebuffer * fbo = nvgluCreateFramebuffer(vg, PACKPNG_SIZE, PACKPNG_SIZE, NVG_IMAGE_DEPTH);

	if (fbo)
	{
		STRPTR path = STRDUPA(globals.level->path);
		int width  = globals.width;
		int height = globals.height;
		globals.width  = PACKPNG_SIZE;
		globals.height = PACKPNG_SIZE;
		nvgluBindFramebuffer(fbo);

		renderShowBlockInfo(True, RENDER_DEBUG_NOCLUTTER);
		renderWorld();

		renderShowBlockInfo(False, RENDER_DEBUG_NOCLUTTER);
		globals.width  = width;
		globals.height = height;
		nvgluBindFramebuffer(NULL);
		glViewport(0, 0, width, height);

		/* retrieve texture */
		DATA8 data = malloc(PACKPNG_SIZE * PACKPNG_SIZE * 3);
		glBindTexture(GL_TEXTURE_2D, fbo->texture);
		glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
		nvgluDeleteFramebuffer(fbo);

		AddPart(path, "../icon.png", 1e6);
		textureSaveSTB(path, PACKPNG_SIZE, PACKPNG_SIZE, 3, data, PACKPNG_SIZE*3);
		free(data);

		/* update interface too (note: it is set twice to reset cache from SITGL) */
		SIT_SetValues(ud, SIT_ImagePath, "", SIT_ImagePath, path, NULL);
	}
	return 1;
}

/* build interface for world info editor */
void mcuiWorldInfo(void)
{
	strcpy(mcuiInfo.folder, globals.level->path);
	ParentDir(mcuiInfo.folder);

	SIT_Widget diag = SIT_CreateWidget("worldinfo.mc", SIT_DIALOG, globals.app,
		SIT_DialogStyles, SITV_Plain,
		NULL
	);

	uint64_t totalTime;
	TEXT     size[16];
	STRPTR   iconPath = STRDUPA(globals.level->path);

	AddPart(iconPath, "../icon.png", 1e6);

	if (! FileExists(iconPath))
		iconPath = "resources/pack.png";

	#define STR(sz)    (TAG_String | (sz << 8))
	mcuiLevelDatParam(&mcuiInfo.mode,       TAG_Int,  "Player.playerGameType");
	mcuiLevelDatParam(&mcuiInfo.difficulty, TAG_Int,  "Data.Difficulty");
	mcuiLevelDatParam(&mcuiInfo.hardcore,   TAG_Int,  "Data.hardcore");
	mcuiLevelDatParam(&mcuiInfo.allowCmds,  TAG_Int,  "Data.allowCommands");
	mcuiLevelDatParam(&mcuiInfo.dayCycle,   TAG_Byte, "GameRules.doDayNightCycle");
	mcuiLevelDatParam(&mcuiInfo.keepInv,    TAG_Byte, "GameRules.keepInventory");
	mcuiLevelDatParam(&mcuiInfo.mobGrief,   TAG_Byte, "GameRules.mobGriefing");
	mcuiLevelDatParam(&mcuiInfo.fireTick,   TAG_Byte, "GameRules.doFireTick");
	mcuiLevelDatParam(&totalTime,           TAG_Long, "Data.Time");
	mcuiLevelDatParam(mcuiInfo.seed,        STR(32),  "Data.RandomSeed");
	mcuiLevelDatParam(mcuiInfo.name,        STR(256), "Data.LevelName");
	#undef STR
	mcuiInfo.days = totalTime / 24000;
	TimeToStr(mcuiInfo.time, totalTime - mcuiInfo.days * 24000ULL);
	FormatNumber(size, sizeof size, "%d K", (FolderSize(mcuiInfo.folder, sizeof mcuiInfo.folder) + 1023) >> 10);

	SIT_Widget max1 = NULL;
	SIT_Widget max2 = NULL;
	SIT_CreateWidgets(diag,
		"<label name=dlgtitle#title title='World info:' left=FORM right=FORM>"
		"<label name=icon#table imagePath=", iconPath, "right=FORM top=WIDGET,dlgtitle,0.8em>"
		"<button name=set.act title='Update icon' top=WIDGET,icon,0.2em left=OPPOSITE,icon right=OPPOSITE,icon tooltip='Will use current 3d view'>"
		"<editbox name=level editBuffer=", mcuiInfo.name, "editLength=", sizeof mcuiInfo.name, "width=15em"
		" right=WIDGET,icon,0.5em top=WIDGET,dlgtitle,0.8em buddyLabel=", "Name:", &max1, ">"
		"<editbox name=seed  editBuffer=", mcuiInfo.seed, "editLength=", sizeof mcuiInfo.seed,
		" right=WIDGET,icon,0.5em top=WIDGET,level,0.5em buddyLabel=", "Seed:", &max1, ">"
		"<editbox name=day width=5.75em top=WIDGET,seed,0.5em minValue=0 buddyLabel=", "Days:", &max1,
		" editType=", SITV_Integer, "curValue=", &mcuiInfo.days, ">"
		"<editbox name=time width=5.75em top=WIDGET,seed,0.5em buddyLabel=", "Time:", NULL,
		" editBuffer=", mcuiInfo.time, "editLength=", sizeof mcuiInfo.time, "right=WIDGET,icon,0.5em>"
		"<button name=open.act title=Folder:>"
		"<editbox name=folder editBuffer=", mcuiInfo.folder, "editLength=", MAX_PATHLEN, "readOnly=1 top=WIDGET,time,0.5em left=OPPOSITE,level right=WIDGET,icon,0.5em>"
		"<label name=size title=", size, "top=WIDGET,folder,0.5em buddyLabel=", "Size:", &max1, ">"
		"<label name=rules#title title='Game rules:' left=FORM right=FORM top=WIDGET,size,0.5em/>"
		/* game mode */
		"<button name=type0 buttonType=", SITV_RadioButton, "curValue=", &mcuiInfo.mode, "title=Survival top=WIDGET,rules,1em buddyLabel=", "Game mode:", &max2, ">"
		"<button name=type1 buttonType=", SITV_RadioButton, "curValue=", &mcuiInfo.mode, "title=Creative top=OPPOSITE,type0 left=WIDGET,type0,0.8em>"
		"<button name=type2 buttonType=", SITV_RadioButton, "curValue=", &mcuiInfo.mode, "title=Spectator radioID=3 top=OPPOSITE,type0 left=WIDGET,type1,0.8em>"
		/* difficulty */
		"<button name=level0 buttonType=", SITV_RadioButton, "curValue=", &mcuiInfo.difficulty, "radioGroup=1"
		" title=Peaceful top=WIDGET,type0,1em buddyLabel=", "Difficulty:", &max2, ">"
		"<button name=level1 buttonType=", SITV_RadioButton, "curValue=", &mcuiInfo.difficulty,
		" radioGroup=1 title=Easy top=OPPOSITE,level0 left=WIDGET,level0,0.8em>"
		"<button name=level2 buttonType=", SITV_RadioButton, "curValue=", &mcuiInfo.difficulty,
		" radioGroup=1 title=Normal top=OPPOSITE,level0 left=WIDGET,level1,0.8em>"
		"<button name=level3 buttonType=", SITV_RadioButton, "curValue=", &mcuiInfo.difficulty,
		" radioGroup=1 title=Hard top=OPPOSITE,level0 left=WIDGET,level2,0.8em>"
		/* hardcore */
		"<button name=hard0 buttonType=", SITV_RadioButton, "radioGroup=2 top=WIDGET,level0,1em"
		" curValue=", &mcuiInfo.hardcore, "title=No buddyLabel=", "Hardcore more:", &max2, ">"
		"<button name=hard1 buttonType=", SITV_RadioButton, "radioGroup=2 top=OPPOSITE,hard0"
		" left=WIDGET,hard0,0.8em curValue=", &mcuiInfo.hardcore, "title=Yes>"
		/* allowCommands */
		"<button name=cmd0 buttonType=", SITV_RadioButton, "radioGroup=3 top=WIDGET,hard0,1em"
		" curValue=", &mcuiInfo.allowCmds, "title=No buddyLabel=", "Allow commands:", &max2, ">"
		"<button name=cmd1 buttonType=", SITV_RadioButton, "radioGroup=3 top=OPPOSITE,cmd0"
		" left=WIDGET,cmd0,0.8em curValue=", &mcuiInfo.allowCmds, "title=Yes>"
		/* doDayNightCycle */
		"<button name=day0 buttonType=", SITV_RadioButton, "radioGroup=4 top=WIDGET,cmd0,1em"
		" curValue=", &mcuiInfo.dayCycle, "title=No buddyLabel=", "Day/night cycle:", &max2, ">"
		"<button name=day1 buttonType=", SITV_RadioButton, "radioGroup=4 top=OPPOSITE,day0"
		" left=WIDGET,day0,0.8em curValue=", &mcuiInfo.dayCycle, "title=Yes>"
		/* keepInventory */
		"<button name=inv0 buttonType=", SITV_RadioButton, "radioGroup=5 top=WIDGET,day0,1em"
		" curValue=", &mcuiInfo.keepInv, "title=No buddyLabel=", "Keep inventory:", &max2, ">"
		"<button name=inv1 buttonType=", SITV_RadioButton, "radioGroup=5 top=OPPOSITE,inv0"
		" left=WIDGET,inv0,0.8em curValue=", &mcuiInfo.keepInv, "title=Yes>"
		/* mobGriefing */
		"<button name=grief0 buttonType=", SITV_RadioButton, "radioGroup=6 top=WIDGET,inv0,1em"
		" curValue=", &mcuiInfo.mobGrief, "title=No buddyLabel=", "Mob griefing:", &max2, ">"
		"<button name=grief1 buttonType=", SITV_RadioButton, "radioGroup=6 top=OPPOSITE,grief0"
		" left=WIDGET,grief0,0.8em curValue=", &mcuiInfo.mobGrief, "title=Yes>"
		/* doFireTick */
		"<button name=fire0 buttonType=", SITV_RadioButton, "radioGroup=7 top=WIDGET,grief0,1em"
		" curValue=", &mcuiInfo.fireTick, "title=No buddyLabel=", "Fire spreading:", &max2, ">"
		"<button name=fire1 buttonType=", SITV_RadioButton, "radioGroup=7 top=OPPOSITE,fire0"
		" left=WIDGET,fire0,0.8em curValue=", &mcuiInfo.fireTick, "title=Yes>"

		"<button name=ko.act title=Cancel top=WIDGET,fire0,0.8em right=FORM buttonType=", SITV_CancelButton, ">"
		"<button name=ok.act title=Save   top=OPPOSITE,ko right=WIDGET,ko,1em buttonType=", SITV_DefaultButton, ">"
	);

	SIT_SetAttributes(diag, "<open top=MIDDLE,folder maxWidth=bsize><time left=NONE><btime right=WIDGET,time,0.5em><icon bottom=OPPOSITE,folder>");
	SIT_AddCallback(SIT_GetById(diag, "ko"),   SITE_OnActivate, mcuiExitWnd, NULL);
	SIT_AddCallback(SIT_GetById(diag, "open"), SITE_OnActivate, mcuiInfoOpenFolder, NULL);
	SIT_AddCallback(SIT_GetById(diag, "ok"),   SITE_OnActivate, mcuiInfoSave, NULL);
	SIT_AddCallback(SIT_GetById(diag, "set"),  SITE_OnActivate, mcuiInfoSetIcon, SIT_GetById(diag, "icon"));

	SIT_ManageWidget(diag);
}


