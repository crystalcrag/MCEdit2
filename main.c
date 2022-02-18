/*
 * MCEdit.c : entry point for program, dispatch to high level module.
 *
 * Minecraft 1.12 world editor. Requires:
 * - OpenGL 4.3+
 * - SDL1
 * - SITGL
 * - C11 compiler
 *
 * Written by T.Pierron, jan 2020
 */

#include <SDL/SDL.h>
#include <glad.h>
#include <malloc.h>
#include <time.h>
#include <stdio.h>
#include "MCEdit.h"
#include "render.h"
#include "skydome.h"
#include "selection.h"
#include "blockUpdate.h"
#include "mapUpdate.h"
#include "interface.h"
#include "library.h"
#include "entities.h"
#include "waypoints.h"
#include "worldSelect.h"
#include "undoredo.h"
#include "keybindings.h"
#include "SIT.h"

GameState_t mcedit;
MCGlobals_t globals;

/* list all the configurable shortcuts */
KeyBindings_t keyBindings = {
	/* keybindings page */
	{DLANG("Forward"),              "KeyForward",       SITK_FlagUp + 'W'},
	{DLANG("Backward"),             "KeyBackward",      SITK_FlagUp + 'S'},
	{DLANG("Strafe left"),          "KeyStrafeLeft",    SITK_FlagUp + 'A'},
	{DLANG("Strafe right"),         "KeyStrafeRight",   SITK_FlagUp + 'D'},
	{DLANG("Switch to off-hand"),   "KeyOffHand",       'G'},
	{DLANG("Open inventories"),     "KeyOpenInventory", 'I'},
	{DLANG("Trow item"),            "KeyTrowItem",      'T'},
	{DLANG("Jump"),                 "KeyJump",          SITK_FlagUp + SITK_Space},
	{DLANG("Fly down"),             "KeyFlyDown",       SITK_FlagUp + SITK_LShift},
	{DLANG("2D slice view"),        "KeySliceView",     SITK_Tab},
	{DLANG("Place block"),          "KeyPlaceBlock",    SITK_LMB},
	{DLANG("Move view"),            "KeyMoveView",      SITK_RMB},
	{DLANG("Activate device"),      "KeyActivateBlock", SITK_RMB},
	{DLANG("Pick block"),           "KeyPickBlock",     SITK_MMB},

	/* menu commands page */
	{DLANG("Hide HUD"),             "CmdHideHud",       SITK_F1},
	{DLANG("Show debug info"),      "CmdDebugInfo",     SITK_F3},
	{DLANG("Advance time"),         "CmdAdvanceTime",   SITK_FlagUp + SITK_F5},
	{DLANG("Save location"),        "CmdSaveLocation",  SITK_F10},
	{DLANG("Waypoint editor"),      "CmdWaypoints",     SITK_FlagCtrl + 'G'},
	{DLANG("Library schematics"),   "CmdSchemaLibrary", SITK_FlagCtrl + 'L'},
	{DLANG("Undo change"),          "CmdUndoChange",    SITK_FlagCtrl + 'Z'},
	{DLANG("Redo change"),          "CmdRedoChange",    SITK_FlagCtrl + 'Y'},
	{DLANG("Close world"),          "CmdCloseWorld",    SITK_FlagCtrl + 'W'},
	{DLANG("Take screenshot"),      "CmdTakeCapture",   SITK_F2},
	{DLANG("Back in time"),         "CmdBackInTime",    SITK_FlagUp + SITK_F6},
	{DLANG("Switch player mode"),   "CmdSwitchMode",    SITK_F8},
	{DLANG("Toggle fullscreen"),    "CmdFullscren",     SITK_F11},
	{DLANG("Clear selection"),      "CmdClearSel",      SITK_FlagCtrl + 'D'},
	{DLANG("Copy selection"),       "CmdCopySel",       SITK_FlagCtrl + 'C'},
	{DLANG("Paste from clipboard"), "CmdPasteClip",     SITK_FlagCtrl + 'V'},
	{DLANG("World info editor"),    "CmdWorldInfo",     SITK_FlagCtrl + 'I'},
	{DLANG("Save changes"),         "CmdSaveChanges",   SITK_FlagCtrl + 'S'},

	/* KBD_SLOT_[0~9]: not configurable (yet?) */
	{NULL, NULL, '0'},
	{NULL, NULL, '1'},
	{NULL, NULL, '2'},
	{NULL, NULL, '3'},
	{NULL, NULL, '4'},
	{NULL, NULL, '5'},
	{NULL, NULL, '6'},
	{NULL, NULL, '7'},
	{NULL, NULL, '8'},
	{NULL, NULL, '9'},
	/* quick options: Ctrl + O */
	{NULL, NULL, SITK_FlagCtrl + 'O'},
};

static int mSDLKtoSIT[] = {
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
	SDLK_LSUPER,    SITK_LCommand,
	SDLK_RSUPER,    SITK_RCommand,
	SDLK_MENU,      SITK_AppCommand,
	SDLK_RETURN,    SITK_Return,
	SDLK_INSERT,    SITK_Insert,
	SDLK_DELETE,    SITK_Delete,
	SDLK_PRINT,     SITK_Impr,
	SDLK_SPACE,     SITK_Space,
	SDLK_TAB,       SITK_Tab,
	SDLK_LCTRL,     SITK_LCtrl,
	SDLK_RCTRL,     SITK_RCtrl,
	SDLK_CAPSLOCK,  SITK_Caps,
	SDLK_NUMLOCK,   SITK_NumLock,
	SDLK_HELP,      SITK_Help,
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
};

static void mceditSetWndCaption(Map map);
static int  mceditExit(SIT_Widget, APTR cd, APTR ud);

int takeScreenshot(SIT_Widget w, APTR cd, APTR ud)
{
	time_t      now = time(NULL);
	struct tm * local = localtime(&now);
	DATA8       buffer = malloc(globals.width * globals.height * 3);
	STRPTR      path = alloca(strlen(mcedit.capture) + 64);
	int         len, num = 2;

	glReadBuffer(GL_FRONT);
	glReadPixels(0, 0, globals.width, globals.height, GL_RGB, GL_UNSIGNED_BYTE, buffer);

	len = sprintf(path, "%s/%d-%02d-%02d_%02d.%02d.%02d.png", mcedit.capture, local->tm_year+1900, local->tm_mon+1, local->tm_mday,
		local->tm_hour, local->tm_min, local->tm_sec);

	if (! IsDir(mcedit.capture))
		CreatePath(mcedit.capture, False);

	while (FileExists(path))
	{
		sprintf(path + len - 4, "_%d.png", num);
		num ++;
	}
	textureSavePNG(path, buffer, 0, globals.width, globals.height, 3);
	fprintf(stderr, "screenshot saved in %s\n", path);
	free(buffer);
	return 1;
}

