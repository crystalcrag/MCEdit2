/*
 * blocks.h : public function and datatypes to get block information per id
 *
 * Written by T.Pierron, jan 2020
 */

#ifndef BLOCKS_H
#define BLOCKS_H

#include "utils.h"

typedef struct Block_t *       Block;
typedef struct BlockState_t *  BlockState;
typedef struct VTXBBox_t *     VTXBBox;
typedef struct WriteBuffer_t * WriteBuffer;
typedef struct BlockOrient_t * BlockOrient;

Bool    blockCreate(const char * file, STRPTR * keys, int lineNum);
void    blockParseConnectedTexture(void);
void    blockParseBoundingBox(void);
void    blockParseInventory(int vbo);
void    blockPostProcessTexture(DATA8 * data, int * w, int * h, int bpp);
void    blockCenterModel(DATA16 vertex, int count, int U, int V, VTXBBox bbox);
int     blockGetConnect(BlockState, DATA8 neighbors);
VTXBBox blockGetBBox(BlockState);
int     blockGetConnect4(DATA8 neighbors, int type);
int     blockGenVertexBBox(BlockState b, VTXBBox box, int flag, int * vbo);
int     blockInvGetModelSize(int id);
Bool    blockGetBoundsForFace(VTXBBox, int face, vec4 V0, vec4 V1, vec4 offset, int cnxFlags);
Bool    blockIsSolidSide(int blockId, int side);
Bool    blockIsAttached(int blockId, int side);
Bool    blockIsSideHidden(int blockId, DATA16 face, int side);
int     blockAdjustPlacement(int blockId, BlockOrient info);
int     blockAdjustOrient(int blockId, BlockOrient info, vec4 inter);
int     blockGenModel(int vbo, int blockId);
int     blockCountModelVertex(float * vert, int count);
DATA8   blockGetDurability(float dura);
DATA8   blockCreateTileEntity(int blockId, vec4 pos, APTR arg);
DATA16  blockParseModel(float * values, int count, DATA16 buffer);
void    blockGetEmitterLocation(int blockId, float offset[5]);

void    halfBlockGenMesh(WriteBuffer, DATA8 model, int size /* 2 or 8 */, DATA8 xyz, DATA8 tex, DATA16 blockIds);
DATA8   halfBlockGetModel(BlockState, int size /* 1, 2 or 8 */, DATA16 blockIds);
void    halfBlockInit(void);

struct Block_t             /* per id information */
{
	uint16_t id;           /* [0-255] */
	int8_t   type;         /* enum */
	int8_t   inventory;    /* enum */
	int8_t   invState;
	int8_t   category;     /* enum */
	int8_t   bbox;         /* enum */
	int8_t   bboxPlayer;   /* enum */
	int8_t   particle;     /* enum */
	uint8_t  states;
	uint8_t  emitLight;    /* value of light emitted 0 ~ 15 */
	uint8_t  opacSky;      /* reduction of skyLight */
	uint8_t  opacLight;    /* reduction of blockLight */
	uint8_t  orientHint;   /* auto-orient based on camera angle */
	uint8_t  tileEntity;   /* type of tile entity (TILE_*) */
	uint8_t  special;      /* enum BLOCK_* */
	uint8_t  rswire;       /* redstone wire can attach to this block */
	uint8_t  rsupdate;     /* update state if redstone signal change around block */
	uint8_t  copyModel;    /* copy invmodel from this block id */
	uint8_t  placement;    /* allowed blocks this one can be placed on (index in blocks.placements) */
	uint8_t  gravity;      /* block affected by gravity */
	STRPTR   name;         /* description as displayed to user */
	STRPTR   tech;         /* technical name as stored in NBT */
	DATA16   model;        /* custom inventory model */
	DATA8    emitters;     /* particle emitter locations */
};

struct BlockState_t        /* information per block state (32bytes) */
{
	uint16_t id;           /* block id + meta data */
	uint8_t  type;         /* enum */
	uint8_t  ref;          /* reference model if reused (state - ref) */

	STRPTR   name;         /* description */

