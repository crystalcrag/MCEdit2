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
#include <time.h>
#include <malloc.h>
#include <math.h>
#include "SIT.h"
#include "render.h"
#include "globals.h"
#include "worldSelect.h"
#include "mcedit.h"
#include "keybindings.h"


static struct WorldSelect_t worldSelect;
static struct KeyBinding_t  editBindings[KBD_MAX_CONFIG];
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
			SIT_SetValues(worldSelect.enterKey, SIT_Title, LANG("N/A"), NULL);
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

/* "default" button activation callback */
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

/* SITE_OnFinalize */
static int optionsClearRef(SIT_Widget w, APTR cd, APTR ud)
{
	worldSelect.options = NULL;
	return 1;
}

/* interface for quick access to some common options (Ctrl+O by default) */
SIT_Widget optionsQuickAccess(void)
{
	SIT_Widget diag = worldSelect.options = SIT_CreateWidget("quickopt.mc", SIT_DIALOG, globals.app,
		SIT_DialogStyles, SITV_Plain | SITV_Movable,
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
		"<label name=dlgtitle#title title=", LANG("Quick options:"), "left=FORM right=FORM>"
		/* compass size */
		"<editbox name=compSize width=5em editType=", SITV_Integer, "right=FORM top=WIDGET,dlgtitle,0.5em>"
		"<slider name=compass minValue=49 curValue=", &worldSelect.compassSize, "maxValue=150 pageSize=1 width=15em"
		" top=MIDDLE,compSize left=FORM right=WIDGET,compSize,0.5em buddyEdit=compSize buddyLabel=", LANG("Compass (%):"), &max, ">"
		/* FOV */
		"<editbox name=fov width=5em editType=", SITV_Integer, "right=FORM top=WIDGET,compSize,0.5em>"
		"<slider name=fovval minValue=20 curValue=", &worldSelect.fov, "maxValue=140 pageSize=1 top=MIDDLE,fov right=WIDGET,fov,0.5em"
		" buddyEdit=fov buddyLabel=", LANG("Field of view:"), &max, ">"
		/* GUI scale */
		"<editbox name=gui width=5em editType=", SITV_Integer, "right=FORM top=WIDGET,fov,0.5em>"
		"<slider name=guiscale minValue=50 curValue=", &worldSelect.guiScale, "maxValue=200 pageSize=1 top=MIDDLE,gui"
		" right=WIDGET,gui,0.5em buddyEdit=gui buddyLabel=", LANG("GUI scale:"), &max, ">"
		/* brightness */
		"<label name=brightval right=FORM left=OPPOSITE,gui>"
		"<slider name=bright curValue=", &worldSelect.brightness, "maxValue=101 pageSize=1 top=WIDGET,guiscale,0.5em"
		" right=WIDGET,brightval,0.5em buddyLabel=", LANG("Brightness:"), &max, ">"
		/* render distance */
		"<editbox name=dist width=6em editType=", SITV_Integer, "top=WIDGET,bright,0.5em minValue=1 maxValue=16 curValue=", &worldSelect.renderDist,
		" buddyLabel=", LANG("Render distance:"), &max, ">"
		"<label name=msg title=", LANG("chunks"), "left=WIDGET,dist,0.5em top=MIDDLE,dist>"
		/* redstone tick */
		"<editbox name=tick width=6em minValue=1 stepValue=100 curValue=", &globals.redstoneTick, "top=WIDGET,dist,0.5em editType=", SITV_Integer,
		" buddyLabel=", LANG("Redstone tick:"), &max, ">"
		"<label name=msg left=WIDGET,tick,0.5em top=MIDDLE,tick title='ms (def: 100)'>"
		/* distance FOG */
		"<button name=fog buttonType=", SITV_CheckBox, "curValue=", &worldSelect.fog, "title=", LANG("Enable distance fog."),
		" top=WIDGET,tick,0.5em left=OPPOSITE,tick>"

		"<button name=ko.act title=", LANG("Use"), "top=WIDGET,fog,0.5em right=FORM>"
		"<button name=ok.act title=", LANG("Save"), "top=OPPOSITE,ko right=WIDGET,ko,0.5em nextCtrl=ko buttonType=", SITV_DefaultButton, ">"
		"<button name=def.act title=", LANG("Default"), "top=OPPOSITE,ko right=WIDGET,ok,0.5em nextCtrl=ok>"
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
	SIT_AddCallback(SIT_GetById(diag, "ok"),       SITE_OnActivate, optionsExit, (APTR) 1);
	SIT_AddCallback(SIT_GetById(diag, "ko"),       SITE_OnActivate, optionsExit, NULL);
	SIT_AddCallback(SIT_GetById(diag, "def"),      SITE_OnActivate, optionsSetDefault, diag);
	SIT_AddCallback(diag, SITE_OnFinalize, optionsClearRef, NULL);

	if (worldSelect.compassSize < 50)
		optionsSetValue(NULL, NULL, NULL);
	optionsSetValue(NULL, NULL, (APTR) 4);

	SIT_ManageWidget(diag);
	return diag;
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

/* Esc or Alt+F4 */
static int worldSelectExit(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_Exit(EXIT_APP);
	return 1;
}

static SIT_Accel dialogAccels[] = { /* override ESC shortcut from top-level interface */
	{SITK_FlagCapture + SITK_FlagAlt + SITK_F4, SITE_OnActivate, 0, NULL, worldSelectExit},
	{SITK_FlagCapture + SITK_Escape,            SITE_OnClose},
	{SITK_F2, SITE_OnActivate, KBD_TAKE_SCREENSHOT, NULL, takeScreenshot},
	{0}
};

/* display an about dialog */
static int worldSelectAbout(SIT_Widget w, APTR cd, APTR ud)
{
	keysReassign(dialogAccels);

	SIT_Widget about = SIT_CreateWidget("about.mc dark", SIT_DIALOG, ud,
		SIT_AccelTable,   dialogAccels,
		SIT_DialogStyles, SITV_Movable | SITV_Plain | SITV_Transcient,
		SIT_Style,        "font-size: 1.1em",
		NULL
	);

	static char headerFmt[] =
		DLANG("%s for %s<br>"
		      "Written by %s.<br>"
		      "Compiled on %s with %s");

	static STRPTR libraries[] = {
		"Mikko Memononen",  "<a href='https://github.com/memononen/nanovg/'>nanovg</a>",
		NULL,               "<a href='https://github.com/nothings/stb'>stb_truetype</a>,",
		NULL,               "<a href='https://github.com/nothings/stb'>stb_image</a>,",
		"Sean Barret",      "<a href='https://github.com/nothings/stb'>stb_include</a>",
		"T.Pierron",        "<a href='https://github.com/nothings/SITGL'>SITGL</a>",
		"Sam Lantinga",     "<a href='https://www.libsdl.org/'>SDL</a>",
		"Jean-loup Gailly, "
		"Mark Adler",       "<a href='https://www.zlib.net/'>zlib</a>"
	};

	static char license[] =
		DLANG("Under terms of BSD 2-clause license.<br>"
		      "No warranty, use at your own risk.");

	TEXT vendor[128];
	TEXT header[256];
	TEXT thanks[1024];
	int  i, len;

	STRPTR format = LANG("- %s by %s<br>");
	STRPTR altFmt = STRDUPA(format);
	STRPTR sep    = strstr(altFmt, "%s");
	if (sep) strcpy(sep + 2, "<br>");

	thanks[0] = 0;
	len = sprintf(thanks, "%s<br>", LANG("Make use of the following libraries:"));

	for (i = 0; i < DIM(libraries); i += 2)
	{
		STRPTR author = libraries[i];
		STRPTR source = libraries[i+1];

		if (author == NULL)
			len += snprintf(thanks + len, sizeof thanks - len, altFmt, source);
		else
			len += snprintf(thanks + len, sizeof thanks - len, format, source, author);
	}

	snprintf(header, sizeof header, LANG(headerFmt), "<a href='https://github.com/crystalcrag/MCEdit2'>MCEdit</a> "MCEDIT_VERSION, PLATFORM,
		"T.Pierron", __DATE__, COMPILER);

	snprintf(vendor, sizeof vendor, "%s<br>Open GL v%s", (STRPTR) glGetString(GL_RENDERER), (STRPTR) glGetString(GL_VERSION));

	SIT_CreateWidgets(about,
		"<label name=what.big style='text-align: center' title=", header, "left=FORM right=FORM>"
		"<label name=thanks title=", thanks, "top=WIDGET,what,1em>"
		"<label name=legal.big title=", LANG("License"), "top=WIDGET,thanks,1em left=", SITV_AttachCenter, ">"
		"<label name=license title=", LANG(license), "top=WIDGET,legal,0.5em>"
		"<label name=gpu.big title=", LANG("Graphics card in use:"), "top=WIDGET,license,1em left=", SITV_AttachCenter, ">"
		"<label name=version title=", vendor, "top=WIDGET,gpu,0.5em>"

		"<button name=close.act title=", LANG("Close"), "top=WIDGET,version,1em buttonType=", SITV_CancelButton, "left=", SITV_AttachCenter, ">"
	);

	SIT_ManageWidget(about);

	return 1;
}

/* key bindings button activation */
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
	case 0:
		format = num > 1 ? LANG("%d chunks") : LANG("%d chunk");
		/* hack: "chunks" is often hard to translate, use meters instead */
		if (format[0] == 'x')
			format ++, num *= 16;
		/* english has only 1 %d, but other languages can have up to 2 */
		snprintf(buffer, sizeof buffer, format, num, num * 16);
		SIT_SetValues(ud, SIT_Title, buffer, NULL);
		return 1;
	case 1: format = "%d&#xb0;"; break; /* FOV */
	case 2: format = num == 150 ? LANG("Uncapped FPS") : LANG("%d FPS"); break;
	case 3: format = num == 101 ? LANG("Full brightness") : "+%d%%"; break;
	case 4:
	case 5: format = "%d%%"; break;
	case 6: format = num == 49 ? LANG("Disabled") : "%d%%"; break;
	default: return 0;
	}
	snprintf(buffer, sizeof buffer, format, num);
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
	SIT_Widget dir = worldSelect.dirSelect;

	if (dir == NULL)
	{
		dir = worldSelect.dirSelect = SIT_CreateWidget("dirsel", SIT_DIRSELECT, w,
			SIT_Title, LANG("Select your destination path"),
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
	key &= ~(SITK_Flags | SITK_FlagModified);
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
		if (key >= RAWKEY(SITK_NTH))
		{
			/* nth mouse button */
			sprintf(keyName + len, "MB%d", key >> 16);
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

static void worldSelectList(SIT_Widget list, STRPTR dir, int max);

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
	globals.lockMouse     = worldSelect.lockMouse;
	globals.fullScrWidth  = worldSelect.fullScrW;
	globals.fullScrHeight = worldSelect.fullScrH;

	mcedit.autoEdit       = worldSelect.autoEdit;
	mcedit.fullScreen     = worldSelect.fullScreen;

	memcpy(keyBindings, editBindings, sizeof editBindings);

	STRPTR folder;
	SIT_GetValues(worldSelect.capture, SIT_Title, &folder, NULL);
	CopyString(mcedit.capture, folder, sizeof mcedit.capture);

	SIT_GetValues(worldSelect.worlds, SIT_Title, &folder, NULL);
	if (strcasecmp(mcedit.worldsDir, folder))
	{
		CopyString(mcedit.worldsDir, folder, sizeof mcedit.worldsDir);
		/* rescan the new directory for potential world save */
		worldSelectList(worldSelect.worldList, mcedit.worldsDir, sizeof mcedit.worldsDir);
	}

	if (save)
	{
		TEXT resol[32];
		sprintf(resol, "%dx%d", globals.fullScrWidth, globals.fullScrHeight);
		SetINIValue(PREFS_PATH, "Options/FullScrResol", resol);

		SetINIValueInt(PREFS_PATH, "Options/MouseSpeed", lroundf(globals.mouseSpeed*100));
		SetINIValueInt(PREFS_PATH, "Options/Brightness", globals.brightness);
		SetINIValueInt(PREFS_PATH, "Options/TargetFPS",  globals.targetFPS);
		SetINIValueInt(PREFS_PATH, "Options/UsePreview", globals.showPreview);
		SetINIValueInt(PREFS_PATH, "Options/LockMouse",  globals.lockMouse);

		SetINIValueInt(PREFS_PATH, "Options/AutoEdit",   mcedit.autoEdit);
		SetINIValueInt(PREFS_PATH, "Options/FullScreen", mcedit.fullScreen);

		int i;
		for (i = KBD_MAX_CONFIG-1; i >= 0; i --)
		{
			KeyBinding kbd = keyBindings + i;
			if (kbd->key & SITK_FlagModified)
			{
				TEXT keyName[32];
				TEXT config[32];

				/* will prevent useless updates */
				kbd->key &= ~SITK_FlagModified;
				SITK_ToText(keyName, sizeof keyName, kbd->key);
				switch (kbd->config[0]) {
				case 'C': sprintf(config, "MenuCommands/%s", kbd->config); break;
				case 'D': sprintf(config, "Extra/%s", kbd->config); break;
				default:  sprintf(config, "KeyBindings/%s", kbd->config);
				}
				SetINIValue(PREFS_PATH, config, keyName);
			}
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

	if (tab == 2 || tab == 3 || tab == 5)
	{
		static TEXT note[] =
			DLANG("Note:<br>"
			      "&#x25cf; Fly mode is activated by pushing the jump button twice.<br>"
			      "&#x25cf; 'Move view' is only used if 'Mouse lock' option is disabled.<br>"
			      "&#x25cf; To disable a command, click on a button and push 'Esc' key.");

		static TEXT note2[] =
			DLANG("&#x25cf; Player mode will toggle between survival, creative and spectator.");

		static TEXT note3[] =
			DLANG("This shortcuts are mostly useful for debugging.<br>You might want to disable them.");

		SIT_CreateWidgets(parent,
			"<label tabNum=", tab, "name=note title=", LANG(tab == 2 ? note : tab == 5 ? note3 : note2), "top=", SITV_AttachWidget, prev1, SITV_Em(0.5), ">"
		);
	}
}

/* key combination set by user: add it to the interface */
static void worldSelectAssignBinding(SIT_Widget button, int key)
{
	KeyBinding kbd;
	TEXT keyName[80];
	SIT_GetValues(button, SIT_UserData, &kbd, NULL);
	if (kbd->key & SITK_FlagUp)
	{
		/* since we'll need to track up and down event, can't have any qualifiers on these */
		key &= ~SITK_Flags;
		key |= SITK_FlagUp;
	}
	SITK_ToText(keyName, sizeof keyName, key);
	SIT_SetValues(button, SIT_Title, keyName, NULL);
	kbd->key = key | SITK_FlagModified;
}

/* selection changed on lang popup */
static int worldSelectLang(SIT_Widget w, APTR cd, APTR ud)
{
	if (cd)
	{
		STRPTR label;
		SIT_ComboGetRowTag(w, (int) cd, &label);
		CopyString(worldSelect.lang, strchr(label, 0) + 1, sizeof worldSelect.lang);
	}
	else worldSelect.lang[0] = 0;
	return 1;
}

/* list all language files found in resources/lang */
static void worldSelectFillLang(SIT_Widget combo)
{
	ScanDirData scan;
	if (! ScanDirInit(&scan, RESDIR "lang"))
		return;

	do
	{
		if (scan.isDir == 0)
		{
			TEXT buffer[128];
			snprintf(buffer, sizeof buffer, RESDIR "lang/%s", scan.name);
			FILE * in = fopen_enc(buffer, "rb");
			if (in)
			{
				int line = 0;
				STRPTR sep = strrchr(scan.name, '.');
				if (sep) *sep = 0;
				/* check for #name: directive */
				while (fgets(buffer, sizeof buffer, in) && line < 10)
				{
					if (strncmp(buffer, "#name:", 6) == 0)
					{
						STRPTR name = buffer + 6;
						while (isspace(*name)) name ++;
						StripCRLF(buffer);
						/* add file name at end */
						sep = strchr(buffer, 0) + 1;
						CopyString(sep, scan.name, sizeof buffer - (sep - buffer));
						sep = strchr(sep, 0);

						line = SIT_ComboInsertItem(combo, -1, name, sep - (name), NULL);
						if (strcasecmp(mcedit.lang, scan.name) == 0)
							SIT_SetValues(combo, SIT_SelectedIndex, line, NULL);
						break;
					}
					line --;
				}
				fclose(in);
			}
		}
	}
	while (ScanDirNext(&scan));

	SIT_AddCallback(combo, SITE_OnChange, worldSelectLang, NULL);
}

static int worldSelectChooseResol(SIT_Widget w, APTR cd, APTR ud)
{
	APTR resol = SIT_ComboGetRowTag(w, (int) cd, NULL);

	worldSelect.fullScrW = (int) resol & 0xffff;
	worldSelect.fullScrH = (int) resol >> 16;

	return 1;
}

/* fill all supported resolutions from current monitor */
static void worldSelectFillResol(SIT_Widget resol)
{
	DATA16 list;
	int    selIndex = 0;
	SIT_GetValues(globals.app, SIT_MonitorResol, &list, NULL);
	if (list[0] > 0)
	{
		int i;
		for (i = list[0], list ++; i > 0; i --, list += 2)
		{
			TEXT label[32];
			sprintf(label, "%d x %d", list[0], list[1]);
			if (list[0] == globals.fullScrWidth &&
			    list[1] == globals.fullScrHeight)
				SIT_GetValues(resol, SIT_ItemCount, &selIndex, NULL);
			SIT_ComboInsertItem(resol, -1, label, -1, (APTR) (list[0] | (list[1] << 16)));
		}
	}
	else SIT_ComboInsertItem(resol, -1, "No resolution found?", -1, NULL);
	SIT_SetValues(resol, SIT_SelectedIndex, selIndex, NULL);
	SIT_AddCallback(resol, SITE_OnChange, worldSelectChooseResol, NULL);
}

/* config options dialog */
static int worldSelectConfig(SIT_Widget w, APTR cd, APTR ud)
{
	keysReassign(dialogAccels);

	SIT_Widget dialog = SIT_CreateWidget("config.mc dark", SIT_DIALOG, ud,
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
	worldSelect.lockMouse   = globals.lockMouse;
	worldSelect.fullScreen  = mcedit.fullScreen;
	worldSelect.fullScrW    = globals.fullScrWidth;
	worldSelect.fullScrH    = globals.fullScrHeight;
	worldSelect.autoEdit    = mcedit.autoEdit;

	memcpy(editBindings, keyBindings, sizeof editBindings);

	SIT_Widget max = NULL;
	SIT_Widget max2 = NULL;
	SIT_CreateWidgets(dialog,
		"<tab name=tabs left=FORM tabActive=", worldSelect.curTab, "right=FORM tabStr=", LANG("Configuration\tKey bindings\tMenu commands\tGraphics\tExtra"),
		" tabSpace=", SITV_Em(1), "tabStyle=", SITV_AlignHCenter, ">"
			/*
			 * general configuration tab
			 */
			"<editbox tabNum=1 name=folder width=25em title=", mcedit.worldsDir, "buddyLabel=", LANG("World folder:"), &max,
			" editLength=", sizeof mcedit.worldsDir, "top=FORM,,1em>"
			"<button tabNum=1 name=selfolder.act title='...' left=WIDGET,folder,0.5em top=OPPOSITE,folder bottom=OPPOSITE,folder>"
			"<editbox tabNum=1 name=userdata width=25em title=", mcedit.userDir, "editLength=", sizeof mcedit.userDir,
			" buddyLabel=", LANG("User data:"), &max, "top=WIDGET,folder,0.5em>"
			"<button tabNum=1 name=seluser.act title='...' left=WIDGET,userdata,0.5em top=OPPOSITE,userdata bottom=OPPOSITE,userdata>"
			"<editbox tabNum=1 name=capture width=25em title=", mcedit.capture, "buddyLabel=", LANG("Screenshot folder:"), &max,
			" top=WIDGET,userdata,0.5em editLength=", sizeof mcedit.capture, ">"
			"<button tabNum=1 name=capdir.act title='...' left=WIDGET,capture,0.5em top=OPPOSITE,capture bottom=OPPOSITE,capture>"
			/* language */
			"<combobox tabNum=1 name=lang width=15em initialValues='English (US)'"
			" top=WIDGET,capture,0.5em buddyLabel=", LANG("Language:"), &max, ">"
			"<label tabNum=1 name=warn2#dim left=WIDGET,lang,0.5em top=MIDDLE,lang title=", LANG("(need restart)"), ">"
			/* fullscreen resolution */
			"<combobox tabNum=1 name=resol width=15em top=WIDGET,lang,0.5em buddyLabel=", LANG("Fullscreen resolution"), &max, ">"

			/* mouse sensitivity */
			"<slider tabNum=1 userdata=4 name=speed width=15em minValue=50 maxValue=400 curValue=", &worldSelect.sensitivity, "buddyLabel=",
				LANG("Mouse sensitivity:"), &max, "top=WIDGET,resol,0.5em>"
			"<label tabNum=1 name=speedval left=WIDGET,speed,0.5em top=MIDDLE,speed>"
			/* gui scale adjustment */
			"<slider tabNum=1 userdata=5 name=scale pageSize=1 width=15em minValue=50 maxValue=200 curValue=", &worldSelect.guiScale, "buddyLabel=",
				LANG("Interface scale:"), &max, "top=WIDGET,speed,0.5em>"
			"<label tabNum=1 name=scaleval left=WIDGET,scale,0.5em top=MIDDLE,scale>"
			/* preview block */
			"<button tabNum=1 name=preview checkState=1 buttonType=", SITV_CheckBox, "curValue=", &worldSelect.showPreview,
			" title=", LANG("Show a preview of the block that will be placed."), "left=OPPOSITE,scale top=WIDGET,scale,0.5em>"
			/* full screen */
			"<button tabNum=1 name=full buttonType=", SITV_CheckBox, "title=", LANG("Set the window in full screen on startup."),
			" curValue=", &worldSelect.fullScreen, "left=OPPOSITE,scale top=WIDGET,preview,0.5em>"
			/* auto-load */
			"<button tabNum=1 name=autoload buttonType=", SITV_CheckBox, "title=", LANG("Automatically load last selected world on startup."),
			" curValue=", &worldSelect.autoEdit, "left=OPPOSITE,scale top=WIDGET,full,0.5em>"
			/* lock mouse */
			"<button tabNum=1 name=lock buttonType=", SITV_CheckBox, "curValue=", &worldSelect.lockMouse,
			" title=", LANG("Lock mouse when the window has the focus."), "left=OPPOSITE,scale top=WIDGET,autoload,0.5em>"

			/*
			 * graphics tab
			 */

			/* render distance */
			"<slider tabNum=4 name=dist width=15em minValue=1 pageSize=1 maxValue=16 curValue=", &worldSelect.renderDist, "buddyLabel=",
				LANG("Render distance:"), &max2, "top=FORM,,1em>"
			"<label tabNum=4 name=distval left=WIDGET,dist,0.5em top=MIDDLE,dist>"
			/* field of view */
			"<slider tabNum=4 userdata=1 name=fov width=15em pageSize=1 minValue=40 maxValue=140 curValue=", &worldSelect.fov, "buddyLabel=",
				LANG("Field of view:"), &max2, "top=WIDGET,dist,0.5em>"
			"<label tabNum=4 name=fovval left=WIDGET,fov,0.5em top=MIDDLE,fov>"

			/* frame per second */
			"<slider tabNum=4 userdata=2 name=fps width=15em pageSize=1 minValue=20 maxValue=150 curValue=", &worldSelect.fps, "buddyLabel=",
				LANG("Frame per second:"), &max2, "top=WIDGET,fov,0.5em>"
			"<label tabNum=4 name=fpsval left=WIDGET,fps,0.5em top=MIDDLE,fps>"

			/* brightness */
			"<slider tabNum=4 userdata=3 name=bright width=15em maxValue=101 pageSize=1 curValue=", &worldSelect.brightness, "buddyLabel=",
				LANG("Dark area brightness:"), &max2, "top=WIDGET,fps,0.5em>"
			"<label tabNum=4 name=brightval left=WIDGET,bright,0.5em top=MIDDLE,bright>"

			/* compass size */
			"<slider tabNum=4 userdata=6 name=compass minValue=49 maxValue=150 pageSize=1 width=15em curValue=", &worldSelect.compassSize,
			" buddyLabel=", LANG("Compass size:"), &max2, "top=WIDGET,bright,0.5em>"
			"<label tabNum=4 name=compassval left=WIDGET,compass,0.5em top=MIDDLE,compass>"

			/* fog */
			"<button tabNum=4 name=fog buttonType=", SITV_CheckBox, "top=WIDGET,compass,0.5em title=", LANG("Enable distance fog."),
			" curValue=", &worldSelect.fog, ">"
			"<label name=note#dim tabNum=4 left=FORM right=FORM title=",
				LANG("Fog will blend terrain with the sky, but you will lose some viewing distance.<br>Disabling fog will make the terrain look out of place though."),
			"top=WIDGET,fog,0.2em left=FORM,,1.2em>"

		"</tab>"
		"<button name=ko.act title=", LANG("Cancel"), "right=FORM top=WIDGET,tabs,1em buttonType=", SITV_CancelButton, ">"
		"<button name=use.act title=", LANG("Use"), "right=WIDGET,ko,0.5em top=OPPOSITE,ko>"
		"<button name=ok.act title=", LANG("Save"), "right=WIDGET,use,0.5em top=OPPOSITE,ko buttonType=", SITV_DefaultButton, ">"
		"<label name=msg.big title=", LANG("Enter your key combination or <a href=#>cancel</a>."), "visible=0 top=MIDDLE,ko>"
	);

	worldSelect.enterKey = SIT_GetById(dialog, "msg");
	worldSelect.capture  = SIT_GetById(dialog, "capture");
	worldSelect.worlds   = SIT_GetById(dialog, "folder");
	worldSelect.options  = dialog;

	SIT_AddCallback(worldSelect.enterKey, SITE_OnActivate, worldSelectCancelKbd, NULL);
	SIT_AddCallback(SIT_GetById(dialog, "selfolder"), SITE_OnActivate, worldSelectSelectFolder, SIT_GetById(dialog, "folder"));
	SIT_AddCallback(SIT_GetById(dialog, "seluser"),   SITE_OnActivate, worldSelectSelectFolder, SIT_GetById(dialog, "userdata"));
	SIT_AddCallback(SIT_GetById(dialog, "capdir"),    SITE_OnActivate, worldSelectSelectFolder, SIT_GetById(dialog, "capture"));
	SIT_AddCallback(SIT_GetById(dialog, "ok"),  SITE_OnActivate, worldSelectSave, (APTR) 1);
	SIT_AddCallback(SIT_GetById(dialog, "use"), SITE_OnActivate, worldSelectSave, NULL);
	SIT_AddCallback(dialog, SITE_OnFinalize, optionsClearRef, NULL);

	worldSelectFillResol(SIT_GetById(dialog, "resol"));
	worldSelectFillLang(SIT_GetById(dialog, "lang"));
	worldSelectSetCb(dialog, "dist");
	worldSelectSetCb(dialog, "fov");
	worldSelectSetCb(dialog, "fps");
	worldSelectSetCb(dialog, "bright");
	worldSelectSetCb(dialog, "speed");
	worldSelectSetCb(dialog, "scale");
	worldSelectSetCb(dialog, "compass");

	SIT_Widget parent = SIT_GetById(dialog, "tabs");

	worldSelectBindings(parent, editBindings,      14, 2);
	worldSelectBindings(parent, editBindings + 14, 14, 3);
	worldSelectBindings(parent, editBindings + 28,  6, 5);

	SIT_AddCallback(parent, SITE_OnChange, worldSelectTabChanged, NULL);

	SIT_ManageWidget(dialog);

	return 1;
}

/* sort world by decreasing last play time */
static int worldSelectSort(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_OnSort * sort = cd;
	int64_t diff =
		((WorldInfo)sort->item2)->timestamp -
		((WorldInfo)sort->item1)->timestamp;

	if (diff < 0) return -1; else
	if (diff > 0) return  1; else return 0;
}

/* create a icon for the main listview of world selection */
static void worldSelectAddWorld(SIT_Widget list, STRPTR levelDat)
{
	NBTFile_t nbt = {.page = 1023};

	if (NBT_Parse(&nbt, levelDat))
	{
		SIT_Widget td, detail;
		WorldInfo  info;
		TEXT       folder[48];
		TEXT       worldName[48];
		TEXT       version[16];
		TEXT       lastPlayed[24];
		STRPTR     mode;
		time_t     timestamp = TimeStamp(levelDat, 2);

		strftime(lastPlayed, sizeof lastPlayed, "%b %d, %Y %H:%M:%S", localtime(&timestamp));
		ParentDir(levelDat);
		CopyString(folder, BaseName(levelDat), sizeof folder);
		strcpy(worldName, folder);
		NBT_GetString(&nbt, NBT_FindNode(&nbt, 0, "LevelName"), worldName, sizeof worldName);
		NBT_GetString(&nbt, NBT_FindNode(&nbt, 0, "Version.Name"), version, sizeof version);
		switch (NBT_GetInt(&nbt, NBT_FindNode(&nbt, 0, "playerGameType"), 0)) {
		case MODE_SURVIVAL:  mode = LANG("Survival"); break;
		case MODE_CREATIVE:  mode = LANG("Creative"); break;
		case MODE_SPECTATOR: mode = LANG("Spectator"); break;
		default:             mode = LANG("<unknown>");
		}
		NBT_Free(&nbt);
		if (version[0] == 0)
			strcpy(version, "< 1.8");

		int row = SIT_ListInsertItem(list, -1, NULL, SITV_TDSubChild);
		td = SIT_ListInsertControlIntoCell(list, row, 0);

		int len = strlen(levelDat);
		AddPart(levelDat, "icon.png", 1e6);
		SIT_CreateWidgets(td,
			"<label name=icon currentDir=1 imagePath=", FileExists(levelDat) ? levelDat : "resources/pack.png", ">"
			"<label name=wname title=", worldName, "left=WIDGET,icon,0.5em>"
			"<listbox extra=", sizeof *info + len, "name=list columnNames='Name\tValue' listBoxFlags=", SITV_NoHeaders,
			" left=WIDGET,icon,0.5em top=WIDGET,wname>"
		);
		SIT_SetAttributes(td, "<icon top=FORM bottom=OPPOSITE,list>");
		detail = SIT_GetById(td, "list");
		SIT_ListInsertItem(detail, -1, NULL, LANG("Folder:"), folder);
		SIT_ListInsertItem(detail, -1, NULL, LANG("Last played:"), lastPlayed);
		SIT_ListInsertItem(detail, -1, NULL, LANG("Mode:"), mode);
		SIT_ListInsertItem(detail, -1, NULL, LANG("Version:"), version);
		SIT_GetValues(detail, SIT_UserData, &info, NULL);
		info->timestamp = timestamp;
		CopyString(info->folder, levelDat, len+1);
		SIT_SetValues(list, SIT_RowTag(row), info, NULL);
		SIT_ListFinishInsertControl(list);
	}
}

/* scan all sub-folders for potential world saves */
static void worldSelectList(SIT_Widget list, STRPTR dir, int max)
{
	ScanDirData args;
	SIT_ListDeleteRow(list, DeleteAllRows);
	if (ScanDirInit(&args, dir))
	{
		int len = strlen(dir);
		do
		{
			if (args.isDir)
			{
				AddPart(dir, args.name, max);
				AddPart(dir, "level.dat", max);
				if (FileExists(dir))
					worldSelectAddWorld(list, dir);
				dir[len] = 0;
			}
		}
		while (ScanDirNext(&args));
	}
}

/* SITE_OnActivate on world list item */
static int worldSelectEdit(SIT_Widget w, APTR cd, APTR ud)
{
	WorldInfo info = cd;
	CopyString(mcedit.worldEdit, info->folder, sizeof mcedit.worldEdit);
	mcedit.state = GAMELOOP_WORLDEDIT;
	SIT_Exit(EXIT_LOOP);
	return 1;
}

/* "Edit selected" button */
static int worldSelectEditSelected(SIT_Widget w, APTR cd, APTR ud)
{
	int index;
	SIT_GetValues(ud, SIT_SelectedIndex, &index, NULL);
	if (index >= 0)
	{
		WorldInfo info;
		SIT_GetValues(ud, SIT_RowTag(index), &info, NULL);
		worldSelectEdit(w, info, NULL);
	}
	return 1;
}

/* "Open..." callback */
static int worldSelectFile(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_Widget file = worldSelect.fileSelect;

	if (file == NULL)
	{
		file = worldSelect.fileSelect = SIT_CreateWidget("fileselect", SIT_FILESELECT, globals.app,
			SIT_Filters,   "Level.dat\t*.dat\n"
			               "Any\t*",
			SIT_SelFilter, 0,
			SIT_DlgFlags,  SITV_FileMustExist,
			NULL
		);
	}

	if (SIT_ManageWidget(file))
	{
		STRPTR path;
		int    nb;

		SIT_GetValues(file, SIT_SelPath, &path, SIT_NbSelect, &nb, NULL);

		if (nb > 0)
		{
			CopyString(mcedit.worldEdit, path, sizeof mcedit.worldEdit);
			ParentDir(mcedit.worldEdit);
			mcedit.state = GAMELOOP_WORLDEDIT;
			SIT_Exit(EXIT_LOOP);
		}
	}
	return 1;
}

/* drag'n drop files onto the main window */
static int worldSelectDropFiles(SIT_Widget w, APTR cd, APTR ud)
{
	STRPTR * files = cd;

	if (files && files[0])
	{
		int    max  = strlen(files[0]) + 16;
		STRPTR path = alloca(max);
		strcpy(path, files[0]);
		/* can drop a file or a directory */
		AddPart(path, IsDir(path) ? "level.dat" : "../level.dat", max);
		if (! FileExists(path))
		{
			SIT_Log(SIT_INFO, LANG("The path %s does not appear to contain a valid world save."), files[0]);
			return 1;
		}
		ParentDir(path);
		CopyString(mcedit.worldEdit, path, sizeof mcedit.worldEdit);
		mcedit.state = GAMELOOP_WORLDEDIT;
		SIT_Exit(EXIT_LOOP);
	}
	return 1;
}

static void AbsolutePath(STRPTR dest, int max)
{
	STRPTR cwd;
	STRPTR rel = STRDUPA(dest);
	SIT_GetValues(globals.app, SIT_CurrentDir, &cwd, NULL);
	CopyString(dest, cwd, max);
	AddPart(dest, rel, max);
}

static int worldSelectFS(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_ToggleFullScreen(globals.fullScrWidth, globals.fullScrHeight);
	return 1;
}

/*
 * entry point for GAMELOOP_WORLDSELECT
 */
void mceditWorldSelect(void)
{
	static char nothingFound[] =
		DLANG("No worlds found in \"<a href=\"#\">%s</a>\".<br><br>"
		      "Click on \"SETTINGS\" to select a different folder.<br><br>"
		      "Or drag'n drop a world save onto this window.");

	static SIT_Accel accels[] = {
		{SITK_FlagCapture + SITK_FlagAlt + SITK_F4, SITE_OnActivate, 0, NULL, worldSelectExit},
		{SITK_FlagCapture + SITK_Escape,            SITE_OnActivate, 0, NULL, worldSelectExit},
		{SITK_FlagCapture + SITK_F11,               SITE_OnActivate, KBD_FULLSCREEN, NULL, worldSelectFS},
		{SITK_FlagCapture + SITK_F2,                SITE_OnActivate, KBD_TAKE_SCREENSHOT, NULL, takeScreenshot},

		{SITK_FlagCtrl + 'A', SITE_OnActivate, 0, "about"},
		{SITK_FlagCtrl + 'O', SITE_OnActivate, 0, "open"},
		{SITK_FlagCtrl + 'S', SITE_OnActivate, 0, "opt"},
		{0}
	};
	SIT_Accel * oldAccels = NULL;

	keysReassign(accels);
	if (IsRelativePath(mcedit.worldsDir))
		AbsolutePath(mcedit.worldsDir, sizeof mcedit.worldsDir);

	SIT_Widget app = globals.app;

	SIT_GetValues(app, SIT_AccelTable, &oldAccels, NULL);
	SIT_SetValues(app,
		SIT_RefreshMode, SITV_RefreshAsNeeded,
		SIT_AccelTable,  accels,
		NULL
	);

	SIT_CreateWidgets(app,
		"<canvas name=header left=FORM right=FORM>"
		"  <button name=opt title=", LANG("Settings..."), ">"
		"  <button name=open title=", LANG("Open..."), "left=WIDGET,opt,1em>"
		"  <label name=appname title='MCEdit "MCEDIT_VERSION"' right=FORM>"
		"  <button name=about title=", LANG("About..."), "right=WIDGET,appname,1em>"
		"  <button name=exit title=", LANG("Exit"), "right=WIDGET,about,1em nextCtrl=about>"
		"  <label name=select title=", LANG("Select world below to edit:"), "left=WIDGET,open,1em right=WIDGET,exit,1em"
		"   style='text-align: center; text-decoration: underline'>"
		"</canvas>"
		"<canvas name=footer left=FORM right=FORM bottom=FORM>"
		"  <button name=edit enabled=0 title=", LANG("Edit selected"), "left=", SITV_AttachCenter, ">"
		"</canvas>"
		"<listbox sortColumn=0 name=worlds viewMode=", SITV_ListViewIcon, "left=FORM right=FORM top=WIDGET,header"
		" bottom=WIDGET,footer nextCtrl=footer>"
	);
	SIT_SetAttributes(app, "<appname top=MIDDLE,about><select top=MIDDLE,open>");

	SIT_Widget list = worldSelect.worldList = SIT_GetById(app, "worlds");
	SIT_AddCallback(SIT_GetById(app, "about"), SITE_OnActivate, worldSelectAbout, app);
	SIT_AddCallback(SIT_GetById(app, "opt"),   SITE_OnActivate, worldSelectConfig, app);
	SIT_AddCallback(SIT_GetById(app, "edit"),  SITE_OnActivate, worldSelectEditSelected, list);
	SIT_AddCallback(SIT_GetById(app, "open"),  SITE_OnActivate, worldSelectFile, NULL);
	SIT_AddCallback(SIT_GetById(app, "exit"),  SITE_OnActivate, worldSelectExit, NULL);

	SIT_SetValues(list, SIT_Title|XfMt, nothingFound, mcedit.worldsDir, NULL);
	SIT_AddCallback(list, SITE_OnChange,   worldSelectEnableEdit, SIT_GetById(app, "edit"));
	SIT_AddCallback(list, SITE_OnSortItem, worldSelectSort, NULL);
	SIT_AddCallback(list, SITE_OnActivate, worldSelectEdit, NULL);

	//SIT_AddCallback(app, SITE_OnDropFiles, worldSelectDropFiles, NULL);

	/* scan folder for potential world saves */
	worldSelectList(list, mcedit.worldsDir, sizeof mcedit.worldsDir);

	SDL_EnableUNICODE(1);

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
					default:                   key = RAWKEY(SITK_NTH + event.button.button);
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
				mcedit.exit = EXIT_APP;
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
	SIT_DelCallback(app, SITE_OnDropFiles, worldSelectDropFiles, NULL);
	SIT_SetValues(app,
		SIT_RefreshMode, SITV_RefreshAlways,
		SIT_AccelTable,  oldAccels,
		NULL
	);
	SIT_Nuke(SITV_NukeCtrl);
	worldSelect.fileSelect = NULL;
	worldSelect.dirSelect  = NULL;
}

/*
 * store most of engine kbd shortcut in a simple hash-table: not the best implementation out there,
 * but still way better than linear scan.
 */
void keysHash(KeyHash hash, KeyBinding kbd)
{
	int i, slot, count = hash->count;
	memset(hash->hash, 0x00, count * 4);
	memset(hash->next, 0xff, count);
	for (i = 0; i < KBD_MAX; i ++, kbd ++)
	{
		int key = kbd->key, flag;
		if ('A' <= (key&0xff) && (key&0xff) <= 'Z')
			key += 32;

		flag = key & SITK_FlagUp ? 0x80 : 0;
		add_accel:
		slot = key % count;
		if (hash->hash[slot])
		{
			int next = slot;
			do {
				next ++;
				if (next == count) next = 0;
			}
			while (hash->hash[next]);
			hash->next[next] = hash->next[slot];
			hash->next[slot] = next;
			slot = next;
		}
		/* <key> uses only 24bits */
		hash->hash[slot] = key | ((i | flag) << 24);

		/* if key has the FlagUp set, we will trigger callback for up and down event, otherwise only down */
		if (key & SITK_FlagUp)
		{
			key &= ~SITK_FlagUp;
			goto add_accel;
		}
	}
}

int keysFind(KeyHash hash, int key)
{
	int slot, count = hash->count;
	int command = -1;
	/* can't use qualifier while tracking up/down keys: there can be multiple keys pressed at any time */
	if (hash->hasUp > 0)
		key &= ~(SITK_Flags & ~SITK_FlagUp);
	for (slot = key % count; slot < count && hash->hash[slot]; slot = hash->next[slot])
	{
		if ((hash->hash[slot] & 0xffffff) != key)
			continue;

		uint8_t cmd = hash->hash[slot] >> 24;
		if (cmd & 0x80)
		{
			if (key & SITK_FlagUp) hash->hasUp --;
			else hash->hasUp ++;
		}
		/* need to continue searching in case there are multiple commands for the same shortcut */
		if (command < 0)
			command = cmd & 0x7f;
		else
			command = (command << 8) | (cmd & 0x7f);
	}
	return command;
}

void keysReassign(SIT_Accel * list)
{
	while (list->key)
	{
		if (list->tag > 0)
			list->key = keyBindings[list->tag].key;
		list ++;
	}
}

/*
 * simple yes/no dialog
 */
static int mceditCloseDialog(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_CloseDialog(w);
	return 1;
}

/* ask a question to the user with Yes/No as possible answer */
void mceditYesNo(SIT_Widget parent, STRPTR msg, SIT_CallProc cb, Bool yesNo)
{
	SIT_Widget ask = SIT_CreateWidget("ask.mc", SIT_DIALOG, parent,
		SIT_DialogStyles, SITV_Plain | SITV_Modal | SITV_Movable,
		SIT_Style,        "padding: 1em",
		NULL
	);

	SIT_CreateWidgets(ask, "<label name=label title=", msg, ">");

	if (yesNo)
	{
		SIT_CreateWidgets(ask,
			"<button name=ok.danger title=", LANG("Yes"), "top=WIDGET,label,0.8em buttonType=", SITV_DefaultButton, ">"
			"<button name=ko title=", LANG("No"), "top=OPPOSITE,ok right=FORM buttonType=", SITV_CancelButton, ">"
		);
		SIT_SetAttributes(ask, "<ok right=WIDGET,ko,1em>");
	}
	else /* only a "no" button */
	{
		SIT_CreateWidgets(ask, "<button name=ok right=FORM title=", LANG("Close"), "top=WIDGET,label,0.8em buttonType=", SITV_DefaultButton, ">");
		cb = mceditCloseDialog;
	}
	SIT_AddCallback(SIT_GetById(ask, "ok"), SITE_OnActivate, cb, NULL);
	SIT_ManageWidget(ask);
}
