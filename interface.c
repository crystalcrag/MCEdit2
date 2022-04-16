/*
 * interface.c: handle user interface for MCEdit (based on SITGL); contains code for the following interfaces:
 *  - creative inventory editor
 *  - chest/furnace/dropper/dispenser/hopper editor
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
#include "inventories.h"
#include "selection.h"
#include "mapUpdate.h"
#include "render.h"
#include "player.h"
#include "sign.h"
#include "undoredo.h"
#include "mcedit.h"
#include "globals.h"

static struct MCInterface_t mcui;
static struct MCInventory_t selfinv = {.invRow = 3, .invCol = MAXCOLINV, .groupId = 1, .itemsNb = MAXCOLINV * 3};
static struct MCInventory_t toolbar = {.invRow = 1, .invCol = MAXCOLINV, .groupId = 1, .itemsNb = MAXCOLINV};
static int category[] = {BUILD, DECO, REDSTONE, CROPS, RAILS, ALLCAT};


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
	mcui.clipItems = 0;
	mcui.resize = NULL;
	mcui.itemSize = inventoryReset();

	TEXT style[64];
	sprintf(style, "background: id(%d); background-size: 100%% 100%%", mcui.nvgImage);
	SIT_SetValues(globals.app, SIT_Style|XfMt, style, NULL);
}

/* start of a refresh phase */
void mcuiInitDrawItems(void)
{
	mcui.itemRender = 0;
}

/* refresh phase done: render items */
void mcuiDrawItems(void)
{
	int itemSz = mcui.itemSize[0];
	if (mcui.clipItems)
	{
		glScissor(mcui.clipRect[0], mcui.clipRect[1], mcui.clipRect[2], mcui.clipRect[3]);
		glEnable(GL_SCISSOR_TEST);
	}
	renderItems(mcui.items, mcui.itemRender, itemSz);
	if (mcui.clipItems)
		glDisable(GL_SCISSOR_TEST);

	Item drag = inventoryDraggedItem();

	if (drag->id > 0)
	{
		struct Item_t item = *drag;
		item.x -= itemSz/2;
		item.y += itemSz/2;
		renderItems(&item, 1, itemSz);
	}
}

void mcuiResize(void)
{
	inventoryResize();
	if (mcui.resize)
		mcui.resize(NULL, NULL, NULL);
}

Item mcuiAddItemToRender(void)
{
	return mcui.items + mcui.itemRender ++;
}

/*
 * save before exit
 */
void mcuiAskSave(SIT_CallProc cb)
{
	SIT_Widget ask = SIT_CreateWidget("ask.mc", SIT_DIALOG, globals.app,
		SIT_DialogStyles, SITV_Plain | SITV_Modal,
		SIT_Style,        "padding: 1em",
		NULL
	);

	SIT_CreateWidgets(ask,
		"<label name=label title=", LANG("Some changes have not been saved, what do you want to do ?"), ">"
		"<button name=cancel.act title=", LANG("Cancel"), "top=WIDGET,label,1em right=FORM buttonType=", SITV_CancelButton, ">"
		"<button name=ok.act title=", LANG("Save"), "top=OPPOSITE,cancel nextCtrl=cancel right=WIDGET,cancel,0.5em buttonType=", SITV_DefaultButton, ">"
		"<button name=ko.act title=", LANG("Don't save"), "top=OPPOSITE,ok nextCtrl=ok right=WIDGET,ok,0.5em>"
	);

	SIT_AddCallback(SIT_GetById(ask, "cancel"), SITE_OnActivate, cb, (APTR) 2);
	SIT_AddCallback(SIT_GetById(ask, "ok"),     SITE_OnActivate, cb, (APTR) 1);
	SIT_AddCallback(SIT_GetById(ask, "ko"),     SITE_OnActivate, cb, (APTR) 0);
	SIT_ManageWidget(ask);
}

/*
 * creative inventory editor
 */


/* SITE_OnActivate on exch button */
static int mcuiExchangeLine(SIT_Widget w, APTR cd, APTR ud)
{
	Inventory player = ud;
	Item_t    items[MAXCOLINV];
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
	Item drag = inventoryDraggedItem();
	if (drag->id > 0)
	{
		/* an item was dragged and click over delete button: cancel drag instead of clearing whole inventory */
		drag->id = 0;
		SIT_InitDrag(NULL);
		SIT_ForceRefresh();
	}
	else
	{
		Inventory player = ud;
		memset(player->items, 0, sizeof player->items);
	}
	return 1;
}

/* SITE_OnClick on clear button */
static int mcuiCancelDrag(SIT_Widget w, APTR cd, APTR ud)
{
	/* will prevent from clearing the whole inventory if clicked with an item being dragged */
	return 1;
}

/* SITE_OnChange on tab: click on a different tab */
static int mcuiChangeTab(SIT_Widget w, APTR cd, APTR ud)
{
	MCInventory inv = ud;
	inv->top = 0;
	inv->itemsNb = itemGetInventoryByCat(inv->items, category[mcui.curTab = (int)cd]);
	inventoryResetScrollbar(inv);
	return 1;
}

static int mcuiResizeCreativeInv(SIT_Widget w, APTR cd, APTR ud)
{
	/* some values are hardcodrd outside of SITGL control */
	int cellSz = mcui.itemSize[1];
	SIT_SetAttributes(mcui.curDialog,
		"<exch1 height=", cellSz, ">"
		"<exch2 height=", cellSz, ">"
		"<exch3 height=", cellSz, ">"
		"<del   height=", cellSz, ">"
	);
	SIT_Widget tab = SIT_GetById(mcui.curDialog, "items");
	int i;
	for (i = 0; i < 6; i ++)
	{
		w = SIT_TabGetNth(tab, i);
		SIT_SetValues(w, SIT_LabelSize, SITV_LabelSize(cellSz, cellSz), NULL);
	}
	return 1;
}

/* SITE_OnPaint handler for tab items */
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
	item->y = globals.height - (paint->y + padding[1]/2) - mcui.itemSize[0];
	item->id = (int) blockId;
	item->count = 1;
	return 0;
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
	inventoryResetScrollbar(inv);

	return 1;
}