	uint8_t  nzU, nzV, pxU, pxV, pzU, pzV, nxU, nxV, pyU, pyV, nyU, nyV; /* tex coord per face (S, E, N, W, T, B) */

	uint16_t rotate;       /* rotation to apply to tex coord */
	uint8_t  special;      /* BLOCK_* */
	uint8_t  inventory;

	DATA16   custModel;
	uint16_t invId;        /* vbo slot for inventory */
	uint16_t bboxId;       /* index in BlockPrivate.bbox array */
};

struct BlockOrient_t       /* blockAdjustOffset() extra parameters */
{
	uint16_t pointToId;
	uint8_t  direction;
	uint8_t  side;
	uint8_t  topHalf;
	uint8_t  keepPos;
	float    yaw;
};

struct BlockSides_t        /* convert block data into SIDE_* enum */
{
	uint8_t torch[8];      /* side within the block it is attached */
	uint8_t lever[8];      /* buttons and lever: where it is attached (within its block) */
	uint8_t sign[8];       /* wall sign only */
	uint8_t piston[8];     /* where extended part is */
	uint8_t repeater[4];   /* side where power is coming from (to get where it is output to, XOR the value with 2) */
	uint8_t SWNE[4];       /* generic orient */
};

struct VTXBBox_t           /* used to store custom bounding box */
{
	uint16_t pt1[3];       /* lowest coord of box */
	uint16_t pt2[3];       /* highest coord of box */
	uint8_t  flags;        /* models with optional parts */
	uint8_t  cont;         /* 1 if more bbox for this model */
	uint8_t  sides;        /* bitfield for faces that are defined: S, E, N, W, T, B */
	uint8_t  aabox;        /* contain only axis-aligned box */
};

struct WriteBuffer_t
{
	DATA16 start, end;
	DATA16 cur;
	int    alpha;
	void (*flush)(WriteBuffer);
};

enum                       /* values for Block_t.special */
{
	BLOCK_NORMAL,
	BLOCK_CHEST,           /* !ender: check for double chest */
	BLOCK_DOOR,            /* need to convert metadata */
	BLOCK_NOSIDE,          /* render without cull face enabled (QUAD only) */
	BLOCK_HALF,            /* half slab */
	BLOCK_STAIRS,          /* ... */
	BLOCK_GLASS,           /* pane */
	BLOCK_FENCE,           /* wooden fence */
	BLOCK_FENCE2,          /* nether fence */
	BLOCK_WALL,            /* cobble walls */
	BLOCK_RSWIRE,          /* redstone wire */
	BLOCK_LEAVES,          /* leaves: nocull and special skyLight/blockLight */
	BLOCK_LIQUID,          /* water/lava */
	BLOCK_DOOR_TOP,        /* top part of a door */
	BLOCK_TALLFLOWER,      /* weird implementation from minecraft */
	BLOCK_RAILS,           /* check for connected rails */
	BLOCK_TRAPDOOR,
	BLOCK_SIGN,            /* need extra special processing for rendering */
	BLOCK_PLATE,           /* pressure plate */
	BLOCK_LASTSPEC,
	BLOCK_BED,
	BLOCK_CNXTEX    = 64,  /* relocate texture to connected texture row */
	BLOCK_NOCONNECT = 128, /* SOLID blocks for which connected models should not connect or CUST that have no connected models */
};

#define BLOCK_FENCEGATE    (BLOCK_FENCE|BLOCK_NOCONNECT)

enum                       /* possible values for block_t.particle */
{
	PARTICLE_NONE,
	PARTICLE_BITS,         /* bits of texture from blocks, exploding */
	PARTICLE_SMOKE,        /* cycle through texture located at 31, 9, moving up in the air */
	PARTICLE_NETHER        /* nether particle coming toward the block (ender chest) */
};

enum                       /* values for BlockState.pxU if BlockState.type == QUAD */
{
	QUAD_CROSS,            /* flower, grass, crops ... */
	QUAD_CROSS2,
	QUAD_NORTH,            /* attach to north side (on the inside) */
	QUAD_SOUTH,
	QUAD_EAST,
	QUAD_WEST,
	QUAD_BOTTOM,
	QUAD_ASCE,             /* rails: ascending E, W, N, S */
	QUAD_ASCW,
	QUAD_ASCN,
	QUAD_ASCS
};

