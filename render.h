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

#define NEAR_PLANE     0.1f
#define FONTSIZE       30
#define FONTSIZE_MSG   20
#define ITEMSCALE      1.3f

Bool renderInitStatic(void);
Map  renderInitWorld(STRPTR path, int renderDist);
void renderCloseWorld(void);
void renderFreeMesh(Map map, Bool clear);
void renderWorld();
void renderSetViewMat(vec4 pos, vec4 lookat, float * yawPitch);
void renderToggleDebug(int what);
void renderDebugBlock(void);
void renderPointToBlock(int mx, int my);
void renderShowBlockInfo(Bool show, int what);
void renderSetInventory(Inventory);
void renderAddModif(void);
void renderCancelModif(void);
void renderAllSaved(void);
void renderFrustum(Bool snapshot);
void renderSaveRestoreState(Bool save);
void renderResetFrustum(void);
void renderDrawMap(Map map);
void renderSetCompassOffset(float offset);
void renderSetSelectionPoint(int action);
int  renderGetTerrain(int size[2], int * texId);
int  renderInitUBO(void);
void renderSetFOV(int fov);
void renderSetFOG(int fogEnabled);
Bool renderRotatePreview(int dir);
void renderSetSelection(DATA32 points);
MapExtraData renderGetSelectedBlock(vec4 pos, int * blockModel);

enum /* possible values for <action> of renderSetSelectionPoint */
{
	RENDER_SEL_CLEAR    = 0,
	RENDER_SEL_ADDPT    = 1,
	RENDER_SEL_INIT     = 2,
	RENDER_SEL_AUTO     = 3,
	RENDER_SEL_AUTOMOVE = 4,
	RENDER_SEL_STOPMOVE = 5
};

enum /* possible values for <what> of renderToggleDebug */
{
	RENDER_DEBUG_CURCHUNK = 1,
	RENDER_DEBUG_FRUSTUM  = 2,
	RENDER_DEBUG_BRIGHT   = 4,
	RENDER_DEBUG_NOHUD    = 8
};

/* side view */
void debugSetPos(int * exitCode);
void debugWorld(void);
void debugScrollView(int dx, int dy);
void debugMoveSlice(int dz);
void debugRotateView(int dir);
void debugZoomView(int x, int y, int dir);
void debugBlock(int x, int y, int dump);
void debugToggleInfo(int what);
void debugLoadSaveState(STRPTR path, Bool load);

enum /* possible flags for paramter <what> of debugToggleInfo() (side view) and renderShowBlockInfo() */
{
	DEBUG_BLOCK     = 1,           /* show tooltip about block selected */
	DEBUG_LIGHT     = 2,           /* 2d view */
	DEBUG_CHUNK     = 3,           /* 2d view */
	DEBUG_SELECTION = 4,           /* force block selection */
	DEBUG_NOCLUTTER = 8,           /* remove all HuD elements */
	DEBUG_SHOWITEM  = 16           /* show entity preview and info */
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
#define UBO_LOOKAT_OFFSET          (2 * sizeof (mat4))
#define UBO_CAMERA_OFFSET          (UBO_LOOKAT_OFFSET + sizeof (vec4))
#define UBO_SUNDIR_OFFSET          (UBO_CAMERA_OFFSET + sizeof (vec4))
#define UBO_NORMALS                (UBO_SUNDIR_OFFSET + sizeof (vec4))
#define UBO_SHADING_OFFSET         (UBO_NORMALS + sizeof (vec4) * 6)
#define UBO_TOTAL_SIZE             (UBO_SHADING_OFFSET + sizeof (vec4) * 6)

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
	int     blockId;               /* block to show preview of */
	int     blockVtx;              /* nb of vertex for glDrawArrays() */
	uint8_t selFlags;              /* bitfield, see below */
	uint8_t rotationY90;           /* number of Y90 rotation to apply to blockId [0 ~ 3] */
	Extra_t extra;
};

enum                               /* bitfield for SelBlock.selFlags */
{
	SEL_POINTTO   = 1,             /* point to a block with the mouse */
	SEL_NOCURRENT = 2,             /* cannot place block */
	SEL_OFFHAND   = 4,             /* mouse hovering offhand */
	SEL_MOVE      = 8,             /* clone selection follow mouse */
	SEL_BLOCKPOS  = 16,            /* use selection.blockPos to place block */
};

struct Message_t
{
	int     chrLen;
	int     pxLen;
	uint8_t text[80];
};

struct RenderWorld_t
{
	mat4       matModel;           /* MVP mat */
	mat4       matPerspective;
	mat4       matInventoryItem;   /* ortho matrix for rendering blocks in inventory */
	vec4       camera;             /* player pos */
	GLuint     shaderBlocks;       /* compiled shaders */
	GLuint     shaderItems;
	GLuint     vaoInventory;       /* vao to draw inventory object */
	GLuint     vaoBBox;
	GLuint     vaoPreview;
	GLuint     vaoParticles;
	GLuint     vboBBoxVTX;         /* bounding box models buffer */
	GLuint     vboBBoxIdx;
	GLuint     uboShader;
	GLuint     texBlock;           /* main texture */
	GLuint     texSky;
	GLuint     vboInventoryMDAI;   /* same for inventory rendering */
	GLuint     vboInventoryLoc;
	GLuint     vboPreview;
	GLuint     vboPreviewLoc;
	GLuint     vboInventory;       /* block model for rendering inventory */
	GLuint     vboParticles;
	GLuint     fboSky;
	int        compass;            /* image id from nanovg */
	float      compassOffset;      /* pixel offset from right border to start drawing compass */
	float      yaw, pitch;
	float      scale;
	uint8_t    debug;              /* 1 if debug info is displayed (chunk boundaries) */
	uint8_t    debugInfo;          /* tooltip over block highligted (DEBUG_*) */
	uint8_t    setFrustum;         /* recompute chunk visible list */
	uint8_t    previewItem;        /* >0 == preview item being displayed */
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
	Message_t  message;            /* message at bottom left of screen */
	int        oldBlockPos[3];     /* check if we need to change tooltip message */
	APTR       blockInfo;          /* SIT_TOOLTIP */
	APTR       selWnd, libWnd;     /* SIT_DIALOG */
	APTR       editWnd;            /* SIT_DIALOG */
	ItemID_t   previewItemId;      /* id of preview item entity */
	double     animUpdate;
};

struct MeshBuffer_t                /* temporary buffer used to collect data from chunkUpdate() */
{
	ListNode   node;
	ChunkData  chunk;
	uint16_t   usage;
	uint32_t   buffer[0];          /* 64Kb: not declared here because gdb doesn't like big table */
};

enum                               /* possible values for render.previewItem */
{
	PREVIEW_NOTHING = 0,
	PREVIEW_PICKUP  = 1,
	PREVIEW_BLOCK   = 2
};

enum                               /* special index value for shading[] array (must match what's declared in uniformBlock.glsl) */
{
	SHADING_VPWIDTH    = 1,        /* width of window in px */
	SHADING_VPHEIGHT   = 2,        /* height */
	SHADING_ISINVITEM  = 3,        /* only used by items.vsh */
	SHADING_FOGDIST    = 5,        /* distance in blocks the fog will extend (0 = disabled) */
	SHADING_BRIGHTNESS = 6,        /* increase brightness of dark area */
};

/* debug info */
void debugBlockVertex(vec4 pos, int side);
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
