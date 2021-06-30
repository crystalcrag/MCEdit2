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

#define JUMP_STRENGTH          0.3
#define MAX_SPEED              4.317
#define FLY_SPEED             10.000
#define FALL_SPEED             8.0
#define MAX_FALL              10.000
#define BASE_ACCEL            32.0

static float sensitivity = 1/1000.;

void playerInit(Player p, NBTFile levelDat)
{
	float rotation[2];
	int player = p->playerBranch = NBT_FindNode(levelDat, 0, "Player");

	memset(p, 0, sizeof *p);

	NBT_ToFloat(levelDat, NBT_FindNode(levelDat, player, "Pos"), p->pos, 3);
	NBT_ToFloat(levelDat, NBT_FindNode(levelDat, player, "Rotation"), rotation, 2);

	p->pos[VT]  = p->lookat[VT] = 1;
	p->onground = NBT_ToInt(levelDat, NBT_FindNode(levelDat, player, "OnGround"), 1);
	p->pmode    = NBT_ToInt(levelDat, NBT_FindNode(levelDat, player, "playerGameType"), MODE_SURVIVAL);
	p->levelDat = levelDat;
	p->fly      = ! p->onground;

	/*
	 * rotation[] comes from level.dat: they are not trigonometric angles:
	 * rotation[0]: yaw, clockwise, degrees, where 0 = south.
	 * rotation[1]: pitch, degrees, +/- 90. negative = up, positive = down
	 */
	p->angleh = fmod((rotation[0] + 90) * (M_PI / 180), 2*M_PI);
	p->anglev = - rotation[1] * (2*M_PI / 360);
	if (p->angleh < 0) p->angleh += 2*M_PI;
	p->angleHCur = p->angleh;

	float cv = cosf(p->anglev);
	p->lookat[VX] = p->pos[VX] + 8 * cosf(p->angleh) * cv;
	p->lookat[VZ] = p->pos[VZ] + 8 * sinf(p->angleh) * cv;
	p->lookat[VY] = p->pos[VY] + 8 * sinf(p->anglev);

	/* get inventory content */
	int offset = NBT_FindNode(levelDat, player, "Inventory");
	if (offset > 0)
		mapDecodeItems(p->inventory.items, MAXCOLINV * 4, (NBTHdr) (levelDat->mem + offset));

	p->inventory.selected = NBT_ToInt(levelDat, NBT_FindNode(levelDat, player, "SelectedItemSlot"), 0);
}

/* save single player position and orientation in levelDat */
void playerSaveLocation(Player p, NBTFile levelDat)
{
	float rotation[2];
	int   player = NBT_FindNode(levelDat, 0, "Player");
	float select = p->inventory.selected;

	/* convert radians into degrees */
	rotation[0] = p->angleh * 180 / M_PI - 90;
	rotation[1] = - p->anglev * 180 / M_PI;
	if (rotation[0] < 0) rotation[0] += 360;

	NBT_SetFloat(levelDat, NBT_FindNode(levelDat, player, "Pos"), p->pos, 3);
	NBT_SetFloat(levelDat, NBT_FindNode(levelDat, player, "Rotation"), rotation, 2);
	NBT_SetFloat(levelDat, NBT_FindNode(levelDat, player, "SelectedItemSlot"), &select, 1); select = p->onground;
	NBT_SetFloat(levelDat, NBT_FindNode(levelDat, player, "OnGround"), &select, 1);
}

void playerSensitivity(float s)
{
	sensitivity = 1/s;
}

#define LEFT      's'
#define RIGHT     'f'
#define FORWARD   'e'
#define BACKWARD  'd'
#define JUMP      SITK_Space
#define FLYDOWN   SITK_LShift

static void playerSetDir(Player p)
{
	float angle = p->angleh;
	switch (p->keyvec & 15) {
	case PLAYER_MOVE_FORWARD: break;
	case PLAYER_MOVE_BACK:    angle += M_PI; break;
	case PLAYER_STRAFE_LEFT:  angle -= M_PI_2; break;
	case PLAYER_STRAFE_RIGHT: angle += M_PI_2; break;
	case PLAYER_MOVE_FORWARD |
	     PLAYER_STRAFE_LEFT:  angle -= M_PI_4; break;
	case PLAYER_MOVE_BACK |
	     PLAYER_STRAFE_LEFT:  angle -= M_PI_2 + M_PI_4; break;
	case PLAYER_MOVE_FORWARD |
	     PLAYER_STRAFE_RIGHT: angle += M_PI_4; break;
	case PLAYER_MOVE_BACK |
	     PLAYER_STRAFE_RIGHT: angle += M_PI_2 + M_PI_4; break;
	default:
		if (p->velocity != 0)
			p->keyvec |= PLAYER_STOPPING;
		return;
	}
	if (angle < 0)      angle += 2*M_PI; else
	if (angle > 2*M_PI) angle -= 2*M_PI;

	p->angleHDir = angle;
}