enum                       /* orientation method: Block.orientHint */
{
	ORIENT_NONE,
	ORIENT_LOG,
	ORIENT_FULL,           /* dispenser */
	ORIENT_BED,
	ORIENT_SLAB,
	ORIENT_TORCH,
	ORIENT_STAIRS,
	ORIENT_SENW,           /* chest */
	ORIENT_SWNE,           /* terracotta */
	ORIENT_DOOR,
	ORIENT_SE,             /* fence gate */
	ORIENT_LEVER,
	ORIENT_SNOW
};

enum                       /* editable tile entity XXX deprecated */
{
	TILE_DISPENSER = 1,
	TILE_INV,
	TILE_DBLINV,
	TILE_FURNACE,
	TILE_BED,
	TILE_BREWING,
	TILE_ANVIL,
	TILE_SPAWNER,
	TILE_SIGN,
	TILE_HOPPER
};

enum                       /* code returned by blockAdjustPlacement() */
{
	PLACEMENT_NONE   = 0,
	PLACEMENT_OK     = 1,
	PLACEMENT_WALL   = 0xff00,
	PLACEMENT_GROUND = 0xfe00,
	PLACEMENT_SOLID  = 0xfd00,
};

enum                       /* possible values for <side> parameter of blockIsSolidSide() */
{
	SIDE_SOUTH,
	SIDE_EAST,
	SIDE_NORTH,
	SIDE_WEST,
	SIDE_TOP,
	SIDE_BOTTOM
};

enum                       /* common redstone devices */
{
	RSDISPENSER    = 23,
	RSNOTEBLOCK    = 25,
	RSPOWERRAILS   = 27,
	RSSTICKYPISTON = 29,
	RSPISTON       = 33,
	RSPISTONHEAD   = 34,
	RSPISTONEXT    = 36,
	RSWIRE         = 55,
	RSLEVER        = 69,
	RSTORCH_OFF    = 75,
	RSTORCH_ON     = 76,
	RSREPEATER_OFF = 93,
	RSREPEATER_ON  = 94,
	RSLAMP         = 123,
	RSBLOCK        = 152,
	RSHOPPER       = 154,
	RSDROPPER      = 158,
	RSOBSERVER     = 218
};

#define blockGetByIdData(id,data) (blockStates + blockStateIndex[((id) << 4) | (data)])
#define blockGetById(id)          (blockStates + blockStateIndex[id])
#define ID(id, data)              (((id) << 4) | (data))
#define TYPE(block)               (block->type)
#define SIDE_NONE                 0

#define MAXSKY        15   /* maximum values for SkyLight table */
#define MAXLIGHT      15   /* maximum value for light emitter */

enum                       /* values for Block.bbox */
{
	BBOX_NONE,             /* can't target block = no box */
	BBOX_AUTO,             /* SOLID, TRANS and QUAD = auto box */
	BBOX_MAX,              /* CUST: union of all boxes = max box */
	BBOX_FULL              /* CUST: keep all boxes from custom model = full box */
};

enum                       /* values for Block.type */
{
	INVIS,                 /* nothing to render (air, block 36...) */
	SOLID,                 /* competely opaque: can hide inner blocks */
	TRANS,                 /* alpha is either 0 or 255 (can be rendered with OPAQUE, but do not hide inner) */
	QUAD,                  /* block that are 2 quads crossing (flowers, crops, ...) */
	LIKID,                 /* lava and water XXX need to be removed */
	CUST                   /* arbitrary triangles: need special models/processing */
};

enum                       /* flags for Block.inventory (render type) */
{
	CUBE = 1<<4,           /* unit cube */
	ITEM = 2<<4,           /* flat quad */
	MODL = 3<<4,           /* dedicated object */
};

enum                       /* flags for Block.inventory (category) */
{
	ALLCAT   = 0,
	BUILD    = 1,
	DECO     = 2,
	REDSTONE = 3,
	CROPS    = 4,          /* or food */
	RAILS    = 5
};

