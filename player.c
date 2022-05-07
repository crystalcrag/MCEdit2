/*
 * player.c : manage player movements and stats.
 *
 * Written by T.Pierron, jan 2020
 */

#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "player.h"
#include "blocks.h"
#include "physics.h"
#include "entities.h"
#include "SIT.h"
#include "mapUpdate.h"
#include "inventories.h"
#include "keybindings.h"
#include "globals.h"

#define JUMP_STRENGTH          0.29f
#define MAX_SPEED              4.317f
#define FLY_SPEED             10.000f
#define FALL_SPEED             1.0f
#define MAX_FALL              10.000f
#define BASE_ACCEL            24.0f
#define viscosity             pos[VT]

static float sensitivity = 1/1000.;

struct ENTBBox_t playerBBox = {
	.pt1 = {-0.3, 0,   -0.3},
	.pt2 = { 0.3, 1.8,  0.3},
	.push = 1
};

void playerInit(Player p)
{
	float rotation[2];
	NBTFile levelDat = &globals.level->levelDat;
	int player = NBT_FindNode(levelDat, 0, "Player");

	memset(p, 0, sizeof *p);

	NBT_GetFloat(levelDat, NBT_FindNode(levelDat, player, "Pos"), p->pos, 3);
	NBT_GetFloat(levelDat, NBT_FindNode(levelDat, player, "Rotation"), rotation, 2);

	p->onground = NBT_GetInt(levelDat, NBT_FindNode(levelDat, player, "OnGround"), 1);
	p->pmode    = NBT_GetInt(levelDat, NBT_FindNode(levelDat, player, "playerGameType"), MODE_SURVIVAL);
	p->levelDat = levelDat;
	p->fly      = ! p->onground;

	/*
	 * rotation[] comes from level.dat: they are not trigonometric angles:
	 * rotation[0]: yaw, clockwise, degrees, where 0 = south.
	 * rotation[1]: pitch, degrees, +/- 90. negative = up, positive = down
	 */
	p->angleh = fmodf((rotation[0] + 90) * DEG_TO_RAD, 2*M_PIf);
	p->anglev = - rotation[1] * DEG_TO_RAD;
	if (p->angleh < 0) p->angleh += 2*M_PIf;
	p->yawNoClamp = p->angleh;

	float cv = cosf(p->anglev);
	p->lookat[VX] = p->pos[VX] + cosf(p->angleh) * cv;
	p->lookat[VZ] = p->pos[VZ] + sinf(p->angleh) * cv;
	p->lookat[VY] = p->pos[VY] + sinf(p->anglev);
	p->lookat[VT] = p->pos[VT] = 1;

	/* get inventory content */
	playerUpdateInventory(p);

	p->inventory.selected = NBT_GetInt(levelDat, NBT_FindNode(levelDat, player, "SelectedItemSlot"), 0);
}

/* inventory in NBT changed: update items */
void playerUpdateInventory(Player p)
{
	NBTFile levelDat = &globals.level->levelDat;
	int offset = NBT_FindNode(levelDat, 0, "Player.Inventory");
	if (offset > 0)
		inventoryDecodeItems(p->inventory.items, MAXCOLINV * 4, (NBTHdr) (levelDat->mem + offset));
}


/* save single player position and orientation in levelDat */
void playerSaveLocation(Player p)
{
	float   rotation[2];
	NBTFile levelDat = p->levelDat;
	int     player = NBT_FindNode(levelDat, 0, "Player");
	float   select = p->inventory.selected;

	/* convert radians into degrees */
	rotation[0] = p->angleh * RAD_TO_DEG - 90;
	rotation[1] = - p->anglev * RAD_TO_DEG;
	if (rotation[0] < 0) rotation[0] += 360;

	NBT_SetFloat(levelDat, NBT_FindNode(levelDat, player, "Pos"), p->pos, 3);
	NBT_SetFloat(levelDat, NBT_FindNode(levelDat, player, "Rotation"), rotation, 2);
	NBT_SetFloat(levelDat, NBT_FindNode(levelDat, player, "SelectedItemSlot"), &select, 1); select = p->onground;
	NBT_SetFloat(levelDat, NBT_FindNode(levelDat, player, "OnGround"), &select, 1); select = p->pmode;
	NBT_SetFloat(levelDat, NBT_FindNode(levelDat, player, "playerGameType"), &select, 1);
	NBT_MarkForUpdate(levelDat, 0, 1);
}

