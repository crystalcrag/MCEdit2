/*
 * MCEdit.c : entry point for program, dispatch to high level module.
 *
 * Minecraft 1.12 world editor. Requires:
 * - OpenGL 4.3+
 * - SDL1
 * - SITGL
 *
 * Written by T.Pierron, jan 2020
 */

#include <SDL/SDL.h>
#include <glad.h>
#include <malloc.h>
#include <math.h>
#include <time.h>
#include <stdio.h>
#include "MCEdit.h"
#include "render.h"
#include "skydome.h"
#include "blocks.h"
#include "blockUpdate.h"
#include "mapUpdate.h"
#include "interface.h"
#include "nanovg.h"
#include "SIT.h"

GameState_t mcedit;
double      curTime;
int breakPoint;   /* easier to place break points :-/ */

static void takeScreenshot(void)
{
	time_t      now = time(NULL);
	struct tm * local = localtime(&now);
	DATA8       buffer = malloc(mcedit.width * mcedit.height * 3);
	STRPTR      path = alloca(strlen(mcedit.capture) + 64);
	int         len, num = 2;

	glReadPixels(0, 0, mcedit.width, mcedit.height, GL_RGB, GL_UNSIGNED_BYTE, buffer);

	len = sprintf(path, "%s/%d-%02d-%02d_%02d.%02d.%02d.png", mcedit.capture, local->tm_year+1900, local->tm_mon+1, local->tm_mday,
		local->tm_hour, local->tm_min, local->tm_sec);

	if (! IsDir(mcedit.capture))
		CreatePath(mcedit.capture, False);

	while (FileExists(path))
	{
		sprintf(path + len - 4, "_%d.png", num);
		num ++;
	}
	textureSaveSTB(path, mcedit.width, mcedit.height, 3, buffer, mcedit.width*3);
	fprintf(stderr, "screenshot saved in %s\n", path);
	free(buffer);
}

static void prefsInit(void)
{
	INIFile ini = ParseINI(PREFS_PATH);

	mcedit.width   = GetINIValueInt(ini, "WndWidth",   1600);
	mcedit.height  = GetINIValueInt(ini, "WndHeight",  1080);
	mcedit.maxDist = GetINIValueInt(ini, "RenderDist", 4);

	CopyString(mcedit.capture, GetINIValueStr(ini, "CaptureDir", "screenshots"), sizeof mcedit.capture);

	DOS2Unix(mcedit.capture);
	debugLoadSaveState((STRPTR) ini, True);

	FreeINI(ini);
}

#if 0
static void prefsSave(void)
{
	SetINIValueInt(PREFS_PATH, "WndWidth",  mcedit.width);
	SetINIValueInt(PREFS_PATH, "WndHeight", mcedit.height);
}
#endif


static int SDLKtoSIT[] = {
	SDLK_HOME,      SITK_Home,
	SDLK_END,       SITK_End,
	SDLK_PAGEUP,    SITK_PrevPage,
	SDLK_PAGEDOWN,  SITK_NextPage,
	SDLK_UP,        SITK_Up,
	SDLK_DOWN,      SITK_Down,
	SDLK_LEFT,      SITK_Left,
	SDLK_RIGHT,     SITK_Right,
	SDLK_LSHIFT,    SITK_LShift,
	SDLK_RSHIFT,    SITK_RShift,
	SDLK_LAST,      SITK_LAlt,
	SDLK_RALT,      SITK_RAlt,
	SDLK_LCTRL,     SITK_LCtrl,
	SDLK_RCTRL,     SITK_RCtrl,
	SDLK_LSUPER,    SITK_LCommand,
	SDLK_RSUPER,    SITK_RCommand,
	SDLK_MENU,      SITK_AppCommand,
	SDLK_RETURN,    SITK_Return,
	SDLK_CAPSLOCK,  SITK_Caps,
	SDLK_INSERT,    SITK_Insert,
	SDLK_DELETE,    SITK_Delete,
	SDLK_NUMLOCK,   SITK_NumLock,
	SDLK_PRINT,     SITK_Impr,
	SDLK_F1,        SITK_F1,
	SDLK_F2,        SITK_F2,
	SDLK_F3,        SITK_F3,
	SDLK_F4,        SITK_F4,
	SDLK_F5,        SITK_F5,
	SDLK_F6,        SITK_F6,
	SDLK_F7,        SITK_F7,
	SDLK_F8,        SITK_F8,
	SDLK_F9,        SITK_F9,
	SDLK_F10,       SITK_F10,
	SDLK_F11,       SITK_F11,
	SDLK_F12,       SITK_F12,
	SDLK_F13,       SITK_F13,
	SDLK_F14,       SITK_F14,
	SDLK_F15,       SITK_F15,
	SDLK_BACKSPACE, SITK_BackSpace,
	SDLK_ESCAPE,    SITK_Escape,
	SDLK_SPACE,     SITK_Space,
	SDLK_HELP,      SITK_Help,
};

