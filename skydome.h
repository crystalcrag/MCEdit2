/*
 * skydome.h : public and private definitions for rendering sky
 *
 * Written by T.Pierron, feb 2021.
 */


#ifndef SKYDOME_H
#define SKYDOME_H

#include "utils.h"

Bool skydomeInit(void);
void skydomeRender(void);
void skydomeMoveSun(int sunMove);
void skydomeGetSunPos(vec4 pos);

/*
 * private stuff below
 */

struct SkyDome_t
{
	float sunAngle;
	int   shader;
	int   vao;
	int   vbo, vboIndices;
	int   vertex, indices;
	int   texTint, texTint2;
	int   texClouds, texClouds2;
	int   texMoon, texSun;
};

#endif
