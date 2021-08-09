/*
 * interface.c: handle user interface for MCEdit (based on SITGL).
 *
 * Written by T.Pierron, oct 2020
 */

#define MCUI_IMPL
#include <glad.h>
#include <malloc.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include "blocks.h"
#include "nanovg.h"
#include "SIT.h"
#include "interface.h"
#include "selection.h"
#include "render.h"
#include "player.h"
#include "sign.h"

static struct MCInterface_t mcui;
static struct MCInventory_t selfinv = {.invRow = 3, .invCol = MAXCOLINV, .groupId = 1, .itemsNb = MAXCOLINV * 3};
static struct MCInventory_t toolbar = {.invRow = 1, .invCol = MAXCOLINV, .groupId = 1, .itemsNb = MAXCOLINV};
static int category[] = {BUILD, DECO, REDSTONE, CROPS, RAILS, 0};

/*
 * before displaying a user interface, take a snapshot of current framebuffer and
 * use this as a background: prevent from continuously rendering the same frame
 */
void mcuiTakeSnapshot(SIT_Widget app, int width, int height)
{
	SIT_GetValues(app, SIT_NVGcontext, &mcui.nvgCtx, NULL);
	if (mcui.glBack == 0)
		glGenTextures(1, &mcui.glBack);

	glBindTexture(GL_TEXTURE_2D, mcui.glBack);
	if (mcui.width != width || mcui.height != height)
	{
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
		if (mcui.nvgImage)
			nvgUpdateImage(mcui.nvgCtx, mcui.nvgImage, NULL);
	}
	/* copy framebuffer content into texture */
	glBindTexture(GL_TEXTURE_2D, mcui.glBack);
	glReadBuffer(GL_FRONT);
	glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, 0, 0, width, height, 0);
	glGenerateMipmap(GL_TEXTURE_2D);

	if (mcui.nvgImage == 0)
		mcui.nvgImage = nvgCreateImage(mcui.nvgCtx, (const char *) mcui.glBack, NVG_IMAGE_FLIPY | NVG_IMAGE_GLTEX);

	mcui.app = app;
	mcui.width = width;
	mcui.height = height;

	TEXT style[32];
	sprintf(style, "background: id(%d)", mcui.nvgImage);
	SIT_SetValues(app, SIT_Style|XfMt, style, NULL);
}

/*
 * creative inventory interface
 */