static void playerSetDir(Player p)
{
	float angle = p->angleh;
	switch (p->keyvec & 15) {
	case PLAYER_MOVE_FORWARD: break;
	case PLAYER_MOVE_BACK:    angle += M_PIf; break;
	case PLAYER_STRAFE_LEFT:  angle -= M_PI_2f; break;
	case PLAYER_STRAFE_RIGHT: angle += M_PI_2f; break;
	case PLAYER_MOVE_FORWARD |
	     PLAYER_STRAFE_LEFT:  angle -= M_PI_4f; break;
	case PLAYER_MOVE_BACK |
	     PLAYER_STRAFE_LEFT:  angle -= M_PI_2f + M_PI_4f; break;
	case PLAYER_MOVE_FORWARD |
	     PLAYER_STRAFE_RIGHT: angle += M_PI_4f; break;
	case PLAYER_MOVE_BACK |
	     PLAYER_STRAFE_RIGHT: angle += M_PI_2f + M_PI_4f; break;
	default:
		if (p->velocity[VX] != 0 || p->velocity[VZ] != 0)
		{
			p->dir[VX] = p->dir[VZ] = 0;
			p->keyvec |= PLAYER_STOPPING;
		}
		return;
	}

	p->dir[VX] = cosf(angle);
	p->dir[VZ] = sinf(angle);
}

