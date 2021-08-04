/*
 * selection.h: public function to manipulate extended selection.
 *
 * Written by T.Pierron, aug 2021
 */

#ifndef MC_SELECTION_H
#define MC_SELECTION_H

void selectionInitStatic(int shader);
void selectionSet(vec4 pos, int point);
void selectionRender(void);
void selectionClear(void);
vec  selectionGetPoints(void);

#ifdef SELECTION_IMPL     /* private stuff below */
struct Selection_t
{
	int  shader;
	int  infoLoc;
	int  vao;
	int  vboVertex;
	int  vboIndex;
	int  vboCount;
	int  hasPoint;        /* &1: first point set, &2: second point set */
	vec4 firstPt;
	vec4 secondPt;
	vec4 regionPt;
	vec4 regionSize;
};

#define MAX_SELECTION     1024
#define MAX_VERTEX        (8*2+36+24)
#define MAX_INDEX         ((24 + 36)*2)

#endif
#endif