/* set keyvec state according to key press/released */
Bool playerProcessKey(Player p, int key, int mod)
{
	/* do not hi-jack keypress that involve Ctrl or Alt qualifier */
	if (mod & (SITK_FlagCtrl | SITK_FlagAlt))
		return False;
	uint8_t keyvec = p->keyvec & 15;
	if ((mod & SITK_FlagUp) == 0)
	{
		static int lastTick;
		//p->slower = (mod & SITK_FlagShift) > 0;
		switch (key) {
		case FORWARD:  p->keyvec &= ~(PLAYER_STOPPING|PLAYER_MOVE_BACK);    p->keyvec |= PLAYER_MOVE_FORWARD; break;
		case BACKWARD: p->keyvec &= ~(PLAYER_STOPPING|PLAYER_MOVE_FORWARD); p->keyvec |= PLAYER_MOVE_BACK; break;
		case LEFT:     p->keyvec &= ~(PLAYER_STOPPING|PLAYER_STRAFE_RIGHT); p->keyvec |= PLAYER_STRAFE_LEFT; break;
		case RIGHT:    p->keyvec &= ~(PLAYER_STOPPING|PLAYER_STRAFE_LEFT);  p->keyvec |= PLAYER_STRAFE_RIGHT; break;
		case FLYDOWN:  p->keyvec &= ~PLAYER_UP;                             p->keyvec |= PLAYER_DOWN; break;
		case '1': case '2': case '3': case '4': case '5':
		case '6': case '7': case '8': case '9':
			playerScrollInventory(p, (key - '1') - p->inventory.selected);
			return True;
		case JUMP:
			if ((int) curTime - lastTick < 250)
			{
				p->fly ^= 1;
				if (p->fly)
				{
					p->keyvec &= ~ PLAYER_FALL;
					p->velocityY = 0;
				}
				else p->keyvec |= PLAYER_FALL;
				//fprintf(stderr, "toggle flying: %d\n", p->fly);
			}
			lastTick = curTime;
			if (p->fly)
			{
				p->keyvec &= ~PLAYER_DOWN;
				p->keyvec |= PLAYER_UP;
			}
			else if (p->onground) /* initiate a jump */
			{
				p->keyvec |= PLAYER_FALL | PLAYER_JUMP;
				p->velocityY = -JUMP_STRENGTH;
				p->onground = 0;
			}
			break;
		default: return False;
		}
	}
	else /* released */
	{
		switch (key) {
		case FORWARD:  p->keyvec &= ~PLAYER_MOVE_FORWARD; break;
		case BACKWARD: p->keyvec &= ~PLAYER_MOVE_BACK; break;
		case LEFT:     p->keyvec &= ~PLAYER_STRAFE_LEFT; break;
		case RIGHT:    p->keyvec &= ~PLAYER_STRAFE_RIGHT; break;
		case JUMP:     p->keyvec &= ~(PLAYER_UP | PLAYER_JUMP); break;
		case FLYDOWN:  p->keyvec &= ~PLAYER_DOWN; break;
		default:       return False;
		}
	}
	if (keyvec == 0)
		p->tick = curTime;
	if (keyvec != (p->keyvec & 15))
		playerSetDir(p);
	/* return whether or not the key was processed or not */
	return True;
}

/* change lookat position according to mouse movement and sensitivity */
void playerLookAt(Player p, int dx, int dy)
{
	/* keep yaw between 0 and 2 * pi */
	float yaw = fmod(p->angleh + dx * sensitivity, 2*M_PI);
	float pitch = p->anglev - dy * sensitivity;
	if (yaw < 0) yaw += 2*M_PI;
	/* and pitch between -pi and pi */
	if (pitch < -M_PI_2+EPSILON) pitch = -M_PI_2+EPSILON;
	if (pitch >  M_PI_2-EPSILON) pitch =  M_PI_2-EPSILON;
	p->angleh = yaw;
	p->anglev = pitch;
	float cv = cosf(pitch);
	p->lookat[VX] = p->pos[VX] + 8 * cosf(yaw) * cv;
	p->lookat[VZ] = p->pos[VZ] + 8 * sinf(yaw) * cv;
	p->lookat[VY] = p->pos[VY] + 8 * sinf(pitch);
	if ((p->keyvec & PLAYER_STOPPING) == 0)
		playerSetDir(p);
}

