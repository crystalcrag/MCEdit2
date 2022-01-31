/*
 * worldSelect.c : interface for listing worlds and all dialog related to this screen (mostly config).
 *
 * written by T.Pierron, dec 2021.
 */

#define WORLDSELECT_IMPL
#include <SDL/SDL.h>
#include <glad.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "SIT.h"
#include "render.h"
#include "globals.h"
#include "worldSelect.h"
#include "mcedit.h"
#include "keybindings.h"


static struct WorldSelect_t worldSelect;
static struct KeyBinding_t  editBindings[KBD_MAX];
extern struct GameState_t   mcedit;


int optionsExit(SIT_Widget w, APTR cd, APTR save)
{
	if (save)
	{
		SetINIValueInt(PREFS_PATH, "Options/CompassSize",   lroundf(globals.compassSize * 100));
		SetINIValueInt(PREFS_PATH, "Options/GUIScale",      globals.guiScale);
		SetINIValueInt(PREFS_PATH, "Options/FieldOfVision", globals.fieldOfVision);
		SetINIValueInt(PREFS_PATH, "Options/RedstoneTick",  globals.redstoneTick);
		SetINIValueInt(PREFS_PATH, "Options/RenderDist",    globals.renderDist);
		SetINIValueInt(PREFS_PATH, "Options/UseFOG",        globals.distanceFOG);
	}
	if (w == NULL)
		w = worldSelect.options;
	if (w)
	{
		SIT_CloseDialog(w);
		worldSelect.options = NULL;
		return 1;
	}
	return 0;
}

static int optionsSetValue(SIT_Widget w, APTR cd, APTR ud)
{
	switch ((int) ud) {
	case 0: /* compass size */
		if (worldSelect.compassSize < 50)
		{
			SIT_SetValues(worldSelect.enterKey, SIT_Title, "N/A", NULL);
			globals.compassSize = 0;
		}
		else globals.compassSize = worldSelect.compassSize * 0.01f;
		break;
	case 1: /* field of view */
		globals.fieldOfVision = worldSelect.fov;
		renderSetFOV(worldSelect.fov);
		break;
	case 2: /* interface scale */
		globals.guiScale = worldSelect.guiScale;
		SIT_SetValues(globals.app, SIT_FontScale, worldSelect.guiScale, NULL);
		break;
	case 3: /* render distance */
		globals.renderDist = worldSelect.renderDist;
		mapSetRenderDist(globals.level, worldSelect.renderDist);
		renderResetFrustum();
		break;
	case 4: /* brightness */
		{
			TEXT num[16];
			sprintf(num, "+%d%%", worldSelect.brightness);
			SIT_SetValues(worldSelect.brightval, SIT_Title, worldSelect.brightness == 101 ? "Full" : num, NULL);
			globals.brightness = worldSelect.brightness;
			renderToggleDebug(RENDER_DEBUG_BRIGHT);
		}
		break;
	case 5: /* fog enabled */
		globals.distanceFOG = worldSelect.fog;
		renderSetFOG(worldSelect.fog);
	}
	return 1;
}

static int optionsSetDefault(SIT_Widget w, APTR cd, APTR ud)
{
	globals.compassSize = 1;
	globals.guiScale = 100;
	globals.redstoneTick = 100;
	globals.fieldOfVision = 80;
	globals.brightness = 0;
	globals.distanceFOG = 1;
	SIT_SetValues(SIT_GetById(ud, "compass"),  SIT_SliderPos, 100, NULL);
	SIT_SetValues(SIT_GetById(ud, "guiscale"), SIT_SliderPos, globals.guiScale, NULL);
	SIT_SetValues(SIT_GetById(ud, "fovval"),   SIT_SliderPos, globals.fieldOfVision, NULL);
	SIT_SetValues(SIT_GetById(ud, "bright"),   SIT_SliderPos, 0, NULL);
	SIT_SetValues(SIT_GetById(ud, "tick"), SIT_Title, NULL, NULL);
	renderSetFOG(globals.distanceFOG);
	renderToggleDebug(RENDER_DEBUG_BRIGHT);
	renderSetFOV(globals.fieldOfVision);
	return 1;
}

static int optionsClearRef(SIT_Widget w, APTR cd, APTR ud)
{
	worldSelect.options = NULL;
	return 1;
}

