/*
 * blocks.gsh : convert GL_POINT from blocks.vsh into GL_QUAD.
 *
 * check doc/internals.html for vertex format: abandon all hope without reading this first.
 */
#version 430 core

layout (points) in;
layout (triangle_strip, max_vertices = 4) out;

/* extension provided by stb_include.h */
#include "uniformBlock.glsl"

in vec3 vertex1[];
in vec3 vertex2[];
in vec3 vertex3[];
in vec4 texCoord[];
in uint skyBlockLight[];
in uint ocsField[];
in uint normFlags[];
in vec3 offsets[];

out vec2  tc;
out vec2  ocspos;
out float skyLight;
out float blockLight;
flat out uint rswire;
flat out uint ocsmap;
flat out int  normal;

void main(void)
{
	mat4 MVP   = projMatrix * mvMatrix;
	bool keepX = (normFlags[0] & (1 << 3)) > 0;

	normal = int(normFlags[0] & 7);

	/* shading per face (OCS is done in fragment shader) */
	float shade = normal < 6 ? shading[normal].x / 15 : 1/15.;
	float Usz   = (texCoord[0].y - texCoord[0].x) * 32;
	float Vsz   = (texCoord[0].w - texCoord[0].z) * 64;
	if (Usz < 0) Usz = -Usz;
	if (Vsz < 0) Vsz = -Vsz;
	rswire = normal == 7 ? (skyBlockLight[0] & 15) + 1 : 0;
	ocsmap = ocsField[0];

	vec3 V1 = vertex1[0];
	vec3 V2 = vertex2[0];
	vec3 V3 = vertex3[0];
	vec3 V4 = vertex3[0] + (vertex2[0] - vertex1[0]);
	if ((normFlags[0] & (1 << 4)) > 0 && dot(vertex1[0] - camera.xyz, cross(V3-V1, V2-V1)) < 0)
	{
		/* this face must not be culled by back-face culling, but using current vertex emit order, it will */
		V2 = V1; V1 = vertex2[0];
		V3 = V4; V4 = vertex3[0];
	}

	/* first vertex */
	gl_Position = MVP * vec4(V1, 1);
	skyLight    = float(bitfieldExtract(skyBlockLight[0], 28, 4)) * shade;
	blockLight  = float(bitfieldExtract(skyBlockLight[0], 24, 4)) * shade;
	ocspos      = vec2(Usz, 0);
	tc          = keepX ? vec2(texCoord[0].x, texCoord[0].w) :
						  vec2(texCoord[0].y, texCoord[0].z) ;
	EmitVertex();

	/* second vertex */
	gl_Position = MVP * vec4(V2, 1);
	skyLight    = float(bitfieldExtract(skyBlockLight[0], 4, 4)) * shade;
	blockLight  = float(bitfieldExtract(skyBlockLight[0], 0, 4)) * shade;
	ocspos      = vec2(0, 0);
	tc          = vec2(texCoord[0].x, texCoord[0].z);
	EmitVertex();
			
	/* third vertex */
	gl_Position = MVP * vec4(V3, 1);
	skyLight    = float(bitfieldExtract(skyBlockLight[0], 20, 4)) * shade;
	blockLight  = float(bitfieldExtract(skyBlockLight[0], 16, 4)) * shade;
	ocspos      = vec2(Usz, Vsz);
	tc          = vec2(texCoord[0].y, texCoord[0].w);
	EmitVertex();

	/* fourth vertex */
	gl_Position = MVP * vec4(V4, 1);
	skyLight    = float(bitfieldExtract(skyBlockLight[0], 12, 4)) * shade;
	blockLight  = float(bitfieldExtract(skyBlockLight[0], 8,  4)) * shade;
	ocspos      = vec2(0, Vsz);
	tc          = keepX ? vec2(texCoord[0].y, texCoord[0].z) :
						  vec2(texCoord[0].x, texCoord[0].w) ;
	EmitVertex();

	EndPrimitive();
}
