/*
 * keybindings.h: structure to store keybindings for most of the controls of this engine.
 *
 * written by T.Pierron, jan 2022.
 */

#ifndef MC_KEYBINDIGNS_H
#define MC_KEYBINDIGNS_H

enum  /* meaning of each index of keyBindings[] table */
{
	KBD_MOVE_FORWARD,
	KBD_MOVE_BACKWARD,
	KBD_STRAFE_LEFT,
	KBD_STRAFE_RIGHT,
	KBD_SWITCH_OFFHAND,
	KBD_OPEN_INVENTORY,
	KBD_TROW_ITEM,

	KBD_JUMP,
	KBD_FLYDOWN,
	KBD_SLICE_VIEW,
	KBD_PLACE_BLOCK,
	KBD_MOVE_VIEW,
	KBD_ACTIVATE_BLOCK,
	KBD_PICK_BLOCK,

	KBD_HIDE_HUD,
	KBD_DEBUG_INFO,
	KBD_ADVANCE_TIME,
	KBD_SAVE_LOCATION,
	KBD_WAYPOINT_EDITOR,
	KBD_SCHEMA_LIBRARY,
	KBD_UNDO_CHANGE,
	KBD_REDO_CHANGE,
	KBD_CLOSE_WORLD,

	KBD_TAKE_SCREENSHOT,
	KBD_BACK_IN_TIME,
	KBD_SWITCH_MODE,
	KBD_FULLSCREEN,
	KBD_CLEAR_SELECTION,
	KBD_COPY_SELECTION,
	KBD_PASTE_CLIPBOARD,
	KBD_WORLD_INFO,
	KBD_SAVE_CHANGES,

	KBD_MAX
};

enum /* not real keys, but will simplify stuff */
{
	SITK_LMB = RAWKEY(100),
	SITK_MMB = RAWKEY(101),
	SITK_RMB = RAWKEY(102),
	SITK_MWU = RAWKEY(103),
	SITK_MWD = RAWKEY(104),
	SITK_NTH = RAWKEY(105),
};

struct KeyBinding_t
{
	STRPTR name;
	STRPTR config;
	int    key;
};

typedef struct KeyBinding_t     KeyBindings_t[KBD_MAX];
typedef struct KeyBinding_t *   KeyBinding;

extern KeyBindings_t keyBindings;

#endif
