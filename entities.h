/*
 * entities.h : public functions to deal with entities (mob mostly)
 *
 * written by T.Pierron, apr 2021
 */


#ifndef MCENTITY_H
#define MCENTITY_H

#include "maps.h"
#include "items.h"
#include "blocks.h"

Bool entityInitStatic(void);
void entityParse(Chunk, NBTFile nbt, int offset);
void entityUnload(Chunk);
void entityAnimate(void);
void entityRender(void);
void entityDebug(int id);
void entityUpdateLight(Chunk);
void entityDeleteById(Map, int entityId);
void entityInfo(int id, STRPTR buffer, int max);
int  entityRaypick(Chunk c, vec4 dir, vec4 camera, vec4 cur, vec4 ret_pos);
void entityUpdateOrCreate(Chunk, vec4 pos, int blockId, vec4 dest, int ticks, DATA8 tile);
void entityDebugCmd(Chunk);
int  entityCount(int start);
Bool entityIter(int * entityId, vec4 pos);
int  entityGetModel(int entityId, int * vtxCount);
void entityGetItem(int entityId, Item ret);
void entityGetPos(int entityId, float ret[3]);
void entityRenderBBox(void);
int  entityCreatePlayer(void);

/* clone entities with selection */
APTR entityCopy(int vtxCount, vec4 origin, DATA16 entityIds, int maxEntities, DATA32 models, int maxModels);
void entityCopyRender(APTR duplicated);
void entityCopyDelete(APTR duplicated);
void entityCopyRelocate(APTR duplicated, vec4 pos);
void entityCopyTransform(APTR duplicated, int transform, vec4 origin, DATA16 size);
void entityCopyToMap(APTR duplicated, Map map);

VTXBBox entityGetBBox(int id);

void worldItemCreatePainting(Map, int paintingId);
void worldItemUseItemOn(Map, int entityId, ItemID_t, vec4 pos);
void worldItemPreview(vec4 camera, vec4 pos, ItemID_t);
int  worldItemCreate(Map, int itemId, vec4 pos, int side);
void worldItemUpdatePreviewPos(vec4 camera, vec4 pos);
void worldItemDeletePreview(void);
void worldItemAdd(Map);
void worldItemDup(Map map, vec info, int entityId);

#define ENTITY_END                 0xffff
#define ENTITY_PAINTINGS           0x800
#define ENTITY_ITEMFRAME           0x801
#define ENTITY_ITEMFRAME_FULL      0x802
#define ENTITY_ITEM                0x80000000  /* differentiate world item from block entity */

enum /* transform param for entityCopyTransform() */
{
	TRANSFORM_ROTATE = 0,
	TRANSFORM_MIRROR = 3
};

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
#define BANK_SIZE                  65536
#define INFO_SIZE                  56
#define LIGHT_SIZE                 24
#define ENTITY_SHIFT               8
#define ENTITY_BATCH               (1 << ENTITY_SHIFT)
#define BANK_NUM(vbo)              ((vbo) & 63)
#define MDAI_INVALID_SLOT          0xffff
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

EntityModel entityGetModelById(int modelBank);

struct Entity_t
{
	uint16_t next;                 /* first ENTITY_SHIFT bits: index in buffer, remain: buffer index (linked list within chunk) */
	uint16_t VBObank;              /* model id: first 6bits: bank index, remain: model index */
	uint16_t mdaiSlot;             /* GL draw index in VBObank */
	uint8_t  enflags;              /* entity with some special processing (see below) */
	uint8_t  entype;
	ItemID_t blockId;
	float    motion[3];
	float    pos[4];               /* X, Y, Z and extra info for shader */
	float    rotation[4];          /* rotation in Y, X, Z axis (radians) and scaling (shader info) */
	uint32_t light[6];             /* lighting values for S, E, N, W, T, B faces (shader info) */
	DATA8    tile;                 /* start of NBT Compound for this entity */
	Entity   ref;
	STRPTR   name;                 /* from NBT ("id" key) */
};

enum                               /* possible values for Entity_t.entype */
{
	ENTYPE_FRAME     = 1,          /* item frame (no items in it) */
	ENTYPE_FILLEDMAP = 2,          /* blockId contains map id to use on disk (data/map_%d.dat) */
	ENTYPE_FRAMEITEM = 3,          /* blockId contains item/block id within the frame */
};

enum                               /* possible flags for Entity_t.enflags */
{
	ENFLAG_POPIFPUSHED = 1,        /* if pushed by piston: convert to item */
	ENFLAG_FIXED       = 2,        /* can't be pushed by piston */
	ENFLAG_FULLLIGHT   = 4,        /* lighting similar to SOLID voxel */
};

struct EntityEntry_t               /* HashTable entry */
{
	ItemID_t id;                   /* model id */
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
	float    maxSize;              /* max dimension from <bbox>, to quickly cull entities */
	uint16_t first;                /* position in vboModel */
	uint16_t count;                /* vertex count */
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
	int          selectedId;       /* entity id */
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

Entity entityAlloc(uint16_t * entityLoc);
Entity entityGetById(int id);
int    entityGetModelId(Entity);
int    entityGetModelBank(ItemID_t);
void   entityAddToCommandList(Entity);
void   entityResetModel(Entity);
void   entityGetLight(Chunk, vec4 pos, DATA32 light, Bool full, int debugLight);
void   entityMarkListAsModified(Map, Chunk);
int    entityAddModel(ItemID_t, int cnx, CustModel cust);
void   entityDeleteSlot(int slot);
void   entityUpdateInfo(Entity, Chunk checkIfChanged);

#endif
#endif