/* set keyvec state according to key press/released */
int playerProcessKey(Player p, int command, int keyUp)
{
	/* do not hi-jack keypress that involve Ctrl or Alt qualifier */
	uint8_t keyvec = p->keyvec & 15;
	if (keyUp == 0)
	{
		static int lastTick;
		//p->slower = (mod & SITK_FlagShift) > 0;
		switch (command) {
		case KBD_MOVE_FORWARD:   p->keyvec &= ~(PLAYER_STOPPING|PLAYER_MOVE_BACK);    p->keyvec |= PLAYER_MOVE_FORWARD; break;
		case KBD_MOVE_BACKWARD:  p->keyvec &= ~(PLAYER_STOPPING|PLAYER_MOVE_FORWARD); p->keyvec |= PLAYER_MOVE_BACK; break;
		case KBD_STRAFE_LEFT:    p->keyvec &= ~(PLAYER_STOPPING|PLAYER_STRAFE_RIGHT); p->keyvec |= PLAYER_STRAFE_LEFT; break;
		case KBD_STRAFE_RIGHT:   p->keyvec &= ~(PLAYER_STOPPING|PLAYER_STRAFE_LEFT);  p->keyvec |= PLAYER_STRAFE_RIGHT; break;
		case KBD_SWITCH_OFFHAND: p->inventory.offhand ^= 1; break;
		case KBD_SLOT_0: p->inventory.offhand ^= 2; break;
		case KBD_SLOT_1: case KBD_SLOT_2: case KBD_SLOT_3:
		case KBD_SLOT_4: case KBD_SLOT_5: case KBD_SLOT_6:
		case KBD_SLOT_7: case KBD_SLOT_8: case KBD_SLOT_9:
			playerScrollInventory(p, (command - KBD_SLOT_1) - p->inventory.selected);
			return 2;
		case KBD_JUMP:
			p->keyvec |= PLAYER_JUMPKEY;
			if ((int) globals.curTime - lastTick < 250 && p->pmode <= MODE_CREATIVE)
			{
				/* push jump key twice within short time: toggle fly mode */
				p->fly ^= 1;
				if (p->fly)
				{
					p->keyvec &= ~ PLAYER_FALL;
					p->velocity[VY] = 0;
				}
				else p->keyvec |= PLAYER_FALL;
			}
			lastTick = globals.curTime;
			if (p->fly || p->viscosity < 1)
			{
				/* if flying or in liquid, jump key == go up */
				p->keyvec &= ~(PLAYER_DOWN | PLAYER_FALL);
				p->keyvec |= PLAYER_UP;
				p->dir[VY] = 1;
			}
			else if (p->onground && p->pmode <= MODE_CREATIVE) /* initiate a jump */
			{
				p->keyvec |= PLAYER_FALL | PLAYER_JUMP;
				p->velocity[VY] = -JUMP_STRENGTH;
				p->onground = 0;
			}
			break;
		case KBD_FLYDOWN:
			p->dir[VY] = -1;
			p->keyvec &= ~PLAYER_UP;
			p->keyvec |= PLAYER_DOWN;
			break;
		default: return 0;
		}
	}
	else /* released */
	{
		switch (command) {
		case KBD_MOVE_FORWARD:  p->keyvec &= ~PLAYER_MOVE_FORWARD; break;
		case KBD_MOVE_BACKWARD: p->keyvec &= ~PLAYER_MOVE_BACK; break;
		case KBD_STRAFE_LEFT:   p->keyvec &= ~PLAYER_STRAFE_LEFT; break;
		case KBD_STRAFE_RIGHT:  p->keyvec &= ~PLAYER_STRAFE_RIGHT; break;
		case KBD_JUMP:
			p->keyvec &= ~PLAYER_JUMPKEY;
			if (p->keyvec & (PLAYER_UP | PLAYER_DOWN))
			{
				/* instant stop */
				p->velocity[VY] = p->dir[VY] = 0;
				if (! p->fly) p->keyvec |= PLAYER_FALL;
			}
			p->keyvec &= ~(PLAYER_UP | PLAYER_JUMP);
			break;
		case KBD_FLYDOWN:
			if (p->keyvec & (PLAYER_UP | PLAYER_DOWN))
				p->velocity[VY] = p->dir[VY] = 0;
			p->keyvec &= ~PLAYER_DOWN;
			break;
		default: return 0;
		}
	}
	if (keyvec == 0)
		p->tick = globals.curTime;
	if (keyvec != (p->keyvec & 15))
		playerSetDir(p);
	/* return whether or not the key was processed */
	return 1;
}

/* change lookat position according to mouse movement and sensitivity */
void playerLookAt(Player p, int dx, int dy)
{
	/* keep yaw between 0 and 2 * pi */
	float yaw = p->angleh + dx * sensitivity * globals.mouseSpeed;
	float pitch = p->anglev - dy * sensitivity * globals.mouseSpeed;
	p->yawNoClamp += dx * sensitivity * globals.mouseSpeed;
	if (yaw < 0) yaw += 2*M_PIf;
	if (yaw > 2*M_PIf) yaw -= 2*M_PIf;
	/* and pitch between -pi and pi */
	if (pitch < -M_PI_2f+EPSILON) pitch = -M_PI_2f+EPSILON;
	if (pitch >  M_PI_2f-EPSILON) pitch =  M_PI_2f-EPSILON;
	p->angleh = yaw;
	p->anglev = pitch;
	float cv = cosf(pitch);
	p->lookat[VX] = p->pos[VX] + 8 * cosf(yaw) * cv;
	p->lookat[VZ] = p->pos[VZ] + 8 * sinf(yaw) * cv;
	p->lookat[VY] = p->pos[VY] + 8 * sinf(pitch);
	if ((p->keyvec & PLAYER_STOPPING) == 0)
		playerSetDir(p);
}

/*
 * smooth velocity according to friction/momentum: given the direction we are currently
 * going (p->velocity) and the diretion we want to go (p->dir), lineraly interpolate
 * any changes between this 2 points.
 */
