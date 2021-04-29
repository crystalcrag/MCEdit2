/*
 * entities.h : public functions to deal with entities (mob mostly)
 *
 * written by T.Pierron, apr 2021
 */


#ifndef MCENTITY_H
#define MCENTITY_H

#include "chunks.h"
#include "blocks.h"

Bool entityInitStatic(void);
void entityParse(Chunk, NBTFile nbt, int offset);
void entityUnload(Chunk);
void entityAnimate(void);
void entityRender(void);
void entityDebug(int id);
void entityInfo(int id, STRPTR buffer, int max);
int  entityRaycast(Chunk c, vec4 dir, vec4 camera, vec4 cur, vec4 ret_pos);

#define ENTITY_END         0xffff
#define PAINTING_ADDTEXU   16
#define PAINTING_ADDTEXV   (32+15)
#define ENTITY_PAINTINGID  0x10000

/* private stuff below */
#ifdef ENTITY_IMPL
#define BANK_SIZE          65536
#define INFO_SIZE          24
#define ENTITY_SHIFT       8
#define ENTITY_BATCH       (1 << ENTITY_SHIFT)

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

typedef struct Entity_t *          Entity;
typedef struct Entity_t            Entity_t;
typedef struct EntityEntry_t *     EntityEntry;
typedef struct EntityBuffer_t *    EntityBuffer;
typedef struct EntityBank_t *      EntityBank;
typedef struct EntityModel_t *     EntityModel;
typedef struct EntityHash_t        EntityHash_t;
typedef struct CustModel_t *       CustModel;

struct Entity_t
{
	uint16_t next;
	uint16_t VBObank;
	uint16_t mdaiSlot;
	uint8_t  select;
	float    pos[3];
	float    motion[3];
	float    rotation[2];
	DATA8    tile;
	STRPTR   name; /* from NBT */
};

struct EntityEntry_t    /* HashTable entry */
{
	int      id;
	uint16_t VBObank;   /* index in bank->models */
	uint16_t next;      /* hash link */
};

struct EntityHash_t
{
	EntityEntry list;
	int         count, max;
};

struct EntityBuffer_t /* entity allocated in batch (avoid realloc) */
{
	ListNode node;
	int      count;
	uint32_t usage[ENTITY_BATCH>>5];
	Entity_t entities[ENTITY_BATCH];
};

struct EntityModel_t   /* where model data is and bounding box info */
{
	VTXBBox  bbox;
	uint16_t first;
	uint16_t count;
};

struct EntityBank_t
{
	ListNode    node;
	EntityModel models;
	int         modelCount;
	uint16_t    vtxCount;
	uint8_t     dirty;
	int         vao;
	int         vboModel;
	int         vboLoc;
	int         vboMDAI;
	int         mdaiCount;
	int         mdaiMax;
	DATA32      mdaiUsage;
};

struct EntitiesPrivate_t
{
	EntityHash_t hash;
	ListHead     list;         /* EntityBuffer */
	ListHead     banks;        /* EntityBank */
	int          shader;
	Entity       selected;
	TEXT         paintings[256];
	uint8_t      paintingNum;
};

struct CustModel_t
{
	float *  model;
	int      vertex;
	uint16_t U, V;
};

#endif
#endif
