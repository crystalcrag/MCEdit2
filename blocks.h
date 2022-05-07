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
typedef struct ENTBBox_t *     ENTBBox;
typedef struct BlockOrient_t * BlockOrient;
typedef uint32_t               ItemID_t;

Bool    blockCreate(const char * file, STRPTR * keys, int lineNum);
void    blockParseConnectedTexture(void);
void    blockParseBoundingBox(void);
void    blockParseInventory(int vbo);
APTR    blockPostProcessTexture(DATA8 * data, int * w, int * h, int bpp);
void    blockCenterModel(DATA16 vertex, int count, int U, int V, int faceId, Bool shiftY, DATA16 sizes);
int     blockGetConnect(BlockState, DATA16 neighbors);
VTXBBox blockGetBBox(BlockState);
VTXBBox blockGetBBoxForVertex(BlockState);
int     blockGetConnect4(DATA16 neighbors, int type);
int     blockGenVertexBBox(BlockState b, VTXBBox box, int flag, int * vbo, int texCoord, int offsets);
int     blockInvGetModelSize(int id);
Bool    blockGetBoundsForFace(VTXBBox, int face, vec4 V0, vec4 V1, vec4 offset, int cnxFlags);
Bool    blockIsSolidSide(int blockId, int side);
Bool    blockIsAttached(int blockId, int side, Bool def);
Bool    blockIsSideHidden(int blockId, DATA16 face, int side);
int     blockAdjustPlacement(int blockId, BlockOrient info);
int     blockAdjustOrient(int blockId, BlockOrient info, vec4 inter);
int     blockGenModel(int vbo, int blockId);
int     blockModelStairs(DATA16 buffer, int blockId);
int     blockCountModelVertex(float * vert, int count);
DATA8   blockGetDurability(float dura);
DATA8   blockCreateTileEntity(int blockId, vec4 pos, APTR arg);
DATA16  blockParseModel(float * values, int count, DATA16 buffer, int forceRot90);
Bool    blockParseModelJSON(vec table, int max, STRPTR value);
void    blockGetEmitterLocation(int blockId, float offset[5]);
int     blockInvCountVertex(DATA16 model, int faceId);
int     blockInvCopyFromModel(DATA16 ret, DATA16 model, int faceId);
int     blockAdjustInventory(int blockId);
Bool    blockGetAlphaTex(DATA8 bitmap, int U, int V);

//void    halfBlockGenMesh(WriteBuffer, DATA8 model, int size /* 2 or 8 */, DATA8 xyz, BlockState, DATA16 blockIds, DATA8 skyBlock, int genSides);
DATA8   halfBlockGetModel(BlockState, int size /* 1, 2 or 8 */, DATA16 blockIds);
void    halfBlockGetBBox(DATA16 blockIds, VTXBBox array, int max);
void    halfBlockInit(void);

struct Block_t                   /* per id information */
{
	uint16_t id;                 /* [0-255] */
	int8_t   type;               /* enum */
	int8_t   inventory;          /* enum */

	int8_t   invState;
	int8_t   category;           /* enum */
	int8_t   bbox;               /* enum */
	int8_t   bboxPlayer;         /* enum */

	int8_t   particle;           /* enum */
	uint8_t  tall;               /* this block will need more than 1 voxel */
	uint8_t  emitLight;          /* value of light emitted [0-15] */
	uint8_t  opacSky;            /* reduction of skyLight [0-15] */

	uint8_t  opacLight;          /* reduction of blockLight [0-15] */
	uint8_t  orientHint;         /* auto-orient based on camera angle (ORIENT_*) */
	uint8_t  tileEntity;         /* type of tile entity (TILE_*) */
	uint8_t  special;            /* enum BLOCK_* */

	uint8_t  rswire;             /* redstone wire can attach to this block */
	uint8_t  rsupdate;           /* update state if redstone signal change around block (RSUPDATE_*) */
	uint8_t  copyModel;          /* copy invmodel from this block id (init phase only) */
	uint8_t  placement;          /* allowed blocks this one can be placed on (index in blocks.placements) */

