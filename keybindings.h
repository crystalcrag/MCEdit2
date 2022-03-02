/*
 * keybindings.h: structure to store keybindings for most of the controls of this engine.
 *
 * written by T.Pierron, jan 2022.
 */

#ifndef MC_KEYBINDIGNS_H
#define MC_KEYBINDIGNS_H

typedef enum  /* meaning of each index of keyBindings[] table */
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
	KBD_PLACE_BLOCK,
	KBD_MOVE_VIEW,
	KBD_ACTIVATE_BLOCK,
	KBD_PICK_BLOCK,

	KBD_MOVE_SEL_UP,
	KBD_HIDE_HUD,
	KBD_WAYPOINT_EDITOR,
	KBD_SCHEMA_LIBRARY,
	KBD_UNDO_CHANGE,
	KBD_REDO_CHANGE,
	KBD_CLOSE_WORLD,
	KBD_QUICK_OPTIONS,

	KBD_MOVE_SEL_DOWN,
	KBD_TAKE_SCREENSHOT,
	KBD_FULLSCREEN,
	KBD_CLEAR_SELECTION,
	KBD_COPY_SELECTION,
	KBD_PASTE_CLIPBOARD,
	KBD_WORLD_INFO,
	KBD_SAVE_CHANGES,

	KBD_DEBUG_INFO,
	KBD_BACK_IN_TIME,
	KBD_ADVANCE_TIME,
	KBD_SWITCH_MODE,
	KBD_SAVE_LOCATION,
	KBD_FRAME_ADVANCE,
	KBD_SLICE_VIEW,

	KBD_SLOT_0,
	KBD_SLOT_1,
	KBD_SLOT_2,
	KBD_SLOT_3,
	KBD_SLOT_4,
	KBD_SLOT_5,
	KBD_SLOT_6,
	KBD_SLOT_7,
	KBD_SLOT_8,
	KBD_SLOT_9,

}	KbdCmd_t;

#define KBD_MAX               (KBD_SLOT_9+1)
#define KBD_MAX_CONFIG        KBD_SLOT_0

enum /* not real keys, but will simplify stuff */
{
	SITK_LMB = RAWKEY(100),
	SITK_MMB = RAWKEY(101),
	SITK_RMB = RAWKEY(102),
	SITK_MWU = RAWKEY(103),
	SITK_MWD = RAWKEY(104),
	SITK_NTH = 105,
};

struct KeyBinding_t
{
	STRPTR name;
	STRPTR config;
	int    key;
};

struct KeyHash_t
{
	DATA32   hash;
	DATA8    next;
	uint16_t count;
	uint16_t hasUp;
};

typedef struct KeyBinding_t     KeyBindings_t[KBD_MAX];
typedef struct KeyBinding_t *   KeyBinding;
typedef struct KeyHash_t *      KeyHash;

extern KeyBindings_t keyBindings;

void keysHash(KeyHash, KeyBinding);
int  keysFind(KeyHash hash, int key);
void keysReassign(SIT_Accel *);

#endif
