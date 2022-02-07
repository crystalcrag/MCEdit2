/*
 * globals.h: link to global variables that are pretty much needed everywhere in the code.
 *
 * Written by T.Pierron, sep 2021.
 */

#ifndef MCGLOBALS_H
#define MCGLOBALS_H

#include "utils.h"

/*
 * Some state variables are needed everywhere: using a function just to return a variable is kind
 * of useless, so keep them accessible in this struct, to avoid exposing too many internal details
 * from all the modules.
 */

typedef struct MCGlobals_t
{
	/*
	 * which selection points are active: bitfield of "1 << SEL_POINT_*". &1: first point, &2: second point,
	 * #8: clone brush. Used by selection.c
	 */
	uint8_t selPoints;

	/* cardinal direction player is facing: 0 = south, 1 = east, 2 = north, 3 = west */
	uint8_t direction;

	/* edit box is active: restrict some keyboard shortcut */
	uint8_t inEditBox;

	/* map being edited */
	Map level;

	/* screen/window width/height */
	int width, height;

	/* SITGL root widget */
	APTR app;

	/* time in milliseconds for current frame */
	double curTime;

	/* time spent in a modal UI: can't use curTime */
	double curTimeUI;

	/* 2 floats containing player angular looking direction (in radians) */
	float * yawPitch;

	/* model-view-projection matrix (4x4) */
	mat4 matMVP;

	/* inverse of matMVP (raypicking and frustum culling will need this) */
	mat4 matInvMVP;

	/* nanovg context */
	struct NVGcontext * nvgCtx;

	/* config options */
	float   compassSize;      /* % over base value */
	float   mouseSpeed;       /* % over base value */
	int     redstoneTick;     /* in millisec */
	int     targetFPS;        /* < 0 == uncapped */
	uint8_t fieldOfVision;    /* in degrees */
	uint8_t guiScale;         /* [50-200] % */
	uint8_t brightness;       /* [0-101] => map [0-100] to ambient values [0.2 - 0.4], 101 means full brightness */
	uint8_t renderDist;       /* in chunks */
	uint8_t distanceFOG;      /* 1 = use fog, 0 = don't */
	uint8_t showPreview;      /* 1 = show preview block, 0 = outline only */

	/* if world is being edited */
	int modifCount;

	/* Uniform Buffer Object used by all shaders */
	int uboShader;

	/* easier to place break points :-/ */
	int breakPoint;

}	MCGlobals_t;

extern struct MCGlobals_t globals;

#endif
