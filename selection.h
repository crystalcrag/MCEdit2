/*
 * selection.h: public functions to manipulate extended selection.
 *
 * Written by T.Pierron, aug 2021
 */

#ifndef MC_SELECTION_H
#define MC_SELECTION_H

#include "maps.h"
#include "SIT.h"

void selectionInitStatic(int shader);
void selectionLoadState(INIFile);
void selectionSaveState(STRPTR path);
void selectionSetPoint(float scale, vec4 pos, int point);
void selectionIterTE(SIT_CallProc cb, APTR data);
void selectionIterEntities(SIT_CallProc cb, APTR data);
void selectionSetSize(void);
void selectionRender(void);
void selectionCancel(void);
void selectionCancelOperation(void);
vec  selectionGetPoints(void);
int  selectionHasPoints(void);
Map  selectionAllocBrush(uint16_t sizes[3]);
Bool selectionProcessKey(int command, int key, int mod);
void selectionAutoSelect(vec4 pos, float scale);
void selectionSelect(vec4 pos, float scale);
int  selectionFill(DATA32 progress, int blockId, int side, int direction);
int  selectionReplace(DATA32 progress, int blockId, int replId, int side, Bool doSimilar);
int  selectionFillWithShape(DATA32 progress, int blockId, int shape, vec4 size, int direction);
int  selectionCylinderAxis(vec4 size, int direction);
Map  selectionClone(vec4 toPos, int side, Bool genMesh);
Map  selectionCopy(void);
Map  selectionCopyShallow(void);
void selectionUseBrush(Map brush, Bool dup);
void selectionSetClonePt(vec4 pos, int side);
int  selectionCancelClone(SIT_Widget w, APTR cd, APTR ud);
int  selectionCopyBlocks(SIT_Widget w, APTR cd, APTR ud);
void selectionFreeBrush(Map brush);
void selectionGetRange(int points[6], Bool relative);

enum /* flags for <shape> parameter of function selectionFillWithShape() */
{
	SHAPE_SPHERE   = 0,
	SHAPE_CYLINDER = 1,
	SHAPE_DIAMOND  = 2,

	/* these flags can be or'ed */
	SHAPE_HOLLOW   = 0x10,
	SHAPE_OUTER    = 0x20,
	SHAPE_FILLAIR  = 0x40,

	/* used by cylinder shape */
	SHAPE_AXIS_W   = 0x100,
	SHAPE_AXIS_L   = 0x200,
	SHAPE_AXIS_H   = 0x400
};

enum /* special values for <side> parameter of selectionSetClonePt() */
{
	SEL_CLONEPT_IS_SET  = -1,  /* no need to reset clonePt[] */
	SEL_CLONEOFF_IS_SET = -2,  /* no need to reset editbox offset */
	SEL_CLONEMOVE_STOP  = 128  /* stop clone selection from following mouse */
};

enum /* selection pointId */
{
	SEL_POINT_1     = 0,       /* first point (yellow) */
	SEL_POINT_2     = 1,       /* second point (blue) */
	SEL_POINT_BOX   = 2,       /* white rectangle around selection */
	SEL_POINT_CLONE = 3        /* green rectangle around brush */
};

/* repurpose some unused field for Map_t */
#define sharedBanks            path[MAX_PATHLEN-1]
#define BRUSH_ENTITIES(map)    ((APTR) (map)->dirty)
#define BRUSH_SETENT(map,ent)  ((map)->dirty = (APTR) (ent))

#ifdef SELECTION_IMPL          /* private stuff below */
struct Selection_t
{
	int      shader;           /* selection.vsh/fsh */
	int      infoLoc;          /* shader uniform location */
	int      vao;              /* GL buffer to render selection points/box */
	int      vboVertex;
	int      vboIndex;
	int      vboLOC;
	uint8_t  nudgePoint;       /* which point is being held in the nudge window */
	uint8_t  nudgeStep;
	Mutex    wait;             /* used by asynchronous actions (fill/replace/brush) */
	vec4     firstPt;          /* coord in world space */
	vec4     secondPt;
	vec4     regionPt;
	vec4     regionSize;
	vec4     clonePt;          /* where the cloned selection is */
	vec4     cloneSize;
	int      cloneOff[3];      /* value for clone selection window */
	int      cloneRepeat;
	int      copyAir;
	int      copyWater;
	int      copyEntity;
	uint8_t  loadSettings[4];  /* check if settings have changed on exit */
	STRPTR   ext[4];           /* directionnal dependant icon for roll button */
	APTR     nudgeDiag;        /* SIT_DIALOG */
	APTR     editBrush;        /* SIT_DIALOG */
	APTR     nudgeSize;        /* SIT_LABEL */
	APTR     brushSize;        /* SIT_LABEL */
	APTR     brushOff[3];      /* SIT_EDITBOX */
	Map      brush;            /* mesh for cloned selection */
};

#define SEL_INIT_STAT          64

struct SelEntities_t           /* gather some stat about selected entities */
{
	int      nbVertex;
	int      nbIds,  nbModels;
	int      maxIds, maxModels;
	uint16_t buffer[SEL_INIT_STAT];
	uint32_t modelIds[SEL_INIT_STAT];
	DATA16   ids;
	DATA32   models;
};

typedef struct SelEntities_t * SelEntities;

#define MAX_REPEAT             128
#define MAX_SELECTION          1024 /* blocks */
#define MAX_VERTEX             (8*2+(36+24)*2)
#define MAX_INDEX              ((24 + 36)*2)
#define VTX_EPSILON            0.005f

#endif
#endif
