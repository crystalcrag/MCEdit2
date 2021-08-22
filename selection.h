/*
 * selection.h: public function to manipulate extended selection.
 *
 * Written by T.Pierron, aug 2021
 */

#ifndef MC_SELECTION_H
#define MC_SELECTION_H

#include "maps.h"

void selectionInitStatic(int shader, DATA8 direction);
void selectionSet(APTR sitRoot, float scale, vec4 pos, int point);
void selectionSetSize(void);
void selectionRender(void);
void selectionClear(void);
void selectionCancelOperation(void);
vec  selectionGetPoints(void);
Bool selectionProcessKey(int key, int mod);
int  selectionFill(Map map, DATA32 progress, int blockId, int side, int direction);
int  selectionReplace(Map map, DATA32 progress, int blockId, int replId, int side, Bool doSimilar);

#ifdef SELECTION_IMPL     /* private stuff below */
struct Selection_t
{
	int   shader;
	int   infoLoc;        /* shader uniform location */
	int   vao;            /* GL buffer to render selection points/box */
	int   vboVertex;
	int   vboIndex;
	int   vboCount;
	int   hasPoint;       /* &1: first point set, &2: second point set */
	int   nudgePoint;     /* which point is being held in the nudge window */
	int   nudgeStep;
	vec4  firstPt;        /* coord in world space */
	vec4  secondPt;
	vec4  regionPt;
	vec4  regionSize;
	APTR  nudgeDiag;      /* SIT_DIALOG */
	APTR  nudgeSize;      /* SIT_LABEL */
	DATA8 direction;      /* from render.c: used by selection nudge */
};

#define MAX_SELECTION     1024
#define MAX_VERTEX        (8*2+36+24)
#define MAX_INDEX         ((24 + 36)*2)

#endif
#endif
