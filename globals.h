/*
 * globals.h: link to global variables that are pretty much needed everywhere in the code.
 *
 * Written by T.Pierron, sep 2021.
 */

#ifndef MCGLOBALS_H
#define MCGLOBALS_H

#include "utils.h"

/*
 * Some state variables are needed everywhere: using a function just to return a variable is kind
 * of useless, so keep them accessible in this struct, to avoid exposing too many internal details
 * from all the modules.
 */

typedef struct MCGlobals_t
{
	/*
	 * which selection points are active: bitfield of "1 << SEL_POINT_*". &1: first point, &2: second point,
	 * #8: clone brush. Used by selection.c
	 */
	uint8_t selPoints;

	/* cardimal direction player is facing: 0 = south, 1 = east, 2 = north, 3 = west */
	uint8_t direction;

	/* map being edited */
	Map level;

	/* SITGL root widget */
	APTR app;

	/* time in milliseconds for current frame */
	double curTime;

	/* easier to place break points :-/ */
	int breakPoint;

}	MCGlobals_t;

extern struct MCGlobals_t globals;

#endif
