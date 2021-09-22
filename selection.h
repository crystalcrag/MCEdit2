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
void selectionSetPoint(float scale, vec4 pos, int point);
void selectionSetSize(void);
void selectionRender(void);
void selectionCancel(void);
void selectionCancelOperation(void);
vec  selectionGetPoints(void);
int  selectionHasPoints(void);
Map  selectionAllocBrush(uint16_t sizes[3]);
Bool selectionProcessKey(int key, int mod);
void selectionAutoSelect(vec4 pos, float scale);
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

#define sharedBanks            path[MAX_PATHLEN-1]

#ifdef SELECTION_IMPL          /* private stuff below */
struct Selection_t
{
	int      shader;
	int      shaderBlocks;
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
	vec4     clonePt;
	vec4     cloneSize;
	int      cloneOff[3];
	int      cloneRepeat;
	int      copyAir;
	int      copyWater;
	int      copyEntity;
	STRPTR   ext[4];           /* directionnal dependant icon for roll button */
	APTR     nudgeDiag;        /* SIT_DIALOG */
	APTR     editBrush;        /* SIT_DIALOG */
	APTR     nudgeSize;        /* SIT_LABEL */
	APTR     brushSize;        /* SIT_LABEL */
	APTR     brushOff[3];      /* SIT_EDITBOX */
	Map      brush;            /* mesh for cloned selection */
};

#define MAX_REPEAT             128
#define MAX_SELECTION          1024 /* blocks */
#define MAX_VERTEX             (8*2+(36+24)*2)
#define MAX_INDEX              ((24 + 36)*2)
#define VTX_EPSILON            0.005

#endif
#endif