void playerAdjustVelocity(Player p, float delta)
{
	float a = p->angleHCur;
	float d = p->angleHDir;
	float v = a - d;
	Bool  stop = p->keyvec & PLAYER_STOPPING;
	Bool  add = v < 0;

	if (fabsf(v) > M_PI)
	{
		if (v < 0) v += 2*M_PI, d += 2*M_PI;
		else       v -= 2*M_PI, d -= 2*M_PI;
		add = ! add;
	}

	delta *= BASE_ACCEL;
	if (p->onground == 0 && ! p->fly) delta *= 0.15;
	if (fabsf(v) > M_PI_2)
	{
		/* opposite direction: reduce velocity to zero first */
		v = p->velocity;
		if (v < 0)
		{
			v += delta;
			if (v > 0) v = 0;
		}
		else
		{
			v -= delta;
			if (v < 0) v = 0;
		}
		p->velocity = v;
		if (v != 0) return;
		else delta = M_PI, stop = True;
	}
	if (add)
	{
		a += delta;
		if (a > d) a = d;
	}
	else
	{
		a -= delta;
		if (a < d) a = d;
	}
	if (a > 2*M_PI) a -= 2*M_PI;
	if (a < 0)      a += 2*M_PI;
	p->angleHCur = a;
	p->cosh = cosf(a);
	p->sinh = sinf(a);
	v = p->velocity;
	if (stop)
	{
		v -= delta;
		if (v < 0) v = 0, p->keyvec &= ~PLAYER_STOPPING;
	}
	else
	{
		v += delta;
		a = p->fly ? FLY_SPEED : MAX_SPEED;
		if (v > a) v = a;
	}
	p->velocity = v;
}

void playerMove(Player p, Map map)
{
	float diff = curTime - p->tick;
	int   keyvec = p->keyvec;
	if (diff == 0) return;
	if (diff > 100) diff = 100; /* lots of lag :-/ */
	diff *= 1/1000.;
	p->tick = curTime;
	vec4 orig_pos;

	memcpy(orig_pos, p->pos, 16);
//	if (p->slower) speed *= 0.25;
	if (keyvec & (PLAYER_UP|PLAYER_DOWN))
	{
		p->pos[VY] += p->keyvec & PLAYER_UP ? FALL_SPEED*diff : -FALL_SPEED*diff;
	}
	if (keyvec & (PLAYER_STRAFE_LEFT|PLAYER_STRAFE_RIGHT|PLAYER_MOVE_FORWARD|PLAYER_MOVE_BACK|PLAYER_STOPPING))
	{
		p->pos[VX] += p->velocity * p->cosh * diff;
		p->pos[VZ] += p->velocity * p->sinh * diff;

		playerAdjustVelocity(p, diff);

//		fprintf(stderr, "%c v = %.3f, %d -> %d\n", p->keyvec & PLAYER_STOPPING ? '-' : ' ', p->velocity, (int) (p->angleHCur * 180 / M_PI),
//			(int) (p->angleHDir * 180 / M_PI));
	}
	if (keyvec & PLAYER_FALL)
	{
		p->pos[VY] -= p->velocityY;
		p->velocityY += diff;
		//fprintf(stderr, "velocity = %g\n", p->fallVelocity);
		if (p->velocityY > MAX_FALL)
			p->velocityY = MAX_FALL;
	}
	if (keyvec & PLAYER_CLIMB)
	{
		p->velocityY += 2*diff;
		p->pos[VY] += p->velocityY;
		if (p->pos[VY] > p->targetY)
		{
			fprintf(stderr, "climb done (%g / %g)\n", p->pos[VY], p->targetY);
			p->pos[VY] = p->targetY, p->keyvec &= ~ PLAYER_CLIMB;
			p->velocityY = 0;
		}
	}
	if (p->pmode <= MODE_CREATIVE)
	{
		/* bounding box of voxels will constraint movement in these modes */
		int collision = physicsCheckCollision(map, orig_pos, p->pos, entityGetBBox(ENTITY_PLAYER), 0.5);
		if (collision & 2)
		{
			/* auto-climb */
			p->targetY = p->pos[VY];
			p->pos[VY] = orig_pos[VY];
			p->keyvec |= PLAYER_CLIMB;
			fprintf(stderr, "climbing to %g (from %g)\n", p->targetY, p->pos[VY]);
		}
	#if 0
		if (collision & 1) p->velocity[VX] = 0;
		if (collision & 4) p->velocity[VZ] = 0;
	#endif
	}
	diff = p->onground;
	p->onground = physicsCheckOnGround(map, p->pos, entityGetBBox(ENTITY_PLAYER));
	if (diff != p->onground)
	{
		if (diff == 0)
		{
			/* cancel fall */
			p->velocityY = 0;
			p->keyvec &= ~PLAYER_FALL;
			p->fly = 0;
			if (keyvec & PLAYER_JUMP)
			{
				/* start a new jump as soon as we hit the ground */
				p->keyvec |= PLAYER_FALL | PLAYER_JUMP;
				p->velocityY = -JUMP_STRENGTH;
				p->onground = 0;
			}
		}
		else /* not on ground: init fall */
		{
			p->keyvec &= ~ PLAYER_CLIMB;
			p->keyvec |= PLAYER_FALL;
		}

		//fprintf(stderr, "onground: %d\n", p->onground);
	}
	vecSub(orig_pos, p->pos, orig_pos);
	vecAdd(p->lookat, p->lookat, orig_pos);
}

