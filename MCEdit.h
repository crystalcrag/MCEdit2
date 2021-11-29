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

typedef struct GameState_t     GameState_t;

struct GameState_t
{
	Player_t player;           /* current view */
	int      state;            /* event loop we currently are */
	int      mouseX, mouseY;   /* current mouse pos */
	int      exit;             /* managed by SITGL */
	int      maxDist;          /* render distance in chunks */
	uint8_t  forceSel;         /* don't display preview item */
	TEXT     capture[128];     /* screenshot directory */
};

enum /* possible values for state: which game loop are we running */
{
	GAMELOOP_WORLDSELECT,
	GAMELOOP_WORLD,
	GAMELOOP_OVERLAY,
	GAMELOOP_SIDEVIEW
};

void mceditWorld(void);        /* gameloop for WORLD */
void mceditUIOverlay(int);     /* display an interface on top of editor */
void mceditSideView(void);     /* gameloop for SIDEVIEW */
void mceditWorldSelect(void);  /* world selection */
Bool mceditActivate(void);     /* toggle state of some blocks (door, button, lever, repeater, ...) */
void mceditPlaceBlock(void);

enum /* possible value for parameter mceditUIOverlay() */
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
	MCUI_SEL_CLONE
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