void playerAdjustVelocity(Player p, float delta)
{
	float max = (p->fly ? FLY_SPEED : MAX_SPEED) * fminf(p->viscosity * 4, 1);
	vec   v   = p->velocity;
	float dest[3] = {p->dir[VX] * max, 0, p->dir[VZ] * max};
	float diff[3];

	vecSub(diff, dest, p->velocity);

	/* only adjust VX and VZ here */
	max = sqrtf(diff[VX]*diff[VX] + diff[VZ] * diff[VZ]);
	if (max > EPSILON)
	{
		int i;
		max = BASE_ACCEL * delta / max;
		if (! p->fly)
		{
			/* falling: harder to steer than when on ground */
			if (p->onground == 0) max *= 0.5f;
		}
		else if (p->keyvec & PLAYER_STOPPING) max *= 0.75f;
		for (i = VX; i <= VZ; i += 2)
		{
			float d = diff[i] * max;
			if (v[i] < dest[i])
			{
				v[i] += d;
				if (v[i] > dest[i]) v[i] = dest[i];
			}
			else if (v[i] > dest[i])
			{
				v[i] += d;
				if (v[i] < dest[i]) v[i] = dest[i];
			}
		}
	}
	else v[VX] = dest[VX], v[VZ] = dest[VZ];

	if ((p->keyvec & PLAYER_STOPPING) && v[VX] == 0 && v[VZ] == 0)
		p->keyvec &= ~PLAYER_STOPPING;
}

/* slightly simplified compare to playerAdjustVelocity() */
static void playerAdjustVelocityY(Player p, float delta)
{
	float dest = p->dir[VY] * (FALL_SPEED * 0.5f); if (! p->fly) dest *= p->viscosity;
	float diff = dest - p->velocity[VY];
	float max  = fabsf(diff);
	vec   vecY = p->velocity + VY;

	if (max > EPSILON)
	{
		float adjust = diff * (BASE_ACCEL/16) * delta / max;
		if (vecY[0] < dest)
		{
			vecY[0] += adjust;
			if (vecY[0] > dest) vecY[0] = dest;
		}
		else if (vecY[0] > dest)
		{
			vecY[0] += adjust;
			if (vecY[0] < dest) vecY[0] = dest;
		}
	}
	else vecY[0] = dest;

//	fprintf(stderr, "velocityY = %g, target = %g, keyvec = %x\n", vecY[0], p->dir[VY], p->keyvec);
}

