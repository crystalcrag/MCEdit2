/*
 * worldSelect.c : interface for listing worlds and all dialog related to this screen.
 *
 * written by T.Pierron, dec 2021.
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "SIT.h"
#include "render.h"
#include "globals.h"
#include "worldSelect.h"
#include "mcEdit.h"

static struct /* private */
{
	SIT_Widget options;

}	worldSelect;

int optionsExit(SIT_Widget w, APTR cd, APTR save)
{
	if (save)
	{
		SetINIValueInt(PREFS_PATH, "Options/CompassSize",   globals.compassSize * 100);
		SetINIValueInt(PREFS_PATH, "Options/GUIScale",      globals.guiScale);
		SetINIValueInt(PREFS_PATH, "Options/FieldOfVision", globals.fieldOfVision);
		SetINIValueInt(PREFS_PATH, "Options/RedstoneTick",  globals.redstoneTick);
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

static int optionsSetCompSize(SIT_Widget w, APTR cd, APTR ud)
{
	globals.compassSize = (int) cd * 0.01f;
	return 1;
}

static int optionsSetFieldOfView(SIT_Widget w, APTR cd, APTR ud)
{
	renderSetFOV((int) cd);
	return 1;
}

static int optionsSetRenderDist(SIT_Widget w, APTR cd, APTR ud)
{
	mapSetRenderDist(globals.level, globals.renderDist);
	renderResetFrustum();
	return 1;
}

static int optionsSetScale(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_SetValues(globals.app, SIT_FontScale, (int) cd, NULL);
	return 1;
}

static int optionsSetDefault(SIT_Widget w, APTR cd, APTR ud)
{
	globals.compassSize = 1;
	globals.guiScale = 100;
	globals.redstoneTick = 100;
	globals.fieldOfVision = 80;
	SIT_SetValues(SIT_GetById(ud, "compass"),  SIT_SliderPos, 100, NULL);
	SIT_SetValues(SIT_GetById(ud, "guiscale"), SIT_SliderPos, globals.guiScale, NULL);
	SIT_SetValues(SIT_GetById(ud, "fovval"),   SIT_SliderPos, globals.fieldOfVision, NULL);
	SIT_SetValues(SIT_GetById(ud, "tick"), SIT_Title, NULL, NULL);
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

	SIT_Widget max = NULL;
	SIT_CreateWidgets(diag,
		"<label name=dlgtitle#title title='Quick options:' left=FORM right=FORM>"
		/* compass size */
		"<editbox name=compSize width=5em editType=", SITV_Integer, "right=FORM top=WIDGET,dlgtitle,0.5em>"
		"<slider name=compass minValue=50 sliderPos=", (int) lroundf(globals.compassSize*100), "maxValue=150 width=15em top=MIDDLE,compSize left=FORM"
		" right=WIDGET,compSize,0.5em buddyEdit=compSize buddyLabel=", "Compass (%):", &max, ">"
		/* FOV */
		"<editbox name=fov width=5em editType=", SITV_Integer, "right=FORM top=WIDGET,compSize,0.5em>"
		"<slider name=fovval minValue=20 curValue=", &globals.fieldOfVision, "maxValue=140 top=MIDDLE,fov right=WIDGET,fov,0.5em"
		" buddyEdit=fov buddyLabel=", "Field of vision:", &max, ">"
		/* GUI scale */
		"<editbox name=gui width=5em editType=", SITV_Integer, "right=FORM top=WIDGET,fov,0.5em>"
		"<slider name=guiscale minValue=50 curValue=", &globals.guiScale, "maxValue=200 top=MIDDLE,gui right=WIDGET,gui,0.5em"
		" buddyEdit=gui buddyLabel=", "GUI scale:", &max, ">"
		/* render distance */
		"<editbox name=dist width=6em editType=", SITV_Integer, "top=WIDGET,gui,0.5em minValue=1 maxValue=16 curValue=", &globals.renderDist,
		" buddyLabel=", "Render dist:", &max, ">"
		"<label name=msg title=chunks left=WIDGET,dist,0.5em top=MIDDLE,dist>"
		/* redstone tick */
		"<editbox name=tick width=6em minValue=1 stepValue=100 curValue=", &globals.redstoneTick, "top=WIDGET,dist,0.5em editType=", SITV_Integer,
		" buddyLabel=", "Redstone tick:", &max, ">"
		"<label name=msg left=WIDGET,tick,0.5em top=MIDDLE,tick title='ms (def: 100)'>"

		"<button name=ko.act title=Use top=WIDGET,tick,0.5em right=FORM>"
		"<button name=ok.act title=Save top=OPPOSITE,ko right=WIDGET,ko,0.5em nextCtrl=ko buttonType=", SITV_DefaultButton, ">"
		"<button name=def.act title=Default top=OPPOSITE,ko right=WIDGET,ok,0.5em nextCtrl=ok>"
	);
	SIT_AddCallback(SIT_GetById(diag, "compass"),  SITE_OnScroll, optionsSetCompSize, NULL);
	SIT_AddCallback(SIT_GetById(diag, "fovval"),   SITE_OnScroll, optionsSetFieldOfView, NULL);
	SIT_AddCallback(SIT_GetById(diag, "guiscale"), SITE_OnScroll, optionsSetScale, NULL);
	SIT_AddCallback(SIT_GetById(diag, "dist"),     SITE_OnChange, optionsSetRenderDist, NULL);
	SIT_AddCallback(SIT_GetById(diag, "ok"),  SITE_OnActivate, optionsExit, (APTR) 1);
	SIT_AddCallback(SIT_GetById(diag, "ko"),  SITE_OnActivate, optionsExit, NULL);
	SIT_AddCallback(SIT_GetById(diag, "def"), SITE_OnActivate, optionsSetDefault, diag);
	SIT_AddCallback(diag, SITE_OnFinalize, optionsClearRef, NULL);

	SIT_ManageWidget(diag);
	return 1;
}
