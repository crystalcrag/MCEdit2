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

enum
{
	INFO_INV_NONE,
	INFO_INV_INIT,
	INFO_INV_SHOW,
	INFO_INV_FADE
};

void playerInit(Player);
void playerSaveLocation(Player);
void playerUpdateNBT(Player);
void playerSensitivity(float s);
void playerLookAt(Player, int mx, int my);
int  playerProcessKey(Player, int key, int pressed);
void playerInitPickup(PickupBlock);
void playerAddInventory(Player, int blockId, DATA8 tileEntity);
void playerScrollInventory(Player, int dir);
void playerSetMode(Player, int mode);
void playerTeleport(Player, vec4 pos);
void playerMove(Player);

struct Player_t
{
	vec4     pos;
	vec4     lookat;
	float    angleh, anglev; /* radians */
	float    velocity[3];
	float    dir[3];
	float    velocityY;
	float    targetY;
	uint8_t  fly;
	uint8_t  onground;
	uint8_t  pmode;
	uint16_t keyvec;
	int      tick;
	NBTFile  levelDat;
	int      playerBranch;
	InvBuf   inventory;
};

enum /* possible values for mode */
{
	MODE_SURVIVAL,
	MODE_CREATIVE,
	MODE_SPECTATOR
};

enum /* possible values for keyvec */
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
	PLAYER_STOPPING     = 0x0200
};

#endif