void mcuiCreateInventory(Inventory player)
{
	static TEXT tip[] = "Exchange row with toolbar";

	SIT_Widget diag = SIT_CreateWidget("inventory", SIT_DIALOG + SIT_EXTRA(itemGetInventoryByCat(NULL, 0) * sizeof (struct Item_t)), globals.app,
		SIT_DialogStyles, SITV_Plain | SITV_Modal,
		NULL
	);

	SIT_CreateWidgets(diag,
		"<tab name=items.bg left=FORM right=FORM top=FORM bottom=FORM tabSpace=4 tabActive=", mcui.curTab, "tabStr='\t\t\t\t\t'>"
		" <editbox name=search right=FORM buddyLabel=", LANG("Search:"), NULL, ">"
		" <canvas composited=1 name=inv.inv left=FORM top=WIDGET,search,0.5em nextCtrl=LAST/>"
		" <scrollbar width=1.2em name=scroll.inv wheelMult=1 top=OPPOSITE,inv,0 bottom=OPPOSITE,inv,0 right=FORM>"
		" <label name=msg title=", LANG("Player inventory:"), "top=WIDGET,inv,0.3em>"
		" <canvas composited=1 name=player.inv top=WIDGET,msg,0.3em  nextCtrl=LAST/>"
		" <canvas composited=1 name=tb.inv left=FORM top=WIDGET,player,0.5em  nextCtrl=LAST/>"
		" <button name=exch1.exch nextCtrl=NONE top=OPPOSITE,player right=FORM tooltip=", tip, "maxWidth=scroll>"
		" <button name=exch2.exch nextCtrl=NONE top=WIDGET,exch1 right=FORM tooltip=", tip, "maxWidth=exch1>"
		" <button name=exch3.exch nextCtrl=NONE top=WIDGET,exch2 right=FORM tooltip=", tip, "maxWidth=exch2>"
		" <button name=del.exch   nextCtrl=NONE top=OPPOSITE,tb title='X' right=FORM tooltip=", LANG("Clear inventory"), "maxWidth=exch3>"
		"</tab>"
		"<tooltip name=info delayTime=", SITV_TooltipManualTrigger, " displayTime=10000 toolTipAnchor=", SITV_TooltipFollowMouse, ">"
	);

	SIT_SetAttributes(diag, "<searchtxt top=MIDDLE,search><inv right=WIDGET,scroll,0.2em>");

	/* callbacks registration */
	mcui.curDialog = diag;
	mcui.resize = mcuiResizeCreativeInv;

	static struct MCInventory_t mcinv = {.invRow = 6, .invCol = MAXCOLINV, .movable = INV_PICK_ONLY};

	SIT_GetValues(diag, SIT_UserData, &mcui.allItems, NULL);
	mcinv.items   = mcui.allItems;
	mcinv.itemsNb = itemGetInventoryByCat(mcui.allItems, category[mcui.curTab]);
	mcinv.scroll  = SIT_GetById(diag, "scroll");
	selfinv.items = player->items + MAXCOLINV;
	toolbar.items = player->items;

	inventoryInit(&mcinv,   SIT_GetById(diag, "inv"),    6 + 3 + 2 + 2 + 3);
	inventoryInit(&selfinv, SIT_GetById(diag, "player"), 0);
	inventoryInit(&toolbar, SIT_GetById(diag, "tb"),     0);

	inventoryResetScrollbar(&mcinv);

	/* icons for tab: will be rendered as MC items */
	SIT_Widget tab  = SIT_GetById(diag, "items");
	SIT_Widget find = SIT_GetById(diag, "search");
	int i;
	for (i = 0; i < 6; i ++)
	{
		/* tab icons:           build     deco        redstone       crops          rails      search/all */
		static ItemID_t blockId[] = {ID(45,0), ID(175,15), ITEMID(331,0), ITEMID(260,0), ID(27, 0), ITEMID(345,0)};
		SIT_Widget w = SIT_TabGetNth(tab, i);
		SIT_SetValues(w, SIT_UserData, (APTR) blockId[i], NULL);
		SIT_AddCallback(w, SITE_OnPaint, mcuiGrabItemCoord, NULL);
	}

	mcuiResizeCreativeInv(diag, NULL, NULL);
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
 * single/double chest (ender, shulker or normal), dispenser, dropper inventory editor
 */
void mcuiEditChestInventory(Inventory player, Item items, int count, Block type)
{
	static struct MCInventory_t chest = {.invRow = 3, .invCol = MAXCOLINV, .groupId = 2, .movable = INV_TRANSFER};
	static struct MCInventory_t slot0 = {.invRow = 1, .invCol = 1, .groupId = 3, .itemsNb = 1};
	static struct MCInventory_t slot1 = {.invRow = 1, .invCol = 1, .groupId = 4, .itemsNb = 1};
	static struct MCInventory_t slot2 = {.invRow = 1, .invCol = 1, .groupId = 5, .itemsNb = 1};

	SIT_Widget diag = SIT_CreateWidget("container", SIT_DIALOG, globals.app,
		SIT_DialogStyles, SITV_Plain | SITV_Modal,
		NULL
	);

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
				"<inv left=", SITV_AttachCenter, ">"
				"<msg left=", SITV_AttachCenter, ">"
			);
			if (count == MAXCOLINV)
				chest.invRow = chest.invCol = 3;
			else
				chest.invRow = 1, chest.invCol = count;
		}
		chest.items   = items;
		chest.itemsNb = count;
		inventoryInit(&chest, SIT_GetById(diag, "inv"), 3 + 3 + 2);
	}
	else /* furnace */
	{
		TEXT buffer[32];
		snprintf(buffer, sizeof buffer, "%s:", LANG("Furnace"));
		SIT_CreateWidgets(diag,
			/* fire should be between slot0 and slot1, but who cares? */
			"<label name=msg title=", buffer, "left=", SITV_AttachCenter, ">"
			"<label name=furnace imagePath=furnace.png left=", SITV_AttachCenter, "top=WIDGET,msg,2em>"
			"<canvas composited=1 name=slot0.inv right=WIDGET,furnace,1em bottom=WIDGET,furnace,-0.5em nextCtrl=LAST/>"
			"<canvas composited=1 name=inv.inv right=WIDGET,furnace,1em top=WIDGET,furnace,-0.5em nextCtrl=LAST/>"
			"<canvas composited=1 name=slot2.inv left=WIDGET,furnace,1em top=MIDDLE,furnace nextCtrl=LAST/>"
		);
		slot0.items = items;
		slot1.items = items + 1;
		slot2.items = items + 2;
		inventoryInit(&slot0, SIT_GetById(diag, "slot0"), 1);
		inventoryInit(&slot1, SIT_GetById(diag, "inv"),   0);
		inventoryInit(&slot2, SIT_GetById(diag, "slot2"), 0);
		SIT_SetValues(SIT_GetById(diag, "furnace"), SIT_Height, mcui.itemSize[0]/2, NULL);
	}

	SIT_CreateWidgets(diag,
		"<label name=msg2 title=", LANG("Player inventory:"), "top=WIDGET,inv,0.3em>"
		"<canvas composited=1 name=player.inv top=WIDGET,msg2,0.3em nextCtrl=LAST/>"
		"<canvas composited=1 name=tb.inv left=FORM top=WIDGET,player,0.5em nextCtrl=LAST/>"
		"<tooltip name=info delayTime=", SITV_TooltipManualTrigger, "displayTime=10000 toolTipAnchor=", SITV_TooltipFollowMouse, ">"
	);

	selfinv.items = player->items + MAXCOLINV;
	toolbar.items = player->items;

	inventoryInit(&toolbar, SIT_GetById(diag, "tb"),     0);
	inventoryInit(&selfinv, SIT_GetById(diag, "player"), 0);

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
	SIT_Exit(3);
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
	int height = globals.height / 4;
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
		"<label name=msg title=", LANG("Edit sign message:"), "left=", SITV_AttachCenter, ">"
		"<editbox name=signedit title=", signText, " wordWrap=", SITV_WWChar, "editType=", SITV_Multiline, "width=", width, "height=", height,
		" maxLines=4 tabStyle=", SITV_TabEditForbid, "style=", styles, "top=WIDGET,msg,4em>"
		"<button name=ok title=", LANG("Done"), "left=OPPOSITE,signedit top=WIDGET,signedit,4em left=", SITV_AttachCenter, ">"
	);
	SIT_Widget text = SIT_GetById(diag, "signedit");
	SIT_AddCallback(SIT_GetById(diag, "ok"), SITE_OnActivate, mcuiSaveSign, text);
	SIT_SetFocus(text);

	SIT_ManageWidget(diag);
}


/*
 * show a summary about all the blocks in the selection
 */

/* only grab items to render */
static int mcuiGrabItem(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_OnCellPaint * ocp = cd;

	if ((ocp->rowColumn & 0xff) > 0) return 0;

	mcui.itemSize[0] = ocp->LTWH[3] - 2;
	mcui.clipItems = 1;
	SIT_GetValues(w, SIT_ClientRect, mcui.clipRect, NULL);
	mcui.clipRect[1] = globals.height - mcui.clipRect[1] - mcui.clipRect[3];

	/* note: itemRender is set to 0 before rendering loop starts */
	Item item = mcui.items + mcui.itemRender ++;
	APTR rowTag;

	SIT_GetValues(w, SIT_RowTag(ocp->rowColumn>>8), &rowTag, NULL);

	item->x = ocp->LTWH[0];
	item->y = globals.height - ocp->LTWH[1] - ocp->LTWH[3] + 1;
	item->id = (int) rowTag;
	item->count = 1;
	return 0;
}

int mcuiExitWnd(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_Exit(EXIT_LOOP);
	return 1;
}

struct InvBlock_t
{
	STRPTR tech;
	int    cat;
};

typedef struct TileList_t *     TileList;
typedef struct ItemStat_t *     ItemStat;

struct TileList_t
{
	SIT_Widget list;
	SIT_Widget listSub;
	SIT_Widget stat, ok, back;
	DATA8 *    groups;
	TEXT       filter[32];
	int        max;
	int        count[6];
	int        volume, nonAir, isSub;
};

struct ItemStat_t
{
	ItemID_t itemId;
	int      count;
	int      tileId;
};