/* interface for quick access to some common options (Ctrl+O by default) */
int optionsQuickAccess(SIT_Widget unused1, APTR unused2, APTR unused3)
{
	SIT_Widget diag = worldSelect.options = SIT_CreateWidget("quickopt.mc", SIT_DIALOG, globals.app,
		SIT_DialogStyles, SITV_Plain,
		NULL
	);

	/* don't edit real values just yet */
	worldSelect.renderDist  = globals.renderDist;
	worldSelect.fov         = globals.fieldOfVision;
	worldSelect.compassSize = lroundf(globals.compassSize * 100);
	worldSelect.guiScale    = globals.guiScale;
	worldSelect.fog         = globals.distanceFOG;
	worldSelect.brightness  = globals.brightness;

	SIT_Widget max = NULL;
	SIT_CreateWidgets(diag,
		"<label name=dlgtitle#title title='Quick options:' left=FORM right=FORM>"
		/* compass size */
		"<editbox name=compSize width=5em editType=", SITV_Integer, "right=FORM top=WIDGET,dlgtitle,0.5em>"
		"<slider name=compass minValue=49 curValue=", &worldSelect.compassSize, "maxValue=150 pageSize=1 width=15em"
		" top=MIDDLE,compSize left=FORM right=WIDGET,compSize,0.5em buddyEdit=compSize buddyLabel=", "Compass (%):", &max, ">"
		/* FOV */
		"<editbox name=fov width=5em editType=", SITV_Integer, "right=FORM top=WIDGET,compSize,0.5em>"
		"<slider name=fovval minValue=20 curValue=", &worldSelect.fov, "maxValue=140 pageSize=1 top=MIDDLE,fov right=WIDGET,fov,0.5em"
		" buddyEdit=fov buddyLabel=", "Field of vision:", &max, ">"
		/* GUI scale */
		"<editbox name=gui width=5em editType=", SITV_Integer, "right=FORM top=WIDGET,fov,0.5em>"
		"<slider name=guiscale minValue=50 curValue=", &worldSelect.guiScale, "maxValue=200 pageSize=1 top=MIDDLE,gui"
		" right=WIDGET,gui,0.5em buddyEdit=gui buddyLabel=", "GUI scale:", &max, ">"
		/* brightness */
		"<label name=brightval right=FORM left=OPPOSITE,gui>"
		"<slider name=bright curValue=", &worldSelect.brightness, "maxValue=101 pageSize=1 top=WIDGET,guiscale,0.5em"
		" right=WIDGET,brightval,0.5em buddyLabel=", "Brightness:", &max, ">"
		/* render distance */
		"<editbox name=dist width=6em editType=", SITV_Integer, "top=WIDGET,bright,0.5em minValue=1 maxValue=16 curValue=", &worldSelect.renderDist,
		" buddyLabel=", "Render dist:", &max, ">"
		"<label name=msg title=chunks left=WIDGET,dist,0.5em top=MIDDLE,dist>"
		/* redstone tick */
		"<editbox name=tick width=6em minValue=1 stepValue=100 curValue=", &globals.redstoneTick, "top=WIDGET,dist,0.5em editType=", SITV_Integer,
		" buddyLabel=", "Redstone tick:", &max, ">"
		"<label name=msg left=WIDGET,tick,0.5em top=MIDDLE,tick title='ms (def: 100)'>"
		/* distance FOG */
		"<button name=fog buttonType=", SITV_CheckBox, "curValue=", &worldSelect.fog, "title=", "Enable distance fog.",
		" top=WIDGET,tick,0.5em left=OPPOSITE,tick>"

		"<button name=ko.act title=Use top=WIDGET,fog,0.5em right=FORM>"
		"<button name=ok.act title=Save top=OPPOSITE,ko right=WIDGET,ko,0.5em nextCtrl=ko buttonType=", SITV_DefaultButton, ">"
		"<button name=def.act title=Default top=OPPOSITE,ko right=WIDGET,ok,0.5em nextCtrl=ok>"
	);
	SIT_SetAttributes(diag, "<brightval top=MIDDLE,bright>");
	worldSelect.enterKey  = SIT_GetById(diag, "compSize");
	worldSelect.brightval = SIT_GetById(diag, "brightval");
	SIT_AddCallback(SIT_GetById(diag, "compass"),  SITE_OnChange, optionsSetValue, NULL);
	SIT_AddCallback(SIT_GetById(diag, "fovval"),   SITE_OnChange, optionsSetValue, (APTR) 1);
	SIT_AddCallback(SIT_GetById(diag, "guiscale"), SITE_OnChange, optionsSetValue, (APTR) 2);
	SIT_AddCallback(SIT_GetById(diag, "dist"),     SITE_OnChange, optionsSetValue, (APTR) 3);
	SIT_AddCallback(SIT_GetById(diag, "bright"),   SITE_OnChange, optionsSetValue, (APTR) 4);
	SIT_AddCallback(SIT_GetById(diag, "fog"),      SITE_OnActivate, optionsSetValue, (APTR) 5);
	SIT_AddCallback(SIT_GetById(diag, "ok"),  SITE_OnActivate, optionsExit, (APTR) 1);
	SIT_AddCallback(SIT_GetById(diag, "ko"),  SITE_OnActivate, optionsExit, NULL);
	SIT_AddCallback(SIT_GetById(diag, "def"), SITE_OnActivate, optionsSetDefault, diag);
	SIT_AddCallback(diag, SITE_OnFinalize, optionsClearRef, NULL);

	if (worldSelect.compassSize < 50)
		optionsSetValue(NULL, NULL, NULL);
	optionsSetValue(NULL, NULL, (APTR) 4);

	SIT_ManageWidget(diag);
	return 1;
}

/*
 * world selection interface
 */
int SDLMtoSIT(int mod);
int SDLKtoSIT(int key);
int SITKtoSDLK(int key);
int takeScreenshot(SIT_Widget w, APTR cd, APTR ud);