static int SITK_FromText(STRPTR keyName)
{
	STRPTR sep;
	int    key = 0;
	/* parse qualifier first */
	while ((sep = strchr(keyName, '+')))
	{
		*sep = 0;
		switch (FindInList("Ctrl,Shift,Alt,Cmd", keyName, 0)) {
		case 0: key |= SITK_FlagCtrl;  break;
		case 1: key |= SITK_FlagShift; break;
		case 2: key |= SITK_FlagAlt;   break;
		case 3: key |= SITK_FlagCmd;   break;
		}
		keyName = sep + 1;
	}
	if (keyName[1] == 0 && 'A' <= keyName[0] && keyName[0] <= 'Z')
	{
		key |= keyName[0];
	}
	else
	{
		int num;
		if (sscanf(keyName, "F%d", &num) == 1)
		{
			key |= SITK_F1 + RAWKEY(num - 1);
		}
		else if (sscanf(keyName, "MB%d", &num) == 1)
		{
			key |= RAWKEY(SITK_NTH + num);
		}
		else switch (FindInList("LMB,MMB,RMB,MWU,MWD", keyName, 0)) {
		case 0: key |= SITK_LMB; break;
		case 1: key |= SITK_MMB; break;
		case 2: key |= SITK_RMB; break;
		case 3: key |= SITK_MWU; break;
		case 4: key |= SITK_MWD; break;
		default:
			num = FindInList(
				"Home,End,Page up,Page down,Up,Down,Left,Right,Left shift,Right shift,Left alt,"
				"Right alt,Left super,Right sper,Menu,Return,Insert,Delete,Print screen,Space,Tab",
				keyName, 0
			);
			if (num > 0) key |= mSDLKtoSIT[num*2+1];
		}
	}
	return key;
}

/* read config MCEdit.ini, default values are also set here */
static void prefsInit(void)
{
	INIFile ini = ParseINI(PREFS_PATH);

	globals.width         = GetINIValueInt(ini, "WndWidth",      1600);
	globals.height        = GetINIValueInt(ini, "WndHeight",     900);
	globals.renderDist    = GetINIValueInt(ini, "RenderDist",    4);
	globals.redstoneTick  = GetINIValueInt(ini, "RedstoneTick",  100);
	globals.compassSize   = GetINIValueInt(ini, "CompassSize",   100) * 0.01f;
	globals.fieldOfVision = GetINIValueInt(ini, "FieldOfVision", 80);
	globals.guiScale      = GetINIValueInt(ini, "GuiScale",      100);
	globals.mouseSpeed    = GetINIValueInt(ini, "MouseSpeed",    100) * 0.01f;
	globals.brightness    = GetINIValueInt(ini, "Brightness",    0);
	globals.targetFPS     = GetINIValueInt(ini, "TargetFPS",     40);
	globals.distanceFOG   = GetINIValueInt(ini, "UseFOG",        0);
	globals.showPreview   = GetINIValueInt(ini, "UsePreview",    1);
	globals.lockMouse     = GetINIValueInt(ini, "LockMouse",     0);

	mcedit.autoEdit       = GetINIValueInt(ini, "AutoEdit",      0);
	mcedit.fullScreen     = GetINIValueInt(ini, "FullScreen",    0);

	CopyString(mcedit.capture,   GetINIValue(ini, "CaptureDir"), sizeof mcedit.capture);
	CopyString(mcedit.worldsDir, GetINIValue(ini, "WorldsDir"),  sizeof mcedit.worldsDir);
	CopyString(mcedit.userDir,   GetINIValue(ini, "UserData"),   sizeof mcedit.userDir);
	CopyString(mcedit.worldEdit, GetINIValue(ini, "WorldEdit"),  sizeof mcedit.worldEdit);
	CopyString(mcedit.lang,      GetINIValue(ini, "Lang"),       sizeof mcedit.lang);

	STRPTR resol = GetINIValue(ini, "FullScrResol");
	if (resol && sscanf(resol, "%dx%d", &globals.fullScrWidth, &globals.fullScrHeight) != 2)
		globals.fullScrWidth = 0;

	if (mcedit.userDir[0] == 0)
	{
		GetDefaultPath(FOLDER_MYDOCUMENTS, mcedit.userDir, MAX_PATHLEN);
		AddPart(mcedit.userDir, "MCEdit/Schematics", MAX_PATHLEN);
	}

	if (mcedit.capture[0] == 0)
	{
		strcpy(mcedit.capture, mcedit.userDir);
		AddPart(mcedit.capture, "../screenshots", MAX_PATHLEN);
	}
	if (mcedit.worldsDir[0] == 0)
		ExpandEnvVarBuf("%appdata%\\.minecraft\\saves", mcedit.worldsDir, MAX_PATHLEN);

	int i;
	for (i = 0; i < KBD_MAX_CONFIG; i ++)
	{
		STRPTR shortcut = GetINIValue(ini, keyBindings[i].config);
		if (shortcut)
			keyBindings[i].key = SITK_FromText(shortcut) | (keyBindings[i].key & SITK_FlagUp);
	}
	DOS2Unix(mcedit.capture);
	DOS2Unix(mcedit.worldsDir);

	selectionLoadState(ini);
	debugLoadSaveState((STRPTR) ini, True);

	FreeINI(ini);
}

static void prefsReadLang(void)
{
	TEXT path[64];
	sprintf(path, RESDIR "lang/%s.po", mcedit.lang);
	if (LangParse(path))
	{
		int i;
		for (i = 0; i < KBD_MAX_CONFIG; i ++)
			keyBindings[i].name = LANG(keyBindings[i].name);
	}
}

static void prefsSave(void)
{
	//SetINIValueInt(PREFS_PATH, "WndWidth",  globals.width);
	//SetINIValueInt(PREFS_PATH, "WndHeight", globals.height);
	SetINIValue(PREFS_PATH, "WorldEdit", mcedit.worldEdit);
}

/* convert keycodes from SDL to SITGL and vice versa */
int SDLKtoSIT(int key)
{
	int * sdlk;
	if (32 < key && key < 123)
		return key;
	for (sdlk = mSDLKtoSIT; sdlk < EOT(mSDLKtoSIT); sdlk += 2)
	{
		if (sdlk[0] == key)
			return sdlk[1];
	}
	return 0;
}

int SITKtoSDLK(int key)
{
	if (32 < key && key < 123)
		return key;
	int * sdlk;
	for (sdlk = mSDLKtoSIT; sdlk < EOT(mSDLKtoSIT); sdlk += 2)
	{
		if (sdlk[1] == key)
			return sdlk[0];
	}
	return 0;
}

int SDLMtoSIT(int mod)
{
	int ret = 0;
	if (mod & KMOD_CTRL)  ret |= SITK_FlagCtrl;
	if (mod & KMOD_SHIFT) ret |= SITK_FlagShift;
	if (mod & KMOD_ALT)   ret |= SITK_FlagAlt;
	return ret;
}

int SDLButtontoSIT(int button)
{
	switch (button) {
	case SDL_BUTTON_LEFT:      return SITK_LMB;
	case SDL_BUTTON_MIDDLE:    return SITK_MMB;
	case SDL_BUTTON_RIGHT:     return SITK_RMB;
	case SDL_BUTTON_WHEELDOWN: return SITK_MWD;
	case SDL_BUTTON_WHEELUP:   return SITK_MWU;
	default:                   return RAWKEY(SITK_NTH + button);
	}
}

/* handle extended selection toolbar actions */
static void mceditCommands(int cmd)
{
	if (globals.selPoints == 3)
	{
		if (cmd != MCUI_SEL_CLONE)
		{
			/* remove current brush */
			selectionCancelClone(NULL, NULL, NULL);
			/* will render the slot change */
			renderWorld();
			SIT_RenderNodes(globals.curTime);
			SDL_GL_SwapBuffers();
			FrameSaveRestoreTime(True);
			mceditUIOverlay(cmd);
			FrameSaveRestoreTime(False);
		}
		else /* brush manipulation: doesn't use any popup */
		{
			vec4 pos;
			MapExtraData sel = renderGetSelectedBlock(pos, NULL);
			if (sel == NULL)
			{
				memset(pos, 0, sizeof pos);
				selectionClone(pos, 0, True);
				renderSetSelectionPoint(RENDER_SEL_AUTOMOVE);
			}
			else selectionClone(pos, sel->side, True);
		}
	}
}

