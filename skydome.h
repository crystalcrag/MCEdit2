/*
 * skydome.h : public and private definitions for rendering sky
 *
 * Written by T.Pierron, feb 2021.
 */


#ifndef SKYDOME_H
#define SKYDOME_H

#include "utils.h"

Bool skydomeInit(void);
void skydomeRender(int fboSky, int underWater);
void skydomeMoveSun(int sunMove);
void skydomeGetSunPos(vec4 pos);

/*
 * private stuff below
 */
#ifdef SKYDOME_IMPL
struct SkyDome_t
{
	float sunAngle;
	int   shader;
	int   vao;
	int   vbo, vboIndices;
	int   vertex, indices;
	int   texTint, texTint2;
	int   texMoon, texSun;
	int   texLightShade;
	int   uniformTime, uniformTexOnly;

	/* 6 sides of a cube will have a different lighting effect */
	DATA8 lightingTex;
	float interpolate[16];
	float sunLightColor[3];
	float moonLightColor[3];
	float dawnDuskGlowColor[3];
	float blockLightColor[3];
};
#endif

#define SKYDOME_FBO_SIZE       256
#define DARK_OVERWORLD         0.03518437967f   // pow(0.8, 15)
#define DARK_NETHER            0.2058910429f    // pow(0.9, 15)
#define LIGHTING_PATCH         18
#define LIGHTING_STRIDE        (16*4)

#endif
