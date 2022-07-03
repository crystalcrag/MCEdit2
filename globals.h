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
typedef struct Map_t *     Map;

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
	int     targetFPS;        /* 0 == uncapped */
	float   fieldOfVision;    /* in degrees */
	uint8_t guiScale;         /* [50-200] % */
	uint8_t brightness;       /* [0-101] => map [0-100] to ambient values [0.2 - 0.4], 101 means full brightness */
	uint8_t renderDist;       /* in chunks */
	uint8_t distanceFOG;      /* 1 = use fog, 0 = don't */
	uint8_t showPreview;      /* 1 = show preview block, 0 = outline only */
	uint8_t lockMouse;        /* 1 = mouse lock within window, 0 = free mouse */
	uint8_t fullScreen;       /* 0 = window, 1 = full screen, 2 = auto full-screen */
	int     fullScrWidth;     /* full screen resolution */
	int     fullScrHeight;

	/* if world is being edited */
	int modifCount;

	/* Uniform Buffer Object used by all shaders */
	int uboShader;

	/* easier to place break points :-/ */
	int breakPoint;

}	MCGlobals_t;

extern struct MCGlobals_t globals;

/*
 * default textures binding point : GL_TEXTURExxx constant are assigned to a particular texture; must
 * match what's declared in uniformTexture.glsl.
 */

// GL_TEXTURE0 is actually the only one that can vary between shaders

#define TEX_DEFAULT      GL_TEXTURE0   // usually terrain tex (512 x 1024 x RGBA)
#define TEX_ENTITIES     GL_TEXTURE1   // entity models (512 x 1024 x RGBA)
#define TEX_TINTSKY1     GL_TEXTURE2   // color of the sky on the half-sphere where the sun is (time x height x RGB)
#define TEX_TINTSKY2     GL_TEXTURE3   // color of the sky on the opposite half-sphere (time x height x RGB)
#define TEX_SUN          GL_TEXTURE4   // sun color (radius x time of day x RGBA)
#define TEX_LIGHTSHADE   GL_TEXTURE5   // skylight + blocklight per face shading (16 x 108 x RGB)
#define TEX_SKY          GL_TEXTURE6   // texture for blending terrain with sky (256 x 256 x RGB)


/*
 * start of lighting banks (skylight + blocklight), can use as many as needed tex after this.
 * usually need about 3 or 4 on 16 chunks render distance (144 x 144 x 144 x RG: R = skylight, G = blockLight)
 */
#define TEX_LIGHTBANKS   GL_TEXTURE8

#endif