enum                       /* special flags in BlockState.rotate */
{
	TRIMNAME = 1<<13,      /* use name from Block_t, not BlockState_t */
	CNXTEX   = 1<<14,      /* adjust U tex to simulate connection */
	ALPHATEX = 1<<15       /* this block use alpha, stored in alpha banks */
};

enum                       /* special values for Block_t.rswire */
{
	ALLDIR    = 1,
	FRONTBACK = 2,
	BACKONLY  = 3
};

#define BYTES_PER_VERTEX   10
#define INT_PER_VERTEX     5
#define BASEVTX            2048      /* 65536/32 */
#define ORIGINVTX          15360     /* 7.5 * 2048 */
#define VERTEX(x)          ((x) * BASEVTX + ORIGINVTX)
#define STATEFLAG(b,x)     ((b)->rotate & x)
#define SPECIALSTATE(b)    ((b)->special & 31)
#define CATFLAGS           0x0f
#define MODELFLAGS         0xf0
#define FACEIDSHIFT        8
#define ALLFACEIDS         0xffffff

/*
 * mostly private stuff below that point
 */

struct BlockPrivate_t      /* static info kept in blocks.c */
{
	int      totalStates;  /* total block states */
	int      totalInv;     /* max items in invModelOff */
	int      vboInv;       /* VBO for inventory models */
	DATA16   invModelOff;  /* offset in VBO, indexed by Block_t.invId */
	DATA16   placements;   /* placement constraint for blocks */
	VTXBBox  bbox;         /* bounding box vertex (array), indexed by Block_t.bboxId */
	int      totalVtx;     /* mem allocated for custom model data (mostly debug info) */
	int      bboxMax;      /* max bounding box in array */
	int      maxVtxCust;   /* max number of vertices for custom models */
	int      curVtxCount;  /* vertex count stored in lastModel */
	STRPTR   curFile;      /* current file being parsed in blockParse() (mostly for error reporting) */
	DATA8    duraColors;   /* durability color from terrain.png (RGBA) */
	int      duraMax;
	float *  lastModel;
	uint8_t  cnxTex[128];  /* quadruplet describing which texture to generate connected info and where */
	int      cnxCount;
	uint16_t modelKeep;
	uint16_t modelRef[16];
	uint16_t modelCount[16];
};

typedef struct BlockVertex_t *     BlockVertex;

struct BlockVertex_t       /* store custom block model vertex data (needed by chunk meshing) */
{
	BlockVertex next;
	uint16_t    usage;
	uint16_t    max;
	uint8_t     buffer[8];
};

#define BHDR_FACESMASK     63
#define BHDR_INVERTNORM    0x40
#define BHDR_CUBEMAP       0x80
#define BHDR_FUSED         0x80
#define BHDR_FUSE          0x1000
#define BHDR_CONTINUE      0x100
#define BHDR_ROT90SHIFT    9
#define BHDR_DETAILFACES   11
#define BHDR_INCFACEID     (1<<17)
#define SAME_AS            -100
#define COPY_MODEL         1e6

#define GET_UCOORD(vertex)       ((vertex)[3] & 511)
#define GET_VCOORD(vertex)       ((((vertex)[3] & (127<<9)) >> 6) | ((vertex)[4] & 7))
#define GET_NORMAL(vertex)       (((vertex)[4] >> 3) & 7)

#define SET_UVCOORD(vertex,U,V)  ((vertex)[3] = (U) | ((V) & ~7) << 6, (vertex)[4] = (V) & 7)
#define CHG_UVCOORD(vertex,U,V)  ((vertex)[3] = (U) | ((V) & ~7) << 6, (vertex)[4] &= ~7, (vertex)[4] |= (V) & 7)

#define STR_POOL_SIZE            (4096 - offsetof(struct BlockVertex_t, buffer))

#define NEW_BBOX                 0x8000

extern struct Block_t        blockIds[];
extern struct BlockState_t * blockStates;
extern uint16_t              blockStateIndex[];

extern uint8_t vertex[]; /* from chunks.c */
extern uint8_t cubeIndices[];
extern int8_t  normals[];

#endif