	uint8_t  gravity;            /* block affected by gravity */
	uint8_t  pushable;           /* can be pushed by piston or retracted by sticky piston (PUSH_*) */
	uint8_t  updateNearby;       /* 6 nearby blocks can be changed if block is placed/deleted (chunk meshing optimization if not) */
	uint8_t  bboxIgnoreBit;      /* ignore some states for player bounding box (fence gate) */

	uint8_t  containerSize;      /* number of items this container can contains */

	uint16_t emitInterval;       /* particles emitter interval in millisec */
	uint16_t particleTTL;        /* minimum particle life time in millisec */

	float    density;            /* entity/particle physics */
	float    viscosity;          /* semi-solid block with reduced gravity */
	float    friction;           /* ground physics */
	STRPTR   name;               /* description as displayed to user */
	STRPTR   tech;               /* technical name as stored in NBT */
	DATA16   model;              /* custom inventory model */
	DATA8    emitters;           /* particle emitter locations */
};

struct BlockState_t              /* information per block state (32bytes) */
{
	uint16_t id;                 /* block id + meta data */
	uint8_t  type;               /* enum */
	uint8_t  ref;                /* reference model if reused (state - ref) */

	STRPTR   name;               /* description */

	uint8_t  nzU, nzV, pxU, pxV; /* tex coord per face (S, E, N, W, T, B) */
	uint8_t  pzU, pzV, nxU, nxV;
	uint8_t  pyU, pyV, nyU, nyV;

	uint16_t rotate;             /* rotation to apply to tex coord */
	uint8_t  special;            /* BLOCK_* */
	uint8_t  inventory;          /* (Block_t.inventory << 4) | Block_t.category (0 if NONE) */

	DATA16   custModel;
	uint16_t invId;              /* vbo slot for inventory */
	uint16_t bboxId;             /* index in BlockPrivate.bbox array */
};

struct BlockOrient_t             /* blockAdjustOffset() extra parameters */
{
	uint16_t pointToId;
	uint8_t  direction;
	uint8_t  side;
	uint8_t  topHalf;
	uint8_t  keepPos;
	float    yaw;
};

struct BlockSides_t              /* convert block data into SIDE_* enum */
{
	uint8_t torch[8];            /* side within the block it is attached */
	uint8_t lever[8];            /* buttons and lever: where it is attached (within its block) */
	uint8_t sign[8];             /* wall sign only */
	uint8_t piston[8];           /* where extended part is (note: also observed face for observer and block pointed by hopper) */
	uint8_t repeater[4];         /* side where power is coming from (to get where it is output to, XOR the value with 2) */
	uint8_t SWNE[4];             /* generic orient */
};

struct VTXBBox_t                 /* used to store custom bounding box */
{
	uint16_t pt1[3];             /* lowest coord of box */
	uint16_t pt2[3];             /* highest coord of box */
	uint8_t  flags;              /* models with optional parts */
	uint8_t  cont;               /* 1 if more bbox for this model */
	uint8_t  sides;              /* bitfield for faces that are defined: S, E, N, W, T, B */
	uint8_t  aabox;              /* contain only axis-aligned box */
};

struct ENTBBox_t                 /* entity/player bbox: use float and are always axis aligned */
{
	float    pt1[3];
	float    pt2[3];
	int      push;
};

enum                             /* values for Block.type */
{
	INVIS,                       /* nothing to render (air, block 36...) */
	SOLID,                       /* competely opaque: can hide inner blocks */
	TRANS,                       /* alpha is either 0 or 255 (can be rendered with OPAQUE, but do not hide inner) */
	QUAD,                        /* block that are 2 quads crossing (flowers, crops, ...) */
	CUST                         /* arbitrary triangles: need special models/processing */
};

