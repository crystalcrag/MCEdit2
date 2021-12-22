/*
 * skydome.c : init and render everything related to the sky (sun, moon, clouds, stars, ...).
 *
 * Written by T.Pierron, feb 2021
 */

#include <glad.h>
#include <malloc.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include "skydome.h"
#include "models.h"
#include "maps.h"
#include "globals.h"

static struct SkyDome_t skydome;

static void skydomeGetSunPos(vec4 pos)
{
	pos[0] = 0;
	pos[1] = sinf(skydome.sunAngle);
	pos[2] = cosf(skydome.sunAngle);
	pos[3] = 1;
}

Bool skydomeInit(void)
{
	/* sky dome */
	Model model = modelSphere(1, 40);

	skydome.shader = createGLSLProgram("skydome.vsh", "skydome.fsh", NULL);
	if (! skydome.shader)
		return False;

	glGenVertexArrays(1, &skydome.vao);
	glGenBuffers(1, &skydome.vbo);
	glBindVertexArray(skydome.vao);
	skydome.vertex  = model->vertex;
	skydome.indices = model->index;
	skydome.sunAngle = M_PI_2;

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

	skydome.texTint    = textureLoad(RESDIR SKYDIR, "tint.png",    0, NULL);
	skydome.texTint2   = textureLoad(RESDIR SKYDIR, "tint2.png",   0, NULL);
	skydome.texSun     = textureLoad(RESDIR SKYDIR, "sun.png",     0, NULL);
	skydome.texClouds  = textureLoad(RESDIR SKYDIR, "clouds1.png", 0, NULL);
	skydome.texClouds2 = textureLoad(RESDIR SKYDIR, "clouds1.png", 0, NULL);

	glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, skydome.texTint);
	glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, skydome.texTint2);
	glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_2D, skydome.texSun);
	glActiveTexture(GL_TEXTURE5); glBindTexture(GL_TEXTURE_2D, skydome.texClouds);
	glActiveTexture(GL_TEXTURE6); glBindTexture(GL_TEXTURE_2D, skydome.texClouds2);

	vec4 sunPos;
	skydomeGetSunPos(sunPos);
	setShaderValue(skydome.shader, "sun_pos", 4, sunPos); sunPos[0] = 1;
	setShaderValue(skydome.shader, "weather", 1, sunPos); sunPos[0] = 0;
	setShaderValue(skydome.shader, "time",    1, sunPos);

	return True;
}

void skydomeMoveSun(int sunMove)
{
	vec4 sunPos;
	skydome.sunAngle += sunMove & 1 ? -0.01f : 0.01f;
	skydomeGetSunPos(sunPos);
	setShaderValue(skydome.shader, "sun_pos", 4, sunPos);
}

void skydomeRender(void)
{
	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	glEnable(GL_CULL_FACE);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glFrontFace(GL_CW);
	glBindVertexArray(skydome.vao);
	glUseProgram(skydome.shader);

	float time = globals.curTime * 0.000005;
	setShaderValue(skydome.shader, "time", 1, &time);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, skydome.vboIndices);
	glDrawElements(GL_TRIANGLES, skydome.indices, GL_UNSIGNED_SHORT, 0);
}