/* enable auto-repeat for text widget */
static int mceditTrackFocus(SIT_Widget w, APTR cd, APTR ud)
{
	int type;
	SIT_GetValues(SIT_GetFocus(), SIT_CtrlType, &type, NULL);
	if (cd && type == SIT_EDITBOX)
	{
		if (globals.inEditBox == 0)
			SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY, SDL_DEFAULT_REPEAT_INTERVAL);
		globals.inEditBox = 1;
	}
	else if (globals.inEditBox)
	{
		SDL_EnableKeyRepeat(0, 0);
		globals.inEditBox = 0;
	}
	return 0;
}

/* ESC key pressed: cancel stuff, if nothing to cancel, exit then */
static int mceditCancelStuff(SIT_Widget w, APTR cd, APTR ud)
{
	if (optionsExit(NULL, NULL, NULL))
	{
		;
	}
	else if (selectionCancelClone(NULL, NULL, NULL))
	{
		if (globals.selPoints == 0)
			renderSetSelectionPoint(RENDER_SEL_CLEAR);
	}
	else if (mcedit.state == GAMELOOP_OVERLAY)
		SIT_Exit(EXIT_LOOP);
	else if (globals.selPoints)
		renderSetSelectionPoint(RENDER_SEL_CLEAR);
	else
		mceditExit(w, NULL, (APTR) EXIT_APP);
	return 1;
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
	if (nb > 1)
	{
		/* edit immediately if passed as command line arg */
		CopyString(mcedit.worldEdit, argv[1], sizeof mcedit.worldEdit);
		mcedit.autoEdit = True;
	}

	SDL_Surface * screen = SDL_SetVideoMode(globals.width, globals.height, 32, SDL_HWSURFACE | SDL_GL_DOUBLEBUFFER | SDL_OPENGL | SDL_RESIZABLE);
	if (screen == NULL)
	{
		SIT_Log(SIT_ERROR, "failed to set video mode, aborting.");
		return 1;
	}
	SDL_WM_SetCaption("MCEdit", "MCEdit");

	if (gladLoadGL() == 0)
	{
		SIT_Log(SIT_ERROR,
			LANG("Fail to initialize OpenGL: minimum version required is 4.3\n\nVersion installed: %s"),
			glGetString ? (STRPTR) glGetString(GL_VERSION) : "<unknown> (fail to load opengl32.dll :-/)"
		);
		return 1;
	}

	globals.app = SIT_Init(SIT_NVG_FLAGS, globals.width, globals.height, RESDIR INTERFACE "default.css", 1);

	if (! globals.app)
	{
		SIT_Log(SIT_ERROR, LANG("failed to initialize SITGL:\n\n%s"), SIT_GetError());
		return 1;
	}

	static SIT_Accel accels[] = {
		{SITK_FlagCapture + SITK_FlagAlt + SITK_F4, SITE_OnActivate, EXIT_APP, NULL, mceditExit},
		{SITK_Escape, SITE_OnActivate, 0, NULL, mceditCancelStuff},
		{0}
	};

	SIT_SetValues(globals.app,
		SIT_DefSBSize,   SITV_Em(0.5),
		SIT_RefreshMode, SITV_RefreshAlways,
		SIT_AddFont,     "sans-serif",      "system",
		SIT_AddFont,     "sans-serif-bold", "system/Bold",
		SIT_AccelTable,  accels,
		SIT_ExitCode,    &mcedit.exit,
		SIT_SetAppIcon,  1,
		SIT_FontScale,   globals.guiScale,
		NULL
	);
	SIT_GetValues(globals.app, SIT_NVGcontext, &globals.nvgCtx, NULL);
	SIT_AddCallback(globals.app, SITE_OnFocus, mceditTrackFocus, NULL);
	SIT_AddCallback(globals.app, SITE_OnBlur,  mceditTrackFocus, NULL);

	/* need to be done after SITGL init */
	if (mcedit.lang[0])
		prefsReadLang();
	if (globals.fullScrWidth == 0 || globals.fullScrHeight == 0)
		SIT_GetValues(globals.app, SIT_ScreenWidth, &globals.fullScrWidth, SIT_ScreenHeight, &globals.fullScrHeight, NULL);

	if (! renderInitStatic())
	{
		/* shaders compilation failed usually */
		return 1;
	}

	mcedit.state = mcedit.autoEdit && mcedit.worldEdit[0] ? GAMELOOP_WORLDEDIT : GAMELOOP_WORLDSELECT;
	FrameSetFPS(globals.targetFPS);

	while (mcedit.exit != EXIT_APP)
	{
		mcedit.exit = 0;
		switch (mcedit.state) {
		case GAMELOOP_WORLDSELECT: mceditWorldSelect(); break;
		case GAMELOOP_WORLDEDIT:   mceditWorld();       break;
		default: break;
		}
	}
	SDL_FreeSurface(screen);
	SDL_Quit();
	selectionSaveState(PREFS_PATH);
	prefsSave();
	return 0;
}

#if 0
void minecartPushManual(int entityId, int up);

static void mceditPushManual(int up)
{
	vec4 pos;
	MapExtraData sel = renderGetSelectedBlock(pos, NULL);

	if (sel->entity > 0)
		minecartPushManual(sel->entity, up);
}
#endif

static uint8_t toolbarCmds[] = {
	MCUI_OVERLAY_REPLACE, MCUI_OVERLAY_FILL, MCUI_SEL_CLONE, MCUI_OVERLAY_LIBRARY, MCUI_OVERLAY_ANALYZE,
	MCUI_OVERLAY_SAVESEL, MCUI_OVERLAY_FILTER, MCUI_OVERLAY_DELPARTIAL, MCUI_OVERLAY_PIXELART
};

#define NO_EXTENDED_SEL_TOOLBAR \
	((mcedit.player.inventory.offhand & 1) == 0 && globals.selPoints == 0)

struct EventState_t
{
	uint8_t capture;
	uint8_t ignore;
	uint8_t sunMove;
	int     command;
};
typedef struct EventState_t *    EventState;

static int mceditRestoreCapture(SIT_Widget w, APTR cd, APTR ud)
{
	EventState state = ud;
	if (globals.lockMouse)
		state->ignore = 2, state->capture = 1;
	return 1;
}