enum                             /* values for Block_t.special */
{
	BLOCK_NORMAL,
	BLOCK_CHEST,                 /* !ender: check for double chest */
	BLOCK_DOOR,                  /* need to convert metadata */
	BLOCK_HALF,                  /* half slab */
	BLOCK_STAIRS,                /* ... */
	BLOCK_GLASS,                 /* pane */
	BLOCK_FENCE,                 /* wooden fence */
	BLOCK_FENCE2,                /* nether fence */
	BLOCK_WALL,                  /* cobble walls */
	BLOCK_RSWIRE,                /* redstone wire */
	BLOCK_LEAVES,                /* leaves: nocull and special skyLight/blockLight */
	BLOCK_LIQUID,                /* water/lava */
	BLOCK_DOOR_TOP,              /* top part of a door */
	BLOCK_TALLFLOWER,            /* weird implementation from minecraft */
	BLOCK_RAILS,                 /* check for connected rails */
	BLOCK_TRAPDOOR,
	BLOCK_SIGN,                  /* need extra special processing for rendering */
	BLOCK_PLATE,                 /* pressure plate */
	BLOCK_SOLIDOUTER,            /* custom model with solid cube as outer face (slime block) */
	BLOCK_JITTER,                /* add a small random XYZ offset to block mesh (QUAD only) */
	BLOCK_POT,                   /* flower pot: need to check for tile entity */
	BLOCK_LASTSPEC,
	BLOCK_BED,

	/* these flags are set on BlockState, but not on Block */
	BLOCK_DUALSIDE  = 32,        /* back face should not be culled */
	BLOCK_CNXTEX    = 64,        /* relocate texture to connected texture row */
	BLOCK_NOCONNECT = 128,       /* SOLID blocks for which connected models should not connect or CUST that have no connected models */
};

#define BLOCK_FENCEGATE          (BLOCK_FENCE|BLOCK_NOCONNECT)

enum                             /* possible values for block_t.particle */
{
	PARTICLE_NONE,
	PARTICLE_BITS,               /* bits of texture from blocks, exploding */
	PARTICLE_SMOKE,              /* cycle through texture located at 31, 9, moving up in the air */
	PARTICLE_DUST,               /* smoke particle going down using texture of block */
	PARTICLE_DRIP,               /* similar to dust, but slowly emerge from ceiling */
	PARTICLE_MAX                 /* sentinel */
};

enum                             /* values for BlockState.pxU if BlockState.type == QUAD */
{
	QUAD_CROSS,                  /* flower, grass, crops ... */
	QUAD_CROSS2,                 /* internal: 2nd part of cross */
	QUAD_SQUARE,                 /* crops usually */
	QUAD_SQUARE2,                /* internal: 2nd face of square */
	QUAD_SQUARE3,
	QUAD_SQUARE4,
	QUAD_NORTH,                  /* attach to north side (on the inside) */
	QUAD_SOUTH,
	QUAD_EAST,
	QUAD_WEST,
	QUAD_BOTTOM,
	QUAD_ASCE,                   /* rails: ascending E, W, N, S */
	QUAD_ASCW,
	QUAD_ASCN,
	QUAD_ASCS,
};

enum                             /* orientation method: Block.orientHint */
{
	ORIENT_NONE,
	ORIENT_LOG,
	ORIENT_FULL,                 /* dispenser */
	ORIENT_BED,
	ORIENT_SLAB,
	ORIENT_TORCH,
	ORIENT_STAIRS,
	ORIENT_NSWE,                 /* chest */
	ORIENT_SWNE,                 /* terracotta */
	ORIENT_DOOR,
	ORIENT_RAILS,                /* rails */
	ORIENT_SE,
	ORIENT_LEVER,
	ORIENT_SNOW,
	ORIENT_VINES,
	ORIENT_HOPPER
};

enum                             /* editable tile entity XXX deprecated */
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

enum                             /* code returned by blockAdjustPlacement() */
{
	PLACEMENT_NONE   = 0,
	PLACEMENT_OK     = 1,
	PLACEMENT_WALL   = 0xff00,
	PLACEMENT_GROUND = 0xfe00,
	PLACEMENT_SOLID  = 0xfd00,
};

enum                             /* possible values for <side> parameter of blockIsSolidSide() */
{
	SIDE_SOUTH,
	SIDE_EAST,
	SIDE_NORTH,
	SIDE_WEST,
	SIDE_TOP,
	SIDE_BOTTOM
};

