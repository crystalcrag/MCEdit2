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

static float sensitivity = 1/1000.;
extern double curTime; /* from main.c */

void playerInit(Player p, NBTFile levelDat)
{
	float rotation[2];
	int player = NBT_FindNode(levelDat, 0, "Player");

	memset(p, 0, sizeof *p);

	NBT_ToFloat(levelDat, NBT_FindNode(levelDat, player, "Pos"), p->pos, 3);
	NBT_ToFloat(levelDat, NBT_FindNode(levelDat, player, "Rotation"), rotation, 2);

	p->pos[VT] = p->lookat[VT] = 1;
	p->sinh    = -1;
	p->cosv    = 1;
	p->speed   = 6; /* blocks per second */
	p->pmode   = NBT_ToInt(levelDat, NBT_FindNode(levelDat, player, "playerGameType"), MODE_SURVIVAL);

	/*
	 * rotation[] comes from level.dat: they are not trigonometric angles:
	 * rotation[0]: yaw, clockwise, degrees, where 0 = south.
	 * rotation[1]: pitch, degrees, +/- 90. negative = up, positive = down
	 */
	p->angleh = fmod((rotation[0] + 90) * (M_PI / 180), 2*M_PI);
	p->anglev = - rotation[1] * (2*M_PI / 360);
	if (p->angleh < 0) p->angleh += 2*M_PI;

	float cv = p->cosv = cosf(p->anglev);
	p->lookat[VX] = p->pos[VX] + 8 * (p->cosh = cosf(p->angleh)) * cv;
	p->lookat[VZ] = p->pos[VZ] + 8 * (p->sinh = sinf(p->angleh)) * cv;
	p->lookat[VY] = p->pos[VY] + 8 * (p->sinv = sinf(p->anglev));

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
	NBT_SetFloat(levelDat, NBT_FindNode(levelDat, player, "SelectedItemSlot"), &select, 1);
}

void playerSensitivity(float s)
{
	sensitivity = 1/s;
}

#define LEFT      's'
#define RIGHT     'f'
#define FORWARD   'e'
#define BACKWARD  'd'

/* set keyvec state according to key press/released */
Bool playerProcessKey(Player p, int key, int mod)
{
	/* do not hi-jack keypress that involve Ctrl or Alt qualifier */
	if (mod & (SITK_FlagCtrl | SITK_FlagAlt))
		return False;
	if ((mod & SITK_FlagUp) == 0)
	{
		p->slower = (mod & SITK_FlagShift) > 0;
		switch (key) {
		case FORWARD:  p->keyvec &= ~PLAYER_MOVE_BACK;    p->keyvec |= PLAYER_MOVE_FORWARD; break;
		case BACKWARD: p->keyvec &= ~PLAYER_MOVE_FORWARD; p->keyvec |= PLAYER_MOVE_BACK; break;
		case LEFT:     p->keyvec &= ~PLAYER_STRAFE_RIGHT; p->keyvec |= PLAYER_STRAFE_LEFT; break;
		case RIGHT:    p->keyvec &= ~PLAYER_STRAFE_LEFT;  p->keyvec |= PLAYER_STRAFE_RIGHT; break;
		case 'q':      p->keyvec &= ~PLAYER_DOWN;         p->keyvec |= PLAYER_UP; break;
		case 'z':      p->keyvec &= ~PLAYER_UP;           p->keyvec |= PLAYER_DOWN; break;
		case '1': case '2': case '3': case '4': case '5':
		case '6': case '7': case '8': case '9':
			playerScrollInventory(p, (key - '1') - p->inventory.selected);
			return True;
		default: return False;
		}
		if (p->tick == 0)
			p->tick = curTime;
	}
	else /* released */
	{
		switch (key) {
		case FORWARD:  p->keyvec &= ~PLAYER_MOVE_FORWARD; break;
		case BACKWARD: p->keyvec &= ~PLAYER_MOVE_BACK; break;
		case LEFT:     p->keyvec &= ~PLAYER_STRAFE_LEFT; break;
		case RIGHT:    p->keyvec &= ~PLAYER_STRAFE_RIGHT; break;
		case 'q':      p->keyvec &= ~PLAYER_UP; break;
		case 'z':      p->keyvec &= ~PLAYER_DOWN; break;
		default:       return False;
		}
		if (p->keyvec == 0)
			p->tick = 0;
	}
	/* return if the key was processed or not */
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
	float cv = p->cosv = cosf(pitch);
	p->lookat[VX] = p->pos[VX] + 8 * (p->cosh = cosf(yaw)) * cv;
	p->lookat[VZ] = p->pos[VZ] + 8 * (p->sinh = sinf(yaw)) * cv;
	p->lookat[VY] = p->pos[VY] + 8 * (p->sinv = sinf(pitch));
//	fprintf(stderr, "angleh = %g, anglev = %g, lookat = %g,%g,%g\n", p->angleh, p->anglev * 180 / M_PI,
//		p->lookat[VX], p->lookat[VY], p->lookat[VZ]);
}

