/*
 * MCEdit.h : datatype for handling main event loops.
 *
 * Written by T.Pierron, Feb 2020
 */

#ifndef MCEDIT_H
#define MCEDIT_H

#include "maps.h"
#include "utils.h"
#include "player.h"
#include "globals.h"

#define PREFS_PATH             "MCEdit.ini"
#define MCEDIT_VERSION         "2.0b1"

typedef struct GameState_t     GameState_t;

struct GameState_t
{
	Player_t player;           /* current view */
	int      state;            /* event loop we currently are (GAMELOOP_*) */
	int      mouseX, mouseY;   /* current mouse pos */
	int      exit;             /* managed by SITGL */
	uint8_t  autoEdit;         /* edit last selected world on startup */
	uint8_t  forceSel;         /* don't display preview item */
	uint8_t  fullScreen;       /* go fullscreen on startup */
	uint8_t  askIfSave;        /* 0: don't save, exit, 1: save, exit, 2: cancel */
	TEXT     capture[128];     /* screenshot directory */
	TEXT     userDir[128];     /* schematics library */
	TEXT     worldsDir[256];   /* folder where saved worlds are */
	TEXT     worldEdit[256];   /* world being edited (folder) */
	TEXT     lang[32];         /* name of language used for interface */
};

enum /* possible values for state: which game loop are we running */
{
	GAMELOOP_WORLDSELECT,
	GAMELOOP_WORLDEDIT,
	GAMELOOP_OVERLAY,
	GAMELOOP_SIDEVIEW
};

void mceditWorld(void);        /* gameloop for WORLD */
void mceditUIOverlay(int);     /* display an interface on top of editor */
void mceditSideView(void);     /* gameloop for SIDEVIEW */
void mceditWorldSelect(void);  /* world selection */
Bool mceditActivate(void);     /* toggle state of some blocks (door, button, lever, repeater, ...) */
void mceditPlaceBlock(void);

enum /* possible value for mceditUIOverlay() */
{
	MCUI_OVERLAY_BLOCK,
	MCUI_OVERLAY_GOTO,
	MCUI_OVERLAY_ANALYZE,
	MCUI_OVERLAY_REPLACE,
	MCUI_OVERLAY_FILL,
	MCUI_OVERLAY_DELALL,
	MCUI_OVERLAY_DELPARTIAL,
	MCUI_OVERLAY_LIBRARY,
	MCUI_OVERLAY_SAVESEL,
	MCUI_OVERLAY_PAINTING,
	MCUI_OVERLAY_PIXELART,
	MCUI_OVERLAY_WORLDINFO,
	MCUI_OVERLAY_FILTER,
	MCUI_OVERLAY_ASKIFSAVE,
	MCUI_SEL_CLONE
};

enum /* possible values for SIT_Exit() */
{
	EXIT_APP  = 1,
	EXIT_LOOP = 2
};

/* ID string */
#ifdef __GNUC__
 #define COMPILER     "gcc " TOSTRING(__GNUC__) "." TOSTRING(__GNUC_MINOR__) "." TOSTRING(__GNUC_PATCHLEVEL__)
 #ifdef WIN32
  #if __x86_64__
   #define PLATFORM   "MS-Windows-x64"
  #else
   #define PLATFORM   "MS-Windows-x86"
  #endif
 #elif LINUX
  #if __x86_64__
   #define PLATFORM   "GNU-Linux-x64"
  #else
   #define PLATFORM   "GNU-Linux-x86"
  #endif
 #else
  #if __x86_64__
   #define PLATFORM   "Unknown-x64"
  #else
   #define PLATFORM   "Unknown-x86"
  #endif
 #endif
#else
 #define COMPILER    "unknown compiler"
 #ifdef WIN32
  #define PLATFORM   "MS-Windows-x86"
 #else
  #define PLATFORM   "GNU-Linux-x86"
 #endif
#endif

#endif
