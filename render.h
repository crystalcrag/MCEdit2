/*
 * render.h : public function to render a voxel world
 *
 * written by T.Pierron, july 2020
 */

#ifndef MC_RENDER
#define MC_RENDER

#include "maps.h"
#include "player.h"
#include "utils.h"

#define NEAR_PLANE     0.1
#define DEF_FOV        80
#define FONTSIZE       30
#define FONTSIZE_MSG   20
#define ITEMSCALE      1.3

Bool renderInitStatic(int width, int height, APTR sitRoot);
Map  renderInitWorld(STRPTR path, int renderDist);
void renderWorld();
void renderSetViewMat(vec4 pos, vec4 lookat, float * yawPitch);
void renderToggleDebug(int what);
void renderDebugBlock(void);
void renderPointToBlock(int mx, int my);
void renderShowBlockInfo(Bool show, int what);
void renderSetInventory(Inventory);
void renderAddModif(void);
void renderAllSaved(void);
void renderFrustum(Bool snapshot);
void renderResetViewport(void);
void renderSaveRestoreState(Bool save);
void renderResetFrustum(void);
void renderDrawMap(Map map, vec4 pos);
int  renderSetSelectionPoint(int action);
int  renderGetTerrain(int size[2]);
int  renderGetFacingDirection(void);
MapExtraData renderGetSelectedBlock(vec4 pos, int * blockModel);

enum /* possible values for <action> of renderSetSelectionPoint */
{
	RENDER_SEL_CLEAR    = 0,
	RENDER_SEL_ADDPT    = 1,
	RENDER_SEL_COMPLETE = 2,
	RENDER_SEL_INIT     = 3,
	RENDER_SEL_AUTO     = 4,
};

enum /* possible values for <what> of renderToggleDebug */
{
	RENDER_DEBUG_CURCHUNK = 1,
	RENDER_DEBUG_FRUSTUM  = 2
};

/* side view */
void debugSetPos(APTR sitroot, int * exitCode);
void debugWorld(void);
void debugScrollView(int dx, int dy);
void debugMoveSlice(int dz);
void debugRotateView(int dir);
void debugZoomView(int x, int y, int dir);
void debugBlock(int x, int y);
void debugToggleInfo(int what);
void debugLoadSaveState(STRPTR path, Bool load);

enum /* possible flags for paramter <what> of debugToggleInfo() (side view) */
{
	DEBUG_BLOCK = 1,
	DEBUG_LIGHT = 2,
	DEBUG_CHUNK = 3,
	DEBUG_SELECTION = 4,
};

/* chunk transfer to GPU */
void renderInitBuffer(ChunkData cd, WriteBuffer, WriteBuffer);
void renderFinishMesh(Map map, Bool updateVtxSize);
void renderFreeArray(ChunkData);

/* house keeping */
void renderClearBank(Map map);
void renderAddToBank(ChunkData);
void renderAllocCmdBuffer(Map map);

void renderItems(Item items, int count, float scale);

#define UBO_BUFFER_INDEX           2 /* must match what's declared in uniformBlock.glsl */
#define UBO_MVMATRIX_OFFSET        (sizeof (mat4))
#define UBO_VECLIGHT_OFFSET        (2 * sizeof (mat4))
#define UBO_CAMERA_OFFFSET         (2 * sizeof (mat4) + sizeof (vec4))
#define UBO_SHADING_OFFSET         (2 * sizeof (mat4) + sizeof (vec4) * 8)

#ifdef RENDER_IMPL /* private */
#include "maps.h"
#include "models.h"


typedef struct MeshBuffer_t *      MeshBuffer;
typedef struct SelBlock_t          SelBlock_t;
typedef struct MapExtraData_t      Extra_t;
typedef struct Message_t           Message_t;

struct SelBlock_t
{
	GLuint  shader;                /* compiled shader */
	vec4    current;               /* cursor pointing to this block */
	vec4    blockPos;              /* recommended block position */
	int     sel;                   /* which block is valid: &1 = current, &2 = first, &4 = second */
	int     blockId;               /* block to show preview of */
	int     blockVtx;              /* nb of vertex for glDrawArrays() */
	Extra_t extra;
};

enum                               /* bitfield for SelBlock.sel */
{
	SEL_CURRENT   = 1,
	SEL_FIRST     = 2,
	SEL_SECOND    = 4,
	SEL_NOCURRENT = 8,             /* cannot place block */
	SEL_OFFHAND   = 16
};

#define SEL_BOTH                   (SEL_FIRST|SEL_SECOND)

struct Message_t
{
	int     chrLen;
	int     pxLen;
	uint8_t text[80];
};