static int mcuiCopyAnalyze(SIT_Widget w, APTR cd, APTR ud)
{
	TileList tiles = ud;
	STRPTR   bytes = malloc(256);
	int      max   = 256;
	int      i, nb, usage;

	/* copy info into clipboard */
	SIT_Widget list = tiles->isSub ? tiles->listSub : tiles->list;
	SIT_GetValues(list, SIT_ItemCount, &nb, NULL);
	usage = sprintf(bytes, "Number,Type,ID\n");
	for (i = 0; i < nb; i ++)
	{
		STRPTR count = SIT_ListGetCellText(list, 1, i);
		STRPTR name  = SIT_ListGetCellText(list, 2, i);
		STRPTR id    = SIT_ListGetCellText(list, 3, i);

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

/* keep a list of all the tile entities from container we might want to see the content of */
static void storeTileEntity(TileList tiles, int type, DATA8 tile)
{
	tiles->count[0] ++;
	if (tiles->max < tiles->count[0])
	{
		tiles->max = (tiles->count[0] + 127) & ~127;
		DATA8 * list = realloc(tiles->groups, tiles->max * sizeof (DATA8));
		if (! list) return;
		tiles->groups = list;
	}
	int i, j;
	for (i = 1, j = 0; i < type; j += tiles->count[i], i ++);
	memmove(tiles->groups + j + 1, tiles->groups, (tiles->count[0] - j) * sizeof (DATA8));
	tiles->count[type] ++;
	tiles->groups[j] = tile;
}

#define invType       copyModel /* this field is not needed past initialization phase */

static int mergeItems(ItemStat * ret, int max, DATA8 tile, int tileId)
{
	ItemStat  stat  = *ret;
	NBTFile_t nbt   = {.mem = tile};
	int       items = NBT_FindNode(&nbt, 0, "Items");
	NBTIter_t iter;

	if (items < 0)
		return max;

	NBT_InitIter(&nbt, items, &iter);
	/* scan all items of container */
	next_item:
	while ((items = NBT_Iter(&iter)) > 0)
	{
		NBTIter_t properties;
		ItemStat  base;
		ItemID_t  id;
		TEXT      itemId[16];
		int       off, count, data, num;
		/* scan properties of item */
		NBT_IterCompound(&properties, nbt.mem + items);
		id = count = data = 0;
		while ((off = NBT_Iter(&properties)) >= 0)
		{
			switch (FindInList("id,Count,Damage", properties.name, 0)) {
			case 0:  id    = itemGetByName(inventoryItemName(&nbt, items + off, itemId), True); break;
			case 1:  count = NBT_GetInt(&nbt, items + off, 1); break;
			case 2:  data  = NBT_GetInt(&nbt, items + off, 0);
			}
		}
		if (itemMaxDurability(id) < 0)
			id += data;
		/* binary search based on item id */
		for (num = max, base = stat; num > 0; num >>= 1)
		{
			ItemStat pivot = base + (num >> 1);
			int compare = pivot->itemId - id;
			if (compare == 0)
			{
				pivot->count += count;
				goto next_item;
			}
			if (compare < 0)
				base = pivot + 1, num --;
		}
		/* not in the list, add it now */
		num = (max + 127) & ~127;
		if (max + 1 > num)
		{
			num = (max + 128) & ~127;
			off = base - stat;
			stat = realloc(stat, num * sizeof *stat);
			if (! stat) return max;
			base = stat + off;
		}
		memmove(base + 1, base, (max - (base - stat)) * sizeof *base);
		base->itemId = id;
		base->count = count;
		base->tileId = tileId;
		max ++;
	}
	*ret = stat;
	return max;
}


static void mcuiAnalyzeAddItem(TileList tiles, ItemID_t itemId, int num, int tileId)
{
	STRPTR desc;
	STRPTR column0 = "";
	TEXT   count[16];
	TEXT   id[16];

	FormatNumber(count, sizeof count, "%d", num);
	BlockState b = NULL;
	if (isBlockId(itemId))
	{
		b = blockGetById(itemId);
		desc = b->name;
		if (b->inventory == 0)
			/* check if there is an item that generate these type of block */
			itemId = itemCanCreateBlock(itemId, &desc);
	}

	if (isBlockId(itemId))
	{
		Block block = &blockIds[itemId>>4];
		switch (block->orientHint) {
		case ORIENT_LOG:
		case ORIENT_LEVER:
		case ORIENT_BED:
		case ORIENT_TORCH:
		case ORIENT_SNOW:
		case ORIENT_SWNE:
		case ORIENT_NSWE:
		case ORIENT_FULL:
		case ORIENT_RAILS:
		case ORIENT_STAIRS:
		case ORIENT_DOOR:
			/* metadata is useless for these types of blocks */
			sprintf(id, "%d:0", itemId >> 4);
			desc = block->name;
			if (block->invState > 0)
				itemId += block->invState;
			break;
		default:
			if (! isBlockId(itemId))
				id[0] = 0;
			else
				sprintf(id, "%d:%d", itemId >> 4, itemId & 15);
		}
		if (tileId < 0 && block->invType > 0 && tiles->count[block->invType] > 0)
			column0 = "\xC2\xA0\xC2\xA0\xC2\xA0\xC2\xA0...";
	}
	else
	{
		ItemDesc item = itemGetById(itemId);
		if (item == NULL) return;
		desc = item->name;
		if (b)
			sprintf(id, "%d", b->id>>4);
		else
			id[0] = 0;
	}
	SIT_Widget list = tileId < 0 ? tiles->list : tiles->listSub;
	int row = SIT_ListInsertItem(list, -1, (APTR) itemId, column0, count, desc, id);
	if (tileId > 0)
		SIT_SetValues(list, SIT_CellTag(row, 1), (APTR) tileId, NULL);
}


static void mcuiSetAnalyzeTitle(SIT_Widget title, STRPTR msg, int param1, int param2)
{
	STRPTR buffer = alloca(strlen(msg) + 40);
	STRPTR p;

	for (p = buffer; *msg; msg ++)
	{
		if (msg[0] == '%' && msg[1] == 'd')
		{
			p += FormatNumber(p, 20, "%d", param1);
			param1 = param2;
			msg ++;
		}
		else *p++ = *msg;
	}
	*p = 0;

	SIT_SetValues(title, SIT_Title, buffer, NULL);
}


/* double-click on an item in the list */
static int mcuiExpandAnalyze(SIT_Widget w, APTR cd, APTR ud)
{
	TileList tiles = ud;
	ItemID_t itemId = (int) cd;

	if (isBlockId(itemId))
	{
		Block block = &blockIds[itemId >> 4];
		if (block->invType > 0 && tiles->count[block->invType] > 0)
		{
			ItemStat stats = NULL;
			DATA8 *  tile  = NULL;
			int      count = 0;
			int      i, j, total, stacks;
			/* gather all items from all containers of the group */
			for (i = 1, j = 0; i < block->invType; j += tiles->count[i], i ++);
			for (i = tiles->count[i], tile = tiles->groups + j; i > 0; j ++, i --, tile ++)
				count = mergeItems(&stats, count, *tile, j);

			SIT_SetValues(tiles->list, SIT_Visible, False, NULL);
			SIT_SetValues(tiles->ok, SIT_TopObject, tiles->listSub, NULL);
			SIT_SetValues(tiles->listSub, SIT_Visible, True, NULL);
			SIT_SetValues(tiles->back, SIT_Visible, True, NULL);
			ItemStat list;
			for (i = total = stacks = 0, list = stats; i < count; i ++, list ++)
			{
				mcuiAnalyzeAddItem(tiles, list->itemId, list->count, list->tileId);
				total += list->count;
				if (! isBlockId(list->itemId))
				{
					ItemDesc item = itemGetById(list->itemId);
					if (item) stacks += (list->count + item->stack - 1) / item->stack;
				}
				else stacks += (list->count + 63) / 64;
			}
			SIT_ListReorgColumns(tiles->listSub, "**-*");
			free(stats);
			tiles->isSub = 1;
			mcuiSetAnalyzeTitle(tiles->stat, LANG("Items in containers: <b>%d</b><br>Stacks needed: <b>%d</b>"), total, stacks);
		}
	}
	return 1;
}

/* double-click on an item in the detail list: select container it was first found */
static int mcuiSelectContainer(SIT_Widget w, APTR cd, APTR ud)
{
	TileList  tiles = ud;
	vec4      pos;
	NBTIter_t iter;
	int       i;
	APTR      tileId;

	SIT_GetValues(w, SIT_SelectedIndex, &i, NULL);
	SIT_GetValues(w, SIT_CellTag(i, 1), &tileId, NULL);

	NBTFile_t nbt = {.mem = tiles->groups[(int) tileId]};
	NBT_IterCompound(&iter, nbt.mem);
	while ((i = NBT_Iter(&iter)) >= 0)
	{
		switch (FindInList("x,y,z", iter.name, 0)) {
		case 0: NBT_GetFloat(&nbt, i, pos + VX, 1); break;
		case 1: NBT_GetFloat(&nbt, i, pos + VY, 1); break;
		case 2: NBT_GetFloat(&nbt, i, pos + VZ, 1);
		}
	}

	selectionSetPoint(1, pos, SEL_POINT_1);
	selectionSetPoint(1, pos, SEL_POINT_2);

	SIT_Exit(EXIT_LOOP);
	return 1;
}

/* "Back" button */
static int mcuiAnalyzeBackToList(SIT_Widget w, APTR cd, APTR ud)
{
	TileList tiles = ud;
	SIT_ListDeleteRow(tiles->listSub, DeleteAllRows);
	SIT_SetValues(tiles->list, SIT_Visible, True, NULL);
	SIT_SetValues(tiles->ok, SIT_TopObject, tiles->list, NULL);
	SIT_SetValues(tiles->listSub, SIT_Visible, False, NULL);
	SIT_SetValues(tiles->back, SIT_Visible, False, NULL);
	mcuiSetAnalyzeTitle(tiles->stat, LANG("Non air block selected: <b>%d</b><br>Blocks in volume: <b>%d</b>"), tiles->nonAir, tiles->volume);
	tiles->isSub = 0;
	return 1;
}


/* OnChange of text filter */
static int mcuiFilterList(SIT_Widget w, APTR cd, APTR ud)
{
	TileList   tiles = ud;
	TEXT       search[32];
	int        row, rows;
	SIT_Widget list = tiles->isSub ? tiles->listSub : tiles->list;

	/* convert to lowercase */
	CopyString(search, cd, sizeof search);
	StrToLower(search, -1);
	SIT_GetValues(list, SIT_ItemCount, &rows, NULL);

	if (search[0] == 0)
	{
		/* show everything */
		for (row = 0; row < rows; row ++)
			SIT_ListSetRowVisibility(list, row, True);
	}
	else for (row = 0; row < rows; row ++)
	{
		STRPTR name = SIT_ListGetCellText(list, 2, row);

		/* no need to bother with a more complicated sub-string search */
		DATA8 p;
		char  match = 0;
		for (p = name; *p; p ++)
		{
			uint8_t chr = *p;
			if ('A' <= chr && chr <= 'Z') chr += 32;
			if (search[0] == chr)
			{
				DATA8 s, s2;
				for (s = search + 1, s2 = p + 1; *s; s ++, s2 ++)
				{
					chr = *s2;
					if ('A' <= chr && chr <= 'Z') chr += 32;
					if (chr != *s) break;
				}
				if (*s == 0) { match = 1; break; }
			}
		}
		SIT_ListSetRowVisibility(list, row, match);
	}
	return 1;
}

/* SITE_OnFinalize */
static int mcuiClearAnalyze(SIT_Widget w, APTR cd, APTR ud)
{
	TileList tiles = ud;
	free(tiles->groups);
	return 1;
}

/* main interface creation */
void mcuiAnalyze(void)
{
	static struct InvBlock_t invTypes[] = { /* group common containers */
		{"chest",             1},
		{"trapped_chest",     1},
		{"ender_chest",       2},
		{"dispenser",         3},
		{"dropper",           3},
		{"furnace",           4},
		{"lit_furnace",       4},
		{"white_shulker_box", 5},
	};
	static struct TileList_t tiles;
	SIT_Widget diag = SIT_CreateWidget("analyze.bg", SIT_DIALOG, globals.app,
		SIT_DialogStyles, SITV_Plain | SITV_Modal | SITV_Movable,
		NULL
	);
	TEXT columns[64];
	snprintf(columns, sizeof columns, "\xC2\xA0\xC2\xA0\xC2\xA0\xC2\xA0\t%s\t%s\t%s", LANG("Count"), LANG("Name"), LANG("ID"));

	mcui.itemSize[0] = 0;
	memset(&tiles, 0, sizeof tiles);
	SIT_CreateWidgets(diag,
		"<label name=total>"
		"<listbox name=list columnNames=", columns, "width=25em height=15em top=WIDGET,total,0.5em"
		" composited=1 cellPaint=", mcuiGrabItem, ">"
		"<listbox name=chest columnNames=", columns, "width=25em height=15em top=WIDGET,total,0.5em"
		" composited=1 cellPaint=", mcuiGrabItem, "visible=0>"
		"<editbox name=search editLength=", sizeof tiles.filter, "editBuffer=", tiles.filter, "buddyLabel=", LANG("Search:"), NULL,
		" right=FORM top=WIDGET,list,0.2em>"
		"<button name=ok title=", LANG("Close"), "top=WIDGET,search,1em right=FORM buttonType=", SITV_DefaultButton, ">"
		"<button name=save title=", LANG("Copy to clipboard"), "right=WIDGET,ok,0.5em top=OPPOSITE,ok>"
		"<button name=back title=", LANG("Back"), "right=WIDGET,save,0.5em top=OPPOSITE,ok visible=0>"
	);
	tiles.list = SIT_GetById(diag, "list");
	tiles.stat = SIT_GetById(diag, "total");
	tiles.back = SIT_GetById(diag, "back");
	tiles.listSub = SIT_GetById(diag, "chest");
	tiles.ok = SIT_GetById(diag, "search");

	SIT_AddCallback(tiles.ok, SITE_OnChange, mcuiFilterList, &tiles);
	SIT_AddCallback(SIT_GetById(diag, "ok"), SITE_OnActivate, mcuiExitWnd, NULL);
	SIT_AddCallback(tiles.list, SITE_OnActivate, mcuiExpandAnalyze, &tiles);
	SIT_AddCallback(tiles.back, SITE_OnActivate, mcuiAnalyzeBackToList, &tiles);
	SIT_AddCallback(tiles.listSub, SITE_OnActivate, mcuiSelectContainer, &tiles);
	SIT_AddCallback(SIT_GetById(diag, "save"), SITE_OnActivate, mcuiCopyAnalyze, &tiles);
	SIT_AddCallback(diag, SITE_OnFinalize, mcuiClearAnalyze, &tiles);

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

	Block block;
	for (i = 0; i < DIM(invTypes); i ++)
	{
		ItemID_t id = itemGetByName(invTypes[i].tech, False);
		block = &blockIds[id >> 4];
		block->invType = invTypes[i].cat;
	}
	/* select remaining type (color) of shulker box */
	for (i = block->invType, block ++; block->tileEntity; block ++)
		block->invType = i;

	int * statistics = calloc(blockLast - blockStates, sizeof (int));
	struct BlockIter_t iter;
	for (mapInitIter(globals.level, &iter, pos, False); dy > 0; dy --)
	{
		for (j = 0; j < dz; j ++, mapIter(&iter, -dx, 0, 1))
		{
			for (i = 0; i < dx; i ++, mapIter(&iter, 1, 0, 0))
			{
				BlockState b = blockGetById(getBlockId(&iter));
				if (b->id == 0) continue;
				uint16_t id = b->id;
				uint8_t data = id & 15;
				/* check if we can use alternative block state */
				block = &blockIds[id >> 4];
				switch (block->orientHint) {
				case ORIENT_LOG:
					if (4 <= data && data < 12) id -= data & ~3;
					break;
				case ORIENT_SLAB:
					id -= data & ~7;
					break;
				case ORIENT_BED:
				case ORIENT_LEVER:
				case ORIENT_SNOW:
				case ORIENT_TORCH:
				case ORIENT_FULL:
				case ORIENT_RAILS:
				case ORIENT_STAIRS:
				case ORIENT_NSWE:
					id -= data;
					if (id == ID(RSTORCH_OFF, 0))
						id = ID(RSTORCH_ON, 0);
					break;
				case ORIENT_SWNE:
					id -= data & 3;
					break;
				case ORIENT_DOOR:
					if (data >= 8) continue;
					id -= data;
					break;
				default:
					if (block->special == BLOCK_RSWIRE)
						id -= data;
				}
				b = blockGetById(id);
				DATA8 tile;
				/* check if there are items we might want to inspect in this block */
				if (block->invType > 0 && (tile = chunkGetTileEntity(iter.ref, (int[3]) {iter.x, iter.yabs, iter.z})))
				{
					NBTFile_t nbt = {.mem = tile};
					int items = NBT_FindNode(&nbt, 0, "Items");
					if (items >= 0)
					{
						NBTHdr hdr = NBT_Hdr(&nbt, items);
						if (hdr->type == TAG_List_Compound)
							storeTileEntity(&tiles, block->invType, tile);
					}
				}
				statistics[b - blockStates] ++;
			}
		}
		mapIter(&iter, 0, 1, -dz);
	}

	/* build list of items */
	for (i = dx = 0, j = blockLast - blockStates; i < j; i ++)
	{
		if (statistics[i] == 0) continue;
		dx += statistics[i];
		mcuiAnalyzeAddItem(&tiles, blockStates[i].id, statistics[i], -1);
	}
	free(statistics);
	SIT_ListReorgColumns(tiles.list, "**-*");
	tiles.nonAir = dx;
	tiles.volume = dz * (int) (fabsf(points[VX] - points[VX+4]) + 1) *
	                    (int) (fabsf(points[VY] - points[VY+4]) + 1);

	mcuiAnalyzeBackToList(NULL, NULL, &tiles);
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
	inventoryResetScrollbar(inv);

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
		undoLog(LOG_REGION_END);
		SIT_Exit(EXIT_LOOP);
		/* will cancel the timer */
		return -1;
	}
	double timeMS = FrameGetTime();
	if (timeMS - mcuiRepWnd.processStart > 250)
	{
		/* wait a bit before showing/updating progress bar */
		SIT_Widget msg = SIT_GetById(mcuiRepWnd.prog, "../text");
		if (msg) SIT_SetValues(msg, SIT_Visible, False, NULL);
		SIT_SetValues(mcuiRepWnd.prog, SIT_Visible, True, SIT_ProgressPos, (int)
			(uint64_t) mcuiRepWnd.processCurrent * 100 / mcuiRepWnd.processTotal, NULL);
		mcuiRepWnd.processStart = timeMS;
	}
	return 0;
}

/* OnActivate on fill/replace button */
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

	/* register an undo operation for this */
	int range[6];
	selectionGetRange(range, True);
	undoLog(LOG_REGION_START, range);

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
	SIT_SetValues(mcuiRepWnd.accept, SIT_Title, LANG(checked ? "Replace" : "Fill"), NULL);
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
		SIT_Exit(EXIT_LOOP);
	if (mcuiRepWnd.asyncCheck)
	{
		selectionCancelOperation();
		SIT_ActionReschedule(mcuiRepWnd.asyncCheck, -1, -1);
		mcuiRepWnd.asyncCheck = NULL;
		/* show what's been modified */
		mapUpdateEnd(globals.level);
		undoLog(LOG_REGION_END);
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

	SIT_Widget diag = SIT_CreateWidget("fillblock.bg", SIT_DIALOG + SIT_EXTRA((blockLast - blockStates) * sizeof (struct Item_t)), globals.app,
		SIT_DialogStyles, SITV_Plain | SITV_Modal | SITV_Movable,
		SIT_Style,        "padding-top: 0.2em",
		NULL
	);

	mcuiRepWnd.asyncCheck = NULL;

	SIT_CreateWidgets(diag,
		"<label name=dlgtitle.big title=", LANG(fillWithBrush ? DLANG("Geometric brush fill") : DLANG("Fill or replace selection")),
		" left=", SITV_AttachCenter, ">"
		"<editbox name=search right=FORM top=WIDGET,dlgtitle,0.3em buddyLabel=", LANG("Search:"), NULL, ">"
		"<canvas composited=1 name=inv.inv left=FORM top=WIDGET,search,0.5em nextCtrl=LAST/>"
		"<scrollbar width=1.2em name=scroll.inv wheelMult=1 top=OPPOSITE,inv,0 bottom=OPPOSITE,inv,0 right=FORM>"
		"<label name=msg title=", LANG("Fill:"), ">"
		"<canvas composited=1 name=fill.inv left=WIDGET,msg,0.5em top=WIDGET,inv,0.5em nextCtrl=LAST/>"
	);

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

	if (fillWithBrush)
	{
		/* interface to fill selection with a geometric brush */
		TEXT cylinder[32];
		mcuiRepWnd.axisCylinder = selectionCylinderAxis(size, globals.direction);
		sprintf(cylinder, LANG("Cylinder (%c)"), LANG("WLH")[mcuiRepWnd.axisCylinder]);

		SIT_CreateWidgets(diag,
			"<label name=label1 title=", LANG("Shape:"), "left=WIDGET,fill,0.5em top=MIDDLE,fill>"
			"<button name=shape1 title=", LANG("Round"), "checkState=1 curValue=", &mcuiRepWnd.shape, "buttonType=", SITV_RadioButton,
			" top=OPPOSITE,fill left=WIDGET,label1,0.5em>"
			"<button name=shape2 title=", cylinder, "curValue=", &mcuiRepWnd.shape, "buttonType=", SITV_RadioButton,
			" top=WIDGET,shape1,0.3em left=WIDGET,label1,0.5em maxWidth=shape1>"
			"<button name=shape3 title=", LANG("Diamond"), "curValue=", &mcuiRepWnd.shape, "buttonType=", SITV_RadioButton,
			" top=WIDGET,shape2,0.3em left=WIDGET,label1,0.5em maxWidth=shape2>"

			"<button name=outside title=", LANG("Fill outer area"), "curValue=", &mcuiRepWnd.outerArea, "buttonType=", SITV_CheckBox,
			" left=WIDGET,shape1,1em top=OPPOSITE,fill>"
			"<button name=hollow curValue=", &mcuiRepWnd.isHollow, "buttonType=", SITV_CheckBox, "title=", LANG("Hollow"),
			" left=WIDGET,shape1,1em top=WIDGET,outside,0.3em>"
			"<button name=half title=", LANG("Fill with air"), "tooltip=", LANG("Only if hollow"), "curValue=", &mcuiRepWnd.fillAir,
			" buttonType=", SITV_CheckBox, "left=WIDGET,shape1,1em top=WIDGET,hollow,0.3em>"

			"<frame name=title2 title=", LANG("<b>Size:</b> (clipped by selection)"), "left=FORM right=FORM top=WIDGET,shape3,0.5em/>"
			"<label name=label2.big title=", LANG("W:"), ">"
			"<editbox name=xcoord roundTo=2 curValue=", size, "editType=", SITV_Float, "minValue=1"
			" right=", SITV_AttachPosition, SITV_AttachPos(32), 0, "left=WIDGET,label2,0.5em top=WIDGET,title2,0.5em>"
			"<label name=label3.big title=", LANG("L:"), "left=WIDGET,xcoord,0.5em>"
			"<editbox name=zcoord roundTo=2 curValue=", size+1, "editType=", SITV_Float, "minValue=1"
			" right=", SITV_AttachPosition, SITV_AttachPos(64), 0, "left=WIDGET,label3,0.5em top=OPPOSITE,xcoord>"
			"<label name=label4.big title=", LANG("H:"), "left=WIDGET,zcoord,0.5em>"
			"<editbox name=ycoord roundTo=2 curValue=", size+2, "editType=", SITV_Float, "minValue=1"
			" right=FORM,,1em left=WIDGET,label4,0.5em top=OPPOSITE,zcoord>"

			"<button name=ok title=", LANG("Fill"), ">"
			"<button name=cancel title=", LANG("Cancel"), "buttonType=", SITV_CancelButton, "right=FORM top=WIDGET,xcoord,0.5em>"
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
		uint32_t volume = size[0] * size[1] * size[2];
		SIT_CreateWidgets(diag,
			"<button name=doreplace title=", LANG("Replace by:"), "curValue=", &mcuiRepWnd.doReplace, "buttonType=", SITV_CheckBox,
			" left=WIDGET,fill,0.5em top=MIDDLE,fill>"
			"<canvas composited=1 enabled=", mcuiRepWnd.doReplace, "name=replace.inv left=WIDGET,doreplace,0.5em top=WIDGET,inv,0.5em/>"
			"<button name=side1 radioID=1 enabled=0 title=", LANG("Top"), "curValue=", &mcuiRepWnd.side, "buttonType=", SITV_RadioButton,
			" left=WIDGET,replace,0.5em top=OPPOSITE,fill>"
			"<button name=side0 radioID=0 enabled=0 title=", LANG("Bottom"), "curValue=", &mcuiRepWnd.side, "buttonType=", SITV_RadioButton,
			" left=WIDGET,replace,0.5em bottom=OPPOSITE,fill>"
			"<button name=similar enabled=", mcuiRepWnd.doReplace, "curValue=", &mcuiRepWnd.doSimilar, "title=", LANG("Replace similar blocks (strairs, slabs)"),
			" buttonType=", SITV_CheckBox, "left=OPPOSITE,doreplace top=WIDGET,fill,0.5em>"
			"<button name=cancel title=", LANG("Cancel"), "right=FORM top=WIDGET,similar,1em>"
			"<button name=ok title=", LANG(mcuiRepWnd.doReplace ? DLANG("Replace") : DLANG("Fill")), "top=OPPOSITE,cancel"
			" right=WIDGET,cancel,0.5em buttonType=", SITV_DefaultButton, ">"
			"<progress name=prog visible=0 title='%d%%' left=FORM right=WIDGET,ok,1em top=MIDDLE,ok>"
			"<tooltip name=info delayTime=", SITV_TooltipManualTrigger, "displayTime=10000 toolTipAnchor=", SITV_TooltipFollowMouse, ">"
		);
		if (volume > 1)
		{
			TEXT msg[64];
			FormatNumber(msg, sizeof msg, LANG("Volume: %d blocks"), volume);
			SIT_CreateWidgets(diag,
				"<label name=text title=", msg, "left=FORM right=WIDGET,ok,1em top=MIDDLE,ok>"
			);
		}
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

	inventoryInit(&mcinv, SIT_GetById(diag, "inv"), 1);
	inventoryInit(&fillinv, mcuiRepWnd.fill, 0);
	inventoryResetScrollbar(&mcinv);
	if (! fillWithBrush)
	{
		inventoryInit(&replace, mcuiRepWnd.replace, 0);
		SIT_AddCallback(mcuiRepWnd.replace, SITE_OnPaint, mcuiFillDisabled, NULL);
	}

	SIT_AddCallback(mcuiRepWnd.search, SITE_OnChange, mcuiFilterBlocks, &mcinv);
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
	uint8_t flags;
}	mcuiDelWnd = {.blocks = False, .entity = True, .tile = True};

static int mcuiDeleteProgress(SIT_Widget w, APTR cd, APTR ud)
{
	if (mcuiRepWnd.processTotal == mcuiRepWnd.processCurrent)
	{
		/* done */
		mapUpdateEnd(globals.level);
		mcuiRepWnd.asyncCheck = NULL;
		undoLog(LOG_REGION_END);
		SIT_Exit(EXIT_LOOP);
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
				"<label name=title.big title=", LANG("Delete in progress..."), "left=", SITV_AttachCenter, ">"
				"<progress name=prog title=%d%% width=15em top=WIDGET,title,0.5em>"
				"<button name=ko.act title=", LANG("Cancel"), "buttonType=", SITV_CancelButton, "left=WIDGET,prog,1em top=WIDGET,title,0.5em>"
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

static int mcuiDeleteEntities(SIT_Widget w, APTR cd, APTR ud)
{
	if (entityDeleteById(globals.level, * (int *) cd + 1))
		((int *)ud)[0] ++;
	return 1;
}

/* delete key pressed: auto-delete everything in selection */
void mcuiDeleteAll(void)
{
	/* delete all the blocks and entities in the selection */
	mcuiRepWnd.processCurrent = 0;
	mcuiRepWnd.replace = NULL;
	mcuiRepWnd.processTotal = selectionFill(&mcuiRepWnd.processCurrent, 0, 0, 0);
	/* this can be done in the current thread */
	int total = 0;
	selectionIterEntities(mcuiDeleteEntities, &total);
	renderAddModif();

	/* if the interface is stopped early, we need to be notified */
	SIT_AddCallback(globals.app, SITE_OnFinalize, mcuiFillStop, NULL);

	/* register an undo operation for this */
	int range[6];
	selectionGetRange(range, True);
	undoLog(LOG_REGION_START, range);

	/* this function will monitor the thread progress */
	globals.curTimeUI = FrameGetTime();
	mcuiRepWnd.asyncCheck = SIT_ActionAdd(globals.app, mcuiRepWnd.processStart = globals.curTimeUI, globals.curTimeUI + 1e9, mcuiDeleteProgress, NULL);
}

static int mcuiAutoCheck(SIT_Widget w, APTR cd, APTR ud)
{
	/* disable tile entties checkbox if blocks are deleted (because they will be deleted no matter what) */
	SIT_SetValues(ud, SIT_Enabled, ! mcuiDelWnd.blocks, NULL);
	return 1;
}

static int mcuiDeleteTE(SIT_Widget w, APTR cd, APTR ud)
{
	BlockIter iter = (BlockIter) w;
	chunkDeleteTileEntity(iter->ref, (int[3]){iter->x, iter->yabs, iter->z}, False);
	chunkMarkForUpdate(iter->ref, CHUNK_NBT_TILEENTITIES);
	/* <cd> contains offset of last item in hash table, dec by 1 to avoid missing if linked list has to be relocated */
	((int *)cd)[0] --;
	/* total tile entities removed */
	((int *)ud)[0] ++;
	return 1;
}

static int mcuiDoDelete(SIT_Widget w, APTR cd, APTR ud)
{
	if (! mcuiDelWnd.blocks)
	{
		/* this doesn't need to be asynchronous */
		int total;
		if (mcuiDelWnd.tile && (mcuiDelWnd.flags & 2))
		{
			total = 0;
			selectionIterTE(mcuiDeleteTE, &total);
			if (total > 0) renderAddModif();
		}
		if (mcuiDelWnd.entity && (mcuiDelWnd.flags & 1))
		{
			total = 0;
			selectionIterEntities(mcuiDeleteEntities, &total);
			if (total > 0) renderAddModif();
		}
		SIT_Exit(EXIT_LOOP);
	}
	else if (! mcuiRepWnd.asyncCheck)
	{
		mcuiDeleteAll();
		mcuiRepWnd.replace = mcuiDelWnd.diag;
		SIT_SetValues(w, SIT_Enabled, False, NULL);
	}
	return 1;
}

static int mcuiCountObjects(SIT_Widget w, APTR cd, APTR ud)
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

	/* count the number of TileEntities and entities */
	TEXT title[64];
	int  tileEntities = 0;
	int  entities = 0;
	selectionIterTE(mcuiCountObjects, &tileEntities);
	selectionIterEntities(mcuiCountObjects, &entities);

	SIT_CreateWidgets(diag,
		"<label name=dlgtitle.big title=", LANG("Partial delete"), "left=", SITV_AttachCenter, ">"
		"<label name=title title=", LANG("Select the parts you want to delete:"), "top=WIDGET,dlgtitle,0.5em>"
		"<button name=blocks buttonType=", SITV_CheckBox, "curValue=", &mcuiDelWnd.blocks, "title=", LANG("Terrain blocks"), "top=WIDGET,title,0.5em>"
	);
	mcuiDelWnd.flags = 0;

	/* show options depending on what was selected */
	if (entities > 0)
	{
		sprintf(title, LANG("In selection: %d"), entities);
		SIT_CreateWidgets(diag,
			"<button name=entity buttonType=", SITV_CheckBox, "curValue=", &mcuiDelWnd.entity, "title=",
				LANG("Entities (mobs, falling blocks, item frame, ...)"), "top=WIDGET,blocks,0.5em>"
			"<label name=entitynb.big title=", title, "left=FORM,,1.5em top=WIDGET,entity,0.3em>"
		);
		mcuiDelWnd.flags = 1;
	}
	if (tileEntities > 0)
	{
		sprintf(title, LANG("In selection: %d"), tileEntities);
		SIT_CreateWidgets(diag,
			"<button name=tile enabled=", ! mcuiDelWnd.blocks, "buttonType=", SITV_CheckBox, "curValue=", &mcuiDelWnd.tile,
			" title=", LANG("Tile entities (chests inventories, ...)"), "top=WIDGET,#LAST,0.5em>"
			"<label name=tilenb.big title=", title, "left=FORM,,1.5em top=WIDGET,tile,0.3em>"
		);
		mcuiDelWnd.flags |= 2;
	}
	SIT_CreateWidgets(diag,
		"<button name=cancel title=", LANG("Cancel"), "right=FORM top=WIDGET,#LAST,1em buttonType=", SITV_CancelButton, ">"
		"<button name=ok title=", LANG("Delete"), "top=OPPOSITE,cancel right=WIDGET,cancel,0.5em buttonType=", SITV_DefaultButton, ">"
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
	SIT_Widget view, name, error;
	DATA8      lastHover;
	float      scale;
	double     lastClick;
	vec4       validStart;
	uint8_t    validSpot[7];
	uint8_t    axis, side;

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
	SIT_SetValues(mcuiPaintings.error, SIT_Visible, False, NULL);
}

static Bool mcuiPaintingsFindLocation(vec4 ret)
{
	int8_t * normal = &cubeNormals[mcuiPaintings.side * 4];
	DATA8    size  = mcuiPaintings.lastHover;
	uint8_t  tileW = size[2] - size[0];
	uint8_t  tileH = size[3] - size[1];
	uint8_t  i, j;

	/* not the most efficient algo: check all positions until we found one */
	for (j = 0; j < tileH; j ++)
	{
		for (i = 0; i < tileW; i ++)
		{
			/* we start checking from lower-left corner of painting (placed on block selected) up to upper-right */
			uint8_t k;
			uint8_t y = 3 - j;
			uint8_t mask = ((1 << tileW) - 1) << (3 - i);
			DATA8   valid = mcuiPaintings.validSpot + y;

			for (k = 0; k < tileH && (valid[k] & mask) == mask; k ++);
			if (k == tileH)
			{
				/* we found a spot */
				memcpy(ret, mcuiPaintings.validStart, sizeof (vec4));
				ret[VY] -= j;
				ret[mcuiPaintings.axis] -= i;

				/* also check that there are no entities in the area */
				float bbox[6], tmp;
				int   count;
				memcpy(bbox, ret, 12);
				bbox[VX] += normal[VX];
				bbox[VY] += normal[VY];
				bbox[VZ] += normal[VZ];
				bbox[VX+3] = bbox[VX] + (normal[VX] == 0 ? tileW : normal[VX] / 16.f);
				bbox[VZ+3] = bbox[VZ] + (normal[VZ] == 0 ? tileW : normal[VZ] / 16.f);
				bbox[VY+3] = bbox[VY] + tileH;
				if (bbox[VX] > bbox[VX+3]) swap_tmp(bbox[VX], bbox[VX+3], tmp);
				if (bbox[VZ] > bbox[VZ+3]) swap_tmp(bbox[VZ], bbox[VZ+3], tmp);
				quadTreeIntersect(bbox, &count, ENFLAG_ANYENTITY);
				if (count == 0)
					return True;
			}
		}
	}
	return False;
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
				vec4 pos;
				if (mcuiPaintingsFindLocation(pos) &&
				    worldItemCreatePainting(globals.level, (mcuiPaintings.lastHover - paintings.location) >> 2, pos))
				{
					SIT_Exit(EXIT_LOOP);
				}
				else SIT_SetValues(mcuiPaintings.error, SIT_Visible, True, NULL);
			}
			else mcuiPaintings.lastClick = curTime;
		}
	default: break;
	}
	return 1;
}

static int mcuiPaintingsResize(SIT_Widget w, APTR cd, APTR ud)
{
	/* outside of SITGL control */
	int tiles = (globals.height >> 1) / PAINTINGS_TILE_H;
	SIT_SetValues(mcuiPaintings.view, SIT_Width, tiles * PAINTINGS_TILE_W, SIT_Height, tiles * PAINTINGS_TILE_H, NULL);
	return 1;
}

/* check how much space we have around selected position */
static void mcuiGetBlockArea(void)
{
	vec4         pos;
	MapExtraData sel = renderGetSelectedBlock(pos, NULL);
	int8_t *     normal = &cubeNormals[sel->side * 4];
	uint8_t      dx, dz, i, j;
	DATA8        valid;

	/* paintings can only be placed in S, E, N, W plane (second axis is always Y) */
	if (normal[0] == 0) dx = 1, dz = 0, mcuiPaintings.axis = VX;
	else                dx = 0, dz = 1, mcuiPaintings.axis = VZ;

	/*
	 * check if can find a 4x4 (max painting size) area that include the initial pos
	 * since max size of a painting is 4x4, the max area around selected block is 7x7 (3 blocks
	 * away from selection)
	 */
	struct BlockIter_t iter;
	mcuiPaintings.side = sel->side;
	memcpy(mcuiPaintings.validStart, pos, sizeof mcuiPaintings.validStart);
	memset(mcuiPaintings.validSpot, 0, sizeof mcuiPaintings.validSpot);
	pos[mcuiPaintings.axis] -= 3;
	pos[VY] -= 3;
	mapInitIter(globals.level, &iter, pos, False);

	/* any non-air block is a valid block to place a painting */
	for (i = 0, valid = mcuiPaintings.validSpot; i < 7; i ++, mapIter(&iter, -dx*7, 1, -dz*7), valid ++)
	{
		struct BlockIter_t front = iter;
		mapIter(&front, normal[VX], 0, normal[VZ]);
		for (j = 0; j < 7; j ++, mapIter(&iter, dx, 0, dz), mapIter(&front, dx, 0, dz))
		{
			if (! front.blockIds || front.blockIds[front.offset] == 0)
			{
				/* only air in front */
				Block b = iter.blockIds ? &blockIds[iter.blockIds[iter.offset]] : NULL;
				if (b && (b->type == SOLID || b->type == CUST || b->type == TRANS))
					valid[0] |= 1 << j;
			}
		}
	}
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
		"<label name=dlgtitle.big title=", LANG("Select painting"), "left=", SITV_AttachCenter, ">"
		"<label name=title title=", LANG("Double-click on the painting you want to add:"), "top=WIDGET,dlgtitle,0.5em>"
		"<label name=name left=WIDGET,title,0.5em right=FORM top=OPPOSITE,title>"
		"<canvas name=view#table top=WIDGET,title,0.5em height=", tiles * PAINTINGS_TILE_H, "width=", tiles * PAINTINGS_TILE_W, "/>"
		"<button name=ko title=", LANG("Cancel"), "top=WIDGET,view,0.5em right=FORM>"
		"<label name=error title=", LANG("This painting does not fit here"), "visible=0 top=MIDDLE,ko>"
	);

	mcui.resize = mcuiPaintingsResize;

	mcuiGetBlockArea();

	SIT_Widget view = mcuiPaintings.view = SIT_GetById(diag, "view");
	SIT_AddCallback(view, SITE_OnPaint,     mcuiRenderPaintings, NULL);
	SIT_AddCallback(view, SITE_OnClickMove, mcuiSelectPaintings, NULL);
	SIT_AddCallback(SIT_GetById(diag, "ko"), SITE_OnActivate, mcuiExitWnd, NULL);

	mcuiPaintings.name = SIT_GetById(diag, "name");
	mcuiPaintings.error = SIT_GetById(diag, "error");
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
		* (int *) buffer = NBT_GetInt(nbt, off, 0);
		break;
	case TAG_Byte:
		sep = NBT_Payload(nbt, off);
		* (int *) buffer = sep ? strcasecmp(sep, "true") == 0 : 0;
		break;
	case TAG_Long:
		sep = alloca(32);
		NBT_GetString(nbt, off, sep, 32);
		* (uint64_t *) buffer = strtoull(sep, NULL, 10);
		break;
	case TAG_String:
		NBT_GetString(nbt, off, buffer, type >> 8);
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
	SIT_Exit(EXIT_LOOP);
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

		renderShowBlockInfo(True, DEBUG_NOCLUTTER);
		renderWorld();

		renderShowBlockInfo(False, DEBUG_NOCLUTTER);
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
		#if 0
		FILE * out = fopen("dump.ppm", "wb");
		fprintf(out, "P6\n%d %d 255\n", PACKPNG_SIZE, PACKPNG_SIZE);
		fwrite(data, PACKPNG_SIZE * PACKPNG_SIZE, 3, out);
		fclose(out);
		#endif

		/* save some space by converting image to colormap */
		width = textureConvertToCMap(data, PACKPNG_SIZE, PACKPNG_SIZE);
		if (width > 0)
			textureSavePNG(path, data, 0, PACKPNG_SIZE, PACKPNG_SIZE, - width);
		else
			textureSavePNG(path, data, 0, PACKPNG_SIZE, PACKPNG_SIZE, 3);
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
		"<label name=dlgtitle#title title=", LANG("World info:"), "left=FORM right=FORM>"
		"<label name=icon#table currentdir=1 imagePath=", iconPath, "right=FORM top=WIDGET,dlgtitle,0.8em>"
		"<button name=set.act title=", LANG("Update icon"), "top=WIDGET,icon,0.2em left=OPPOSITE,icon"
		" right=OPPOSITE,icon tooltip=", LANG("Will use current 3d view"), ">"
		"<editbox name=level editBuffer=", mcuiInfo.name, "editLength=", sizeof mcuiInfo.name, "width=15em"
		" right=WIDGET,icon,0.5em top=WIDGET,dlgtitle,0.8em buddyLabel=", LANG("Name:"), &max1, ">"
		"<editbox name=seed  editBuffer=", mcuiInfo.seed, "editLength=", sizeof mcuiInfo.seed,
		" right=WIDGET,icon,0.5em top=WIDGET,level,0.5em buddyLabel=", LANG("Seed:"), &max1, ">"
		"<editbox name=day width=5.75em top=WIDGET,seed,0.5em minValue=0 buddyLabel=", LANG("Days:"), &max1,
		" editType=", SITV_Integer, "curValue=", &mcuiInfo.days, ">"
		"<editbox name=time width=5.75em top=WIDGET,seed,0.5em buddyLabel=", LANG("Time:"), NULL,
		" editBuffer=", mcuiInfo.time, "editLength=", sizeof mcuiInfo.time, "right=WIDGET,icon,0.5em>"
		"<button name=open.act title=", LANG("Folder:"), ">"
		"<editbox name=folder editBuffer=", mcuiInfo.folder, "editLength=", MAX_PATHLEN, "readOnly=1"
		" top=WIDGET,time,0.5em left=OPPOSITE,level right=WIDGET,icon,0.5em>"
		"<label name=size title=", size, "top=WIDGET,folder,0.5em buddyLabel=", LANG("Size:"), &max1, ">"
		"<label name=rules#title title=", LANG("Game rules:"), "left=FORM right=FORM top=WIDGET,size,0.5em/>"
		/* game mode */
		"<button name=type0 buttonType=", SITV_RadioButton, "curValue=", &mcuiInfo.mode, "title=", LANG("Survival"),
		" top=WIDGET,rules,1em buddyLabel=", LANG("Game mode:"), &max2, ">"
		"<button name=type1 buttonType=", SITV_RadioButton, "curValue=", &mcuiInfo.mode, "title=", LANG("Creative"),
		" top=OPPOSITE,type0 left=WIDGET,type0,0.8em>"
		"<button name=type2 buttonType=", SITV_RadioButton, "curValue=", &mcuiInfo.mode, "title=", LANG("Spectator"),
		" radioID=3 top=OPPOSITE,type0 left=WIDGET,type1,0.8em>"
		/* difficulty */
		"<button name=level0 buttonType=", SITV_RadioButton, "curValue=", &mcuiInfo.difficulty, "radioGroup=1"
		" title=", LANG("Peaceful"), "top=WIDGET,type0,1em buddyLabel=", LANG("Difficulty:"), &max2, ">"
		"<button name=level1 buttonType=", SITV_RadioButton, "curValue=", &mcuiInfo.difficulty,
		" radioGroup=1 title=", LANG("Easy"), "top=OPPOSITE,level0 left=WIDGET,level0,0.8em>"
		"<button name=level2 buttonType=", SITV_RadioButton, "curValue=", &mcuiInfo.difficulty,
		" radioGroup=1 title=", LANG("Normal"), "top=OPPOSITE,level0 left=WIDGET,level1,0.8em>"
		"<button name=level3 buttonType=", SITV_RadioButton, "curValue=", &mcuiInfo.difficulty,
		" radioGroup=1 title=", LANG("Hard"), "top=OPPOSITE,level0 left=WIDGET,level2,0.8em>"
		/* hardcore */
		"<button name=hard0 buttonType=", SITV_RadioButton, "radioGroup=2 top=WIDGET,level0,1em"
		" curValue=", &mcuiInfo.hardcore, "title=", LANG("No"), "buddyLabel=", LANG("Hardcore mode:"), &max2, ">"
		"<button name=hard1 buttonType=", SITV_RadioButton, "radioGroup=2 top=OPPOSITE,hard0"
		" left=WIDGET,hard0,0.8em curValue=", &mcuiInfo.hardcore, "title=", LANG("Yes"), ">"
		/* allowCommands */
		"<button name=cmd0 buttonType=", SITV_RadioButton, "radioGroup=3 top=WIDGET,hard0,1em"
		" curValue=", &mcuiInfo.allowCmds, "title=", LANG("No"), "buddyLabel=", LANG("Allow commands:"), &max2, ">"
		"<button name=cmd1 buttonType=", SITV_RadioButton, "radioGroup=3 top=OPPOSITE,cmd0"
		" left=WIDGET,cmd0,0.8em curValue=", &mcuiInfo.allowCmds, "title=", LANG("Yes"), ">"
		/* doDayNightCycle */
		"<button name=day0 buttonType=", SITV_RadioButton, "radioGroup=4 top=WIDGET,cmd0,1em"
		" curValue=", &mcuiInfo.dayCycle, "title=", LANG("No"), "buddyLabel=", LANG("Day/night cycle:"), &max2, ">"
		"<button name=day1 buttonType=", SITV_RadioButton, "radioGroup=4 top=OPPOSITE,day0"
		" left=WIDGET,day0,0.8em curValue=", &mcuiInfo.dayCycle, "title=", LANG("Yes"), ">"
		/* keepInventory */
		"<button name=inv0 buttonType=", SITV_RadioButton, "radioGroup=5 top=WIDGET,day0,1em"
		" curValue=", &mcuiInfo.keepInv, "title", LANG("No"), "buddyLabel=", LANG("Keep inventory:"), &max2, ">"
		"<button name=inv1 buttonType=", SITV_RadioButton, "radioGroup=5 top=OPPOSITE,inv0"
		" left=WIDGET,inv0,0.8em curValue=", &mcuiInfo.keepInv, "title=", LANG("Yes"), ">"
		/* mobGriefing */
		"<button name=grief0 buttonType=", SITV_RadioButton, "radioGroup=6 top=WIDGET,inv0,1em"
		" curValue=", &mcuiInfo.mobGrief, "title=", LANG("No"), "buddyLabel=", LANG("Mob griefing:"), &max2, ">"
		"<button name=grief1 buttonType=", SITV_RadioButton, "radioGroup=6 top=OPPOSITE,grief0"
		" left=WIDGET,grief0,0.8em curValue=", &mcuiInfo.mobGrief, "title=", LANG("Yes"), ">"
		/* doFireTick */
		"<button name=fire0 buttonType=", SITV_RadioButton, "radioGroup=7 top=WIDGET,grief0,1em"
		" curValue=", &mcuiInfo.fireTick, "title=", LANG("No"), "buddyLabel=", LANG("Fire spreading:"), &max2, ">"
		"<button name=fire1 buttonType=", SITV_RadioButton, "radioGroup=7 top=OPPOSITE,fire0"
		" left=WIDGET,fire0,0.8em curValue=", &mcuiInfo.fireTick, "title=", LANG("Yes"), ">"

		"<button name=ko.act title=", LANG("Cancel"), "top=WIDGET,fire0,0.8em right=FORM buttonType=", SITV_CancelButton, ">"
		"<button name=ok.act title=", LANG("Save"),   "top=OPPOSITE,ko right=WIDGET,ko,1em buttonType=", SITV_DefaultButton, ">"
	);

	SIT_SetAttributes(diag, "<open top=MIDDLE,folder maxWidth=bsize><time left=NONE><btime right=WIDGET,time,0.5em><icon bottom=OPPOSITE,folder>");
	SIT_AddCallback(SIT_GetById(diag, "ko"),   SITE_OnActivate, mcuiExitWnd, NULL);
	SIT_AddCallback(SIT_GetById(diag, "open"), SITE_OnActivate, mcuiInfoOpenFolder, NULL);
	SIT_AddCallback(SIT_GetById(diag, "ok"),   SITE_OnActivate, mcuiInfoSave, NULL);
	SIT_AddCallback(SIT_GetById(diag, "set"),  SITE_OnActivate, mcuiInfoSetIcon, SIT_GetById(diag, "icon"));

	SIT_ManageWidget(diag);
}

/*
 * Filter interface: TODO
 */

void mcuiFilter(void)
{
	SIT_Widget diag = SIT_CreateWidget("worldinfo.mc", SIT_DIALOG, globals.app,
		SIT_DialogStyles, SITV_Plain,
		NULL
	);

	SIT_CreateWidgets(diag,
		"<label name=dlgtitle#title title='Filter blocks:' left=FORM right=FORM>"
		"<label name=todo title='TODO...' top=WIDGET,#LAST,1em>"
		"<button name=ko.act title=Cancel top=WIDGET,#LAST,1em right=FORM buttonType=", SITV_CancelButton, ">"
		"<button name=ok.act title=Filter top=OPPOSITE,ko right=WIDGET,ko,1em buttonType=", SITV_DefaultButton, ">"
	);

	SIT_AddCallback(SIT_GetById(diag, "ko"), SITE_OnActivate, mcuiExitWnd, NULL);

	SIT_ManageWidget(diag);
}
