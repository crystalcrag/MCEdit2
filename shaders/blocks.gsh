/*
 * blocks.gsh : convert GL_POINT from blocks.vsh into GL_QUAD.
 *
 * check doc/internals.html for vertex format: abandon all hope without reading this first.
 */
#version 430 core

layout (points) in;
layout (triangle_strip, max_vertices = 4) out;

#include "uniformBlock.glsl"  // extension provided by stb_include.h

in vec3 vertex1[];
in vec3 vertex2[];
in vec3 vertex3[];
in vec4 texCoord[];
in uint skyBlockLight[];
in uint ocsField[];
in uint normFlags[];
in uint chunkInfo[];
in vec3 offsets[];

out vec2  tc;
out vec2  ocspos;
out float skyLight;
out float blockLight;
flat out float fogFactor;
flat out uint  rswire;
flat out uint  ocsmap;
flat out uint  normal;
flat out uint  waterFog;

uniform uint underWater;    // player is underwater: denser fog
uniform uint timeMS;

// from chunks.h
#define FLAG_TEX_KEEPX                 (normFlags[0] & (1 << 3)) > 0
#define FLAG_DUAL_SIDE                 (normFlags[0] & (1 << 4)) > 0
#define FLAG_LIQUID                    (normFlags[0] & (1 << 5)) > 0
#define FLAG_UNDERWATER                (normFlags[0] & (1 << 6))
#define FLAG_EXTOCS                    (normFlags[0] & (1 << 7)) > 0

void main(void)
{
	mat4 MVP   = projMatrix * mvMatrix;
	bool keepX = FLAG_TEX_KEEPX;

	normal = normFlags[0] & 7;
	waterFog = FLAG_UNDERWATER;

	float Usz = (texCoord[0].y - texCoord[0].x) * 32;
	float Vsz = (texCoord[0].w - texCoord[0].z) * 64;
	if (Usz < 0) Usz = -Usz;
	if (Vsz < 0) Vsz = -Vsz;

	rswire = normal == 7 ? (skyBlockLight[0] & 15) + 1 : 0;
	ocsmap = ocsField[0] | (FLAG_EXTOCS ? 65536 : 0);

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

	/*
	 * lighting per face: block light (or torch light) will used a fixed shading per face.
	 * skylight will be directionnal.
	 */
	float shadeBlock = normal < 6 ? shading[normal].x / 15 : 1/15.;
	float shadeSky;
	float dotProd;
	float sky = 1;
	if (normal < 4)
	{
		// S, E, N, W face: only take xz component from sun direction :-/
		dotProd = dot(normals[normal].xyz, normalize(vec3(sunDir.x, 0, sunDir.z)));
	}
	else dotProd = dot(normals[normal < 6 ? normal : 4].xyz, sunDir.xyz);

	// only darken face by a factor of 0.7 at most if negative: 0.6 is too dark
	shadeSky = (dotProd < 0 ? 0.1 : 0.2) * dotProd + 0.8;

	// lower skylight contribution if we are at dawn/dusk
	if (sunDir.y < 0.4)
	{
		sky = (sunDir.y + 0.4) * 1.25;
		if (sky < 0) sky = 0; /* night time */
		sky = sqrt(sky);
	}
	// brightness setting (moody == 0, bright = 0.7, full bright = 1
	if (sky < MIN_BRIGHTNESS)
		sky = MIN_BRIGHTNESS;
	shadeSky *= sky * clamp(1 + MIN_BRIGHTNESS * 0.1, 1.0, 1.07) / 15;

	// fogStrength == how much fragment will blend with sky, 0 = normal fragment color, 1 = fragment will use sky color
	if (FOG_DISTANCE > 0)
	{
		float fogStrength;
		if (underWater == 0 || waterFog == 0)
		{
			fogStrength = clamp(distance(camera.xz, (V1.xz+V4.xz) * 0.5) / FOG_DISTANCE, 0.0, 1.0);
			fogFactor = 1 - fogStrength * fogStrength * fogStrength;
		}
		else // quad AND player are underwater: use a different fog for these
		{
			fogStrength = clamp(distance(camera.xz, (V1.xz+V4.xz) * 0.5) / (FOG_DISTANCE * 0.5), 0.0, 1.0);
			fogFactor = 1 - fogStrength;
		}
	}
	else fogFactor = 1; // disabled

	// first vertex
	gl_Position = MVP * vec4(V1, 1);
	skyLight    = float(bitfieldExtract(skyBlockLight[0], 28, 4)) * shadeSky;
	blockLight  = float(bitfieldExtract(skyBlockLight[0], 24, 4)) * shadeBlock;
	ocspos      = vec2(Usz, 0);
	tc          = keepX ? vec2(texCoord[0].x, texCoord[0].w) :
						  vec2(texCoord[0].y, texCoord[0].z) ;
	EmitVertex();

	// second vertex
	gl_Position = MVP * vec4(V2, 1);
	skyLight    = float(bitfieldExtract(skyBlockLight[0], 4, 4)) * shadeSky;
	blockLight  = float(bitfieldExtract(skyBlockLight[0], 0, 4)) * shadeBlock;
	ocspos      = vec2(0, 0);
	tc          = vec2(texCoord[0].x, texCoord[0].z);
	EmitVertex();
			
	// third vertex
	gl_Position = MVP * vec4(V3, 1);
	skyLight    = float(bitfieldExtract(skyBlockLight[0], 20, 4)) * shadeSky;
	blockLight  = float(bitfieldExtract(skyBlockLight[0], 16, 4)) * shadeBlock;
	ocspos      = vec2(Usz, Vsz);
	tc          = vec2(texCoord[0].y, texCoord[0].w);
	EmitVertex();

	// fourth vertex
	gl_Position = MVP * vec4(V4, 1);
	skyLight    = float(bitfieldExtract(skyBlockLight[0], 12, 4)) * shadeSky;
	blockLight  = float(bitfieldExtract(skyBlockLight[0], 8,  4)) * shadeBlock;
	ocspos      = vec2(0, Vsz);
	tc          = keepX ? vec2(texCoord[0].y, texCoord[0].z) :
						  vec2(texCoord[0].x, texCoord[0].w) ;
	EmitVertex();

	EndPrimitive();
}