enum                             /* possible for Block_t.pushable */
{
	NOPUSH,                      /* block is fixed */
	PUSH_ONLY,                   /* can only be pushed, not retracted by sticky (glazed terracotta) */
	PUSH_DESTROY,                /* block will be destroyed on push (flower, crops) */
	PUSH_DROPITEM,               /* block will be removed and droped as an item */
	PUSH_AND_RETRACT,            /* default value */
};

enum                             /* common redstone devices */
{
	RSDISPENSER    = 23,
	RSNOTEBLOCK    = 25,   // TODO
	RSPOWERRAILS   = 27,
	RSDETECTORRAIL = 28,
	RSSTICKYPISTON = 29,
	RSPISTON       = 33,
	RSPISTONHEAD   = 34,
	RSPISTONEXT    = 36,
	RSWIRE         = 55,
	RSRAILS        = 66,
	RSLEVER        = 69,
	RSTORCH_OFF    = 75,
	RSTORCH_ON     = 76,
	RSREPEATER_OFF = 93,
	RSREPEATER_ON  = 94,
	RSLAMP         = 123,
	RSCOMPARATOR   = 149,
	RSBLOCK        = 152,
	RSHOPPER       = 154,
	RSDROPPER      = 158,
	SLIMEBLOCK     = 165,
	RSOBSERVER     = 218
};

#define HOPPER_COOLDOWN           4
#define blockGetByIdData(id,data) (blockStates + blockStateIndex[((id) << 4) | (data)])
#define blockGetById(id)          (blockStates + blockStateIndex[id])
#define ID(id, data)              (((id) << 4) | (data))
#define SIDE_NONE                 0
#define ITEMID_FLAG               0x10000
#define isBlockId(itemId)         (((itemId) & ITEMID_FLAG) == 0)
#define blockIsFullySolid(state)  (state->type == SOLID && state->special != BLOCK_HALF && state->special != BLOCK_STAIRS)

#define MAXSKY        15   /* maximum values for SkyLight table */
#define MAXLIGHT      15   /* maximum value for light emitter */

enum                       /* values for Block.bbox */
{
	BBOX_NONE,             /* can't target block = no box */
	BBOX_AUTO,             /* SOLID, TRANS and QUAD = auto box */
	BBOX_MAX,              /* CUST: union of all boxes = max box */
	BBOX_FULL,             /* CUST: keep all boxes from custom model = full box */
	BBOX_FIRST             /* CUST: only take first primitive from model */
};

enum                       /* flags for Block.inventory (render type) */
{
	CUBE3D = 1<<4,         /* unit cube */
	ITEM2D = 2<<4,         /* flat quad */
	MODEL  = 3<<4,         /* dedicated object */
};

