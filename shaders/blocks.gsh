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

out vec2 tc;
out float skyLight;
out float blockLight;
flat out uint rswire;
flat out vec2 texOrigin;
flat out uint ocsmap;
flat out int  normal;

void main(void)
{
	mat4 MVP   = projMatrix * mvMatrix;
	bool keepX = (normFlags[0] & (1 << 3)) > 0;

	/* ascending quad */
	normal = int(normFlags[0] & 7);
	if ((normFlags[0] & (1 << 4)) > 0)
		normal = 4;

	/* shading per face (OCS is done in fragment shader) */
	float shade = normal < 6 ? shading[normal].x / 15 : 1/15.;
	rswire = normal == 7 ? (skyBlockLight[0] & 15) + 1 : 0;
	texOrigin = vec2(texCoord[0].x, texCoord[0].z);
	ocsmap = ocsField[0];

	/* first vertex */
	gl_Position = MVP * vec4(vertex1[0], 1);
	skyLight    = float(bitfieldExtract(skyBlockLight[0], 28, 4)) * shade;
	blockLight  = float(bitfieldExtract(skyBlockLight[0], 24, 4)) * shade;
	/* ambient occlusion */
	tc          = keepX ? vec2(texCoord[0].x, texCoord[0].w) :
						  vec2(texCoord[0].y, texCoord[0].z) ;
	EmitVertex();

	/* second vertex */
	gl_Position = MVP * vec4(vertex2[0], 1);
	skyLight    = float(bitfieldExtract(skyBlockLight[0], 4, 4)) * shade;
	blockLight  = float(bitfieldExtract(skyBlockLight[0], 0, 4)) * shade;
	tc          = vec2(texCoord[0].x, texCoord[0].z);
	EmitVertex();
			
	/* third vertex */
	gl_Position = MVP * vec4(vertex3[0], 1);
	skyLight    = float(bitfieldExtract(skyBlockLight[0], 20, 4)) * shade;
	blockLight  = float(bitfieldExtract(skyBlockLight[0], 16, 4)) * shade;
	tc          = vec2(texCoord[0].y, texCoord[0].w);
	EmitVertex();

	/* fourth vertex */
	gl_Position = MVP * vec4(vertex3[0] + (vertex2[0] - vertex1[0]), 1);
	skyLight    = float(bitfieldExtract(skyBlockLight[0], 12, 4)) * shade;
	blockLight  = float(bitfieldExtract(skyBlockLight[0], 8,  4)) * shade;
	tc          = keepX ? vec2(texCoord[0].y, texCoord[0].z) :
						  vec2(texCoord[0].x, texCoord[0].w) ;
	EmitVertex();

	EndPrimitive();
}