static int SDLMtoSIT(int mod)
{
	int ret = 0;
	if (mod & KMOD_CTRL)  ret |= SITK_FlagCtrl;
	if (mod & KMOD_SHIFT) ret |= SITK_FlagShift;
	if (mod & KMOD_ALT)   ret |= SITK_FlagAlt;
	return ret;
}

static int mceditSaveChanges(SIT_Widget w, APTR cd, APTR ud)
{
	if (! mapSaveAll(mcedit.level))
		SIT_Log(SIT_ERROR, "Fail to save changes: %s\n", GetError());
	if (mcedit.player.mode == MODE_CREATIVE)
	{
		playerSaveLocation(&mcedit.player, &mcedit.level->levelDat);
		mapSaveLevelDat(mcedit.level);
	}
	renderAllSaved();
	return 1;
}

/* enable auto-repeat for text widget */
static int mceditTrackFocus(SIT_Widget w, APTR cd, APTR ud)
{
	static int repeatOn = 0;
	int type;
	SIT_GetValues(w, SIT_CtrlType, &type, NULL);
	if (cd && type == SIT_EDITBOX)
	{
		SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY, SDL_DEFAULT_REPEAT_INTERVAL);
		repeatOn = 1;
	}
	else if (repeatOn)
	{
		SDL_EnableKeyRepeat(0, 0);
		repeatOn = 0;
	}
	return 0;
}

/*
 * main entry point: init and dispatch to high-level event loop
 */
int main(int nb, char * argv[])
{
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0)
		return 1;

	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 1);

	prefsInit();

	//fprintf(stderr, "SDL_SetVideo()\n");

    SDL_Surface * screen = SDL_SetVideoMode(mcedit.width, mcedit.height, 32, SDL_HWSURFACE | SDL_GL_DOUBLEBUFFER | SDL_OPENGL | SDL_RESIZABLE);
    if (screen == NULL)
    {
		SIT_Log(SIT_ERROR, "failed to set video mode, aborting.");
		return 1;
	}
	SDL_WM_SetCaption("MCEdit", "MCEdit");

	if (gladLoadGL() == 0)
	{
		SIT_Log(SIT_ERROR,
			"Fail to initialize OpenGL: minimum version required is 4.3\n\n"
			"Version installed: %s", glGetString ? (STRPTR) glGetString(GL_VERSION) : "<unknown> (fail to load opengl32.dll :-/)"
		);
		return 1;
	}

	fprintf(stderr, "GL version = %s\n", (STRPTR) glGetString(GL_VERSION));

	mcedit.app = SIT_Init(SIT_NVG_FLAGS, mcedit.width, mcedit.height, RESDIR INTERFACE "default.css", 1);

	if (! mcedit.app)
	{
		SIT_Log(SIT_ERROR, "failed to initialize SITGL:\n\n%s", SIT_GetError());
		return 1;
	}

	static SIT_Accel accels[] = {
		{SITK_FlagCapture + SITK_FlagAlt + SITK_F4, SITE_OnClose, NULL},
		{SITK_FlagCapture + SITK_Escape,            SITE_OnClose, NULL},
		{SITK_FlagCapture + SITK_FlagCtrl + 's',    SITE_OnActivate, NULL, mceditSaveChanges},
		{                   SITK_FlagCtrl + 'a',    SITE_OnActivate, "about"},
		{0}
	};

	SIT_SetValues(mcedit.app,
		SIT_DefSBSize,   SITV_Em(0.5),
		SIT_RefreshMode, SITV_RefreshAlways,
		SIT_AddFont,     "sans-serif",      "system",
		SIT_AddFont,     "sans-serif-bold", "system/Bold",
		SIT_AccelTable,  accels,
		SIT_ExitCode,    &mcedit.exit,
		NULL
	);
	SIT_AddCallback(mcedit.app, SITE_OnFocus, mceditTrackFocus, NULL);
	SIT_AddCallback(mcedit.app, SITE_OnBlur,  mceditTrackFocus, NULL);

	if (! renderInitStatic(mcedit.width, mcedit.height, mcedit.app))
	{
		/* shaders compilation failed usually */
		return 1;
	}

	mcedit.level = renderInitWorld("TestMesh", mcedit.maxDist);
