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
#define PLAYER_HEIGHT         1.6

struct Inventory_t
{
	int     selected;
	int     hover;
	int     texture;
	ItemBuf items[MAXCOLINV * 4 + 5];
	int     x, y, update;
	int     infoState, infoX;
	int     infoTime;
	TEXT    infoTxt[128];
};

enum
{
	INFO_INV_NONE,
	INFO_INV_INIT,
	INFO_INV_SHOW,
	INFO_INV_FADE
};

void playerInit(Player, NBTFile levelDat);
void playerSaveLocation(Player, NBTFile levelDat);
void playerUpdateNBT(Player, NBTFile levelDat);
void playerSensitivity(float s);
void playerLookAt(Player, int mx, int my);
void playerMove(Player);
Bool playerProcessKey(Player, int key, int pressed);
void playerInitPickup(PickupBlock);
void playerAddInventory(Player, int blockId, DATA8 tileEntity);
void playerScrollInventory(Player, int dir);

struct Player_t
{
	vec4    pos;
	vec4    lookat;
	float   angleh, anglev; /* radians */
	float   cosh, sinh;
	float   cosv, sinv;
	float   speed;
	uint8_t onground;
	uint8_t slower;
	uint8_t keyvec;
	uint8_t mode;
	InvBuf  inventory;
};

enum /* possible values for mode */
{
	MODE_SURVIVAL,
	MODE_CREATIVE,
	MODE_SPECTATOR
};

enum /* possible values for keyvec */
{
	PLAYER_UP           = 0x01,
	PLAYER_DOWN         = 0x02,
	PLAYER_STRAFE_LEFT  = 0x04,
	PLAYER_STRAFE_RIGHT = 0x08,
	PLAYER_MOVE_FORWARD = 0x10,
	PLAYER_MOVE_BACK    = 0x20
};

#endif