void playerMove(Player p)
{
	float diff = globals.curTime - p->tick;
	int   keyvec = p->keyvec;
	vec4  orig_pos;

	if (diff < 1) return;
	if (diff > 100) diff = 100; /* lots of lag :-/ */
	diff *= 1/1000.f;

	memcpy(orig_pos, p->pos, 16);
	if (keyvec & PLAYER_PUSHED)
	{
		/* pushed by an entity: check collision with terrain first before setting the position */
		memcpy(p->pos, p->pushedTo, 12);
		p->keyvec &= ~ PLAYER_PUSHED;
	}
	else
	{
		if (keyvec & (PLAYER_UP|PLAYER_DOWN))
		{
			playerAdjustVelocityY(p, diff);
			p->pos[VY] += p->velocity[VY];
		}
		if (keyvec & (PLAYER_STRAFE_LEFT|PLAYER_STRAFE_RIGHT|PLAYER_MOVE_FORWARD|PLAYER_MOVE_BACK|PLAYER_STOPPING))
		{
			playerAdjustVelocity(p, diff);
			p->pos[VX] += p->velocity[VX] * diff;
			p->pos[VZ] += p->velocity[VZ] * diff;
		}
		if (keyvec & PLAYER_FALL)
		{
			/* jumping or falling */
			float dy;
			if (p->ladder == 0)
			{
				dy = p->velocity[VY] += diff * p->viscosity;
				if (p->velocity[VY] > MAX_FALL * p->viscosity)
					p->velocity[VY] = MAX_FALL * p->viscosity;
			}
			else dy = p->velocity[VY] * diff;
			p->pos[VY] -= dy;
		}
		if (keyvec & PLAYER_CLIMB)
		{
			/* smooth vertical transition */
			p->velocity[VY] += 2*diff;
			p->pos[VY] += p->velocity[VY];
			if (p->pos[VY] > p->targetY)
			{
				p->pos[VY] = p->targetY, p->keyvec &= ~ PLAYER_CLIMB;
				p->velocity[VY] = 0;
			}
		}
	}
	if (p->pmode <= MODE_CREATIVE)
	{
		/* bounding box of voxels will constraint movement in these modes */
		float oldVisco = p->viscosity;
		int collision = physicsCheckCollision(globals.level, orig_pos, p->pos, &playerBBox, (keyvec & PLAYER_FALL) ? 0 : 0.5, NULL);
		int climb = -1;

//		fprintf(stderr, "moved to %.2f x %.2f\n", (double) p->pos[VX] - 0.3, (double) p->pos[VX] + 0.3);
//		fprintf(stderr, "velocityY %.2f, pos = %.2f => %.2f [%g - %d], dirY: %g\n", p->velocity[VY], orig_pos[VY], p->pos[VY], p->targetY, collision,
//			p->dir[VY]);

		if ((collision & INSIDE_PLATE) || p->plate)
		{
			physicsCheckPressurePlate(globals.level, orig_pos, p->pos, &playerBBox);
			p->plate = collision & INSIDE_PLATE;
		}
		if ((collision & INSIDE_LADDER) && (climb = physicsCheckIfCanClimb(globals.level, p->pos, &playerBBox)))
		{
			if ((collision & 5) && (keyvec & PLAYER_MOVE_FORWARD))
				p->ladder = 2, p->velocity[VY] = -1;
			else
				p->ladder = 1, p->velocity[VY] = 2;
			/* can't change position here, need to do collision check first */
			p->keyvec |= PLAYER_FALL;
		}
		else if (p->ladder)
		{
			p->ladder = 0;
			p->velocity[VY] = 0;
		}

		if ((collision & SOFT_COLLISON) == 0)
		{
			if (collision & 1) p->velocity[VX] = 0;
			if (collision & 4) p->velocity[VZ] = 0;
		}
		/* else target might get out of the way at some point */

		if ((collision & 2) && orig_pos[VY] < p->pos[VY])
		{
			/* auto-climb */
			p->targetY = p->pos[VY];
			p->pos[VY] = orig_pos[VY];
			p->keyvec |= PLAYER_CLIMB;
			//fprintf(stderr, "auto-climbing to %g (from %g)\n", (double) p->targetY, (double) p->pos[VY]);
		}
		uint8_t ground = p->onground;
		if ((keyvec & PLAYER_FALL) == 0 || p->velocity[VY] >= 0)
			p->onground = physicsCheckOnGround(globals.level, p->pos, &playerBBox);

		//fprintf(stderr, "pos = %g, %g, %g, ground: %d\n", p->pos[0], p->pos[1], p->pos[2], p->onground);
		if (p->viscosity != oldVisco && ! p->fly)
		{
			if (p->viscosity == 1)
			{
				/* just exited water/lava */
				p->keyvec &= ~ (PLAYER_CLIMB | PLAYER_UP | PLAYER_DOWN);
				p->keyvec |= PLAYER_FALL;
				p->velocity[VY] = -JUMP_STRENGTH/3;
			}
			else if (p->keyvec & PLAYER_JUMPKEY)
			{
				/* just entered water/lava (while jump key is held down) */
				p->keyvec &= ~ PLAYER_FALL;
				p->keyvec |= PLAYER_UP;
			}
		}
		else if (p->ladder == 2)
		{
			;
		}
		else if (ground != p->onground)
		{
			if (ground == 0)
			{
				/* cancel fall */
				p->velocity[VY] = 0;
				p->keyvec &= ~PLAYER_FALL;
				p->fly = 0;
				if (keyvec & PLAYER_JUMP)
				{
					/* start a new jump as soon as we hit the ground */
					p->keyvec |= PLAYER_FALL | PLAYER_JUMP;
					p->velocity[VY] = -JUMP_STRENGTH;
					p->onground = 0;
				}
			}
			else if (p->viscosity == 1) /* not on ground: init fall */
			{
				p->keyvec &= ~ PLAYER_CLIMB;
				p->keyvec |= PLAYER_FALL;
			}
		}
	}
	vecSub(orig_pos, p->pos, orig_pos);
	vecAdd(p->lookat, p->lookat, orig_pos);
	if ((keyvec & PLAYER_PUSHED) && (keyvec & ~PLAYER_PUSHED))
		playerMove(p);
	else
		p->tick = globals.curTime;
}