static int mcuiInventoryRender(SIT_Widget w, APTR cd, APTR ud)
{
	MCInventory inv = ud;
	float       x, y, curX, curY;
	int         i, j, sz = mcui.cellSz;
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
			if ((i == curX && j == curY) || (max > 0 && item->slot > 0))
			{
				nvgBeginPath(mcui.nvgCtx);
				nvgRect(mcui.nvgCtx, x+x2, y+y2, sz, sz);
				nvgFillColorRGBA8(mcui.nvgCtx, "\xff\xff\xff\x7f");
				nvgFill(mcui.nvgCtx);
			}
			SIT_SetValues(inv->cell, SIT_X, x2, SIT_Y, y2, SIT_Width, sz, SIT_Height, sz, NULL);
			SIT_RenderNode(inv->cell);
			/* grab item to render */
			if (max > 0)
			{
				if (item->id > 0)
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
	return 1;
}

/* show info about block hovered in a tooltip */
static void mcuiRefreshTooltip(MCInventory inv)
{
	TEXT title[180];
	int  index = inv->top + inv->curX + inv->curY * inv->invCol;
	if (index >= inv->itemsNb)
	{
		SIT_SetValues(mcui.toolTip, SIT_Visible, False, NULL);
		return;
	}
	Item item = inv->items + index;
	STRPTR p;
	title[0] = 0;
	int tag = NBT_FindNodeFromStream(item->extra, 0, "/tag.ench");

	if (tag >= 0)
		StrCat(title, sizeof title, 0, "<b>");
	if (item->id < ID(256, 0))
	{
		BlockState state = blockGetById(item->id);
		if (state->id > 0)
		{
			StrCat(title, sizeof title, 0,
				STATEFLAG(state, TRIMNAME) ? blockIds[item->id >> 4].name : state->name
			);
		}
		else
		{
			/* a block that shouldn't be in a inventory :-/ */
			SIT_SetValues(mcui.toolTip, SIT_Visible, False, NULL);
			return;
		}
	}
	else
	{
		ItemDesc desc = itemGetById(item->id);
		if (desc == NULL)
		{
			SIT_SetValues(mcui.toolTip, SIT_Visible, False, NULL);
			return;
		}
		StrCat(title, sizeof title, 0, desc->name);
	}
	if (tag >= 0)
		StrCat(title, sizeof title, 0, "</b>");

	/* add id */
	TEXT id[16];
	p = id + sprintf(id, " (#%04d", item->id >> 4);
	if (item->id & 15) p += sprintf(p, "/%d", item->id & 15);
	p += sprintf(p, ")");
	index = StrCat(title, sizeof title, 0, id);

	/* add enchant if any */
	if (tag >= 0)
		itemDecodeEnchants(item->extra + tag, title, sizeof title);

	index = StrCat(title, sizeof title, index, "<br><dim>");

	/* check if item container */
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
	itemGetTechName(item->id, title + index, sizeof title - index);
	StrCat(title, sizeof title, index, "</dim>");

	SIT_SetValues(mcui.toolTip, SIT_Visible, True, SIT_Title, title, SIT_DisplayTime, SITV_ResetTime, NULL);
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
	/* needs to be empty */
	if (addCell->id > 0)
		return;

	/* items need to filled in the order the area was drawn */
	DATA8 slots, group;
	Item  list, eof;
	int   count, split, i;

	addCell[0] = mcui.dragSplit;
	addCell->slot = ++ mcui.selCount;
	count = mcui.dragSplit.count;
	split = count / mcui.selCount;
	if (split < 1) split = 1;

	slots = alloca(mcui.selCount * 2);
	group = slots + mcui.selCount;

	/* linearize all slots */
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
		list->count = split;
		if (count >= split)
			count -= split;
		else
			list->id = 0, list->count = 0;
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

/* highlight cell hovered */
static int mcuiInventoryMouse(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_OnMouse * msg = cd;
	MCInventory   inv = ud;

	int cellx = msg->x / mcui.cellSz;
	int celly = msg->y / mcui.cellSz;
	switch (msg->state) {
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
				MCInventory inv = mcui.groups[i];
				Item list, eof;
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
			SIT_ApplyCallback(mcui.scroll, cd, SITE_OnClick);
			break;
		case SITOM_ButtonMiddle:
			/* grab an entire stack */
			if (inv->items[cellx].id > 0)
			{
				grab_stack:
				mcui.drag = inv->items[cellx];
				itemAddCount(&mcui.drag, 64);
				cellx = SIT_InitDrag(mcuiDragItem);
				mcui.drag.x = cellx & 0xffff;
				mcui.drag.y = mcui.height - (cellx >> 16) - mcui.itemSz;
				SIT_ForceRefresh();
				return -1;
			}
			break;
		case SITOM_ButtonRight:
			if (inv->groupId)
			{
				/* grab half the stack */
				Item cur = inv->items + cellx;
				int  cnt = (cur->count + 1) >> 1;

				cur->count -= cnt;
				mcui.drag = *cur;
				mcui.drag.count = cnt;
				if (cur->count == 0)
					memset(cur, 0, sizeof *cur);
				cellx = SIT_InitDrag(mcuiDragItem);
				mcui.drag.x = cellx & 0xffff;
				mcui.drag.y = mcui.height - (cellx >> 16) - mcui.itemSz;
				return -1;
			}
			/* else no break */
		case SITOM_ButtonLeft:
			/* pick an item */
			if (msg->flags & SITK_FlagShift)
			{
				if (mcui.cb)
				{
					if (mcui.cb(w, inv, (APTR) cellx))
						SIT_ForceRefresh();
				}
				else if (inv->groupId)
				{
					memset(inv->items + cellx, 0, sizeof (struct Item_t));
					SIT_ForceRefresh();
				}
				else goto grab_stack;
			}
			else if (mcui.drag.id > 0)
			{
				if (inv->groupId)
				{
					/* merge stack if same id */
					ItemBuf old = inv->items[cellx];
					inv->items[cellx] = mcui.drag;
					if (old.id == mcui.drag.id)
					{
						/* but only to stack limit (old.count == leftovers) */
						old.count = itemAddCount(&inv->items[cellx], old.count);
						if (old.count == 0) old.id = 0;
					}

					if (old.id > 0)
					{
						/* click on a slot with items in it: exchange with item being dragged */
						mcui.drag.id = old.id;
						mcui.drag.count = old.count;
						mcui.drag.uses = old.uses;
						mcui.drag.extra = old.extra;
						SIT_ForceRefresh();
					}
					else /* start spliting items by drawing on inventory slots */
					{
						mcui.groupIdStart = inv->groupId;
						mcui.dragSplit = mcui.drag;
						mcui.drag.id = 0;
						mcui.selCount = 1;
						inv->items[cellx].slot = 1;
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
				mcui.drag = inv->items[cellx];
				if (mcui.drag.id == 0) break;
				if (inv->groupId) memset(inv->items + cellx, 0, sizeof (struct Item_t));
				cellx = SIT_InitDrag(mcuiDragItem);
				mcui.drag.x = cellx & 0xffff;
				mcui.drag.y = mcui.height - (cellx >> 16) - mcui.itemSz;
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

	return 1;
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

static void mcuiResetScrollbar(MCInventory inv)
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
		SIT_SetValues(mcui.scroll, SIT_MaxValue, 1, SIT_PageSize, 1, SIT_ScrollPos, inv->top, NULL);
	else
		SIT_SetValues(mcui.scroll, SIT_MaxValue, lines, SIT_PageSize, inv->invRow, SIT_LineHeight, 1, SIT_ScrollPos, inv->top, NULL);
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
	if (inv->curX >= 0) mcuiRefreshTooltip(inv);
	return 1;
}

/* not defined by mingw... */
char * strcasestr(const char * hayStack, const char * needle)
{
	int length = strlen(needle);
	int max    = strlen(hayStack);

	if (length > max)
		return NULL;

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
		if (id < ID(256,0))
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

static void mcuiInitInventory(SIT_Widget canvas, MCInventory inv)
{
	inv->cell = SIT_CreateWidget("td", SIT_HTMLTAG, canvas, SIT_Visible, False, NULL);
	inv->curX = -1;
	inv->top  = 0;

	SIT_AddCallback(canvas, SITE_OnPaint,     mcuiInventoryRender,   inv);
	SIT_AddCallback(canvas, SITE_OnClickMove, mcuiInventoryMouse,    inv);
	SIT_AddCallback(canvas, SITE_OnMouseOut,  mcuiInventoryMouseOut, inv);

	SIT_SetValues(canvas, SIT_Width, inv->invCol * mcui.cellSz, SIT_Height, inv->invRow * mcui.cellSz, NULL);

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

	/* same scale than player toolbar... */
	mcui.cellSz = roundf(mcui.width * 17 * ITEMSCALE / (3 * 182.));

	/* ... unless it doesn't fit within window's height */
	if (mcui.cellSz * (6 + 3 + 2 + 3) > mcui.height)
		mcui.cellSz = mcui.height / (6 + 3 + 2 + 2 + 3);

	SIT_Widget diag = SIT_CreateWidget("inventory", SIT_DIALOG + SIT_EXTRA(itemGetInventoryByCat(NULL, 0) * sizeof (ItemBuf)), mcui.app,
		SIT_DialogStyles, SITV_Plain | SITV_Modal,
		NULL
	);

	SIT_CreateWidgets(diag,
		"<tab name=items left=FORM right=FORM top=FORM bottom=FORM tabSpace=4 tabActive=", mcui.curTab, "tabStr=", "\t\t\t\t\t", ">"
		" <label name=searchtxt title='Search:'>"
		" <editbox name=search left=WIDGET,searchtxt,0.5em right=FORM>"
		" <canvas composited=1 name=inv.inv left=FORM top=WIDGET,search,0.5em/>"
		" <scrollbar width=1.2em name=scroll wheelMult=1 top=OPPOSITE,inv,0 bottom=OPPOSITE,inv,0 right=FORM>"
		" <label name=msg title='Player inventory:' top=WIDGET,inv,0.3em>"
		" <canvas composited=1 name=player.inv top=WIDGET,msg,0.3em/>"
		" <canvas composited=1 name=tb.inv left=FORM top=WIDGET,player,0.5em/>"
		" <button name=exch1.exch top=OPPOSITE,player right=FORM tooltip=", tip, "maxWidth=scroll height=", mcui.cellSz, ">"
		" <button name=exch2.exch top=WIDGET,exch1 right=FORM tooltip=", tip, "maxWidth=exch1 height=", mcui.cellSz, ">"
		" <button name=exch3.exch top=WIDGET,exch2 right=FORM tooltip=", tip, "maxWidth=exch2 height=", mcui.cellSz, ">"
		" <button name=del.exch   top=OPPOSITE,tb right=FORM title=X tooltip='Clear inventory' maxWidth=exch3 height=", mcui.cellSz, ">"
		"</tab>"
		"<tooltip name=info delayTime=", SITV_TooltipManualTrigger, " displayTime=10000 toolTipAnchor=", SITV_TooltipFollowMouse, ">"
	);

	SIT_SetAttributes(diag, "<searchtxt top=MIDDLE,search><inv right=WIDGET,scroll,0.2em>");

	/* icons for tab: will be rendered as MC items */
	SIT_Widget tab  = SIT_GetById(diag, "items");
	SIT_Widget find = SIT_GetById(diag, "search");
	int i;
	for (i = 0; i < 6; i ++)
	{
		/* tab icons:           build     deco        redstone       crops          rails      search/all */
		static int blockId[] = {ID(45,0), ID(175,15), ITEMID(331,0), ITEMID(260,0), ID(27, 0), ITEMID(345,0)};
		SIT_Widget w = SIT_TabGetNth(tab, i);

		SIT_SetValues(w, SIT_LabelSize, SITV_LabelSize(mcui.cellSz, mcui.cellSz), SIT_UserData, (APTR) blockId[i], NULL);
		SIT_AddCallback(w, SITE_OnPaint, mcuiGrabItemCoord, NULL);
	}

	/* callbacks registration */
	mcui.scroll = SIT_GetById(diag, "scroll");
	mcui.toolTip = SIT_GetById(diag, "info");
	mcui.selCount = 0;
	mcui.groupCount = 0;
	mcui.cb = NULL;

	static struct MCInventory_t mcinv = {.invRow = 6, .invCol = MAXCOLINV};

	SIT_GetValues(diag, SIT_UserData, &mcui.allItems, NULL);
	mcinv.items   = mcui.allItems;
	mcinv.itemsNb = itemGetInventoryByCat(mcui.allItems, mcui.curTab+1);
	selfinv.items = player->items + MAXCOLINV;
	toolbar.items = player->items;

	mcuiInitInventory(SIT_GetById(diag, "inv"),    &mcinv);
	mcuiInitInventory(SIT_GetById(diag, "player"), &selfinv);
	mcuiInitInventory(SIT_GetById(diag, "tb"),     &toolbar);

	mcuiResetScrollbar(&mcinv);

	SIT_GetValues(mcinv.cell, SIT_Padding, mcui.padding, NULL);
	mcui.itemSz = mcui.cellSz - mcui.padding[0] - mcui.padding[2];

	SIT_SetFocus(find);

	SIT_AddCallback(SIT_GetById(diag, "exch1"), SITE_OnActivate, mcuiExchangeLine, player);
	SIT_AddCallback(SIT_GetById(diag, "exch2"), SITE_OnActivate, mcuiExchangeLine, player);
	SIT_AddCallback(SIT_GetById(diag, "exch3"), SITE_OnActivate, mcuiExchangeLine, player);
	SIT_AddCallback(SIT_GetById(diag, "del"),   SITE_OnActivate, mcuiClearAll,     player);
	SIT_AddCallback(SIT_GetById(diag, "del"),   SITE_OnClick,    mcuiCancelDrag,   player);

	SIT_AddCallback(tab,         SITE_OnChange, mcuiChangeTab,   &mcinv);
	SIT_AddCallback(find,        SITE_OnChange, mcuiFilterItems, &mcinv);
	SIT_AddCallback(mcui.scroll, SITE_OnScroll, mcuiSetTop,      &mcinv);
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

void mcuiEditChestInventory(Inventory player, Item items, int count)
{
	/* same scale than player toolbar... */
	mcui.cellSz = roundf(mcui.width * 17 * ITEMSCALE / (3 * 182.));

	/* ... unless it doesn't fit within window's height */
	if (mcui.cellSz * (3 + 3 + 2) > mcui.height)
		mcui.cellSz = mcui.height / (3 + 3 + 2);

	SIT_Widget diag = SIT_CreateWidget("container", SIT_DIALOG, mcui.app,
		SIT_DialogStyles, SITV_Plain | SITV_Modal,
		NULL
	);

	SIT_CreateWidgets(diag,
		"<label name=msg title='Chest:'>"
		"<canvas composited=1 name=inv.inv left=FORM top=WIDGET,msg,0.5em/>"
		"<label name=msg2 title='Player inventory:' top=WIDGET,inv,0.3em>"
		"<canvas composited=1 name=player.inv top=WIDGET,msg2,0.3em/>"
		"<canvas composited=1 name=tb.inv left=FORM top=WIDGET,player,0.5em/>"
		"<tooltip name=info delayTime=", SITV_TooltipManualTrigger, "displayTime=10000 toolTipAnchor=", SITV_TooltipFollowMouse, ">"
	);

	mcui.toolTip = SIT_GetById(diag, "info");
	mcui.selCount = 0;
	mcui.groupCount = 0;
	mcui.cb = mcuiTransferItems;

	static struct MCInventory_t chest = {.invRow = 3, .invCol = MAXCOLINV, .groupId = 2};

	chest.invRow  = count / MAXCOLINV;
	chest.items   = items;
	chest.itemsNb = count;
	selfinv.items = player->items + MAXCOLINV;
	toolbar.items = player->items;

	mcuiInitInventory(SIT_GetById(diag, "inv"),    &chest);
	mcuiInitInventory(SIT_GetById(diag, "player"), &selfinv);
	mcuiInitInventory(SIT_GetById(diag, "tb"),     &toolbar);

	SIT_GetValues(chest.cell, SIT_Padding, mcui.padding, NULL);
	mcui.itemSz = mcui.cellSz - mcui.padding[0] - mcui.padding[2];

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
	*mcui.exitCode = 2;
	SIT_CloseDialog(w);
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

void mcuiCreateSignEdit(Map map, vec4 pos, int blockId, int * exit)
{
	SIT_Widget diag = SIT_CreateWidget("sign", SIT_DIALOG, mcui.app,
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
	mcui.signChunk = mapGetChunk(map, pos);
	mcui.exitCode = exit;

	if (uv[0] > uv[2]) swap(uv[0], uv[2]);
	if (uv[1] > uv[3]) swap(uv[1], uv[3]);

	int sz[2];
	int height = mcui.height / 4;
	int width  = height * (uv[2] - uv[0]) / (uv[3] - uv[1]);
	int image  = renderGetTerrain(sz);
	int fontsz = mcuiFontSize(mcui.app, signText, width, (height - height / 10) / 4);
	height = (fontsz * 4) * 14 / 10 + 20;
	width += 20;
	int fullw  = sz[0] * width  / (uv[2] - uv[0]);
	int fullh  = sz[1] * height / (uv[3] - uv[1]);
	sprintf(styles, "background: id(%d); background-size: %dpx %dpx; background-position: %dpx %dpx; padding: 10px; line-height: 1.3; font-size: %dpx",
		image, fullw, fullh, - fullw * uv[0] / sz[0] - 1, - fullh * uv[1] / sz[1] - 1, fontsz);

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
 * ask player a coordinate to jump to
 */
static float mcuiCurPos[3];
static int mcuiGetCoord(SIT_Widget w, APTR cd, APTR ud)
{
	memcpy(ud, mcuiCurPos, sizeof mcuiCurPos);
	SIT_CloseDialog(w);
	SIT_Exit(1);
	return 1;
}

void mcuiGoto(SIT_Widget parent, vec4 pos)
{
	SIT_Widget diag = SIT_CreateWidget("goto.bg", SIT_DIALOG, parent,
		SIT_DialogStyles, SITV_Plain | SITV_Modal | SITV_Movable,
		NULL
	);
	memcpy(mcuiCurPos, pos, 12);

	SIT_CreateWidgets(diag,
		"<label name=title title='Enter the coordinate you want to jump to:' left=FORM right=FORM style='text-align: center'>"
		"<label name=Xlab title=X:>"
		"<editbox name=X roundTo=2 editType=", SITV_Float, "width=10em scrollPos=", mcuiCurPos, "top=WIDGET,title,1em left=WIDGET,Xlab,0.5em>"
		"<label name=Ylab title=Y: left=WIDGET,X,1em>"
		"<editbox name=Y roundTo=2 editType=", SITV_Float, "width=10em scrollPos=", mcuiCurPos+1, "top=WIDGET,title,1em left=WIDGET,Ylab,0.5em>"
		"<label name=Zlab title=Z: left=WIDGET,Y,1em>"
		"<editbox name=Z roundTo=2 editType=", SITV_Float, "width=10em scrollPos=", mcuiCurPos+2, "top=WIDGET,title,1em left=WIDGET,Zlab,0.5em>"
		"<button name=ok title=Goto top=WIDGET,X,1em buttonType=", SITV_DefaultButton, ">"
		"<button name=ko title=Cancel top=WIDGET,X,1em right=FORM buttonType=", SITV_CancelButton, ">"
	);
	SIT_SetAttributes(diag,
		"<Xlab top=MIDDLE,X><Ylab top=MIDDLE,Y><Zlab top=MIDDLE,Z><ok right=WIDGET,ko,0.5em>"
	);
	SIT_AddCallback(SIT_GetById(diag, "ok"), SITE_OnActivate, mcuiGetCoord, pos);

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

static int mcuiExitWnd(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_Exit(1);
	return 1;
}

void mcuiAnalyze(SIT_Widget parent, Map map)
{
	SIT_Widget diag = SIT_CreateWidget("analyze.bg", SIT_DIALOG, parent,
		SIT_DialogStyles, SITV_Plain | SITV_Modal | SITV_Movable,
		NULL
	);

	mcui.itemSz = 0;
	SIT_CreateWidgets(diag,
		"<label name=total>"
		"<listbox name=list columnNames='\xC2\xA0\xC2\xA0\xC2\xA0\xC2\xA0\tCount\tName\tID' width=20em height=15em top=WIDGET,total,0.5em"
		" composited=1 cellPaint=", mcuiGrabItem, ">"
		"<button name=ok title=Ok top=WIDGET,list,1em right=FORM>"
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

	int * statistics = calloc(blockGetTotalStates(), sizeof (int));
	struct BlockIter_t iter;
	for (mapInitIter(map, &iter, pos, False); dy > 0; dy --)
	{
		for (j = 0; j < dz; j ++, mapIter(&iter, -dx, 0, 1))
		{
			for (i = 0; i < dx; i ++, mapIter(&iter, 1, 0, 0))
			{
				BlockState b = blockGetById(getBlockId(&iter));
				if (b->inventory == 0) continue;
				statistics[b - blockStates] ++;
			}
		}
		mapIter(&iter, 0, 1, -dz);
	}

	for (i = dx = 0, j = blockGetTotalStates(); i < j; i ++)
	{
		if (statistics[i] == 0) continue;
		dx += statistics[i];
		TEXT count[16];
		TEXT id[16];
		BlockState b = blockStates + i;
		sprintf(count, "%d", statistics[i]);
		sprintf(id, "%d:%d", b->id >> 4, b->id & 15);
		SIT_ListInsertItem(w, -1, (APTR) (int) b->id, "", count, b->name, id);
	}
	free(statistics);
	SIT_ListReorgColumns(w, "**-*");
	dz *= (int) (fabsf(points[VX] - points[VX+4]) + 1) *
	      (int) (fabsf(points[VY] - points[VY+4]) + 1);
	SIT_SetValues(SIT_GetById(diag, "total"), SIT_Title|XfMt, "Non air block selected: <b>%d</b><br>Blocks in volume: <b>%d</b>", dx, dz, NULL);

	SIT_ManageWidget(diag);
}
