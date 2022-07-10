/*
 * skydome.c : init and render everything related to the sky (sun, moon, clouds, stars, ...).
 *             also manage the per-face texture shading (taken for eihort actually).
 *
 * Written by T.Pierron, feb 2021
 */

#define SKYDOME_IMPL
#include <glad.h>
#include <malloc.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include "models.h"
#include "maps.h"
#include "render.h"
#include "skydome.h"
#include "globals.h"

static struct SkyDome_t skydome;

static void updateLightModels(float sunAngle, float dark);

void skydomeGetSunPos(vec4 pos)
{
	pos[0] = cosf(skydome.sunAngle);
	pos[1] = sinf(skydome.sunAngle);
	pos[2] = -0.25;
	pos[3] = 1;
	vecNormalize(pos, pos);
}

Bool skydomeInit(void)
{
	/* sky dome */
	Model model = modelSphere(FAR_PLANE/2, 10);

	skydome.shader = createGLSLProgram("skydome.vsh", "skydome.fsh", NULL);
	if (! skydome.shader)
		return False;

	glGenVertexArrays(1, &skydome.vao);
	glGenBuffers(1, &skydome.vbo);
	glBindVertexArray(skydome.vao);
	skydome.vertex   = model->vertex;
	skydome.indices  = model->index;
	skydome.sunAngle = M_PI_2f;

	skydome.uniformTime    = glGetUniformLocation(skydome.shader, "time");
	skydome.uniformTexOnly = glGetUniformLocation(skydome.shader, "skyTexOnly");

	/* vertex data */
	glBindBuffer(GL_ARRAY_BUFFER, skydome.vbo);
	glBufferData(GL_ARRAY_BUFFER, model->vertex * 12, model->vertices, GL_STATIC_DRAW);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0);
	glEnableVertexAttribArray(0);
	glBindVertexArray(0);

	/* indirect vertex */
	glGenBuffers(1, &skydome.vboIndices);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, skydome.vboIndices);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, model->index * 2, model->indices, GL_STATIC_DRAW);
	modelFree(model);

	skydome.texTint  = textureLoad(RESDIR SKYDIR, "tint.png",    1, NULL);
	skydome.texTint2 = textureLoad(RESDIR SKYDIR, "tint2.png",   1, NULL);
	skydome.texSun   = textureLoad(RESDIR SKYDIR, "sun.png",     1, NULL);

	glActiveTexture(TEX_TINTSKY1); glBindTexture(GL_TEXTURE_2D, skydome.texTint);
	glActiveTexture(TEX_TINTSKY2); glBindTexture(GL_TEXTURE_2D, skydome.texTint2);
	glActiveTexture(TEX_SUN);      glBindTexture(GL_TEXTURE_2D, skydome.texSun);

	/* lightShadeTex */
	glGenTextures(1, &skydome.texLightShade);
	glActiveTexture(TEX_LIGHTSHADE);
	glBindTexture(GL_TEXTURE_2D, skydome.texLightShade);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 16, LIGHTING_PATCH * 6, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

	glActiveTexture(TEX_DEFAULT);

	float arg = 0;
	setShaderValue(skydome.shader, "time",      1, &arg);
	setShaderValue(skydome.shader, "sun_angle", 1, &skydome.sunAngle);

	int i;
	/* curve applied to raw skylight and blocklight values */
	for (i = 0; i < 16; i++)
	{
		arg = i / 15.0f;
		skydome.interpolate[i] = arg * arg;
	}
	/* gdb sucks at displaying big tables :-/ */
	skydome.lightingTex = malloc(16 /*WIDTH*/ * 18*6 /*HEIGHT*/ * 4 /*RGBA*/);

	/*
	 * those values will define how skylight/blocklight will affect shading of each faces of a cube:
	 * - sunLightColor: when skylight is at max, and blockLight at min.
	 * - moonLightColor: applied when middle of night.
	 * - dawnDuskGlowColor: slihgtly yellow-ish tint, due to atmospheric scattering.
	 * - blockLightColor: tint for artifical lighting.
	 */
	memcpy(skydome.sunLightColor,     (vec4) {0.99, 0.99, 0.99}, 12);
	memcpy(skydome.moonLightColor,    (vec4) {59/255., 53/255., 78/255.}, 12);
	memcpy(skydome.dawnDuskGlowColor, (vec4) {0.5*0x9b/255, 0.5*0x40/255, 0.5*0x16/255 }, 12);
	memcpy(skydome.blockLightColor,   (vec4) {1.7, 1.39, 1}, 12);

	updateLightModels(0, DARK_OVERWORLD);


	return True;
}