/* handle shortcuts registered by keybindings[] table */
Bool mceditProcessCommand(EventState state, int keyUp)
{
	int cmd = state->command;
	/* there can be multiple commands with the same shortcut */
	do
	{
		switch ((KbdCmd_t) (cmd & 0xff)) {
		case KBD_MOVE_FORWARD:
		case KBD_MOVE_BACKWARD:
		case KBD_STRAFE_LEFT:
		case KBD_STRAFE_RIGHT:
		case KBD_JUMP:
		case KBD_FLYDOWN:
		case KBD_SWITCH_OFFHAND:
		case KBD_SLOT_0:
		case KBD_SLOT_1: case KBD_SLOT_2: case KBD_SLOT_3:
		case KBD_SLOT_4: case KBD_SLOT_5: case KBD_SLOT_6:
		case KBD_SLOT_7: case KBD_SLOT_8: case KBD_SLOT_9:
			switch (playerProcessKey(&mcedit.player, state->command, keyUp)) {
			case 0: return False;
			case 1:
				/* just switched to offhand: force block highlight (avoid block preview) */
				if (globals.selPoints < 3)
					renderSetSelectionPoint(mcedit.player.inventory.offhand & 1 ? RENDER_SEL_INIT : RENDER_SEL_CLEAR);
				break;
			case 2:
				/* partial extended selection, but switched to main toolbar: cancel selection */
				if (globals.selPoints == 3)
				{
					SDL_WM_GrabInput(SDL_GRAB_OFF);
					SDL_ShowCursor(SDL_ENABLE);
					if (globals.lockMouse == 0)
						state->capture = state->ignore = 0;
					mceditCommands(toolbarCmds[mcedit.player.inventory.selected]);
					if (mcedit.exit) return 1;
				}
				else renderSetSelectionPoint(RENDER_SEL_CLEAR);
			}
			break;
		case KBD_TROW_ITEM:
			playerAddInventory(&mcedit.player, NULL);
			playerUpdateNBT(&mcedit.player);
			break;
		case KBD_PLACE_BLOCK:
			mceditPlaceBlock();
			break;
		case KBD_ACTIVATE_BLOCK:
			mceditActivate();
			break;

		case KBD_HIDE_HUD:
			renderToggleDebug(RENDER_DEBUG_NOHUD);
			break;

		case KBD_TAKE_SCREENSHOT:
			takeScreenshot(NULL, NULL, NULL);
			break;
		case KBD_OPEN_INVENTORY:
			FrameSaveRestoreTime(True);
			mceditUIOverlay(MCUI_OVERLAY_BLOCK);
			FrameSaveRestoreTime(False);
			mcedit.player.inventory.update ++;
			break;
		case KBD_UNDO_CHANGE:
			undoOperation(0);
			break;
		case KBD_REDO_CHANGE:
			undoOperation(1);
			break;
		case KBD_PASTE_CLIPBOARD:
			libraryImport(globals.app, 0, 0);
			break;
		case KBD_CLEAR_SELECTION:
			renderSetSelectionPoint(RENDER_SEL_CLEAR);
			break;
		case KBD_WORLD_INFO:
			FrameSaveRestoreTime(True);
			mceditUIOverlay(MCUI_OVERLAY_WORLDINFO);
			FrameSaveRestoreTime(False);
			break;
		case KBD_SCHEMA_LIBRARY:
			FrameSaveRestoreTime(True);
			mceditUIOverlay(MCUI_OVERLAY_LIBRARY);
			FrameSaveRestoreTime(False);
			break;
		case KBD_WAYPOINT_EDITOR:
			FrameSaveRestoreTime(True);
			mceditUIOverlay(MCUI_OVERLAY_GOTO);
			FrameSaveRestoreTime(False);
			break;
		case KBD_COPY_SELECTION:
			if (globals.selPoints == 3)
			{
				Map brush = selectionCopy();
				if (brush)
					libraryCopySelection(brush);
			}
			break;
		case KBD_DEBUG_INFO:
			renderToggleDebug(RENDER_DEBUG_CURCHUNK);
			break;
		case KBD_ADVANCE_TIME:
			if (keyUp) state->sunMove &= ~1;
			else       state->sunMove |=  1;
			break;
		case KBD_BACK_IN_TIME:
			if (keyUp) state->sunMove &= ~2;
			else       state->sunMove |=  2;
			break;
		case KBD_SLICE_VIEW:
			if (globals.selPoints & 8)
				return 0;
			mceditSideView();
			mcedit.exit = 0;
			break;
		case KBD_SWITCH_MODE:
			playerSetMode(&mcedit.player, mcedit.player.pmode == MODE_CREATIVE ? MODE_SPECTATOR : MODE_CREATIVE);
			break;
		case KBD_SAVE_LOCATION:
			playerSaveLocation(&mcedit.player);
			mapSaveLevelDat(globals.level);
			break;
		case KBD_SAVE_CHANGES:
			if (! mapSaveAll(globals.level))
			{
				SIT_Log(SIT_ERROR, LANG("Fail to save changes: %s"), GetError());
				break;
			}
			if (mcedit.player.pmode >= MODE_CREATIVE)
			{
				playerSaveLocation(&mcedit.player);
				NBT_MarkForUpdate(&globals.level->levelDat, 0, 1);
			}
			if (NBT_IsModified(&globals.level->levelDat))
			{
				mapSaveLevelDat(globals.level);
			}
			renderAllSaved();
			break;
		case KBD_PICK_BLOCK:
			if (NO_EXTENDED_SEL_TOOLBAR)
			{
				/* add block selected to inventory bar */
				vec4 pos;
				MapExtraData sel = renderGetSelectedBlock(pos, NULL);
				if (sel)
				{
					struct Item_t item = {0};
					if (sel->entity == 0)
					{
						int XYZ[] = {pos[0] - sel->chunk->X, pos[1], pos[2] - sel->chunk->Z};
						item.count = 1;
						item.id = sel->blockId;
						item.extra = chunkGetTileEntity(sel->chunk, XYZ);
						playerAddInventory(&mcedit.player, &item);
					}
					else
					{
						entityGetItem(sel->entity, &item);
						playerAddInventory(&mcedit.player, &item);
					}
					playerUpdateNBT(&mcedit.player);
				}
			}
			else renderSetSelectionPoint(RENDER_SEL_AUTO);
			break;
		case KBD_QUICK_OPTIONS:
			SDL_WM_GrabInput(SDL_GRAB_OFF);
			SDL_ShowCursor(SDL_ENABLE);
			SIT_AddCallback(optionsQuickAccess(), SITE_OnFinalize, mceditRestoreCapture, state);
			state->capture = state->ignore = 0;
			return 1;

		case KBD_MOVE_VIEW:
			if (globals.lockMouse)
				break;
			if (mcedit.forceSel)
				renderShowBlockInfo(False, DEBUG_BLOCK|DEBUG_SELECTION), mcedit.forceSel = 0;
			/* ignore any pending mouse move */
			SDL_GetMouseState(&mcedit.mouseX, &mcedit.mouseY);
			state->ignore = 2;
			state->capture = 1;
			break;

		case KBD_CLOSE_WORLD:
			mceditExit(NULL, NULL, (APTR) EXIT_LOOP);
			break;
		case KBD_FULLSCREEN:
			SIT_ToggleFullScreen(globals.fullScrWidth, globals.fullScrHeight);
			break;
		}
		cmd >>= 8;
	}
	while (cmd > 0);

	if (globals.lockMouse && SDL_WM_GrabInput(SDL_GRAB_QUERY) == SDL_GRAB_OFF)
	{
		state->ignore = 2;
		state->capture = 1;
	}

	return 1;
}

static int mceditExit(SIT_Widget w, APTR cd, APTR ud)
{
	if (mcedit.state == GAMELOOP_WORLDEDIT && globals.modifCount > 0)
	{
		static struct EventState_t state = {.command = KBD_SAVE_CHANGES};
		FrameSaveRestoreTime(True);
		mceditUIOverlay(MCUI_OVERLAY_ASKIFSAVE);
		FrameSaveRestoreTime(False);
		switch (mcedit.askIfSave) {
		case 2: /* cancel */
			if (globals.lockMouse)
				/* will need to restore mouse grab on exit */
				globals.lockMouse = 2;
			return 1;
		case 1: /* save, exit */
			mceditProcessCommand(&state, 0);
		}
	}
	SIT_Exit((int) ud);
	return 1;
}