static int worldSelectEnableEdit(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_SetValues(ud, SIT_Enabled, cd != NULL, NULL);
	return 1;
}

static int worldSelectExit(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_Exit(1);
	return 1;
}

static SIT_Accel dialogAccels[] = { /* override ESC shortcut from top-level interface */
	{SITK_FlagCapture + SITK_Escape, SITE_OnClose},
	{SITK_FlagCapture + SITK_F2,     SITE_OnActivate, NULL, takeScreenshot},
	{0}
};

/* display an about dialog */
static int worldSelectAbout(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_Widget about = SIT_CreateWidget("about.mc dark", SIT_DIALOG, ud,
		SIT_AccelTable,   dialogAccels,
		SIT_DialogStyles, SITV_Movable | SITV_Plain,
		SIT_Style,        "font-size: 1.1em",
		NULL
	);

	static char header[] =
		"<a href='https://github.com/crystalcrag/MCEdit2'>MCEdit</a> "MCEDIT_VERSION" for MS-Win32-x86<br>"
		"Written by T.Pierron.<br>"
		"Compiled on " __DATE__ " with gcc " TOSTRING(__GNUC__) "." TOSTRING(__GNUC_MINOR__) "." TOSTRING(__GNUC_PATCHLEVEL__);

	static char thanks[] =
		"Make use of the following libraries:<br>"
		"- <a href='https://github.com/memononen/nanovg/'>nanovg</a> by Mikko Memononen<br>"
		"- <a href='https://github.com/nothings/stb'>stb_truetype</a>,<br>"
		"- <a href='https://github.com/nothings/stb'>stb_image</a>,<br>"
		"- <a href='https://github.com/nothings/stb'>stb_include</a> by Sean Barret<br>"
		"- <a href='https://github.com/nothings/SITGL'>SITGL</a> by T.Pierron<br>"
		"- <a href='https://www.libsdl.org/'>SDL</a> by Sam Lantinga<br>"
		"- <a href='https://www.zlib.net/'>zlib</a> by Jean-loup Gailly and Mark Adler";

	static char license[] =
		"Under terms of BSD 2-clause license.<br>"
		"No warranty, use at your own risk.";

	TEXT vendor[128];

	snprintf(vendor, sizeof vendor, "%s<br>Open GL v%s", (STRPTR) glGetString(GL_RENDERER), (STRPTR) glGetString(GL_VERSION));

	SIT_CreateWidgets(about,
		"<label name=what.big style='text-align: center' title=", header, ">"
		"<label name=thanks title=", thanks, "top=WIDGET,what,1em>"
		"<label name=legal.big title=License top=WIDGET,thanks,1em left=", SITV_AttachCenter, ">"
		"<label name=license title=", license, "top=WIDGET,legal,0.5em>"
		"<label name=gpu.big title='Graphics card in use:' top=WIDGET,license,1em left=", SITV_AttachCenter, ">"
		"<label name=version title=", vendor, "top=WIDGET,gpu,0.5em>"

		"<button name=close.act title=Ok top=WIDGET,version,1em buttonType=", SITV_CancelButton, "left=", SITV_AttachCenter, ">"
	);

	SIT_ManageWidget(about);

	return 1;
}

/* key button activation */
static int worldSelectEnterKey(SIT_Widget w, APTR cd, APTR ud)
{
	if (worldSelect.curKey)
		SIT_SetValues(worldSelect.curKey, SIT_Classes, "key", NULL);
	else
		SIT_SetValues(worldSelect.enterKey, SIT_Visible, True, NULL);
	SIT_SetValues(worldSelect.curKey = w, SIT_Classes, "key sel", NULL);
	worldSelect.curKeySym = 0;
	worldSelect.curKeyMod = 0;
	return 1;
}

/* <a> onclick */
static int worldSelectCancelKbd(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_SetValues(worldSelect.enterKey, SIT_Visible, False, NULL);
	SIT_SetValues(worldSelect.curKey, SIT_Classes, "key", NULL);
	worldSelect.curKey = NULL;
	return 1;
}

/* sync slider value with their labels */
static int worldSelectSyncValue(SIT_Widget w, APTR cd, APTR ud)
{
	STRPTR format;
	TEXT   buffer[64];
	APTR   userData;
	int    num = (int) cd;
	SIT_GetValues(w, SIT_UserData, &userData, NULL);
	/* buddyEdit is not enough: we need more control over how msg is formatted */
	switch ((int) userData) {
	case 0: format = num > 1 ? "%d chunks" : "%d chunk"; break;
	case 1: format = "%d&#xb0;"; break; /* FOV */
	case 2: format = num == 150 ? "Uncapped FPS" : "%d FPS"; break;
	case 3: format = num == 101 ? "Full brightness" : "+%d%%"; break;
	case 4:
	case 5: format = "%d%%"; break;
	case 6: format = num == 49 ? "Disabled" : "%d%%"; break;
	default: return 0;
	}
	sprintf(buffer, format, num);
	SIT_SetValues(ud, SIT_Title, buffer, NULL);
	return 1;
}

