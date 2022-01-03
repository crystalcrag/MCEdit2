#version 430

/*
 * vertex shader for drawing entities (mobs, falling block, paintings, ...).
 */
layout (location=0) in ivec3 position;
layout (location=1) in ivec2 info;
layout (location=2) in vec4  offsets; /* divisor = 1 starting from here */
layout (location=3) in vec4  rotation;
layout (location=4) in uvec3 lightSEN;
layout (location=5) in uvec3 lightWTB;

#include "uniformBlock.glsl"

flat out uint  flags;
     out vec2  texcoord;
     out float skyLight;
     out float blockLight;

void main(void)
{
	vec3 pos = vec3(
		float(position.x - ORIGINVTX) * BASEVTX,
		float(position.y - ORIGINVTX) * BASEVTX,
		float(position.z - ORIGINVTX) * BASEVTX
	) * rotation.w; /* rotation.w == scaling */

	int   norm   = (info.y >> 3) & 7;
	vec3  normal = normals[norm].xyz;
	float angle  = rotation.x;
	mat3  rotate;

	if (angle > 0.001)
	{
		/* yaw: rotate along Y axis actually :-/ */
		float ca = cos(angle);
		float sa = sin(angle);
		rotate = mat3(ca, 0, -sa, 0, 1, 0, sa, 0, ca);
	}
	else rotate = mat3(1, 0, 0, 0, 1, 0, 0, 0, 1);

	angle = /*curtime * 0.001 +*/ rotation.y;
	if (angle > 0.001)
	{
		/* pitch: rotate along X axis actually :-/ */
		float ca = cos(angle);
		float sa = sin(angle);
		rotate *= mat3(1, 0, 0, 0, ca, sa, 0, -sa, ca);
	}

	pos = rotate * pos;
	normal = rotate * normal;

	/* distribute shading per face */
	vec3 absNorm = abs(normal);
	absNorm *= 1 / (absNorm.x + absNorm.y + absNorm.z);
	float shade = shading[normal.x < 0 ? 3 : 1].x * absNorm.x +
	              shading[normal.z < 0 ? 2 : 0].x * absNorm.z +
	              shading[normal.y < 0 ? 5 : 4].x * absNorm.y;

	gl_Position = projMatrix * mvMatrix * vec4(pos + offsets.xyz, 1);
	float U = float(info.x & 511);
	float V = float(((info.x >> 6) & ~7) | (info.y & 7));
	if (V == 1023) V = 1024;
	if (U == 511)  U = 512;
	texcoord = vec2(U * TEX_COORD_X, V * TEX_COORD_Y);

	/* distribute sky/block light according to normal direction */
	uint light, corner;
	/* south/north */
	corner = ((pos.x < 0.5 ? 1 : 0) + (pos.y < 0.5 ? 0 : 2)) << 3;
	light  = (normal.z < 0 ? lightSEN.z : lightSEN.x) >> corner;
	blockLight = float(light & 15)   * (1/15.)  * absNorm.z;
	skyLight   = float(light & 0xf0) * (1/240.) * absNorm.z;

	/* east/west */
	corner = ((pos.z < 0.5 ? 1 : 0) + (pos.y < 0.5 ? 0 : 2)) << 3;
	light  = (normal.x < 0 ? lightWTB.x : lightSEN.y) >> corner;
	blockLight += float(light & 15)   * (1/15.)  * absNorm.x;
	skyLight   += float(light & 0xf0) * (1/240.) * absNorm.x;

	/* top/bottom */
	corner = ((pos.x < 0.5 ? 0 : 1) + (pos.z < 0.5 ? 0 : 2)) << 3;
	light  = (normal.y < 0 ? lightWTB.z : lightWTB.y) >> corner;
	blockLight += float(light & 15)   * (1/15.)  * absNorm.y;
	skyLight   += float(light & 0xf0) * (1/240.) * absNorm.y;

	blockLight *= shade;
	skyLight   *= shade;

	flags = int(offsets.w);
}
