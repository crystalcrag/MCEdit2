/*
 * mobEntity.c: manage mob entities, although this module not doing much right now.
 *
 * written by T.Pierron, apr 2022.
 */

#define ENTITY_IMPL
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <malloc.h>
#include "entities.h"
#include "globals.h"


char mobIdList[] = "minecart,creeper,chicken,sheep,cow,mooshroom,horse,squid,bat,zombie,skeleton,enderman";

static int mobEntityCreate(NBTFile nbt, Entity entity, STRPTR id)
{
	entity->enflags |= ENFLAG_TEXENTITES | ENFLAG_HASBBOX;
	entity->entype = ENTYPE_MOB;

	int entityId = FindInList(mobIdList, id, 0);
	if (entityId < 0) return 0;

	return entityAddModel(ITEMID(ENTITY_MINECART+entityId, 0), 0, NULL, &entity->szx, MODEL_DONT_SWAP);
}

void mobEntityInit(void)
{
	entityRegisterType("creeper", mobEntityCreate);
}

