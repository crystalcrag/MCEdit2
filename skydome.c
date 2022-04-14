/*
 * skydome.c : init and render everything related to the sky (sun, moon, clouds, stars, ...).
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

void skydomeGetSunPos(vec4 pos)
{
	pos[0] = cosf(skydome.sunAngle);
	pos[1] = sinf(skydome.sunAngle);
	pos[2] = -0.25;
	pos[3] = 1;
	vecNormalize(pos, pos);
	//fprintf(stderr, "y = %.1f\n", (double) pos[1]);
}

Bool skydomeInit(void)
{
	/* sky dome */
	Model model = modelSphere(500, 10);

	skydome.shader = createGLSLProgram("skydome.vsh", "skydome.fsh", NULL);
	if (! skydome.shader)
		return False;

	glGenVertexArrays(1, &skydome.vao);
	glGenBuffers(1, &skydome.vbo);
	glBindVertexArray(skydome.vao);
	skydome.vertex   = model->vertex;
	skydome.indices  = model->index;
	skydome.sunAngle = M_PI_2;

	skydome.uniformTime    = glGetUniformLocation(skydome.shader, "time");
	skydome.uniformTexOnly = glGetUniformLocation(skydome.shader, "skyTexOnly");
	skydome.uniformOverlay = glGetUniformLocation(skydome.shader, "overlay");

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

	skydome.texTint    = textureLoad(RESDIR SKYDIR, "tint.png",    1, NULL);
	skydome.texTint2   = textureLoad(RESDIR SKYDIR, "tint2.png",   1, NULL);
	skydome.texSun     = textureLoad(RESDIR SKYDIR, "sun.png",     1, NULL);
	skydome.texClouds  = textureLoad(RESDIR SKYDIR, "clouds1.png", 0, NULL);

	glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, skydome.texTint);
	glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, skydome.texTint2);
	glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_2D, skydome.texSun);
	glActiveTexture(GL_TEXTURE5); glBindTexture(GL_TEXTURE_2D, skydome.texClouds);
	glActiveTexture(GL_TEXTURE0);

	float arg = 1;
	setShaderValue(skydome.shader, "weather",   1, &arg); arg = 0;
	setShaderValue(skydome.shader, "time",      1, &arg);
	setShaderValue(skydome.shader, "sun_angle", 1, &skydome.sunAngle);

	return True;
}

void skydomeMoveSun(int sunMove)
{
	vec4 sunPos;
	skydome.sunAngle += sunMove & 1 ? -0.01f : 0.01f;
	skydomeGetSunPos(sunPos);
	setShaderValue(skydome.shader, "sun_angle", 1, &skydome.sunAngle);
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

	if (underWater & 255)
	{
		float fact = (underWater >> 8) * (1/255.0f);
		vec4 overlay = {0x2f/255.0f * fact, 0x44/255.0f * fact, 0xf4/255.0f * fact, 1};
		glProgramUniform4fv(skydome.shader, skydome.uniformOverlay, 1, overlay);
	}
	else glProgramUniform4fv(skydome.shader, skydome.uniformOverlay, 1, (vec4) {0,0,0,0});

	float time = globals.curTime * 0.000005;
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