enum                       /* flags for Block.inventory (category) */
{
	ALLCAT   = 0,
	BUILD    = 1,
	DECO     = 2,
	REDSTONE = 3,
	CROPS    = 4,          /* or food */
	RAILS    = 5,
	FILLBY   = 6           /* useful in fill by block/geomtric brush only */
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

/* terrain vertex */
#define VERTEX_DATA_SIZE   28        /* 7 * uint32_t */
#define VERTEX_INSTANCE    16        /* per instance size: 3 floats + 1 uint */
#define VERTEX_INT_SIZE    7

/* everything else vertex */
#define BYTES_PER_VERTEX   10
#define INT_PER_VERTEX     5
#define BASEVTX            2048
#define ORIGINVTX          15360
#define MIDVTX             (1 << 13)
#define TOVERTEXint(x)     ((x) >> 11)
#define RELDX(x)           (VERTEX(x) + MIDVTX - X1)
#define RELDY(x)           (VERTEX(x) + MIDVTX - Y1)
#define RELDZ(x)           (VERTEX(x) + MIDVTX - Z1)
#define VERTEX(x)          ((x) * BASEVTX + ORIGINVTX)
#define FROMVERTEX(x)      ((((float)(x)) - ORIGINVTX) * (1.f/BASEVTX))
#define STATEFLAG(b,x)     ((b)->rotate & x)
#define SPECIALSTATE(b)    ((b)->special & 31)
#define CATFLAGS           0x0f
#define MODELFLAGS         0xf0
#define FACEIDSHIFT        8
#define ALLFACEIDS         0xffffff

#define PAINTINGS_TILE_W   16
#define PAINTINGS_TILE_H   9
#define PAINTINGS_TILE_X   16
#define PAINTINGS_TILE_Y   (32+14)
#define UNDERWATER_TILE_X  14
#define UNDERWATER_TILE_Y  14

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
	VTXBBox  bboxExact;    /* boubding box fro exact collision detection, indexed by Block_t.bboxId */
	int      totalVtx;     /* mem allocated for custom model data (mostly debug info) */
	int      bboxMax;      /* max bounding box in array */
	int      maxVtxCust;   /* max number of vertices for custom models */
	int      curVtxCount;  /* vertex count stored in lastModel */
	STRPTR   curFile;      /* current file being parsed in blockParse() (mostly for error reporting) */
	DATA8    alphaTex;     /* keep a bitmap (1bit) version of alpha part of terrain.png */
	int      alphaStride;  /* width of alphaTex in bytes */
	DATA8    duraColors;   /* durability color from terrain.png (RGBA) */
	int      duraMax;      /* number of colors in <duraColors> */
	float *  lastModel;    /* hack when building blockState list */
	uint8_t  cnxTex[128];  /* quadruplet describing which texture to generate connected info and where */
	int      cnxCount;
	uint16_t modelKeep;    /* needed when building blockState list */
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

enum /* special tags in model definition */
{
	BHDR_FACES    = 1,
	BHDR_CUBEMAP  = 2,
	BHDR_DETAIL   = 3,
	BHDR_INHERIT  = 4,
	BHDR_SIZE     = 5,
	BHDR_TR       = 6,
	BHDR_ROT      = 7,
	BHDR_ROTCAS   = 8,
	BHDR_REF      = 9,
	BHDR_ROT90    = 10,
	BHDR_TEX      = 11,
	BHDR_INVERT   = 12,
	BHDR_INCFACE  = 13,
	BHDR_NAME     = 14,
	BHDR_DUALSIDE = 15,
	BHDR_MAXTOK   = 16
};

#define BHDR_FUSED               0x80
#define BHDR_FUSE                0x1000
#define BHDR_CONTINUE            0x100
#define BHDR_ROT90SHIFT          9
#define BHDR_INCFACEID           (1<<17)
#define SAME_AS                  -100
#define COPY_MODEL               1e6f

/* these are for the 10 bytes per vertex model, NOT the 28 bytes per quad */
#define GET_UCOORD(vertex)       ((vertex)[3] & 511)
#define GET_VCOORD(vertex)       ((((vertex)[3] & (127<<9)) >> 6) | ((vertex)[4] & 7))
#define GET_NORMAL(vertex)       (((vertex)[4] >> 3) & 7)

#define SET_UVCOORD(vertex,U,V)  ((vertex)[3] = (U) | ((V) & ~7) << 6, (vertex)[4] = (V) & 7)
#define CHG_UVCOORD(vertex,U,V)  ((vertex)[3] = (U) | ((V) & ~7) << 6, (vertex)[4] = ((vertex)[4] & ~7) | ((V) & 7))

#define STR_POOL_SIZE            (4096 - offsetof(struct BlockVertex_t, buffer))

#define NEW_BBOX                 0x8000

extern struct Block_t            blockIds[];
extern struct BlockState_t *     blockStates;
extern struct BlockState_t *     blockLast;
extern struct BlockSides_t       blockSides;
extern uint16_t                  blockStateIndex[];
extern uint8_t                   blockTexResol;   /* resolution of textures in terrain.png; default is 16 */

extern uint8_t cubeVertex[];
extern uint8_t cubeIndices[];
extern int8_t  cubeNormals[];
extern uint8_t mask8bit[];
extern uint8_t texCoord[];

#endif