/* lots of boilerplate ... */
static void worldSelectSetCb(SIT_Widget parent, STRPTR name)
{
	TEXT nameval[16];
	int * curValue;
	sprintf(nameval, "%sval", name);
	SIT_Widget slider = SIT_GetById(parent, name);
	SIT_Widget value  = SIT_GetById(parent, nameval);
	SIT_GetValues(slider, SIT_CurValue, &curValue, NULL);
	SIT_AddCallback(slider,  SITE_OnChange, worldSelectSyncValue, value);
	worldSelectSyncValue(slider, (APTR) *curValue, value);
}

static int worldSelectSelectFolder(SIT_Widget w, APTR cd, APTR ud)
{
	static SIT_Widget dir;

	if (dir == NULL)
	{
		dir = SIT_CreateWidget("dirsel", SIT_DIRSELECT, w,
			SIT_Title, "Select your destination path",
			NULL
		);
	}
	STRPTR current;
	SIT_GetValues(ud, SIT_Title, &current, NULL);
	SIT_SetValues(dir, SIT_InitPath, current, NULL);

	if (SIT_ManageWidget(dir))
	{
		STRPTR path;
		SIT_GetValues(dir, SIT_InitPath, &path, NULL);
		SIT_SetValues(ud, SIT_Title, path, NULL);
	}
	return 1;
}

/* current tab of config editor has changed */
static int worldSelectTabChanged(SIT_Widget w, APTR cd, APTR ud)
{
	if (worldSelect.curKey)
		worldSelectCancelKbd(NULL, NULL, NULL);
	worldSelect.curTab = (int) cd;
	return 1;
}

/*
 * save/use callback for config file
 */
void SITK_ToText(STRPTR keyName, int max, int key)
{
	static struct KeyBinding_t mods[] = {
		{"Ctrl+",  NULL, SITK_FlagCtrl},
		{"Shift+", NULL, SITK_FlagShift},
		{"Alt+",   NULL, SITK_FlagAlt},
		{"Cmd+",   NULL, SITK_FlagCmd},
	};

	int len, i;

	/* qualifier first */
	for (keyName[0] = 0, len = i = 0; i < DIM(mods); i ++)
	{
		KeyBinding kbd = mods + i;
		if (kbd->key & key)
			len = StrCat(keyName, max, len, kbd->name);
	}
	/* key name */
	key &= ~SITK_Flags;
	if (key == 0)
	{
		/* unassigned */
		strcpy(keyName, "---");
	}
	else if (32 < key && key < 123)
	{
		if ('a' <= key && key <= 'z')
			key += 'A' - 'a';

		keyName[len] = key;
		keyName[len+1] = 0;
	}
	else
	{
		if (key >= SITK_NTH)
		{
			/* nth mouse button */
			sprintf(keyName + len, "MB%d", key - SITK_NTH);
		}
		else
		{
			STRPTR keyFmt;
			/* special key */
			switch (key) {
			case SITK_LMB: keyFmt = "LMB"; break;
			case SITK_MMB: keyFmt = "MMB"; break;
			case SITK_RMB: keyFmt = "RMB"; break;
			case SITK_MWD: keyFmt = "MWD"; break;
			case SITK_MWU: keyFmt = "MWU"; break;
			default:       keyFmt = (key = SITKtoSDLK(key)) > 0 ? SDL_GetKeyName(key) : "???";
			}
			StrCat(keyName, max, len, keyFmt);
			uint8_t chr = keyName[len];
			if ('a' <= chr && chr <= 'z')
				keyName[len] = chr - 'a' + 'A';
		}
	}
}

static int worldSelectSave(SIT_Widget w, APTR cd, APTR save)
{
	/*
	 * the whole reason we edited <worldSelect> instead of <globals> is that we can cancel any changes done
	 * in this interface; the drawback is we'll have to copy everything back if user accepts its changes
	 */
	int oldScale = globals.guiScale;
	globals.compassSize   = worldSelect.compassSize * 0.01f;
	globals.mouseSpeed    = worldSelect.sensitivity * 0.01f;
	globals.fieldOfVision = worldSelect.fov;
	globals.brightness    = worldSelect.brightness;
	globals.targetFPS     = worldSelect.fps;
	globals.guiScale      = worldSelect.guiScale;
	globals.renderDist    = worldSelect.renderDist;
	globals.distanceFOG   = worldSelect.fog;
	globals.showPreview   = worldSelect.showPreview;

	mcedit.autoEdit       = worldSelect.autoEdit;
	mcedit.fullScreen     = worldSelect.fullScreen;
	mcedit.lockMouse      = worldSelect.lockMouse;

	memcpy(keyBindings, editBindings, sizeof editBindings);

	STRPTR folder;
	SIT_GetValues(worldSelect.capture, SIT_Title, &folder, NULL);
	CopyString(mcedit.capture, folder, sizeof mcedit.capture);

	SIT_GetValues(worldSelect.worlds, SIT_Title, &folder, NULL);
	CopyString(mcedit.worldsDir, folder, sizeof mcedit.worldsDir);

	if (save)
	{
		SetINIValueInt(PREFS_PATH, "Options/MouseSpeed",   lroundf(globals.mouseSpeed*100));
		SetINIValueInt(PREFS_PATH, "Options/Brightness",   globals.brightness);
		SetINIValueInt(PREFS_PATH, "Options/TargetFPS",    globals.targetFPS);
		SetINIValueInt(PREFS_PATH, "Options/UsePreview",   globals.showPreview);

		SetINIValueInt(PREFS_PATH, "Options/AutoEdit",     mcedit.autoEdit);
		SetINIValueInt(PREFS_PATH, "Options/FullScreen",   mcedit.fullScreen);
		SetINIValueInt(PREFS_PATH, "Options/LockMouse",    mcedit.lockMouse);

		int i;
		for (i = KBD_MAX-1; i >= 0; i --)
		{
			KeyBinding kbd = keyBindings + i;
			TEXT keyName[32];
			TEXT config[32];

			SITK_ToText(keyName, sizeof keyName, kbd->key);
			sprintf(config, "%s/%s", kbd->config[0] == 'C' ? "MenuCommands" : "KeyBindings", kbd->config);
			SetINIValue(PREFS_PATH, config, keyName);
		}
	}
	/* will save the rest of the config */
	optionsExit(NULL, NULL, save);
	if (oldScale != worldSelect.guiScale)
		SIT_SetValues(globals.app, SIT_FontScale, worldSelect.guiScale, NULL);

	return 1;
}

