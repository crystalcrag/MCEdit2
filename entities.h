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

typedef struct Entity_t *   Entity;
typedef struct QuadTree_t * QuadTree;

void   entityNukeAll(void);
Entity entityParse(Chunk, NBTFile nbt, int offset, Entity prev);
Bool   entityInitStatic(void);
void   entityUnload(Chunk);
void   entityAnimate(void);
void   entityRender(void);
void   entityDebug(int id);
void   entityUpdateLight(Chunk);
Bool   entityDeleteById(Map, int entityId);
void   entityInfo(int id, STRPTR buffer, int max);
int    entityRaypick(Chunk c, vec4 dir, vec4 camera, vec4 cur, vec4 ret_pos);
Entity entityUpdateOrCreate(Chunk, vec4 pos, ItemID_t blockId, vec4 dest, int ticks, DATA8 tile);
void   entityDebugCmd(void);
int    entityCount(int start);
int    entityGetModel(int entityId, int * vtxCount);
void   entityGetItem(int entityId, Item ret);
void   entityGetPos(int entityId, float ret[3]);
void   entityRenderBBox(void);
int    entityCreatePlayer(void);
int    entityGetId(Entity);
void   entityUpdateNearby(BlockIter, int blockId);
int    entityWantItem(ItemID_t);

/* clone entities with selection */
APTR entityCopy(int vtxCount, vec4 origin, DATA16 entityIds, int maxEntities, DATA32 models, int maxModels);
void entityCopyRender(APTR duplicated);
void entityCopyDelete(APTR duplicated);
void entityCopyRelocate(APTR duplicated, vec4 pos);
void entityCopyTransform(APTR duplicated, int transform, vec4 origin, DATA16 size);
void entityCopyToMap(APTR duplicated, Map map);

VTXBBox entityGetBBox(int id);

Bool worldItemCreatePainting(Map, int paintingId, vec4 pos);
void worldItemUseItemOn(Map, int entityId, ItemID_t, vec4 pos);
void worldItemPreview(vec4 camera, vec4 pos, ItemID_t);
int  worldItemCreate(Map, int itemId, vec4 pos, int side);
void worldItemUpdatePreviewPos(vec4 camera, vec4 pos);
void worldItemDeletePreview(void);
void worldItemAdd(Map);
void worldItemDup(Map map, vec info, int entityId);
void worldItemCreateFromBlock(BlockIter, int side);

void quadTreeInit(int x, int z, int size);
void quadTreeDebug(APTR vg);
Entity * quadTreeIntersect(float bbox[6], int * count, int filter);

#define UPDATE_BY_PHYSICS          -1 /* special param for <ticks> of entityUpdateOrCreate() */
#define ENTITY_END                 0xffff

enum                               /* possible flags for Entity_t.enflags */
{
	ENFLAG_POPIFPUSHED = 1,        /* if pushed by piston: convert to item */
	ENFLAG_FIXED       = 2,        /* can't be pushed by piston */
	ENFLAG_FULLLIGHT   = 4,        /* lighting similar to SOLID voxel */
	ENFLAG_OVERLAP     = 8,        /* overlap partition of a quad tree (set by quad tree, do not set manually) */
	ENFLAG_BBOXROTATED = 16,       /* don't apply rotation/scale on bbox */
	ENFLAG_INQUADTREE  = 32,       /* entity is in quad tree (will need removal) */
	ENFLAG_TEXENTITES  = 64,       /* use texture sampler for entities */
	ENFLAG_HASBBOX     = 128,      /* other entities can collide with these */
	ENFLAG_USEMOTION   = 256,      /* use entity->motion as position */
	ENFLAG_INANIM      = 512,      /* used in a animated sequence (need to remove ref when deleted) */
	ENFLAG_ITEM        = 1024,     /* differentiate world item from block entity (Entity_t.blockId) */

	ENFLAG_EQUALZERO   = 2048      /* extra <filter> parameter for quadTreeIntersect() */
};

enum /* transform param for entityCopyTransform() */
{
	TRANSFORM_ROTATE = 0,
	TRANSFORM_MIRROR = 3
};

enum /* entity id and models */
{
	ENTITY_UNKNOWN        = 0,     /* internal id, not saved in NBT */
	ENTITY_PAINTINGS      = 0x800,
	ENTITY_ITEMFRAME      = 0x801,
	ENTITY_ITEMFRAME_FULL = 0x802,
	ENTITY_MINECART       = 0x803,
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
#define ENTITY_SCALE(entity)       ((entity)->rotation[3] * (0.5f / BASEVTX))
#define ENTITY_ITEM                0x80000000

#include "physics.h"

typedef struct Entity_t            Entity_t;
typedef struct EntityEntry_t *     EntityEntry;
typedef struct EntityBuffer_t *    EntityBuffer;
typedef struct EntityBank_t *      EntityBank;
typedef struct EntityModel_t *     EntityModel;
typedef struct EntityHash_t        EntityHash_t;
typedef struct EntityAnim_t *      EntityAnim;
typedef struct EntityType_t *      EntityType;
typedef struct EntityPhysBatch_t * EntityPhysBatch;
typedef struct CustModel_t *       CustModel;
typedef struct PhysicsBBox_t *     PhysicsBBox;

typedef int (*EntityParseCb_t)(NBTFile, Entity);

EntityModel entityGetModelById(int modelBank);

struct Entity_t
{
	uint16_t next;                 /* first ENTITY_SHIFT bits: index in buffer, remain: buffer index (linked list within chunk) */
	uint16_t mdaiSlot;             /* GL draw index in VBObank */