//	mcedit.level = renderInitWorld("World1_12", mcedit.maxDist);
	mcedit.state = GAMELOOP_WORLD;

	if (mcedit.level == NULL)
	{
		SIT_Log(SIT_ERROR, "Fail to load level.dat: aborting.");
		return 1;
	}

	updateAlloc(32);
	playerInit(&mcedit.player, &mcedit.level->levelDat);
	FrameSetFPS(40);

	while (mcedit.exit != 1)
	{
		mcedit.exit = 0;
		switch (mcedit.state) {
		case GAMELOOP_WORLD:    mceditWorld();    break;
		case GAMELOOP_SIDEVIEW: mceditSideView(); break;
		default: break;
		}
	}
	SDL_FreeSurface(screen);
	SDL_Quit();
	//prefsSave();
	return 0;
}

/*
 * Main loop for editing world
 */
void mceditWorld(void)
{
	SDL_Event event;
	uint8_t   paused = 0;
	uint8_t   ignore = 0;
	uint8_t   capture = 0;
	uint8_t   sunMove = 0;

	renderSetInventory(&mcedit.player.inventory);
	renderSetViewMat(mcedit.player.pos, mcedit.player.lookat, &mcedit.player.angleh);

	while (! mcedit.exit)
	{
		while (SDL_PollEvent(&event))
		{
			switch (event.type) {
			case SDL_KEYDOWN:
				switch (event.key.keysym.sym) {
				case SDLK_LALT:
					renderShowBlockInfo(True, DEBUG_BLOCK);
					break;
				case SDLK_LSHIFT:
					mcedit.forceSel = 1;
					renderShowBlockInfo(True, DEBUG_SELECTION);
					break;
				case SDLK_BACKSPACE:
					paused = ! paused;
					FramePauseUnpause(paused);
					break;
				case SDLK_TAB:
					mcedit.state = GAMELOOP_SIDEVIEW;
					mcedit.exit = 2;
					break;
				case SDLK_F1:
					renderDebugBlock();
					break;
				case SDLK_F2:
					takeScreenshot();
					break;
				case SDLK_F3:
					if (event.key.keysym.mod & KMOD_SHIFT)
					{
						renderFrustum(True);
						renderToggleDebug(RENDER_DEBUG_FRUSTUM);
					}
					else renderToggleDebug(RENDER_DEBUG_CURCHUNK);
					break;
				case SDLK_F5: sunMove |= 1; break;
				case SDLK_F6: sunMove |= 2; break;
					break;
				case SDLK_F10: // DEBUG
					playerSaveLocation(&mcedit.player, &mcedit.level->levelDat);
					mapSaveLevelDat(mcedit.level);
					break;
				case SDLK_F7:
					breakPoint = ! breakPoint;
					break;
				case SDLK_EQUALS:
				case SDLK_PLUS:
					if (mapSetRenderDist(mcedit.level, mcedit.maxDist+1))
					{
						renderSetViewMat(mcedit.player.pos, mcedit.player.lookat, &mcedit.player.angleh);
						mcedit.maxDist ++;
					}
					break;
				case SDLK_MINUS:
					if (mapSetRenderDist(mcedit.level, mcedit.maxDist-1))
					{
						renderSetViewMat(mcedit.player.pos, mcedit.player.lookat, &mcedit.player.angleh);
						mcedit.maxDist --;
					}
					break;
				case SDLK_SPACE:
					mceditDoAction(ACTION_ACTIVATE);
					break;
				case SDLK_i:
					mceditUIOverlay();
					mcedit.player.inventory.update ++;
					break;
				default:
					if (! playerProcessKey(&mcedit.player, event.key.keysym.sym, SDLMtoSIT(event.key.keysym.mod)))
						goto forwardKeyPress;
				}
				break;
			case SDL_KEYUP:
				switch (event.key.keysym.sym) {
				case SDLK_LALT:
					renderShowBlockInfo(False, DEBUG_BLOCK);
					break;
				case SDLK_LSHIFT:
					mcedit.forceSel = 0;
					renderShowBlockInfo(False, DEBUG_SELECTION);
					goto forwardKeyPress;
				case SDLK_F5: sunMove &= ~1; break;
				case SDLK_F6: sunMove &= ~2; break;
				case 't': /* throw item */
					playerAddInventory(&mcedit.player, 0, NULL);
					playerUpdateNBT(&mcedit.player, &mcedit.level->levelDat);
					break;
				default:
					if (! playerProcessKey(&mcedit.player, event.key.keysym.sym, SITK_FlagUp))
					{
						int * sdlk;
						forwardKeyPress:
						for (sdlk = SDLKtoSIT; sdlk < EOT(SDLKtoSIT); sdlk += 2)
						{
							if (sdlk[0] == event.key.keysym.sym) {
								SIT_ProcessKey(sdlk[1], SDLMtoSIT(event.key.keysym.mod), event.type == SDL_KEYDOWN);
								break;
							}
						}
						if (32 < event.key.keysym.sym && event.key.keysym.sym < 123)
							SIT_ProcessChar(event.key.keysym.sym, SDLMtoSIT(event.key.keysym.mod));
					}
				}
				break;
			case SDL_MOUSEMOTION:
				switch (ignore) {
				case 1: ignore = 0; // no break;
				case 2: break;
				case 0:
					if (capture)
					{
						playerLookAt(&mcedit.player, event.motion.xrel, event.motion.yrel);
						renderSetViewMat(mcedit.player.pos, mcedit.player.lookat, &mcedit.player.angleh);
						capture = 2;
					}
					else
					{
						renderPointToBlock(event.motion.x, event.motion.y);
						SIT_ProcessMouseMove(event.motion.x, event.motion.y);
					}
				}
				break;
			case SDL_MOUSEBUTTONDOWN:
				switch (event.button.button) {
				case SDL_BUTTON_LEFT:
					mceditDoAction(ACTION_PLACEBLOCK);
					break;
				case SDL_BUTTON_RIGHT:
					/* ignore any pending mouse move */
					SDL_GetMouseState(&mcedit.mouseX, &mcedit.mouseY);
					ignore = 2;
					capture = 1;
					break;
				case SDL_BUTTON_MIDDLE:
					{
						vec4 pos;
						MapExtraData sel = renderGetSelectedBlock(pos, NULL);
						if (sel)
						{
							int XYZ[] = {pos[0] - sel->chunk->X, pos[1], pos[2] - sel->chunk->Z};
							playerAddInventory(&mcedit.player, sel->blockId, chunkGetTileEntity(sel->chunk, XYZ));
							playerUpdateNBT(&mcedit.player, &mcedit.level->levelDat);
						}
					}
					break;
				case SDL_BUTTON_WHEELUP:
					playerScrollInventory(&mcedit.player, -1);
					break;
				case SDL_BUTTON_WHEELDOWN:
					playerScrollInventory(&mcedit.player, 1);
				}
				break;
			case SDL_MOUSEBUTTONUP:
				if (event.button.button == SDL_BUTTON_RIGHT)
				{
					SDL_WM_GrabInput(SDL_GRAB_OFF);
					SDL_ShowCursor(SDL_ENABLE);
					if (capture == 2)
						SDL_WarpMouse(mcedit.width>>1, mcedit.height>>1);
					else
						SDL_WarpMouse(mcedit.mouseX, mcedit.mouseY);
					capture = 0;
					ignore = 0;
				}
				break;
			case SDL_VIDEORESIZE:
				mcedit.width  = event.resize.w;
				mcedit.height = event.resize.h;
				renderResize(event.resize.w, event.resize.h);
				SIT_ProcessResize(event.resize.w, event.resize.h);
				break;
			case SDL_QUIT:
				mcedit.exit = 1;
				break;
			default:
				break;
			}
		}

		if (ignore)
		{
			SDL_WM_GrabInput(SDL_GRAB_ON);
			SDL_ShowCursor(SDL_DISABLE);
			/* ignore the next mouse move (from GRAB_ON) */
			ignore = 1;
		}
		if (mcedit.player.keyvec)
		{
			playerMove(&mcedit.player);
			renderSetViewMat(mcedit.player.pos, mcedit.player.lookat, &mcedit.player.angleh);
			if (! capture)
			{
				SDL_GetMouseState(&mcedit.mouseX, &mcedit.mouseY);
				renderPointToBlock(mcedit.mouseX, mcedit.mouseY);
			}
		}
		if (sunMove)
		{
			skydomeMoveSun(sunMove);
		}
		curTime = FrameGetTime();
		renderWorld();
		updateTick(mcedit.level);
		SIT_RenderNodes(curTime);
		SDL_GL_SwapBuffers();
		FrameWaitNext();
	}
}

