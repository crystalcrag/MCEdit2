/*
 * blocks.gsh : convert GL_POINT from blocks.vsh into GL_QUAD.
 *
 * check doc/internals.html for vertex format: abandon all hope without reading this first.
 */
#version 430 core

layout (points) in;
layout (triangle_strip, max_vertices = 4) out;

#include "uniformBlock.glsl"

in vec3 vertex1[];
in vec3 vertex2[];
in vec3 vertex3[];
in vec4 texCoord[];
in uint lightingTexBank[];
in uint normFlags[];
in vec3 voffset[];

out vec3 vPoint;
out vec2 tc;
flat out uint rswire;
flat out uint normal;
flat out uint waterFog;
flat out uint lightingId;
flat out vec2 texStart;
flat out vec3 chunkStart;

uniform uint underWater;    // player is underwater: denser fog
uniform uint timeMS;

// from chunks.h
#define FLAG_TEX_KEEPX                 (normFlags[0] & (1 << 3)) > 0
#define FLAG_DUAL_SIDE                 (normFlags[0] & (1 << 4)) > 0
#define FLAG_LIQUID                    (normFlags[0] & (1 << 5)) > 0
#define FLAG_UNDERWATER                (normFlags[0] & (1 << 6))
#define FLAG_REPEAT                    (normFlags[0] & (1 << 7)) > 0
#define FLAG_ROUNDVTX                  (normFlags[0] & (1 << 8)) > 0

void main(void)
{
	bool keepX = FLAG_TEX_KEEPX;
	bool roundVtx = FLAG_ROUNDVTX;

	normal = normFlags[0] & 7;
	waterFog = FLAG_UNDERWATER;
	lightingId = lightingTexBank[0];
	chunkStart = voffset[0];

	float Usz = (texCoord[0].y - texCoord[0].x) * 32;
	float Vsz = (texCoord[0].w - texCoord[0].z) * 64;
	if (Usz < 0) Usz = -Usz;
	if (Vsz < 0) Vsz = -Vsz;

	rswire = normal == 7 ? (normFlags[0] >> 9) + 1 : 0;

	if (FLAG_REPEAT)
	{
		// greedy meshing: need to repeat tex from tex atlas (which is set to GL_CLAMP)
		texStart.x = min(texCoord[0].x, texCoord[0].y);
		texStart.y = min(texCoord[0].z, texCoord[0].w);
	}
	else texStart.x = -1;

	vec3 V1 = vertex1[0];
	vec3 V2 = vertex2[0];
	vec3 V3 = vertex3[0];
	vec3 V4 = vertex3[0] + (vertex2[0] - vertex1[0]);
	// dualside quad
	if (FLAG_DUAL_SIDE && dot(vertex1[0] - camera.xyz, cross(V3-V1, V2-V1)) < 0)
	{
		// this face must not be culled by back-face culling, but using current vertex emit order, it will
		V2 = V1; V1 = vertex2[0];
		V3 = V4; V4 = vertex3[0];
	}
	// liquid: lower some of the edges depending on what's nearby XXX need a better approach than this :-/
	if (FLAG_LIQUID)
	{
		if ((normFlags[0] & 0x0200) > 0) V1.y -= 0.1875;
		if ((normFlags[0] & 0x0400) > 0) V2.y -= 0.1875;
		if ((normFlags[0] & 0x0800) > 0) V3.y -= 0.1875;
		if ((normFlags[0] & 0x1000) > 0) V4.y -= 0.1875;
	}

	vPoint      = V1;
	gl_Position = MVP * vec4(V1, 1);
	tc          = keepX ? vec2(texCoord[0].x, texCoord[0].w) :
						  vec2(texCoord[0].y, texCoord[0].z) ;
	if (roundVtx) vPoint.y = floor(vPoint.y + normals[normal].y);

	EmitVertex();

	vPoint      = V2;
	gl_Position = MVP * vec4(V2, 1);
	tc          = vec2(texCoord[0].x, texCoord[0].z);
	if (roundVtx) vPoint.y = floor(vPoint.y + normals[normal].y);
	EmitVertex();
			
	vPoint      = V3;
	gl_Position = MVP * vec4(V3, 1);
	tc          = vec2(texCoord[0].y, texCoord[0].w);
	if (roundVtx) vPoint.y = floor(vPoint.y + normals[normal].y);
	EmitVertex();

	vPoint      = V4;
	gl_Position = MVP * vec4(V4, 1);
	tc          = keepX ? vec2(texCoord[0].y, texCoord[0].z) :
						  vec2(texCoord[0].x, texCoord[0].w) ;
	if (roundVtx) vPoint.y = floor(vPoint.y + normals[normal].y);
	EmitVertex();

	EndPrimitive();
}