	uint16_t VBObank;              /* model id: first 6bits: bank index, remain: model index */
	uint16_t enflags;              /* entity with some special processing (see below) */

	uint8_t  entype;
	uint16_t szx, szy, szz;        /* bbox of entity */

	Chunk    chunkRef;             /* chunk where entity is (should match what's in <pos>) */
	ItemID_t blockId;
	float    motion[3];
	float    pos[4];               /* X, Y, Z and extra info for shader */
	float    rotation[4];          /* rotation in Y, X, Z axis (radians) and scaling (shader info) */
	uint32_t light[6];             /* lighting values for S, E, N, W, T, B faces (shader info) */
	DATA8    tile;                 /* start of NBT Compound for this entity */
	Entity   ref;
	STRPTR   name;                 /* from NBT ("id" key) */
	APTR     private;              /* private stuff allocated by entity */

	/* quadtree fields */
	Entity   qnext;                /* linked list of entities in a quad tree leaf node */
	QuadTree qnode;                /* quadtree node where entity is */
};

struct QuadTree_t                  /* quadtree for entity collision check (36b) */
{
	float    x, z;
	uint16_t size;                 /* always a power of 2 */
	uint8_t  nbLeaf;               /* nb of items in quadrants[] */
	Entity   items;                /* entities that lies in this quadrant */
	QuadTree parent;               /* all "pointers" must be indirect, tree nodes will be relocated */
	QuadTree quadrants[4];         /* leaf of the quad tree */
};

enum                               /* possible values for Entity_t.entype */
{
	ENTYPE_FRAME     = 1,          /* item frame (no items in it) */
	ENTYPE_FILLEDMAP = 2,          /* blockId contains map id to use on disk (data/map_%d.dat) */
	ENTYPE_FRAMEITEM = 3,          /* blockId contains item/block id within the frame */
	ENTYPE_MINECART  = 4
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
	uint16_t first;                /* position in vboModel */
	uint16_t count;                /* vertex count */
	uint16_t texAtlas;             /* texture atlas to use */
	uint16_t bbox[3];              /* bounding box in BASEVTX unit */
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
	int         vboMDAI;           /* glMultiDrawArraysIndirect() command buffer */
	int         mdaiCount;
	int         mdaiMax;
	DATA32      mdaiUsage;
};

struct EntityType_t                /* callbacks to parse content of NBT records */
{
	STRPTR          type;          /* one callback per entity id */
	EntityParseCb_t cb;
};

struct PhysicsBBox_t               /* to alloc bbox at the same time */
{
	struct PhysicsEntity_t physics;
	struct VTXBBox_t       bbox;
};

#define PHYSBOX    struct PhysicsBBox_t

struct EntityPhysBatch_t           /* provide buffers to process physics fo a moving entity */
{
	ListNode node;
	uint32_t usage[4];
	uint8_t  count;
	PHYSBOX  mem[128];
};

struct EntitiesPrivate_t           /* static vars for entity.c */
{
	EntityHash_t hash;             /* item id => vbobank */
	ListHead     list;             /* EntityBuffer */
	ListHead     banks;            /* EntityBank */
	ListHead     physBatch;        /* EntityPhysBatch */
	EntityAnim   animate;
	EntityType   type;
	int          typeCount;
	int          typeMax;
	int          shader;
	int          texEntity;
	/* clear fields below on map exit */
	int          animCount;
	int          animMax;
	Entity       selected;
	int          selectedId;       /* entity id */
	int          initModelCount;   /* clear extra models on exit */
	int          initVtxCount;
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
	uint16_t vertex;
	uint16_t texId;
	uint16_t U, V;
};

void   entityRegisterType(STRPTR id, EntityParseCb_t);
Entity entityAlloc(uint16_t * entityLoc);
Entity entityGetById(int id);
int    entityGetModelId(Entity);
int    entityGetModelBank(ItemID_t);
void   entityAddToCommandList(Entity);
void   entityResetModel(Entity);
void   entityGetLight(Chunk, vec4 pos, DATA32 light, Bool full);
void   entityMarkListAsModified(Map, Chunk);
int    entityAddModel(ItemID_t, int cnx, CustModel cust, DATA16 sizes, int swapAxis);
void   entityDeleteSlot(int slot);
void   entityUpdateInfo(Entity, vec4 oldPos);
void   entityGetBoundsForFace(Entity entity, int face, vec4 V0, vec4 V1);
void   entityFreePhysics(Entity);
void   entityAllocPhysics(Entity);
void   entityInitMove(Entity entity, int side, float factor);

enum                  /* possible values for swapAxis of entityAddModel() */
{
	MODEL_DONT_SWAP,
	MODEL_SWAP_XZ,    /* rotation of 90/270 on Y */
	MODEL_SWAP_ZY     /* rotation of 90/270 on X */
};


void quadTreeClear(void);
void quadTreeDeleteItem(Entity item);
void quadTreeInsertItem(Entity item);
void quadTreeChangePos(Entity item);

Entity worldItemAddItemInFrame(Chunk chunk, Entity frame, int entityId);
void   worldItemInit(void);
void   worldItemDelete(Entity);
void   worldItemCreateGeneric(NBTFile nbt, Entity entity, STRPTR name);
void   worldItemCreateBlock(Entity, Bool fallingEntity);
void   worldItemPlaceOrCreate(Entity);

#endif
#endif