/* left click */
void mceditDoAction(int action)
{
	vec4 pos;
	int  block, id;
	Item item;

	MapExtraData sel = renderGetSelectedBlock(pos, &block);
	if (sel == NULL) return;

	switch (action) {
	case ACTION_PLACEBLOCK:
		item = &mcedit.player.inventory.items[mcedit.player.inventory.selected];
		id   = mcedit.forceSel ? 0 : item->id;
		/* use of an item: check if it creates a block instead */
		if (id >= ID(256, 0))
		{
			ItemDesc desc = itemGetById(id);
			if (desc->refBlock)
			{
				if (blockIds[desc->refBlock].special == BLOCK_SIGN)
					id = block;
				else
					id = block = (desc->refBlock << 4) | (block & 15);
			}
		}
		if (id < ID(256, 0))
		{
			DATA8 tile = item->extra;
			if (id == 0) block = 0;
			if (! tile) tile = blockCreateTileEntity(block, pos, NULL);
			else        tile = NBT_Copy(tile);
			/* bed need extra data :-/ */
			block &= 0xfff;

			/*
			 * udpdate the map and all the tables associated, will also trigger cascading updates
			 * if needed
			 */
			mapUpdate(mcedit.level, pos, block, tile, True);
			renderAddModif();
		}
		break;
	case ACTION_ACTIVATE:
		mapActivate(mcedit.level, pos);
	}
}