void skydomeMoveSun(int sunMove)
{
	vec4 sunPos;
	skydome.sunAngle += sunMove & 1 ? -0.01f : 0.01f;
	skydomeGetSunPos(sunPos);
	setShaderValue(skydome.shader, "sun_angle", 1, &skydome.sunAngle);
	updateLightModels(skydome.sunAngle - M_PI_2f, DARK_OVERWORLD);

	glBindBuffer(GL_UNIFORM_BUFFER, globals.uboShader);
	glBufferSubData(GL_UNIFORM_BUFFER, UBO_SUNDIR_OFFSET, sizeof (vec4), sunPos);
}

void skydomeRender(int fboSky, int underWater)
{
	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	glEnable(GL_CULL_FACE);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glFrontFace(GL_CW);
	glBindVertexArray(skydome.vao);
	glUseProgram(skydome.shader);

	float time = globals.curTime * 0.0002;
	glProgramUniform1fv(skydome.shader, skydome.uniformTime,    1, &time); time = 1;
	glProgramUniform1fv(skydome.shader, skydome.uniformTexOnly, 1, &time); time = 0;

	/* some functions will replace the default FBO with an offscreen one, we'll have to restore it */
	GLint defFBO;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &defFBO);

	/* first, only render the sky in a small texture */
	glViewport(0, 0, SKYDOME_FBO_SIZE, SKYDOME_FBO_SIZE);
	glBindFramebuffer(GL_FRAMEBUFFER, fboSky);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, skydome.vboIndices);
	glDrawElements(GL_TRIANGLES, skydome.indices, GL_UNSIGNED_SHORT, 0);

	/* then the full sky */
	glViewport(0, 0, globals.width, globals.height);
	glBindFramebuffer(GL_FRAMEBUFFER, defFBO);
	glProgramUniform1fv(skydome.shader, skydome.uniformTexOnly, 1, &time);
	glDrawElements(GL_TRIANGLES, skydome.indices, GL_UNSIGNED_SHORT, 0);
}

/*
 * manage per-face texture shading: the code is more or a less a rip off from eihort viewer (lightmodel.cpp
 * and mapview.lua).
 *
 * orinally written by Jason Lloyd-Price, adapted by T.Pierron, july 2022.
 */

static inline float clampf(float val, float min, float max)
{
	if (val < min) return min;
	if (val > max) return max;
	return val;
}

/*
 * Returns the "strength" of light coming from the given angle
 * on normal faces, west-facing faces, and east-facing faces
 */