static void worldSelectBindings(SIT_Widget parent, KeyBinding bindings, int count, int tab)
{
	SIT_Widget prev1 = NULL;
	SIT_Widget prev2 = NULL;
	int i;

	for (i = 0, count >>= 1; i < count; i ++, bindings ++)
	{
		TEXT msg[80];
		/* left column */
		SITK_ToText(msg, sizeof msg, bindings->key);
		SIT_Widget button = SIT_CreateWidget("kbd.key", SIT_BUTTON, parent,
			SIT_Top,      prev1 ? SITV_AttachWidget : SITV_AttachForm, prev1, SITV_Em(0.5),
			SIT_Title,    msg,
			SIT_Right,    SITV_AttachPosition, SITV_AttachPos(45), SITV_Em(-0.5),
			SIT_MaxWidth, prev1,
			SIT_TabNum,   tab,
			SIT_UserData, bindings,
			NULL
		);
		SIT_AddCallback(button, SITE_OnActivate, worldSelectEnterKey, NULL);
		snprintf(msg, sizeof msg, "%s:", bindings->name);

		SIT_CreateWidget("label", SIT_LABEL, parent,
			SIT_Title,          msg,
			SIT_Top,            SITV_AttachMiddle, button, 0,
			SIT_LeftAttachment, SITV_AttachForm,
			SIT_Right,          SITV_AttachWidget, button, SITV_Em(0.5),
			SIT_TabNum,         tab,
			NULL
		);
		prev1 = button;

		/* right column */
		SITK_ToText(msg, sizeof msg, bindings[count].key);
		button = SIT_CreateWidget("kbd.key", SIT_BUTTON, parent,
			SIT_Top,             prev2 ? SITV_AttachWidget : SITV_AttachForm, prev2, SITV_Em(0.5),
			SIT_Title,           msg,
			SIT_RightAttachment, SITV_AttachForm,
			SIT_TabNum,          tab,
			SIT_MaxWidth,        prev2,
			SIT_UserData,        bindings + count,
			NULL
		);
		SIT_AddCallback(button, SITE_OnActivate, worldSelectEnterKey, NULL);
		snprintf(msg, sizeof msg, "%s:", bindings[count].name);

		SIT_CreateWidget("label", SIT_LABEL, parent,
			SIT_Title,  msg,
			SIT_Left,   SITV_AttachPosition, SITV_AttachPos(50), 0,
			SIT_Top,    SITV_AttachMiddle, button, 0,
			SIT_Right,  SITV_AttachWidget, button, SITV_Em(0.5),
			SIT_TabNum, tab,
			NULL
		);
		prev2 = button;
	}

	if (tab == 2 || tab == 3)
	{
		static TEXT note[] =
			"Note:<br>"
			"&#x25cf; Fly mode is activated by pushing the jump button twice.<br>"
			"&#x25cf; 'Move view' is only used if 'Mouse lock' option is disabled.<br>"
			"&#x25cf; To disable a command, click on a button and push 'Esc' key.";

		static TEXT note2[] =
			"&#x25cf; Player mode will toggle between survival, creative and spectator.";

		SIT_CreateWidgets(parent,
			"<label tabNum=", tab, "name=note title=", tab == 2 ? note : note2, "top=", SITV_AttachWidget, prev1, SITV_Em(0.5), ">"
		);
	}
}

static void worldSelectAssignBinding(SIT_Widget button, int key)
{
	KeyBinding kbd;
	TEXT keyName[80];
	SITK_ToText(keyName, sizeof keyName, key);
	SIT_SetValues(button, SIT_Title, keyName, NULL);
	SIT_GetValues(button, SIT_UserData, &kbd, NULL);
	kbd->key = key;
}