void playerCheckNearby(Player p, float areaChanged[6])
{
	if (p->pos[VX] + playerBBox.pt1[VX] < areaChanged[VX+3] && p->pos[VX] + playerBBox.pt2[VX] > areaChanged[VX] &&
		p->pos[VY] + playerBBox.pt1[VY] < areaChanged[VY+3] && p->pos[VY] + playerBBox.pt2[VY] > areaChanged[VY] &&
		p->pos[VZ] + playerBBox.pt1[VZ] < areaChanged[VZ+3] && p->pos[VZ] + playerBBox.pt2[VZ] > areaChanged[VZ])
	{
		p->onground = physicsCheckOnGround(globals.level, p->pos, &playerBBox);
		if (! p->onground)
		{
			p->keyvec &= ~ PLAYER_CLIMB;
			p->keyvec |= PLAYER_FALL;
		}
	}
}

void playerTeleport(Player p, vec4 pos, float rotation[2])
{
	vec4 diff;
	if (rotation == NULL)
	{
		vecSub(diff, pos, p->pos);
		vecAdd(p->lookat, p->lookat, diff);
		memcpy(p->pos, pos, 12);
	}
	else
	{
		memcpy(p->pos, pos, 12);
		p->angleh = rotation[0];
		p->anglev = rotation[1];
		float cv = cosf(p->anglev);
		p->lookat[VX] = p->pos[VX] + 8 * cosf(p->angleh) * cv;
		p->lookat[VZ] = p->pos[VZ] + 8 * sinf(p->angleh) * cv;
		p->lookat[VY] = p->pos[VY] + 8 * sinf(p->anglev);
	}
}

void playerSetMode(Player p, int mode)
{
	if (p->pmode != mode)
	{
		static STRPTR modes[] = {
			DLANG("survival"),
			DLANG("creative"),
			NULL, /* advanture mode: don't care */
			DLANG("spectator")
		};
		p->pmode = mode;
		snprintf(p->inventory.infoTxt, sizeof p->inventory.infoTxt, LANG("Switched to %s mode"), LANG(modes[mode]));
		p->inventory.infoState = INFO_INV_INIT;
	}
	switch (mode) {
	case MODE_SURVIVAL:
		p->onground = physicsCheckOnGround(globals.level, p->pos, &playerBBox);
		p->fly = 0;
		if (! p->onground)
			p->keyvec |= PLAYER_FALL;
		break;
	case MODE_CREATIVE:
		p->onground = physicsCheckOnGround(globals.level, p->pos, &playerBBox);
		p->fly = !p->onground;
		break;
	case MODE_SPECTATOR:
		p->fly = 1;
		p->onground = 0;
	}
}

/*
 * inventory
 */

/* get the text to display while selecting an item in the toolbar */
static void playerSetInfoTip(Player p)
{
	Item item = &p->inventory.items[p->inventory.selected];

	if (item->id > 0)
	{
		TEXT   buffer[32];
		STRPTR name = NULL;

		if (isBlockId(item->id))
		{
			BlockState b = blockGetById(item->id);
			if (b > blockStates) name = STATEFLAG(b, TRIMNAME) ? blockIds[b->id >> 4].name : b->name;
		}
		else
		{
			ItemDesc desc = itemGetById(item->id);
			if (desc) name = desc->name;
		}
		if (name == NULL)
		{
			sprintf(name = buffer, "unknown (%d:%d)", item->id >> 4, item->id & 15);
		}

		CopyString(p->inventory.infoTxt, name, sizeof p->inventory.infoTxt);
		p->inventory.infoState = INFO_INV_INIT;
	}
	else p->inventory.infoState = INFO_INV_NONE;
}