static void setLightModel(int dir, float sunStrength, float moonStrength, float glow, float skyPower, float ambient)
{
	float dark[] = {ambient, ambient, ambient, 1};
	float skyDelta[] = {
		sunStrength * skydome.sunLightColor[0] + moonStrength * skydome.moonLightColor[0] + glow * skydome.dawnDuskGlowColor[0] - dark[0],
		sunStrength * skydome.sunLightColor[1] + moonStrength * skydome.moonLightColor[1] + glow * skydome.dawnDuskGlowColor[1] - dark[1],
		sunStrength * skydome.sunLightColor[2] + moonStrength * skydome.moonLightColor[2] + glow * skydome.dawnDuskGlowColor[2] - dark[2],
		skyPower*1.2f - dark[3]
	};
	float blockDelta[] = {
		skydome.blockLightColor[0] - dark[0],
		skydome.blockLightColor[1] - dark[1],
		skydome.blockLightColor[2] - dark[2],
		0
	};

	skyPower *= 1.2f;

	/* regen texture */
	DATA8 px, line;
	int   x, y, i;
	for (y = 0, px = line = skydome.lightingTex + LIGHTING_STRIDE + dir * LIGHTING_PATCH * LIGHTING_STRIDE; y < 16; y++)
	{
		for(x = 0; x < 16; x++, px += 4)
		{
			for (i = 0; i < 4; i ++)
			{
				float v = skyDelta[i]   * skydome.interpolate[y] + dark[i] +
				          blockDelta[i] * skydome.interpolate[x] * fmaxf(0.0f, 1.0f - skyPower * skydome.interpolate[y]);
				px[i] = 255.0f * clampf(v, 0, 1);
			}
		}
	}
	/* repeating top and bottom line will simulate a GL_CLAMP on a sub-texture */
	memcpy(line - LIGHTING_STRIDE, line, LIGHTING_STRIDE);
	memcpy(px, px - LIGHTING_STRIDE, LIGHTING_STRIDE);
}

static void getStrength(float angle, float strengthNWE[3])
{
	angle = fmodf(angle + M_PIf, M_PIf*2) - M_PIf;
	float strength = powf(clampf(cosf(angle) * 0.8f + 0.2f, 0, 1), 0.8f);
	float mask = powf(strength, 0.8f);
	strengthNWE[0] = strength;
	strengthNWE[1] = clampf(cosf((angle - M_PI_2f)*0.9f) * 0.4f + 0.6f, 0, 1) * mask;
	strengthNWE[2] = clampf(cosf((angle + M_PI_2f)*0.9f) * 0.4f + 0.6f, 0, 1) * mask;
}

/*
 * lighting models for the overworld: sunAngle varies between -pi and pi.
 * 0 = noon, -pi/2 = sunset, pi/2 = sunrise
 */
static void updateLightModels(float sunAngle, float dark)
{
	float strengthSunNWE[3];
	float strenghtMoonNWE[3];

	getStrength(sunAngle, strengthSunNWE);
	getStrength(sunAngle + M_PIf, strenghtMoonNWE);


	/* Glow factors for dawn/dusk */
	float wGlow = fmaxf(0,  powf(sinf(sunAngle), 3) - 0.05f);
	float eGlow = fmaxf(0, -powf(sinf(sunAngle), 3) - 0.05f);

	// Z- (north), Z+ (south)
	#define ADJUST     (0.8f*0.8f)
	setLightModel(0, strengthSunNWE[0]*ADJUST, strenghtMoonNWE[0]*ADJUST, 0, strengthSunNWE[0], dark);
	setLightModel(2, strengthSunNWE[0]*ADJUST, strenghtMoonNWE[0]*ADJUST, 0, strengthSunNWE[0], dark);
	#undef ADJUST

	// X- (west), X+ (east)
	setLightModel(3, strengthSunNWE[1] * 1.1f, strenghtMoonNWE[1], wGlow*1.1f, strengthSunNWE[0], dark);
	setLightModel(1, strengthSunNWE[2] * 1.1f, strenghtMoonNWE[2], eGlow*1.1f, strengthSunNWE[0], dark);
	// Y- (down)
	#define ADJUST     (0.8f*0.8f*0.8f)
	setLightModel(5, strengthSunNWE[0]*ADJUST, strenghtMoonNWE[0]*ADJUST, 0, strengthSunNWE[0], dark);
	#undef ADJUST
	// Y+ (up)
	setLightModel(4, strengthSunNWE[0], strenghtMoonNWE[0], 0, strengthSunNWE[0], dark);

	/* update GL texture */
	glActiveTexture(TEX_LIGHTSHADE);
	glBindTexture(GL_TEXTURE_2D, skydome.texLightShade);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 16, LIGHTING_PATCH * 6, GL_RGBA, GL_UNSIGNED_BYTE, skydome.lightingTex);

	glActiveTexture(TEX_DEFAULT);
}

