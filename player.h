/*
 * player.h: public function and datatypes for player management.
 *
 * written by T.Pierron, jan 2020.
 */

#ifndef PLAYER_H
#define PLAYER_H

#include "utils.h"
#include "items.h"
#include "NBT2.h"

typedef struct Player_t *      Player;
typedef struct Player_t        Player_t;
typedef struct PickupBlock_t * PickupBlock;
typedef struct PickupBlock_t   PickBuf;
typedef struct Inventory_t *   Inventory;
typedef struct Inventory_t     InvBuf;

struct PickupBlock_t
{
	vec4 location;
	vec4 rotation;
	mat4 model;
	int  blockId;
	int  state;
	int  time;
};

enum /* possible values for PickBuf.state */
{
	PICKUP_CHANGE,
	PICKUP_ACTION
};

#define MAXCOLINV             9
#define INFO_INV_DURATION     3
#define INFO_INV_FADEOUT      2
#define PLAYER_HEIGHT         1.6f

#define LEFT      's'
#define RIGHT     'f'
#define FORWARD   'e'
#define BACKWARD  'd'
#define OFFHAND   'g'
#define JUMP      SITK_Space
#define FLYDOWN   SITK_LShift

struct Inventory_t
{
	int      selected;     /* current selected slot */
	uint16_t offhand;      /* current offhand action: &1: first sel pt, &2: second sel pt */
	uint16_t hoverSlot;    /* slot hovered by mouse */
	int      texture;
	ItemBuf  items[MAXCOLINV * 4 + 5];
	int      x, y, update;
	int      infoState, infoX;
	int      infoTime;
	TEXT     infoTxt[128];
};

enum /* flags values */
{
	PLAYER_OFFHAND  = 1,  /* off-hand selected */
	PLAYER_ALTPOINT = 2,  /* use second pt of selection */
	PLAYER_TOOLBAR  = 4   /* mouse is over toolbar (see hoverSlot) */
};

enum /* infoState: display a text about item selected in toolbar */
{
	INFO_INV_NONE,
	INFO_INV_INIT,
	INFO_INV_SHOW,
	INFO_INV_FADE
};

void playerInit(Player);
void playerUpdateInventory(Player);
void playerSaveLocation(Player);
void playerUpdateNBT(Player);
void playerSensitivity(float s);
void playerLookAt(Player, int mx, int my);
int  playerProcessKey(Player, int key, int pressed);
void playerInitPickup(PickupBlock);
Bool playerAddInventory(Player, ItemID_t blockId, DATA8 tileEntity, Bool incCount);
void playerScrollInventory(Player, int dir);
void playerSetMode(Player, int mode);
void playerTeleport(Player, vec4 pos, float rotation[2]);
void playerMove(Player);

struct Player_t
{
	vec4     pos;              /* position of feet */
	vec4     lookat;           /* position looking at (to be used as param to matLookAt()) */
	float    angleh, anglev;   /* yaw, pitch (in radians) */
	float    velocity[3];      /* movement change per tick */
	float    dir[3];           /* target diection */
	float    targetY;          /* smooth clibing */

	uint8_t  fly;              /* 1 if flying */
	uint8_t  onground;         /* 1 if on ground, 0 otherwise */
	uint8_t  pmode;            /* enum, see MODE_* */
	uint8_t  liquid;           /* is in a liquid (water:1 or lava:2) */

	uint16_t keyvec;           /* bitfield of PLAYER_* */

	uint32_t tick;             /* time diff between update */
	NBTFile  levelDat;         /* complete NBT decoding of level.dat */
	InvBuf   inventory;
};

enum /* possible values for <pmode> */
{
	MODE_SURVIVAL  = 0,
	MODE_CREATIVE  = 1,
	MODE_ADVENTURE = 2,
	MODE_SPECTATOR = 3
};

enum /* possible values for <keyvec> */
{
	PLAYER_STRAFE_LEFT  = 0x0001,
	PLAYER_STRAFE_RIGHT = 0x0002,
	PLAYER_MOVE_FORWARD = 0x0004,
	PLAYER_MOVE_BACK    = 0x0008,
	PLAYER_UP           = 0x0010,
	PLAYER_DOWN         = 0x0020,
	PLAYER_JUMP         = 0x0040,
	PLAYER_FALL         = 0x0080,
	PLAYER_CLIMB        = 0x0100,
	PLAYER_STOPPING     = 0x0200,
	PLAYER_JUMPKEY      = 0x0400
};

#endif