/* config options dialog */
static int worldSelectConfig(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_Widget dialog = SIT_CreateWidget("about.mc dark", SIT_DIALOG, ud,
		SIT_AccelTable,   dialogAccels,
		SIT_DialogStyles, SITV_Movable | SITV_Plain | SITV_Modal,
		NULL
	);

	/* don't modify real values yet */
	worldSelect.compassSize = lroundf(globals.compassSize * 100);
	worldSelect.sensitivity = lroundf(globals.mouseSpeed * 100);
	worldSelect.guiScale    = globals.guiScale;
	worldSelect.renderDist  = globals.renderDist;
	worldSelect.fov         = globals.fieldOfVision;
	worldSelect.fps         = globals.targetFPS;
	worldSelect.fog         = globals.distanceFOG;
	worldSelect.brightness  = globals.brightness;
	worldSelect.showPreview = globals.showPreview;
	worldSelect.fullScreen  = mcedit.fullScreen;
	worldSelect.autoEdit    = mcedit.autoEdit;
	worldSelect.lockMouse   = mcedit.lockMouse;

	memcpy(editBindings, keyBindings, sizeof editBindings);

	SIT_Widget max = NULL;
	SIT_Widget max2 = NULL;
	SIT_CreateWidgets(dialog,
		"<tab name=tabs left=FORM tabActive=", worldSelect.curTab, "right=FORM tabStr='Configuration\tKey bindings\tMenu commands\tGraphics'"
		" tabSpace=", SITV_Em(1), "tabStyle=", SITV_AlignHCenter, ">"
			/*
			 * general configuration tab
			 */
			"<editbox tabNum=1 name=folder width=25em title=", mcedit.worldsDir, "buddyLabel=", "World folder:", &max, "top=FORM,,1em>"
			"<button tabNum=1 name=selfolder.act title='...' left=WIDGET,folder,0.5em top=OPPOSITE,folder bottom=OPPOSITE,folder>"
			"<editbox tabNum=1 name=userdata width=25em title=", "C:\\Users\\tpierron\\Documents\\MCEdit",
			" buddyLabel=", "User data:", &max, "top=WIDGET,folder,0.5em>"
			"<button tabNum=1 name=seluser.act title='...' left=WIDGET,userdata,0.5em top=OPPOSITE,userdata bottom=OPPOSITE,userdata>"
			"<editbox tabNum=1 name=capture width=25em title=", mcedit.capture, "buddyLabel=", "Screenshot folder:", &max, "top=WIDGET,userdata,0.5em>"
			"<button tabNum=1 name=capdir.act title='...' left=WIDGET,capture,0.5em top=OPPOSITE,capture bottom=OPPOSITE,capture>"
			/* language */
			"<combobox tabNum=1 name=lang width=15em initialValues='English (US)\tFran\xC3\xA7""ais (Canadian)'"
			" top=WIDGET,capture,0.5em buddyLabel=", "Language:", &max, ">"
			"<label tabNum=1 name=warn2#dim left=WIDGET,lang,0.5em top=MIDDLE,lang title=", "(need restart)", ">"

			/* mouse sensitivity */
			"<slider tabNum=1 userdata=4 name=speed width=15em minValue=50 maxValue=400 curValue=", &worldSelect.sensitivity, "buddyLabel=",
			"Mouse sensitivity:", &max, "top=WIDGET,lang,0.5em>"
			"<label tabNum=1 name=speedval left=WIDGET,speed,0.5em top=MIDDLE,speed>"
			/* gui scale adjustment */
			"<slider tabNum=1 userdata=5 name=scale pageSize=1 width=15em minValue=50 maxValue=200 curValue=", &worldSelect.guiScale, "buddyLabel=",
				"Interface scale:", &max, "top=WIDGET,speed,0.5em>"
			"<label tabNum=1 name=scaleval left=WIDGET,scale,0.5em top=MIDDLE,scale>"
			/* preview block */
			"<button tabNum=1 name=preview checkState=1 buttonType=", SITV_CheckBox, "curValue=", &worldSelect.showPreview,
			" title='Show a preview of the block that will be placed' left=OPPOSITE,scale top=WIDGET,scale,0.5em>"
			/* full screen */
			"<button tabNum=1 name=full buttonType=", SITV_CheckBox, "title='Set the window in full screen on startup.'"
			" curValue=", &worldSelect.fullScreen, "left=OPPOSITE,scale top=WIDGET,preview,0.5em>"
			/* auto-load */
			"<button tabNum=1 name=autoload buttonType=", SITV_CheckBox, "title='Automatically load last selected world on startup.'"
			" curValue=", &worldSelect.autoEdit, "left=OPPOSITE,scale top=WIDGET,full,0.5em>"
			/* lock mouse */
			"<button tabNum=1 name=lock buttonType=", SITV_CheckBox, "curValue=", &worldSelect.lockMouse,
			" title='Lock mouse when the window has the focus' left=OPPOSITE,scale top=WIDGET,autoload,0.5em>"

			/*
			 * graphics tab
			 */

			/* render distance */
			"<slider tabNum=4 name=dist width=15em minValue=1 pageSize=1 maxValue=16 curValue=", &worldSelect.renderDist, "buddyLabel=",
			"Render distance:", &max2, "top=FORM,,1em>"
			"<label tabNum=4 name=distval left=WIDGET,dist,0.5em top=MIDDLE,dist>"
			/* field of view */
			"<slider tabNum=4 userdata=1 name=fov width=15em pageSize=1 minValue=40 maxValue=140 curValue=", &worldSelect.fov, "buddyLabel=",
			"Field of view:", &max2, "top=WIDGET,dist,0.5em>"
			"<label tabNum=4 name=fovval left=WIDGET,fov,0.5em top=MIDDLE,fov>"

			/* frame per second */
			"<slider tabNum=4 userdata=2 name=fps width=15em pageSize=1 minValue=20 maxValue=150 curValue=", &worldSelect.fps, "buddyLabel=",
			"Frame per second:", &max2, "top=WIDGET,fov,0.5em>"
			"<label tabNum=4 name=fpsval left=WIDGET,fps,0.5em top=MIDDLE,fps>"

			/* brightness */
			"<slider tabNum=4 userdata=3 name=bright width=15em maxValue=101 pageSize=1 curValue=", &worldSelect.brightness, "buddyLabel=",
			"Dark area brightness:", &max2, "top=WIDGET,fps,0.5em>"
			"<label tabNum=4 name=brightval left=WIDGET,bright,0.5em top=MIDDLE,bright>"

			/* compass size */
			"<slider tabNum=4 userdata=6 name=compass minValue=49 maxValue=150 pageSize=1 width=15em curValue=", &worldSelect.compassSize,
			" buddyLabel=", "Compass size:", &max2, "top=WIDGET,bright,0.5em>"
			"<label tabNum=4 name=compassval left=WIDGET,compass,0.5em top=MIDDLE,compass>"

			/* fog */
			"<button tabNum=4 name=fog buttonType=", SITV_CheckBox, "top=WIDGET,compass,0.5em title=", "Enable distance fog.",
			" curValue=", &worldSelect.fog, ">"
			"<label name=note#dim tabNum=4 left=FORM right=FORM title=",
				"Fog will blend terrain with the sky, but you will lose some viewing distance.<br>"
				"Disabling fog will make the terrain look out of place though.",
			"top=WIDGET,fog,0.2em left=FORM,,1.2em>"

		"</tab>"
		"<button name=ko.act title=Cancel right=FORM top=WIDGET,tabs,1em buttonType=", SITV_CancelButton, ">"
		"<button name=use.act title=Use right=WIDGET,ko,0.5em top=OPPOSITE,ko>"
		"<button name=ok.act title=Save right=WIDGET,use,0.5em top=OPPOSITE,ko buttonType=", SITV_DefaultButton, ">"
		"<label name=msg.big title='Enter your key combination or <a href=#>cancel</a>.' visible=0 top=MIDDLE,ko>"
	);

	worldSelect.enterKey = SIT_GetById(dialog, "msg");
	worldSelect.capture  = SIT_GetById(dialog, "capture");
	worldSelect.worlds   = SIT_GetById(dialog, "folder");
	worldSelect.options  = dialog;

	SIT_AddCallback(worldSelect.enterKey, SITE_OnActivate, worldSelectCancelKbd, NULL);
	SIT_AddCallback(SIT_GetById(dialog, "selfolder"), SITE_OnActivate, worldSelectSelectFolder, SIT_GetById(dialog, "folder"));
	SIT_AddCallback(SIT_GetById(dialog, "ok"),  SITE_OnActivate, worldSelectSave, (APTR) 1);
	SIT_AddCallback(SIT_GetById(dialog, "use"), SITE_OnActivate, worldSelectSave, NULL);

	worldSelectSetCb(dialog, "dist");
	worldSelectSetCb(dialog, "fov");
	worldSelectSetCb(dialog, "fps");
	worldSelectSetCb(dialog, "bright");
	worldSelectSetCb(dialog, "speed");
	worldSelectSetCb(dialog, "scale");
	worldSelectSetCb(dialog, "compass");

	SIT_Widget parent = SIT_GetById(dialog, "tabs");

	worldSelectBindings(parent, editBindings,      14, 2);
	worldSelectBindings(parent, editBindings + 14, 18, 3);

	SIT_AddCallback(parent, SITE_OnChange, worldSelectTabChanged, NULL);

	SIT_ManageWidget(dialog);

	return 1;
}