void playerUpdateNBT(Player p)
{
	struct NBTFile_t inventory = {0};

	NBTFile levelDat = p->levelDat;
	if (inventorySerializeItems(NULL, 0, "Inventory", p->inventory.items, PLAYER_MAX_ITEMS, &inventory))
	{
		int offset = NBT_Insert(levelDat, "Player.Inventory", TAG_List_Compound, &inventory);
		NBT_Free(&inventory);
		if (offset >= 0)
		{
			inventoryDecodeItems(p->inventory.items, PLAYER_MAX_ITEMS, NBT_Hdr(levelDat, offset));
			p->inventory.update ++;
		}
	}
}

Bool playerAddInventory(Player p, Item add)
{
	ItemID_t itemId;
	Item     item;
	int      slot;

	if (add && add->id > 0)
	{
		itemId = add->id;
		if (isBlockId(itemId))
		{
			BlockState b = blockGetById(itemId);
			if (b->inventory == 0)
			{
				/* this block is not supposed to be in inventory, check for alternative */
				int invId = blockAdjustInventory(itemId);
				/* that entire block type can't be used as an inventory item */
				if (invId == 0 && (invId = itemCanCreateBlock(itemId, NULL)) == itemId)
					return False;

				if (invId == 0)
					return False;
				itemId = invId;
			}
		}

		/* check if it is already in inventory */
		slot = -1;
		do {
			for (item = p->inventory.items, slot ++; slot < PLAYER_MAX_ITEMS && !(item->id == itemId && item->tile == NULL); slot ++, item ++);

			if (slot < PLAYER_MAX_ITEMS)
			{
				if (slot >= MAXCOLINV)
				{
					/* in inventory, but not on inventory bar: exchange with current slot */
					struct Item_t tmp = *item;
					Item   invBar = &p->inventory.items[p->inventory.selected];
					*item = *invBar;
					*invBar = tmp;
				}
				else p->inventory.selected = slot;

				if (p->pmode == MODE_SURVIVAL)
					add->count = itemAddCount(item, add->count);
				else
					break;
			}
			else /* not in inventory: try to add them in the first free slot */
			{
				item = p->inventory.items;
				if (item[p->inventory.selected].id == 0)
					slot = p->inventory.selected, item += slot;
				else
					for (slot = 0; slot < PLAYER_MAX_ITEMS && item->id > 0; slot ++, item ++);

				if (slot < PLAYER_MAX_ITEMS)
				{
					if (slot >= MAXCOLINV)
					{
						/* exchange with active slot */
						Item active = p->inventory.items + p->inventory.selected;
						item[0] = active[0];
						item = active;
					}
					else p->inventory.selected = slot;
					/*
					 * <tileEntity> is a raw pointer within chunk NBT from world map, it can be freed at any time,
					 * but will be serialized within levelDat in playerUpdateNBT()
					 */
					item->id = itemId;
					item->count = add->count;
					item->tile = add->tile;
					item->extraF = add->extraF;
					item->uses = add->uses;
					break;
				}
				else return False; /* inventory full XXX need to update entity count otherwise duping glitch */
			}
		}
		while (add->count > 0);
	}
	else /* else user wants to get rid of this item */
	{
		memset(p->inventory.items + p->inventory.selected, 0, sizeof *item);
	}

	p->inventory.update ++;
	playerSetInfoTip(p);
	return True;
}

void playerScrollInventory(Player p, int dir)
{
	if (p->inventory.offhand < 3)
		/* partial extended selection: cancel all */
		p->inventory.offhand = 0;
	int pos = p->inventory.selected + dir;
	if (pos < 0) pos = MAXCOLINV - 1;
	if (pos >= MAXCOLINV) pos = 0;
	p->inventory.selected = pos;
	if (p->inventory.offhand != 3)
		playerSetInfoTip(p);
}