/*
 * display a modal user interface on top of editor
 */
void mceditUIOverlay(void)
{
	static ItemBuf oldPlayerInv[MAXCOLINV * 4];

	SDL_Event event;
	Item      item;
	int       itemCount;
	int       itemConnect;
	vec4      pos;

	SIT_SetValues(mcedit.app, SIT_RefreshMode, SITV_RefreshAsNeeded, NULL);

	mcuiTakeSnapshot(mcedit.app, mcedit.width, mcedit.height);
	memcpy(oldPlayerInv, mcedit.player.inventory.items, sizeof oldPlayerInv);
	MapExtraData sel = renderGetSelectedBlock(pos, NULL);
	itemCount = 0;

	if (mcedit.forceSel)
	{
		/* selection will have to be released before exiting interface */
		mcedit.forceSel = 0;
		renderShowBlockInfo(False, DEBUG_SELECTION);
	}

	if (sel)
	{
		struct MapExtraData_t link;
		Block  b    = &blockIds[sel->blockId>>4];
		STRPTR tech = b->tech;
		STRPTR sep  = strchr(tech, '_');
		/* skip color name :-/ */
		if (sep && strcmp(sep+1, "shulker_box") == 0)
			goto case_INV;

		/* extract inventories from NBT structure */
		switch (FindInList("chest,trapped_chest,shulker,ender_chest,dispenser,dropper", tech, 0)) {
		case 0:
		case 1:
			/* possibly a double-chest */
			itemConnect = mapConnectChest(mcedit.level, sel, &link);

			if (itemConnect > 0)
			{
				itemCount = 54;
				item = alloca(sizeof *item * 54 * 2);
				switch (itemConnect) {
				case 1:
					mapDecodeItems(item,    27, mapLocateItems(sel));
					mapDecodeItems(item+27, 27, mapLocateItems(&link));
					break;
				case 2:
					mapDecodeItems(item,    27, mapLocateItems(&link));
					mapDecodeItems(item+27, 27, mapLocateItems(sel));
				}
				memcpy(item + 54, item, 54 * sizeof *item);
				mcuiEditChestInventory(&mcedit.player.inventory, item, 54);
				break;
			}
			// else no break;
		case 2:
		case_INV:
			/* single chest */
			itemCount = 27;
			item = alloca(sizeof *item * 27 * 2);
			mapDecodeItems(item, 27, mapLocateItems(sel));
			memcpy(item + 27, item, 27 * sizeof *item);
			mcuiEditChestInventory(&mcedit.player.inventory, item, 27);
			break;
		case 3: /* ender chest */
			break;
		case 4: /* dispenser */
		case 5: /* dropper */
			itemCount = 9;
			item = alloca(sizeof *item * 9 * 2);
			mapDecodeItems(item, 9, mapLocateItems(sel));
			memcpy(item + 9, item, 9 * sizeof *item);
			mcuiEditChestInventory(&mcedit.player.inventory, item, 9);
			break;
		default:
			if (b->special == BLOCK_SIGN)
				mcuiCreateSignEdit(mcedit.level, pos, sel->blockId, &mcedit.exit);
			else
				mcuiCreateInventory(&mcedit.player.inventory);
		}
	}
	else mcuiCreateInventory(&mcedit.player.inventory);

	SDL_EnableUNICODE(1);

	while (! mcedit.exit)
	{
		while (SDL_PollEvent(&event))
		{
			switch (event.type) {
			case SDL_KEYDOWN:
				switch (event.key.keysym.sym) {
				case SDLK_F2:
					takeScreenshot();
				default:
					break;
				}
				// no break;
			case SDL_KEYUP:
				{
					int * sdlk;
					for (sdlk = SDLKtoSIT; sdlk < EOT(SDLKtoSIT); sdlk += 2)
					{
						if (sdlk[0] == event.key.keysym.sym) {
							SIT_ProcessKey(sdlk[1], SDLMtoSIT(event.key.keysym.mod), event.type == SDL_KEYDOWN);
							goto break_loop;
						}
					}
				}
				if (event.key.keysym.unicode > 0)
					SIT_ProcessChar(event.key.keysym.unicode, SDLMtoSIT(event.key.keysym.mod));
			break_loop:
				break;
			case SDL_MOUSEBUTTONDOWN:
				SIT_ProcessClick(event.button.x, event.button.y, event.button.button-1, 1);
				break;
			case SDL_MOUSEBUTTONUP:
				SIT_ProcessClick(event.button.x, event.button.y, event.button.button-1, 0);
				break;
			case SDL_MOUSEMOTION:
				SIT_ProcessMouseMove(event.motion.x, event.motion.y);
				break;
			case SDL_VIDEORESIZE:
				mcedit.width  = event.resize.w;
				mcedit.height = event.resize.h;
				SIT_ProcessResize(mcedit.width, mcedit.height);
				break;
			case SDL_QUIT:
				mcedit.exit = 1;
				goto exit;
			default:
				continue;
			}
		}

		/* update and render */
		mcuiInitDrawItems();
		switch (SIT_RenderNodes(SDL_GetTicks())) {
		case SIT_RenderComposite:
			mcuiDrawItems();
			SIT_RenderNodes(0);
			SDL_GL_SwapBuffers();
			break;
		case SIT_RenderDone:
			mcuiDrawItems();
			SDL_GL_SwapBuffers();
		default: break;
		}
		FrameWaitNext();
	}
	/* loop exit = user hit Esc key */
	mcedit.exit = 0;

	/* check if there was any modifications */
	NBTFile_t chest = {0};
	NBTFile_t playerInv = {0};

	if (itemCount > 0 && memcmp(item, item + itemCount, itemCount * sizeof *item))
		/* changes were made to container */
		mapSerializeItems(sel, "Items", item, itemCount, &chest);

	if (mcedit.player.mode == MODE_CREATIVE && memcmp(oldPlayerInv, mcedit.player.inventory.items, sizeof oldPlayerInv))
		/* only update NBT if player is in creative mode */
		mapSerializeItems(NULL, "Inventory", mcedit.player.inventory.items, DIM(oldPlayerInv), &playerInv);

	if (chest.mem)
	{
		chunkUpdateNBT(sel->chunk, sel->offset + (sel->cd->Y<<8), &chest);
		renderAddModif();
	}

	if (playerInv.mem)
	{
		int offset = NBT_Insert(&mcedit.level->levelDat, "Player.Inventory", TAG_List_Compound, &playerInv);
		NBT_Free(&playerInv);
		if (offset >= 0)
			mapDecodeItems(mcedit.player.inventory.items, MAXCOLINV, NBT_Hdr(&mcedit.level->levelDat, offset));
	}

	exit:
	SIT_Nuke(SITV_NukeCtrl);
	SIT_SetValues(mcedit.app, SIT_RefreshMode, SITV_RefreshAlways, NULL);
	SDL_EnableUNICODE(0);
}