struct RenderWorld_t
{
	int        width, height;      /* glViewport */
	Map        level;              /* map being rendered */
	mat4       matModel;           /* MVP mat */
	mat4       matPerspective;
	mat4       matMVP;             /* model-view-projection combined matrix */
	mat4       matInvMVP;          /* inverse of matMVP (raypicking and frustum culling will need this) */
	mat4       matInventoryItem;   /* ortho matrix for rendering blocks in inventory */
	vec4       lightPos;
	vec4       curLightPos;
	vec4       camera;             /* player pos */
	GLuint     shaderBlocks;       /* compiled shaders */
	GLuint     shaderParticles;
	GLuint     shaderItems;
	GLuint     vaoInventory;       /* vao to draw inventory object */
	GLuint     vaoBBox;
	GLuint     vaoPreview;
	GLuint     vaoParticles;
	GLuint     vboBBoxVTX;         /* bounding box models buffer */
	GLuint     vboBBoxIdx;
	GLuint     uboShader;
	GLuint     texBlock;           /* main texture */
	GLuint     vboInventoryMDAI;   /* same for inventory rendering */
	GLuint     vboInventoryLoc;
	GLuint     vboPreview;
	GLuint     vboPreviewLoc;
	GLuint     vboInventory;       /* block model for rendering inventory */
	GLuint     vboParticles;
	int        custMDAIsize;       /* vboCustMDAI size in draw calls */
	int        custBlocks;         /* nb of custom block models to render */
	DATA16     instanceLoc;        /* instance location in instanceCount, indexed by glObjectId */
	DATA16     instanceCount;      /* instance count */
	DATA16     instanceIds;        /* instance id to draw count times (glObjectId)  */
	int        instanceSize;       /* size in bytes of both arrays */
	float *    custLoc;            /* vertex attrib divisor 1 array for cust models */
	int        custMax;            /* custLoc[] array capacity (in attributes = 4 floats) */
	APTR       nvgCtx;             /* nanovg context */
	int        compass;            /* image id from nanovg */
	float      yaw, pitch;
	float      scale;
	uint8_t    debug;              /* 1 if debug info is displayed (chunk boundaries) */
	uint8_t    debugInfo;          /* tooltip over block highligted */
	uint8_t    direction;          /* player facing direction: 0:south, 1:east, 2:north, 3:west */
	uint8_t    setFrustum;         /* recompute chunk visible list */
	int        debugFont;          /* font id from nanovg (init by SITGL) */
	int        debugTotalTri;      /* triangle count being drawn */
	int        mouseX, mouseY;
	SelBlock_t selection;          /* extra information about block highlighted */
	Inventory  inventory;          /* player inventory (to render inventory bar) */
	int        nvgTerrain;         /* texture for blocks as a NVG image */
	int        invCache;
	int        invCount;
	int        invExt;
	Item       toolbarItem;        /* item being hovered by mouse */
	int        modifCount;         /* displayed at bottom of screen */
	Message_t  message;            /* message at bottom of screen */
	APTR       blockInfo;          /* SIT_TOOLTIP */
	APTR       sitRoot;            /* SIT_APP */
};

struct MeshBuffer_t                /* temporary buffer used to collect data from chunkUpdate() */
{
	ListNode   node;
	ChunkData  chunk;
	uint16_t   usage;
	uint32_t   buffer[0];          /* 64Kb: not declared here because gdb doesn't like big table */
};

/* debug info */
void debugBlockVertex(Map, SelBlock_t *);
void debugInit(void);
void debugShowChunkBoundary(Chunk cur, int Y);
void debugCoord(APTR vg, vec4 camera, int total);
void debugPoint(vec4 pos);
void debugLine(vec4 p1, vec4 p2);
void debugRender(void);


/*
 * store mesh of chunks into banks so that they can be rendered with very little OpenGL draw calls.
 */

#define MEMITEM                    512

/* private definition */
typedef struct GPUBank_t *         GPUBank;
typedef struct GPUMem_t *          GPUMem;

struct GPUMem_t                    /* one allocation with GPUBank */
{
	union
	{
		ChunkData cd;              /* chunk at this location (if size>0) */
		int       next;            /* next free block (if size<0) */
	}	block;
	int size;                      /* in bytes (<0 = free) */
	int offset;                    /* avoid scanning the whole list */
};

struct GPUBank_t                   /* one chunk of memory */
{
	ListNode  node;
	int       memAvail;            /* in bytes */
	int       memUsed;             /* in bytes */
	GPUMem    usedList;            /* array of memory range in use */
	int       maxItems;            /* max items available in usedList */
	int       nbItem;              /* number of items in usedList */
	int       vaoTerrain;          /* VERTEX_ARRAY_OBJECT */
	int       vboTerrain;          /* VERTEX_BUFFER_ARRAY */
	int       vboLocation;         /* VERTEX_BUFFER_ARRAY (divisor 1) */
	int       vboMDAI;             /* glMultiDrawArrayIndirect buffer for solid/quad voxels */
	int       vtxSize;             /* chunk to render in this bank according to frustum */
	int       vboLocSize;          /* current size allocated for vboLocation */
	int       firstFree;           /* item id with first free index */
	MDAICmd   cmdBuffer;           /* mapped GL buffer */
	float *   locBuffer;
	int       cmdTotal;
	int       cmdAlpha;
};
#endif
#endif