/*
 * Main loop for editing world
 */
void mceditWorld(void)
{
	struct EventState_t state = {0};
	struct KeyHash_t    keys;

	globals.level = renderInitWorld(mcedit.worldEdit, globals.renderDist);

	if (globals.level == NULL)
	{
		SIT_Log(SIT_ERROR, LANG("Fail to load level.dat: aborting."));
		mcedit.state = GAMELOOP_WORLDSELECT;
		return;
	}

	mceditSetWndCaption(globals.level);

	globals.yawPitch = &mcedit.player.angleh;
	wayPointsRead();
	ListAddTail(&globals.level->players, &mcedit.player.node);
	playerInit(&mcedit.player);

	keys.count = roundToUpperPrime(KBD_MAX+1);
	keys.hash  = alloca(keys.count * 5);
	keys.next  = (DATA8) (keys.hash + keys.count);
	keys.hasUp = 0;

	keysHash(&keys, keyBindings);

	renderSetInventory(&mcedit.player.inventory);
	renderSetViewMat(mcedit.player.pos, mcedit.player.lookat, &mcedit.player.angleh);

	if (globals.lockMouse)
	{
		state.ignore = 2;
		state.capture = 1;
	}

	while (! mcedit.exit)
	{
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			int key, mod;
			switch (event.type) {
			case SDL_ACTIVEEVENT:
				if (event.active.gain)
					keys.hasUp = 0;
				break;
			case SDL_KEYDOWN:
				key = SDLKtoSIT(event.key.keysym.sym);
				mod = SDLMtoSIT(event.key.keysym.mod);
				if (globals.inEditBox)
					goto forwardKeyPress;

				switch (event.key.keysym.sym) {
				case SDLK_LALT:
					if (state.capture && globals.lockMouse == 0) break;
					mcedit.forceSel = 1;
					renderShowBlockInfo(True, DEBUG_BLOCK|DEBUG_SELECTION);
					break;
				#ifdef DEBUG
				case SDLK_F1:
					SDL_GetMouseState(&mcedit.mouseX, &mcedit.mouseY);
					renderDebugBlock();
					break;
				case SDLK_F7:
					globals.breakPoint = ! globals.breakPoint;
					//FramePauseUnpause(globals.breakPoint);
					break;
				case SDLK_F3:
					if (event.key.keysym.mod & KMOD_CTRL)
					{
						renderFrustum(True);
						break;
					}
					goto case_SDLK;
				#endif
				case SDLK_DELETE:
					if ((globals.selPoints & 8) == 0)
						mceditCommands(MCUI_OVERLAY_DELALL);
					break;
				case SDLK_RETURN:
					if (globals.selPoints & 8)
					{
						selectionCopyBlocks(NULL, NULL, NULL);
						break;
					}
					goto case_SDLK;
				case SDLK_LSHIFT:
					if (! state.capture && NO_EXTENDED_SEL_TOOLBAR)
					{
						renderShowBlockInfo(True, DEBUG_SHOWITEM);
						mcedit.forceSel = 2;
					}
					// no break;
				default:
				case_SDLK:
					state.command = keysFind(&keys, key | mod);
					if (selectionProcessKey(state.command, key, mod))
						break;
					if (state.command >= 0 && mceditProcessCommand(&state, 0))
						break;
					goto forwardKeyPress;
				}
				break;
			case SDL_KEYUP:
				key = SDLKtoSIT(event.key.keysym.sym);
				mod = SDLMtoSIT(event.key.keysym.mod);
				if (globals.inEditBox)
					goto forwardKeyPress;
				state.command = keysFind(&keys, key | mod | SITK_FlagUp);
				if (state.command >= 0)
					mceditProcessCommand(&state, 1);

				switch (event.key.keysym.sym) {
				case SDLK_LALT:
					if (! mcedit.forceSel) break;
					mcedit.forceSel = 0;
					renderShowBlockInfo(False, DEBUG_BLOCK|DEBUG_SELECTION);
					break;
				case SDLK_LSHIFT:
					mcedit.forceSel = 0;
					renderShowBlockInfo(False, DEBUG_SHOWITEM);
					// no break;
				default:
					forwardKeyPress:
					if (key <= 0) break;
					if (event.type == SDL_KEYDOWN)
					{
						if (SIT_ProcessKey(key, mod, True) == 0 && key < SITK_Home)
							SIT_ProcessChar(key, mod);
					}
					else SIT_ProcessKey(key, mod, False);
					if (globals.lockMouse == 2)
					{
						globals.lockMouse = 1;
						state.ignore = 2;
						state.capture = 1;
					}
				}
				break;
			case SDL_MOUSEMOTION:
				SIT_ProcessMouseMove(event.motion.x, event.motion.y);
				switch (state.ignore) {
				case 1: state.ignore = 0; // no break;
				case 2: break;
				case 0:
					if (state.capture)
					{
						playerLookAt(&mcedit.player, event.motion.xrel, event.motion.yrel);
						renderSetViewMat(mcedit.player.pos, mcedit.player.lookat, &mcedit.player.angleh);
						state.capture = 2;
						if (globals.lockMouse)
							/* always point to what is in the middle of screen */
							renderPointToBlock(globals.width >> 1, globals.height >> 1);
					}
					else
					{
						renderPointToBlock(event.motion.x, event.motion.y);
						SIT_ProcessMouseMove(event.motion.x, event.motion.y);
					}
				}
				break;
			case SDL_MOUSEBUTTONDOWN:
				if (SIT_ProcessClick(event.button.x, event.button.y, event.button.button-1, 1))
					break;
				state.command = keysFind(&keys, SDLButtontoSIT(event.button.button));
				if (state.command >= 0 && mceditProcessCommand(&state, 0))
					break;
				switch (event.button.button) {
				case SDL_BUTTON_WHEELUP:
					if (NO_EXTENDED_SEL_TOOLBAR)
						playerScrollInventory(&mcedit.player, -1);
					break;
				case SDL_BUTTON_WHEELDOWN:
					if (NO_EXTENDED_SEL_TOOLBAR)
						playerScrollInventory(&mcedit.player, 1);
				}
				break;
			case SDL_MOUSEBUTTONUP:
				state.command = keysFind(&keys, SDLButtontoSIT(event.button.button) | SITK_FlagUp);
				if (state.command >= 0 && mceditProcessCommand(&state, 1))
					break;
				if (state.capture && globals.lockMouse == 0)
				{
					SDL_WM_GrabInput(SDL_GRAB_OFF);
					SDL_ShowCursor(SDL_ENABLE);
					if (state.capture == 2)
						SDL_WarpMouse(globals.width>>1, globals.height>>1);
					else
						SDL_WarpMouse(mcedit.mouseX, mcedit.mouseY);
					state.capture = state.ignore = 0;
				}
				if (SIT_ProcessClick(event.button.x, event.button.y, event.button.button-1, 0))
					break;
				break;
			case SDL_VIDEORESIZE:
				globals.width  = event.resize.w;
				globals.height = event.resize.h;
				SIT_ProcessResize(event.resize.w, event.resize.h);
				break;
			case SDL_QUIT:
				mcedit.exit = EXIT_APP;
			default:
				break;
			}
		}

		if (state.ignore)
		{
			SDL_WM_GrabInput(SDL_GRAB_ON);
			SDL_ShowCursor(SDL_DISABLE);
			/* ignore the next mouse move (from GRAB_ON) */
			state.ignore = 1;
		}
		if (mcedit.player.keyvec)
		{
			vec4 oldpos;
			memcpy(oldpos, mcedit.player.pos, 12);
			playerMove(&mcedit.player);
			if (memcmp(oldpos, mcedit.player.pos, 12))
			{
				renderSetViewMat(mcedit.player.pos, mcedit.player.lookat, &mcedit.player.angleh);
				if (! state.capture)
				{
					SDL_GetMouseState(&mcedit.mouseX, &mcedit.mouseY);
					renderPointToBlock(mcedit.mouseX, mcedit.mouseY);
				}
			}
		}
		if (state.sunMove)
			skydomeMoveSun(state.sunMove);
		globals.curTime = FrameGetTime();
		renderWorld();
		entityAnimate();
		updateTick();
		SIT_RenderNodes(globals.curTime);
		SDL_GL_SwapBuffers();
		#if 0
		{
			int diff = FrameGetTime() - globals.curTime;
			if (diff > 25)
				fprintf(stderr, "frame took %d ms (%d fake alloc)\n", diff, globals.level->fakeMax);
		}
		#endif
		FrameWaitNext();
	}

	/* if autoEdit is enabled, go back to world selection screen on next startup */
	if (mcedit.exit == EXIT_LOOP)
		mcedit.worldEdit[0] = 0;

	mcedit.state = GAMELOOP_WORLDSELECT;
	renderCloseWorld();
	mceditSetWndCaption(NULL);
	SDL_WM_GrabInput(SDL_GRAB_OFF);
	SDL_ShowCursor(SDL_ENABLE);
}

