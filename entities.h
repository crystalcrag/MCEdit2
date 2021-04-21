/*
 * entities.h : public functions to deal with entities (mob mostly)
 *
 * written by T.Pierron, apr 2021
 */


#ifndef MCENTITY_H
#define MCENTITY_H

#include "chunks.h"

Bool entityInitStatic(void);
void entityParse(Chunk, NBTFile nbt, int offset);
void entityUnload(Chunk);
void entityAnimate(void);
void entityRender(void);
int  entityRaycast(Chunk c, vec4 dir, vec4 camera, vec4 cur);

#define ENTITY_END    0xffff

/* private stuff below */
#ifdef ENTITY_IMPL
#define VERTEX_SIZE   16
#define ENTITY_BATCH  256

enum /* entity id and models */
{
	ENTITY_UNKNOWN,
	ENTITY_CHICKEN,
	ENTITY_SHEEP,
	ENTITY_COW,
	ENTITY_MOOSHROOM,
	ENTITY_HORSE,
	ENTITY_SQUID,
	ENTITY_BAT,
	ENTITY_ZOMBIE,
	ENTITY_CREEPER,
	ENTITY_SKELETON,
	ENTITY_ENDERMAN,
	ENTITY_FALLING
};

typedef struct Entity_t *     Entity;

struct Entity_t
{
	uint16_t next;
	uint16_t modelId;
	uint8_t  select;
	float    pos[3];
	float    motion[3];
	DATA8    tile;
};

struct EntitiesPrivate_t
{
	Entity   list;
	DATA32   usage;
	int      count, max;
	int      vao;
	int      vbo;
	int      vboLoc;
	int      vboMDAI;
	int      shader;
	int      mdaiCount;
	int      selected;
	uint8_t  dirty;
	uint16_t models[50];
	uint16_t vertices[50];
};

#endif
#endif