void playerTeleport(Player p, Map map, vec4 pos)
{
	vec4 diff;
	vecSub(diff, pos, p->pos);
	vecAdd(p->lookat, p->lookat, diff);
	memcpy(p->pos, pos, 12);
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
		STRPTR name;

		if (item->id >= ID(256, 0))
		{
			ItemDesc desc = itemGetById(item->id);
			name = desc->name;
		}
		else
		{
			BlockState b = blockGetById(item->id);
			name = STATEFLAG(b, TRIMNAME) ? blockIds[b->id >> 4].name : b->name;
		}

		CopyString(p->inventory.infoTxt, name, sizeof p->inventory.infoTxt);
		p->inventory.infoState = INFO_INV_INIT;
	}
	else p->inventory.infoState = INFO_INV_NONE;
}

void playerUpdateNBT(Player p, NBTFile levelDat)
{
	struct NBTFile_t inventory = {0};

	if (mapSerializeItems(NULL, "Inventory", p->inventory.items, MAXCOLINV * 4, &inventory))
	{
		int offset = NBT_Insert(levelDat, "Player.Inventory", TAG_List_Compound, &inventory);
		NBT_Free(&inventory);
		if (offset >= 0)
			mapDecodeItems(p->inventory.items, MAXCOLINV, NBT_Hdr(levelDat, offset));
	}
}

void playerAddInventory(Player p, int blockId, DATA8 tileEntity)
{
	if (blockId >= 0)
	{
		BlockState b = blockGetById(blockId);
		Item item;

		if (blockId > 0)
		{
			if (b->inventory == 0)
			{
				/* this block is not supposed to be in inventory, check for alternative */
				for (blockId &= ~15, b = blockGetById(blockId); (b->id & ~15) == blockId && b->inventory == 0; b ++);
				if ((b->id & ~15) != blockId) return;
				blockId = b->id;
			}
			/* check if it is already in inventory */
			int i;
			for (item = p->inventory.items, i = 0; i < MAXCOLINV && !(item->id == blockId && item->extra == NULL); i ++, item ++);
			if (i < MAXCOLINV)
				p->inventory.selected = i;
		}

		item = &p->inventory.items[p->inventory.selected];
		item->id = blockId;
		item->count = 1;
		item->uses = 0;
		item->extra = tileEntity;
		p->inventory.update ++;
		playerSetInfoTip(p);
	}
}

void playerScrollInventory(Player p, int dir)
{
	if (dir == 0) return;
	int pos = p->inventory.selected + dir;
	if (pos < 0) pos = MAXCOLINV - 1;
	if (pos >= MAXCOLINV) pos = 0;
	p->inventory.selected = pos;
	playerSetInfoTip(p);
}

/*
 * pick-up block
 */
static void playerSetMVMat(PickupBlock pickup)
{
	mat4 tmp, view;
	int  i;

	matTranslate(pickup->model, pickup->location[0], pickup->location[1], pickup->location[2]);
	for (i = 0; i < 3; i ++)
	{
		if (pickup->rotation[0] != 0)
			matRotate(tmp, pickup->rotation[i], i), matMult(pickup->model, pickup->model, tmp);
	}
	matScale(tmp, 0.8, 0.8, 0.8);
	matMult(pickup->model, pickup->model, tmp);
	matLookAt(view, 0, 0, 0, 0, 0, 1, 0, 1, 0);
	matMult(pickup->model, view, pickup->model);
}

void playerInitPickup(PickupBlock pickup)
{
	static vec4 pickUpLoc = {-1.8, -1.55, 1.9,  1};
	static vec4 pickUpRot = {-0.08,-0.9,-0.04, 1};

	memcpy(pickup->location, pickUpLoc, sizeof pickUpLoc);
	memcpy(pickup->rotation, pickUpRot, sizeof pickUpRot);

	playerSetMVMat(pickup);
}