void mceditWorldSelect(void)
{
	static char nothingFound[] =
		"No worlds found in \"<a href=\"#\">%s</a>\".<br><br>"
		"Click on \"SETTINGS\" to select a different folder.<br><br>"
		"Or drag'n drop a world save onto this window.";

	static SIT_Accel accels[] = {
		{SITK_FlagCapture + SITK_FlagAlt + SITK_F4, SITE_OnActivate, NULL, worldSelectExit},
		{SITK_FlagCapture + SITK_Escape,            SITE_OnActivate, NULL, worldSelectExit},
		{SITK_FlagCapture + SITK_F2,                SITE_OnActivate, NULL, takeScreenshot},

		{SITK_FlagCtrl + 'A', SITE_OnActivate, "about"},
		{SITK_FlagCtrl + 'O', SITE_OnActivate, "open"},
		{SITK_FlagCtrl + 'S', SITE_OnActivate, "opt"},
		{0}
	};
	SIT_Accel * oldAccels = NULL;

	SIT_Widget app = globals.app;

	SIT_GetValues(app, SIT_AccelTable, &oldAccels, NULL);
	SIT_SetValues(app,
		SIT_RefreshMode, SITV_RefreshAsNeeded,
		SIT_AccelTable,  accels,
		NULL
	);

	SIT_CreateWidgets(app,
		"<canvas name=header left=FORM right=FORM>"
		"  <button name=opt title='Settings...'>"
		"  <button name=open title='Open...' left=WIDGET,opt,1em>"
		"  <label name=appname title='MCEdit "MCEDIT_VERSION"' right=FORM>"
		"  <button name=about title='About...' right=WIDGET,appname,1em>"
		"  <label name=select title='Select world below to edit:' left=WIDGET,open,1em right=WIDGET,about,1em"
		"   style='text-align: center; text-decoration: underline'>"
		"</canvas>"
		"<canvas name=footer left=FORM right=FORM bottom=FORM>"
		"  <button name=edit enabled=0 title='Edit selected' left=", SITV_AttachCenter, ">"
		"</canvas>"
		"<listbox name=worlds viewMode=", SITV_ListViewIcon, "left=FORM right=FORM top=WIDGET,header"
		" bottom=WIDGET,footer nextCtrl=footer>"
	);
	SIT_SetAttributes(app, "<appname top=MIDDLE,about><select top=MIDDLE,open>");

	SIT_AddCallback(SIT_GetById(app, "about"), SITE_OnActivate, worldSelectAbout, app);
	SIT_AddCallback(SIT_GetById(app, "opt"),   SITE_OnActivate, worldSelectConfig, app);
//	SIT_AddCallback(SIT_GetById(app, "open"),  SITE_OnActivate, worldSelectFile, NULL);

	SIT_Widget list = SIT_GetById(app, "worlds");
	SIT_SetValues(list, SIT_Title|XfMt, nothingFound, mcedit.worldsDir, NULL);
	SIT_AddCallback(list, SITE_OnChange, worldSelectEnableEdit, SIT_GetById(app, "edit"));

	while (! mcedit.exit)
	{
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			int key;
			switch (event.type) {
			case SDL_KEYDOWN:
				if (worldSelect.curKey)
				{
					worldSelect.curKeySym = SDLKtoSIT(event.key.keysym.sym);
					worldSelect.curKeyMod = SDLMtoSIT(event.key.keysym.mod);
					break;
				}
			case SDL_KEYUP:
				if (worldSelect.curKey)
				{
					if (event.key.keysym.sym == SDLK_ESCAPE)
					{
						worldSelect.curKeySym = 0;
						worldSelect.curKeyMod = 0;
					}
					worldSelectAssignBinding(worldSelect.curKey, worldSelect.curKeySym | worldSelect.curKeyMod);
					worldSelectCancelKbd(NULL, NULL, NULL);
				}
				key = SDLKtoSIT(event.key.keysym.sym);
				if (key > 0 && SIT_ProcessKey(key, SDLMtoSIT(event.key.keysym.mod), event.type == SDL_KEYDOWN))
					break;

				if (event.key.keysym.unicode > 0)
					SIT_ProcessChar(event.key.keysym.unicode, SDLMtoSIT(event.key.keysym.mod));
				break;
			case SDL_MOUSEBUTTONDOWN:
				if (! SIT_ProcessClick(event.button.x, event.button.y, event.button.button-1, 1) && worldSelect.curKey)
				{
					TEXT keyName[80];
					switch (event.button.button) {
					case SDL_BUTTON_LEFT:      key = SITK_LMB; break;
					case SDL_BUTTON_MIDDLE:    key = SITK_MMB; break;
					case SDL_BUTTON_RIGHT:     key = SITK_RMB; break;
					case SDL_BUTTON_WHEELDOWN: key = SITK_MWD; break;
					case SDL_BUTTON_WHEELUP:   key = SITK_MWU; break;
					default:                   key = SITK_NTH + event.button.button;
					}
					SITK_ToText(keyName, sizeof keyName, key);
					SIT_SetValues(worldSelect.curKey, SIT_Title, keyName, NULL);
					worldSelectCancelKbd(NULL, NULL, NULL);
				}
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
				SIT_ProcessResize(globals.width, globals.height);
				break;
			case SDL_QUIT:
				mcedit.exit = 1;
			default:
				continue;
			}
		}

		/* update and render */
		glViewport(0, 0, globals.width, globals.height);
		if (SIT_RenderNodes(FrameGetTime()))
			SDL_GL_SwapBuffers();
		FrameWaitNext();
	}

	/* restore old values */
	SIT_SetValues(app,
		SIT_RefreshMode, SITV_RefreshAlways,
		SIT_AccelTable,  oldAccels,
		NULL
	);
}

