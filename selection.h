/*
 * selection.h: public function to manipulate extended selection.
 *
 * Written by T.Pierron, aug 2021
 */

#ifndef MC_SELECTION_H
#define MC_SELECTION_H

#include "maps.h"

void selectionInitStatic(int shader, DATA8 direction);
void selectionSetPoint(APTR sitRoot, float scale, vec4 pos, int point);
void selectionSetSize(void);
void selectionRender(void);
void selectionClear(void);
void selectionCancelOperation(void);
vec  selectionGetPoints(void);
Bool selectionProcessKey(int key, int mod);
void selectionAutoSelect(Map map, vec4 pos, APTR sitRoot, float scale);
int  selectionFill(Map map, DATA32 progress, int blockId, int side, int direction);
int  selectionReplace(Map map, DATA32 progress, int blockId, int replId, int side, Bool doSimilar);
int  selectionFillWithShape(Map map, DATA32 progress, int blockId, int shape, vec4 size, int direction);
int  selectionCylinderAxis(vec4 size, int direction);
void selectionClone(APTR sitRoot, Map map, vec4 toPos, int side);
void selectionSetClonePt(vec4 pos, int side);
Bool selectionCancelClone(void);

enum /* flags for <shape> parameter of function selectionFillWithShape() */
{
	SHAPE_SPHERE   = 0,
	SHAPE_CYLINDER = 1,
	SHAPE_DIAMOND  = 2,

	/* these flags can be or'ed */
	SHAPE_HOLLOW   = 0x10,
	SHAPE_OUTER    = 0x20,
	SHAPE_HALFSLAB = 0x40,

	/* used by cylinder shape */
	SHAPE_AXIS_W   = 0x100,
	SHAPE_AXIS_L   = 0x200,
	SHAPE_AXIS_H   = 0x400
};

enum /* special values for <side> parameter of selectionSetClonePt() */
{
	SEL_CLONEPT_IS_SET  = -1,  /* no need to reset clonePt[] */
	SEL_CLONEOFF_IS_SET = -2,  /* no need to reset editbox offset */
};

#ifdef SELECTION_IMPL        /* private stuff below */
struct Selection_t
{
	int      shader;
	int      shaderBlocks;
	int      infoLoc;        /* shader uniform location */
	int      vao;            /* GL buffer to render selection points/box */
	int      vboVertex;
	int      vboIndex;
	int      vboLOC;
	uint8_t  hasPoint;       /* &1: first point set, &2: second point set */
	uint8_t  hasClone;       /* 1 if selection has been cloned */
	uint8_t  nudgePoint;     /* which point is being held in the nudge window */
	uint8_t  nudgeStep;
	Mutex    wait;           /* used by asynchronous actions (fill/replace/brush) */
	vec4     firstPt;        /* coord in world space */
	vec4     secondPt;
	vec4     regionPt;
	vec4     regionSize;
	vec4     clonePt;
	vec4     cloneSize;
	int      cloneOff[3];
	int      cloneRepeat;
	int      copyAir;
	int      copyWater;
	int      copyBiome;
	APTR     nudgeDiag;      /* SIT_DIALOG */
	APTR     editBrush;      /* SIT_DIALOG */
	APTR     nudgeSize;      /* SIT_LABEL */
	APTR     brushOff[3];    /* SIT_EDITBOX */
	Map      brush;          /* mesh for cloned selection */
	DATA8    direction;      /* from render.c: used by selection nudge */
};

#define MAX_REPEAT           128
#define MAX_SELECTION        1024 /* blocks */
#define MAX_VERTEX           (8*2+(36+24)*2)
#define MAX_INDEX            ((24 + 36)*2)
#define VTX_EPSILON          0.005

#endif
#endif
