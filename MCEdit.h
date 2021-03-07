/*
 * MCEdit.h : datatype for main loop of rendering engine.
 *
 * Written by T.Pierron, Feb 2020
 */

#ifndef MCEDIT_H
#define MCEDIT_H

#include "maps.h"
#include "utils.h"
#include "player.h"

#define PREFS_PATH             "MCEdit.ini"

typedef struct GameState_t     GameState_t;

struct GameState_t
{
	Player_t player;           /* current view */
	APTR     app;              /* SIT_Widget */
	Map      level;            /* current level being edited */
	int      state;            /* event loop we currently are */
	int      width, height;    /* screen size */
	int      mouseX, mouseY;   /* current mouse pos */
	int      exit;             /* managed by SITGL */
	int      maxDist;          /* render distance in chunks */
	TEXT     capture[128];     /* screenshot directory */
};

enum /* possible values for state: which game loop are we running */
{
	GAMELOOP_WORLDSELECT,
	GAMELOOP_WORLD,
	GAMELOOP_SIDEVIEW
};

void mceditWorld(void);        /* gameloop for WORLD */
void mceditUIOverlay(void);    /* display an interface on top of editor */
void mceditSideView(void);     /* gameloop for SIDEVIEW */
void mceditWorldSelct(void);   /* world selection */
void mceditDoAction(int);

enum
{
	ACTION_PLACEBLOCK,
	ACTION_ACTIVATE
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
