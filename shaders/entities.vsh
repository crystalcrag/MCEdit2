/*
 * vertex shader for drawing entities (mobs, falling block, paintings, world items ...).
 *
 * vertex data uses the 10 bytes per vertex format (see doc/internals.html for details).
 */
#version 430

layout (location=0) in ivec3 position;
layout (location=1) in ivec2 info;
layout (location=2) in vec4  offsets; // divisor = 1 starting from here
layout (location=3) in vec4  rotation;
layout (location=4) in uvec3 lightSEN;
layout (location=5) in uvec3 lightWTB;

#include "uniformBlock.glsl"

flat out uint  flags;
     out float fogFactor;
     out vec2  texcoord;
     out float skyLight;
     out float blockLight;

void main(void)
{
	vec3 pos = vec3(
		float(position.x - ORIGINVTX) * BASEVTX,
		float(position.y - ORIGINVTX) * BASEVTX,
		float(position.z - ORIGINVTX) * BASEVTX
	) * rotation.w; // rotation.w == scaling

	int   norm   = (info.y >> 3) & 7; if (norm >= 6) norm = 4; // QUAD
	vec3  normal = normals[norm].xyz;
	float angle  = rotation.x;
	mat3  rotate;

	if (angle > 0.001)
	{
		// yaw: rotate along Y axis actually :-/
		float ca = cos(angle);
		float sa = sin(angle);
		rotate = mat3(ca, 0, sa, 0, 1, 0, -sa, 0, ca);
	}
	else rotate = mat3(1, 0, 0, 0, 1, 0, 0, 0, 1);

	angle = rotation.y;
	if (angle > 0.001)
	{
		// pitch: rotate along X axis actually :-/
		float ca = cos(angle);
		float sa = sin(angle);
		rotate *= mat3(1, 0, 0, 0, ca, -sa, 0, sa, ca);
	}

	angle = rotation.z;
	if (angle > 0.001)
	{
		// rotate along Z axis
		float ca = cos(angle);
		float sa = sin(angle);
		rotate *= mat3(ca, sa, 0, -sa, ca, 0, 0, 0, 1);
	}

	pos = rotate * pos;
	normal = rotate * normal;

	// distribute shading per face
	vec3 absNorm = abs(normal);
	absNorm *= 1 / (absNorm.x + absNorm.y + absNorm.z);

	gl_Position = MVP * vec4(pos + offsets.xyz, 1);
	float U = float(info.x & 511);
	float V = float(((info.x >> 6) & ~7) | (info.y & 7));
	if (V == 1023) V = 1024;
	if (U == 511)  U = 512;
	texcoord = vec2(U * TEX_COORD_X, V * TEX_COORD_Y);

	// distribute sky/block light according to normal direction
	uint light, corner;
	// south/north
	corner = ((pos.x < 0.5 ? 1 : 0) + (pos.y < 0.5 ? 0 : 2)) << 3;
	light  = (normal.z < 0 ? lightSEN.z : lightSEN.x) >> corner;
	blockLight = float(light & 15)   * (1/15.)  * absNorm.z;
	skyLight   = float(light & 0xf0) * (1/240.) * absNorm.z;

	// east/west
	corner = ((pos.z < 0.5 ? 1 : 0) + (pos.y < 0.5 ? 0 : 2)) << 3;
	light  = (normal.x < 0 ? lightWTB.x : lightSEN.y) >> corner;
	blockLight += float(light & 15)   * (1/15.)  * absNorm.x;
	skyLight   += float(light & 0xf0) * (1/240.) * absNorm.x;

	// top/bottom
	corner = ((pos.x < 0.5 ? 0 : 1) + (pos.z < 0.5 ? 0 : 2)) << 3;
	light  = (normal.y < 0 ? lightWTB.z : lightWTB.y) >> corner;
	blockLight += float(light & 15)   * (1/15.)  * absNorm.y;
	skyLight   += float(light & 0xf0) * (1/240.) * absNorm.y;

	// directionnal lighting from sun
	vec3  sunDirXZ = normalize(vec3(sunDir.x, 0, sunDir.z));
	float shadeSky;
	float shadeBlock;
	float dotProdX = dot(vec3(normal.x < 0 ? -1 : 1, 0, 0), sunDirXZ);
	float dotProdZ = dot(vec3(0, 0, normal.z < 0 ? -1 : 1), sunDirXZ);
	float dotProd = dot(normal, sunDir.xyz);

	// faces parallel to Y axis will only use dot product from XZ plane (like blocks.gsh)
	dotProdX = (dotProdX < 0 ? 0.1 : 0.2) * dotProdX + 0.8;
	dotProdZ = (dotProdZ < 0 ? 0.1 : 0.2) * dotProdZ + 0.8;
	dotProd  = (dotProd  < 0 ? 0.1 : 0.2) * dotProd  + 0.8;

	shadeSky = absNorm.x * dotProdX + absNorm.z * dotProdZ + absNorm.y * dotProd;

	// lower skylight contribution if we are at dawn/dusk
	if (sunDir.y < 0.4)
	{
		float sky = (sunDir.y + 0.4) * 1.25;
		if (sky < 0) sky = 0; /* night time */
		shadeSky *= sqrt(sky);
	}

	if (FOG_DISTANCE > 0)
	{
		float fogStrength = clamp(distance(camera.xz, pos.xz + offsets.xz) / FOG_DISTANCE, 0.0, 1.0);
		fogFactor = 1 - fogStrength * fogStrength;
	}
	else fogFactor = 1; // disabled

	shadeBlock = shading[normal.x < 0 ? 3 : 1].x * absNorm.x +
	             shading[normal.z < 0 ? 2 : 0].x * absNorm.z +
	             shading[normal.y < 0 ? 5 : 4].x * absNorm.y;

	blockLight *= shadeBlock;
	skyLight   *= shadeSky;

	flags = int(offsets.w);
}