/*
 * Event loop for viewing world side ways (mostly used for debug).
 */
void mceditSideView(void)
{
	SDL_Event event;
	uint8_t   refresh = 0;
	uint8_t   capture = 0;
	uint8_t   info    = 0;
	int       mx, my;
	int *     sdlk;

	debugSetPos(mcedit.app, &mcedit.exit);
	debugWorld(SDL_GetTicks());
	mx = my = 0;
	SDL_GL_SwapBuffers();

	while (! mcedit.exit && SDL_WaitEvent(&event))
	{
		do {
			switch (event.type) {
			case SDL_KEYDOWN:
				switch (event.key.keysym.sym) {
				case SDLK_LALT:
					debugBlock(mcedit.mouseX, mcedit.mouseY);
					renderShowBlockInfo(True, DEBUG_BLOCK);
					info = 1;
					break;
				case SDLK_F3:    debugToggleInfo(DEBUG_CHUNK); break;
				case SDLK_F7:    breakPoint = ! breakPoint; break;
				case SDLK_TAB:   mcedit.exit = 2; break;
				case SDLK_e:
				case SDLK_UP:    debugMoveSlice(1); break;
				case SDLK_d:
				case SDLK_DOWN:  debugMoveSlice(-1); break;
				case SDLK_MINUS: debugRotateView(-1); break;
				case SDLK_EQUALS:
				case SDLK_PLUS:  debugRotateView(1); break;
				case SDLK_b:     debugToggleInfo(DEBUG_LIGHT); break;
				default:
					goto forwardKeyPress;
				}
				refresh = 1;
				break;
			case SDL_KEYUP:
				switch (event.key.keysym.sym) {
				case SDLK_LALT:
					renderShowBlockInfo(False, DEBUG_BLOCK);
					info = 0;
					refresh = 1;
					break;
				default:
					forwardKeyPress:
					for (sdlk = SDLKtoSIT; sdlk < EOT(SDLKtoSIT); sdlk += 2)
					{
						if (sdlk[0] == event.key.keysym.sym) {
							SIT_ProcessKey(sdlk[1], SDLMtoSIT(event.key.keysym.mod), event.type == SDL_KEYDOWN);
							break;
						}
					}
					if (event.key.keysym.unicode > 0)
						SIT_ProcessChar(event.key.keysym.unicode, SDLMtoSIT(event.key.keysym.mod));
				}
				break;
			case SDL_MOUSEMOTION:
				mcedit.mouseX = event.motion.x;
				mcedit.mouseY = event.motion.y;
				SIT_ProcessMouseMove(mcedit.mouseX, mcedit.mouseY);
				if (capture)
				{
					debugScrollView(event.motion.x - mx, event.motion.y - my);
					mx = event.motion.x;
					my = event.motion.y;
					refresh = 1;
				}
				else if (info)
				{
					debugBlock(event.motion.x, event.motion.y);
					refresh = 1;
				}
				break;
			case SDL_MOUSEBUTTONDOWN:
				if (SIT_ProcessClick(mcedit.mouseX, mcedit.mouseY, event.button.button-1, 1))
					refresh = 1;
				else switch (event.button.button) {
				case SDL_BUTTON_LEFT:
				case SDL_BUTTON_RIGHT:
					SDL_GetMouseState(&mx, &my);
					capture = 1;
					break;
				case SDL_BUTTON_WHEELUP:
					debugZoomView(mcedit.mouseX, mcedit.mouseY, 1);
					refresh = 1;
					break;
				case SDL_BUTTON_WHEELDOWN:
					debugZoomView(mcedit.mouseX, mcedit.mouseY, -1);
					refresh = 1;
				}
				break;
			case SDL_MOUSEBUTTONUP:
				if (SIT_ProcessClick(mcedit.mouseX, mcedit.mouseY, event.button.button-1, 0))
					refresh = 1;
				else if (event.button.button == SDL_BUTTON_LEFT || event.button.button == SDL_BUTTON_RIGHT)
					capture = 0;
				break;
			case SDL_QUIT:
				mcedit.exit = 1;
				break;
			case SDL_VIDEORESIZE:
				mcedit.width  = event.resize.w;
				mcedit.height = event.resize.h;
				SIT_ProcessResize(mcedit.width, mcedit.height);
				refresh = 1;
			default: break;
			}
		} while (SDL_PollEvent(&event));

		if (refresh || SIT_NeedRefresh())
		{
			debugWorld(SDL_GetTicks());
			SDL_GL_SwapBuffers();
			refresh = 0;
		}
	}
	debugLoadSaveState(PREFS_PATH, False);
	mcedit.state = GAMELOOP_WORLD;
	SIT_Nuke(SITV_NukeCtrl);
}

#ifdef	WIN32
#include <windows.h>
int WINAPI WinMain(
    HINSTANCE hInstance,
    HINSTANCE hPrevInstance,
    LPSTR lpCmdLine,
    int nCmdShow)
{
	return main(0, NULL);
}
#endif
