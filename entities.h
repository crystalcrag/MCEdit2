/*
 * entities.h : public functions to deal with entities (mob mostly)
 *
 * written by T.Pierron, apr 2021
 */


#ifndef MCENTITY_H
#define MCENTITY_H

#include "maps.h"
#include "blocks.h"

Bool entityInitStatic(void);
void entityParse(Chunk, NBTFile nbt, int offset);
void entityUnload(Chunk);
void entityAnimate(void);
void entityRender(void);
void entityDebug(int id);
void entityUpdateLight(Chunk c);
void entityDeleteById(Map map, int id);
void entityInfo(int id, STRPTR buffer, int max);
int  entityRaycast(Chunk c, vec4 dir, vec4 camera, vec4 cur, vec4 ret_pos);
void entityUpdateOrCreate(Chunk c, vec4 pos, int blockId, vec4 dest, int ticks, DATA8 tile);
void entityUseItemOn(Map, int entityId, int itemId, vec4 pos);
void entityDebugCmd(Chunk c);
int  entityCount(int start);
int  entityCreate(Map map, int itemId, vec4 pos, int side);
void entityCreatePainting(Map map, int id);
int  entityGetBlockId(int id);

VTXBBox entityGetBBox(int id);

#define ENTITY_END                 0xffff
#define ENTITY_PAINTINGID          0x10000
#define ENTITY_ITEMFRAME           0x20000
#define ENTITY_ITEMFRAME_FULL      0x40000
#define ENTITY_ITEM                0x1000000  /* differentiate item from block entity */


enum /* entity id and models */
{
	ENTITY_UNKNOWN,
	ENTITY_PLAYER,
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

typedef struct Paintings_t         Paintings_t;
typedef union EntityUUID_t         EntityUUID_t;
struct Paintings_t
{
	TEXT    names[256];            /* comma separated list of strings */
	uint8_t location[128];         /* 4 values per painting: tile coord relative to PAINTINGS_TILE_X/Y */
	int     count;
};

union EntityUUID_t                 /* 128bits unique identifier for entities */
{
	uint8_t  uuid8[16];
	uint64_t uuid64[2];
};

extern Paintings_t paintings;      /* convert painting string id to model id */

/* private stuff below */
#ifdef ENTITY_IMPL
#define BANK_SIZE          65536
#define INFO_SIZE          56
#define LIGHT_SIZE         24
#define ENTITY_SHIFT       8
#define ENTITY_BATCH       (1 << ENTITY_SHIFT)
#define BANK_NUM(vbo)      ((vbo) & 63)
#define MDAI_INVALID_SLOT  0xffff
#define BOX(szx,szy,szz)   \
	{-szx/2 * BASEVTX + ORIGINVTX, -szy/2 * BASEVTX + ORIGINVTX, -szz/2 * BASEVTX + ORIGINVTX}, \
	{ szx/2 * BASEVTX + ORIGINVTX,  szy/2 * BASEVTX + ORIGINVTX,  szz/2 * BASEVTX + ORIGINVTX}
#define BOXCY(szx,szy,szz) \
	{-szx/2 * BASEVTX + ORIGINVTX,                 ORIGINVTX, -szz/2 * BASEVTX + ORIGINVTX}, \
	{ szx/2 * BASEVTX + ORIGINVTX, szy * BASEVTX + ORIGINVTX,  szz/2 * BASEVTX + ORIGINVTX}

typedef struct Entity_t *          Entity;
typedef struct Entity_t            Entity_t;
typedef struct EntityEntry_t *     EntityEntry;
typedef struct EntityBuffer_t *    EntityBuffer;
typedef struct EntityBank_t *      EntityBank;
typedef struct EntityModel_t *     EntityModel;
typedef struct EntityHash_t        EntityHash_t;
typedef struct EntityAnim_t *      EntityAnim;
typedef struct CustModel_t *       CustModel;
typedef struct BBoxBuffer_t *      BBoxBuffer;

struct Entity_t
{
	uint16_t next;                 /* first ENTITY_SHIFT bits: index in buffer, remain: buffer index (linked list within chunk) */
	uint16_t VBObank;              /* first 6bits: bank index, remain: model index */
	uint16_t mdaiSlot;             /* GL draw index in VBObank */
	uint8_t  map;                  /* 1 if it contains a map (uses a different model) */
	uint8_t  fullLight;
	int      blockId;
	float    motion[3];
	float    pos[4];               /* X, Y, Z and extra info for shader */
	float    rotation[4];          /* rotation in Y, X, Z axis (radians) and scaling */
	uint32_t light[6];
	DATA8    tile;                 /* start of NBT Compound for this entity */
	Entity   ref;
	STRPTR   name;                 /* from NBT ("id" key) */
};

struct EntityEntry_t               /* HashTable entry */
{
	int      id;                   /* model id */
	uint16_t VBObank;              /* index in bank->models */
	uint16_t next;                 /* hash link */
};

struct EntityHash_t
{
	EntityEntry list;
	int         count, max;
};

struct EntityBuffer_t              /* entity allocated in batch (avoid realloc) */
{
	ListNode node;
	int      count;
	uint32_t usage[ENTITY_BATCH>>5];
	Entity_t entities[ENTITY_BATCH];
};

struct EntityModel_t               /* where model data is and bounding box info */
{
	VTXBBox  bbox;                 /* from block models */
	uint16_t first;                /* position in vboModel */
	uint16_t count;
};

struct EntityBank_t                /* contains models up to 65536 vertices (BANK_SIZE) */
{
	ListNode    node;
	EntityModel models;            /* array, capacity in modelCount */
	int         modelCount;        /* item in array */
	uint16_t    vtxCount;          /* current nb of vertices in vboModel */
	uint8_t     dirty;             /* vboLoc and vboMDAI need to be redone for this bank */
	int         vao;               /* gl buffers needed by glMultiDrawArraysIndirect() */
	int         vboModel;          /* VBO to quote models from */
	int         vboLoc;            /* meta data for entity (glAttribDivisor == 1) */
	int         vboMDAI;           /* MultiDrawArrayIndirect command buffer */
	int         mdaiCount;
	int         mdaiMax;
	DATA32      mdaiUsage;
};

struct EntitiesPrivate_t           /* static vars for entity.c */
{
	EntityHash_t hash;
	ListHead     list;             /* EntityBuffer */
	ListHead     banks;            /* EntityBank */
	ListHead     bbox;             /* BBoxBuffer */
	EntityAnim   animate;
	int          animCount;
	int          animMax;
	int          shader;
	Entity       selected;
	int          selectedId;
	vec4         createPos;
	uint8_t      createSide;
};

struct EntityAnim_t
{
	int      stopTime;
	int      prevTime;
	Entity   entity;
};

struct CustModel_t
{
	float *  model;
	int      vertex;
	uint16_t U, V;
	VTXBBox  bbox;
};

struct BBoxBuffer_t
{
	ListNode         node;
	struct VTXBBox_t bbox[ENTITY_BATCH];
	int              count;
};

#endif
#endif