/* left click */
void mceditPlaceBlock(void)
{
	Player   p = &mcedit.player;
	vec4     pos;
	ItemID_t block, id;
	Item     item;

	MapExtraData sel = renderGetSelectedBlock(pos, &block);

	if (mcedit.forceSel == 2)
	{
		/* pointing at a world item entity */
		if (sel && sel->side == SIDE_ENTITY)
		{
			struct Item_t buffer = {0};
			entityGetItem(sel->entity, &buffer);
			if (buffer.id > 0 && playerAddInventory(&mcedit.player, &buffer))
			{
				if (entityDeleteById(globals.level, sel->entity))
				{
					renderAddModif();
					renderPointToBlock(mcedit.mouseX, mcedit.mouseY);
				}
				mcedit.forceSel = 0;
			}
		}
		else /* place current item */
		{
			worldItemAdd(globals.level);
			renderShowBlockInfo(False, DEBUG_SHOWITEM);
			mcedit.forceSel = 0;
		}
		return;
	}

	if (p->inventory.offhand & PLAYER_TOOLBAR)
	{
		/* click while hovering slot from toolbar: select slot */
		if (p->inventory.hoverSlot == 9)
		{
			/* hovering off-hand */
			if ((p->inventory.offhand & PLAYER_OFFHAND) == 0)
			{
				p->inventory.offhand |= PLAYER_OFFHAND;
				renderSetSelectionPoint(RENDER_SEL_INIT);
			}
			else p->inventory.offhand ^= PLAYER_ALTPOINT;
		}
		else /* hovering toolbar slot */
		{
			playerScrollInventory(p, p->inventory.hoverSlot - p->inventory.selected);
			if (globals.selPoints < 3)
				renderSetSelectionPoint(RENDER_SEL_CLEAR);
			else if ((globals.selPoints & 3) == 3)
				mceditCommands(toolbarCmds[mcedit.player.inventory.selected]);
		}
		return;
	}

	if (globals.selPoints & 8)
	{
		/* clone brush active: move brush instead */
		if (sel) selectionSetClonePt(pos, sel->side|SEL_CLONEMOVE_STOP);
		return;
	}

	if (p->inventory.offhand & PLAYER_OFFHAND)
	{
		/* off-hand slot selected: set selection point */
		renderSetSelectionPoint(RENDER_SEL_ADDPT);
		return;
	}
	if (sel == NULL) return;

	item = &p->inventory.items[p->inventory.selected];
	id   = mcedit.forceSel ? 0 : item->id;
	/* use of an item: check if it creates a block instead */
	if (! isBlockId(id))
	{
		ItemDesc desc = itemGetById(id);
		if (desc && desc->refBlock)
		{
			if (blockIds[desc->refBlock].special == BLOCK_SIGN)
				id = block;
			else
				id = block = (desc->refBlock << 4) | (block & 15);
		}
		else itemUse(id, sel->inter, sel->blockId);
	}
	if (sel->entity > 0) /* pointing at an entity */
	{
		if (id == 0 /* no block selected in inventory bar */)
		{
			if (sel->side == SIDE_ENTITY && entityDeleteById(globals.level, sel->entity))
				renderAddModif();
		}
		else worldItemUseItemOn(globals.level, sel->entity, item->id, pos);
	}
	else if (isBlockId(id))
	{
		/* 2 slabs in same block try to convert them in 1 double-slab */
		if (blockIds[block>>4].special == BLOCK_HALF)
		{
			/* need to be of the same type */
			int curId = mapGetBlockId(globals.level, pos, NULL);
			if ((curId & ~8) == (block & ~8) && (curId & 8) != (block & 8))
				/* can be combined */
				block = (block-16) & ~8;
		}
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
		if (mapUpdate(globals.level, pos, block, tile, UPDATE_NEARBY))
			renderAddModif();
	}
	else /* selected an item: check if we can create an entity instead */
		worldItemCreate(globals.level, id, pos, sel->side);
}

/* right click */
Bool mceditActivate(void)
{
	vec4 pos;
	int  block;

	MapExtraData sel = renderGetSelectedBlock(pos, &block);
	if (sel == NULL) return False;

	return mapActivate(globals.level, pos);
}

static NBTHdr mceditGetEnderItems(void)
{
	int enderItems = NBT_FindNode(mcedit.player.levelDat, 0, "Player.EnderItems");

	if (enderItems > 0)
		return NBT_Hdr(mcedit.player.levelDat, enderItems);
	else
		return NULL;
}

/* show level name in title bar */
static void mceditSetWndCaption(Map map)
{
	TEXT appName[] = " - MCEdit";
	TEXT levelName[64];

	if (map)
	{
		if (! NBT_GetString(&map->levelDat, NBT_FindNode(&map->levelDat, 0, "LevelName"), levelName, sizeof levelName - sizeof appName))
			CopyString(levelName, BaseName(map->path), sizeof levelName - sizeof appName);

		strcat(levelName, appName);
	}
	else strcpy(levelName, appName + 3);
	SDL_WM_SetCaption(levelName, levelName);
}

/* save/don't save callback from "ask if save" dialog */
static int mceditChooseSave(SIT_Widget w, APTR cd, APTR ud)
{
	mcedit.askIfSave = (int) ud;
	SIT_CloseDialog(w);
	SIT_Exit(EXIT_LOOP);
	return 1;
}

/*
 * display a modal user interface on top of editor
 */