void playerMove(Player p, Map map)
{
	int diff = (int) curTime - p->tick;
	if (diff == 0) return;
	if (diff > 100) diff = 100; /* lots of lag :-/ */
	p->tick = curTime;

	float speed = p->speed * diff * 0.001;
	vec4  orig_pos;

	memcpy(orig_pos, p->pos, 16);
	if (p->slower) speed *= 0.25;
	if (p->keyvec & (PLAYER_UP|PLAYER_DOWN))
	{
		p->pos[VY] += p->keyvec & PLAYER_UP ? speed : -speed;
	}
	if (p->keyvec & (PLAYER_STRAFE_LEFT|PLAYER_STRAFE_RIGHT))
	{
		float s = p->keyvec & PLAYER_STRAFE_RIGHT ? speed : -speed;
		p->pos[VX] += - p->sinh * s; // cos(angleh+90) == - sin(angleh)
		p->pos[VZ] +=   p->cosh * s; // sin(angleh+90) ==   cos(angleh)
	}
	if (p->keyvec & (PLAYER_MOVE_FORWARD|PLAYER_MOVE_BACK))
	{
		float s = p->keyvec & PLAYER_MOVE_FORWARD ? speed : -speed;
		#if 0
		/* follow direction pointed by camera */
		p->pos[VY] += p->sinv * s;
		p->pos[VX] += p->cosh * s * p->cosv;
		p->pos[VZ] += p->sinh * s * p->cosv;
		#else
		/* only move on the XZ plane */
		p->pos[VX] += p->cosh * s;
		p->pos[VZ] += p->sinh * s;
		#endif
	}
	#if 0
	if (p->pmode <= MODE_CREATIVE)
	{
		/* bounding box of voxels will constraint movement in these modes */
		physicsCheckCollision(map, orig_pos, p->pos, entityGetBBox(ENTITY_PLAYER));
	}
	#endif
	vecSub(orig_pos, p->pos, orig_pos);
	vecAdd(p->lookat, p->lookat, orig_pos);
}

void playerTeleport(Player p, Map map, vec4 pos)
{
	vec4 diff;
	if (p->pmode <= MODE_CREATIVE)
		physicsCheckCollision(map, p->pos, pos, entityGetBBox(ENTITY_PLAYER));
	vecSub(diff, pos, p->pos);
	vecAdd(p->lookat, p->lookat, diff);
	memcpy(p->pos, pos, 12);
}

void playerStickToGround(Player p, Map map)
{
	struct BlockIter_t iter;
	fprintf(stderr, "pos = %g, %g, Y: %g", p->pos[VX], p->pos[VZ], p->pos[VY]);
	mapInitIter(map, &iter, p->pos, False);
	while (iter.blockIds[iter.offset] == 0)
		mapIter(&iter, 0, -1, 0);

	float diff = iter.yabs + 1 - p->pos[VY];
	p->pos[VY] = iter.yabs + 1;
	p->lookat[VY] += diff;
	int ground = physicsCheckOnGround(map, p->pos, entityGetBBox(ENTITY_PLAYER));

	fprintf(stderr, " - new Y: %g, onground: %d\n", p->pos[VY], ground);
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