void mceditUIOverlay(int type)
{
	static ItemBuf oldPlayerInv[MAXCOLINV * 4];
	struct MapExtraData_t link;

	SDL_Event event;
	Item      item;
	int       itemCount;
	int       itemConnect;
	uint8_t   enderItems;
	float     rotation[2];
	vec4      pos;

	SDL_ShowCursor(SDL_ENABLE);
	SDL_WM_GrabInput(SDL_GRAB_OFF);

	SIT_SetValues(globals.app, SIT_RefreshMode, SITV_RefreshAsNeeded, NULL);
	mcuiTakeSnapshot(globals.width, globals.height);
	renderSaveRestoreState(True);
	mcedit.state = GAMELOOP_OVERLAY;

	MapExtraData sel = NULL;
	itemCount = 0;
	enderItems = 0;
	itemConnect = 0;
	item = NULL;
	switch (type) {
	case MCUI_OVERLAY_BLOCK:
		/* show list of blocks to edit player's inventory */
		memcpy(oldPlayerInv, mcedit.player.inventory.items, sizeof oldPlayerInv);
		sel = renderGetSelectedBlock(pos, NULL);

		if (mcedit.forceSel)
		{
			/* selection will have to be released before exiting interface */
			mcedit.forceSel = 0;
			renderShowBlockInfo(False, DEBUG_SELECTION);
		}

		if (sel)
		{
			Block  b    = &blockIds[sel->blockId>>4];
			STRPTR tech = b->tech;
			STRPTR sep  = strchr(tech, '_');
			/* skip color name :-/ */
			if (sep && strcmp(sep+1, "shulker_box") == 0)
				goto case_INV;

			/* extract inventories from NBT structure */
			switch (FindInList("chest,trapped_chest,ender_chest,dispenser,dropper,furnace,lit_furnace", tech, 0)) {
			case 0:
			case 1:
				/* possibly a double-chest */
				itemConnect = mapConnectChest(globals.level, sel, &link);

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
					mcuiEditChestInventory(&mcedit.player.inventory, item, 54, b);
					break;
				}
				// else no break;
			case_INV:
				/* single chest */
				itemCount = 27;
				item = alloca(sizeof *item * 27 * 2);
				mapDecodeItems(item, 27, mapLocateItems(sel));
				memcpy(item + 27, item, 27 * sizeof *item);
				mcuiEditChestInventory(&mcedit.player.inventory, item, 27, b);
				break;
			case 2: /* ender chest */
				itemCount = 27;
				enderItems = 1;
				item = alloca(sizeof *item * 27 * 2);
				mapDecodeItems(item, 27, mceditGetEnderItems());
				memcpy(item + 27, item, 27 * sizeof *item);
				mcuiEditChestInventory(&mcedit.player.inventory, item, 27, b);
				break;
			case 3: /* dispenser */
			case 4: /* dropper */
				itemCount = 9;
				item = alloca(sizeof *item * 9 * 2);
				mapDecodeItems(item, 9, mapLocateItems(sel));
				memcpy(item + 9, item, 9 * sizeof *item);
				mcuiEditChestInventory(&mcedit.player.inventory, item, 9, b);
				break;
			case 6: /* lit furnace */
				b --;
			case 5: /* furnace */
				itemCount = 3;
				item = alloca(sizeof *item * 3 * 2);
				mapDecodeItems(item, 9, mapLocateItems(sel));
				memcpy(item + 3, item, 3 * sizeof *item);
				mcuiEditChestInventory(&mcedit.player.inventory, item, 3, b);
				break;

			default:
				if (b->special == BLOCK_SIGN)
					mcuiCreateSignEdit(pos, sel->blockId);
				else
					mcuiCreateInventory(&mcedit.player.inventory);
			}
		}
		else mcuiCreateInventory(&mcedit.player.inventory);
		break;

	case MCUI_OVERLAY_GOTO:
		memcpy(pos, mcedit.player.pos, sizeof pos);
		memcpy(rotation, &mcedit.player.angleh, 8);
		wayPointsEdit(pos, rotation);
		break;

	case MCUI_OVERLAY_ANALYZE:    mcuiAnalyze(); break;
	case MCUI_OVERLAY_REPLACE:    mcuiFillOrReplace(False); break;
	case MCUI_OVERLAY_FILL:       mcuiFillOrReplace(True); break;
	case MCUI_OVERLAY_DELALL:     mcuiDeleteAll(); break;
	case MCUI_OVERLAY_LIBRARY:
	case MCUI_OVERLAY_SAVESEL:    libraryShow(type); break;
	case MCUI_OVERLAY_DELPARTIAL: mcuiDeletePartial(); break;
	case MCUI_OVERLAY_PAINTING:   mcuiShowPaintings(); break;
	case MCUI_OVERLAY_PIXELART:   mcuiShowPixelArt(mcedit.player.pos); break;
	case MCUI_OVERLAY_WORLDINFO:  mcuiWorldInfo(); break;
	case MCUI_OVERLAY_FILTER:     mcuiFilter(); break;
	case MCUI_OVERLAY_ASKIFSAVE:  mcuiAskSave(mceditChooseSave); mcedit.askIfSave = 2; break;
	}

	SDL_EnableUNICODE(1);

	while (! mcedit.exit)
	{
		while (SDL_PollEvent(&event))
		{
			int key, mod;
			switch (event.type) {
			case SDL_KEYDOWN:
			case SDL_KEYUP:
				key = SDLKtoSIT(event.key.keysym.sym);
				mod = SDLMtoSIT(event.key.keysym.mod);
				/* only one command is interesting here, no need to check hash table for this */
				if (keyBindings[KBD_TAKE_SCREENSHOT].key == (key|mod) && event.type == SDL_KEYDOWN)
				{
					takeScreenshot(NULL, NULL, NULL);
					break;
				}
				if (key > 0 && SIT_ProcessKey(key, mod, event.type == SDL_KEYDOWN))
					break;

				if (event.key.keysym.unicode > 0)
					SIT_ProcessChar(event.key.keysym.unicode, mod);
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
			case SDL_VIDEOEXPOSE:
				SIT_ForceRefresh();
				break;
			case SDL_VIDEORESIZE:
				globals.width  = event.resize.w;
				globals.height = event.resize.h;
				mcuiResize();
				SIT_ProcessResize(globals.width, globals.height);
				break;
			case SDL_QUIT:
				mcedit.exit = EXIT_APP;
				goto exit;
			default:
				continue;
			}
		}

		/* update and render */
		mcuiInitDrawItems();
		glViewport(0, 0, globals.width, globals.height);
		switch (SIT_RenderNodes(globals.curTimeUI = FrameGetTime())) {
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

	/* check if there was any modifications */
	switch (type) {
	case MCUI_OVERLAY_BLOCK:
	{
		NBTFile_t chest = {0};
		NBTFile_t chest2 = {0};
		NBTFile_t playerInv = {0};

		if (itemCount > 0 && memcmp(item, item + itemCount, itemCount * sizeof *item))
		{
			/* changes were made to container */
			if (enderItems)
			{
				/* these need to be stored in level.dat */
				mapSerializeItems(NULL, "EnderItems", item, itemCount, &chest);
				NBT_Insert(mcedit.player.levelDat, "Player.EnderItems", TAG_List_Compound, &chest);
				NBT_Free(&chest);
				memset(&chest, 0, sizeof chest);
			}
			/* double-chest items need to be split in 2 different tile entity */
			else switch (itemConnect) {
			case 1:
				mapSerializeItems(sel,   "Items", item,    27, &chest);
				mapSerializeItems(&link, "Items", item+27, 27, &chest2);
				break;
			case 2:
				mapSerializeItems(&link, "Items", item,    27, &chest2);
				mapSerializeItems(sel,   "Items", item+27, 27, &chest);
				break;
			default:
				mapSerializeItems(sel, "Items", item, itemCount, &chest);
			}
		}

		if (mcedit.player.pmode >= MODE_CREATIVE && memcmp(oldPlayerInv, mcedit.player.inventory.items, sizeof oldPlayerInv))
			/* only update NBT if player is in creative mode */
			mapSerializeItems(NULL, "Inventory", mcedit.player.inventory.items, DIM(oldPlayerInv), &playerInv);

		if (chest.mem)
		{
			if (chest2.mem)
			{
				/* double-chest are split in 2 */
				undoLog(LOG_BLOCK | UNDO_LINK, link.blockId, chunkGetTileEntityFromOffset(link.chunk, link.cd->Y, link.offset),
					link.cd, link.offset);
				chunkUpdateNBT(link.chunk, link.offset + (link.cd->Y<<8), &chest2);
			}
			undoLog(LOG_BLOCK, sel->blockId, chunkGetTileEntityFromOffset(sel->chunk, sel->cd->Y, sel->offset), sel->cd, sel->offset);
			chunkUpdateNBT(sel->chunk, sel->offset + (sel->cd->Y<<8), &chest);
			mapAddToSaveList(globals.level, sel->chunk);
			renderAddModif();
		}
		if (mcedit.exit == 3)
		{
			/* sign changed */
			mcedit.exit = EXIT_LOOP;
			mapAddToSaveList(globals.level, sel->chunk);
			renderAddModif();
		}

		if (playerInv.mem)
		{
			int offset = NBT_Insert(&globals.level->levelDat, "Player.Inventory", TAG_List_Compound, &playerInv);
			NBT_Free(&playerInv);
			NBT_MarkForUpdate(&globals.level->levelDat, 0, 1);
			if (offset >= 0)
				playerUpdateInventory(&mcedit.player);
		}
	}	break;
	case MCUI_OVERLAY_GOTO:
		playerTeleport(&mcedit.player, pos, rotation);
		renderSetViewMat(mcedit.player.pos, mcedit.player.lookat, &mcedit.player.angleh);
		break;

	case MCUI_OVERLAY_ANALYZE:
	case MCUI_OVERLAY_REPLACE:
	case MCUI_OVERLAY_FILL:
	case MCUI_OVERLAY_PIXELART:
		mcedit.player.inventory.update ++;
		break;
	case MCUI_OVERLAY_WORLDINFO:
		/* level.dat modified: reparse player inventory */
		playerUpdateInventory(&mcedit.player);
	}

	exit:
	SIT_Nuke(SITV_NukeCtrl);
	SIT_SetValues(globals.app, SIT_RefreshMode, SITV_RefreshAlways, NULL);
	SDL_EnableUNICODE(0);
	renderSaveRestoreState(False);
	mcedit.state = GAMELOOP_WORLDEDIT;
	if (mcedit.exit == EXIT_LOOP)
		/* otherwise we would immediately exit from GAMELOOP_WORLDEDIT */
		mcedit.exit = 0;
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

	SDL_ShowCursor(SDL_ENABLE);
	SDL_WM_GrabInput(SDL_GRAB_OFF);

	FrameSaveRestoreTime(True);
	renderSaveRestoreState(True);
	debugSetPos(&mcedit.exit);
	debugWorld();
	mx = my = 0;
	SDL_GL_SwapBuffers();

	while (! mcedit.exit && SDL_WaitEvent(&event))
	{
		do {
			switch (event.type) {
			case SDL_KEYDOWN:
				switch (event.key.keysym.sym) {
				case SDLK_LALT:
					debugBlock(mcedit.mouseX, mcedit.mouseY, 0);
					renderShowBlockInfo(True, DEBUG_BLOCK);
					info = 1;
					break;
				case SDLK_F1:    debugBlock(mcedit.mouseX, mcedit.mouseY, 1); break;
				case SDLK_F3:    debugToggleInfo(DEBUG_CHUNK); break;
				case SDLK_F7:    globals.breakPoint = ! globals.breakPoint; break;
				case SDLK_TAB:   mcedit.exit = EXIT_LOOP; break;
				case SDLK_e:
				case SDLK_UP:    debugMoveSlice(1); break;
				case SDLK_d:
				case SDLK_DOWN:  debugMoveSlice(-1); break;
				case SDLK_MINUS: debugRotateView(-1); break;
				case SDLK_EQUALS:
				case SDLK_PLUS:  debugRotateView(1); break;
				case SDLK_b:     debugToggleInfo(DEBUG_LIGHT); break;
				default:         goto forwardKeyPress;
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
					mx = SDLKtoSIT(event.key.keysym.sym);
					if (mx > 0 && SIT_ProcessKey(mx, SDLMtoSIT(event.key.keysym.mod), event.type == SDL_KEYDOWN))
						break;
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
					debugBlock(event.motion.x, event.motion.y, 0);
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
				mcedit.exit = EXIT_APP;
				break;
			case SDL_VIDEOEXPOSE:
				SIT_ForceRefresh();
				break;
			case SDL_VIDEORESIZE:
				globals.width  = event.resize.w;
				globals.height = event.resize.h;
				SIT_ProcessResize(globals.width, globals.height);
				refresh = 1;
			default: break;
			}
		} while (SDL_PollEvent(&event));

		if (refresh || SIT_NeedRefresh())
		{
			debugWorld();
			SDL_GL_SwapBuffers();
			refresh = 0;
		}
	}
	debugLoadSaveState(PREFS_PATH, False);
	mcedit.state = GAMELOOP_WORLDEDIT;
	SIT_Nuke(SITV_NukeCtrl);
	FrameSaveRestoreTime(False);
	renderSaveRestoreState(False);
}

#ifdef	WIN32
#include <windows.h>

int WINAPI WinMain(
    HINSTANCE hInstance,
    HINSTANCE hPrevInstance,
    LPSTR CmdLine,
    int nCmdShow)
{
	/* CmdLine parameter is not unicode aware even with UNICODE macro set */
	int      nb, i;
	LPWSTR * argvUTF16 = CommandLineToArgvW(GetCommandLineW(), &nb);
	STRPTR * argvUTF8  = (STRPTR *) argvUTF16;

	HANDLE user32 = LoadLibraryA("user32.dll");
	if (user32)
	{
		WINAPI BOOL (*proc)() = GetProcAddress(user32, "SetProcessDPIAware");
		fprintf(stderr, "result = %d\n", proc());
	}

	/* convert strings to UTF8 */
	for (i = 0; i < nb; )
	{
		int len = wcslen(argvUTF16[i]);
		int sz  = UTF16ToUTF8(NULL, 0, (STRPTR) argvUTF16[i], len) + 1;

		CmdLine = alloca(sz);

		sz = UTF16ToUTF8(CmdLine, sz, (STRPTR) argvUTF16[i], len);

		argvUTF8[i++] = CmdLine;
	}

	i = main(nb, argvUTF8);
	/* will make memory leak detection tool happy */
	LocalFree(argvUTF16);
	return i;
}
#endif
