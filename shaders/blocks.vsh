#version 430 core

/*
 * vertex shader for drawing opaque and transparent blocks only.
 *
 * position and info encode the following values:
 * - position.x
 * - position.y
 * - position.z : position relative to offsets + (position.xyz - 1024) / 2048.
 * - info.x[bit0  -  8] : U tex coord (0 - 511)
 * - info.x[bit9  - 15] : V tex coord (7bits, hi part).
 * - info.y[bit0  -  2] : V tex coord (3bits, lo part).
 * - info.y[bit3  -  5] : side (3bits, normal vector): 0 = south, 1 = east, 2 : north, 3 = west, 4 = top, 5 = bottom, 6 = don't care
 * - info.y[bit6  -  7] : ambient occlusion
 * - info.y[bit8  - 11] : block light value
 * - info.y[bit12 - 15] : sky light value
 */
layout (location=0) in uvec4 position;
layout (location=1) in uvec3 info;
layout (location=2) in vec3  offsets;
layout (binding=0) uniform sampler2D blockTex;

uniform vec3 biomeColor;

/* extension provided by stb_include.h */
#include "uniformBlock.glsl"

/* GL_POINT needs to be converted to quad */
out vec3 vertex1;
out vec3 vertex2;
out vec3 vertex3;
out vec4 texCoord;
out uint skyBlockLight;
out uint ocsNorm;

void main(void)
{
	/* simply extract value from vertex buffers, and let geometry shader output real value for fsh */
	uint Usz = bitfieldExtract(info.y, 16, 8);
	uint Vsz = bitfieldExtract(info.y, 24, 8);
	uint U   = bitfieldExtract(info.x, 14, 9);
	uint V   = bitfieldExtract(info.x, 23, 9) | (bitfieldExtract(position.w, 28, 1) << 9);

	if (V == 1023) V = 1024;
	if (U == 511)  U = 512;

	#define ORIGINVTX     15360
	#define BASEVTX       0.00048828125
	#define MIDVTX        4

	vertex1 = vec3(
		(float(bitfieldExtract(position.x,  0, 16)) - ORIGINVTX) * BASEVTX + offsets.x,
		(float(bitfieldExtract(position.x, 16, 16)) - ORIGINVTX) * BASEVTX + offsets.y,
		(float(bitfieldExtract(position.y,  0, 16)) - ORIGINVTX) * BASEVTX + offsets.z
	);

	/* 2nd and 3rd vertex are relative to 1st (saves 2 bits per coord) */
	vertex2 = vec3(
		float(bitfieldExtract(position.y, 16, 14)) * BASEVTX - MIDVTX + vertex1.x,
		float(bitfieldExtract(position.z,  0, 14)) * BASEVTX - MIDVTX + vertex1.y,
		float(bitfieldExtract(position.z, 14, 14)) * BASEVTX - MIDVTX + vertex1.z
	);
	vertex3 = vec3(
		float(bitfieldExtract(position.w,  0, 14)) * BASEVTX - MIDVTX + vertex1.x,
		float(bitfieldExtract(position.w, 14, 14)) * BASEVTX - MIDVTX + vertex1.y,
		float(bitfieldExtract(info.x,      0, 14)) * BASEVTX - MIDVTX + vertex1.z
	);

	texCoord = vec4(
		float(U) * 0.001953125,  float(U + Usz - 128) * 0.001953125,
		float(V) * 0.0009765625, float(V + Vsz - 128) * 0.0009765625
	);
	skyBlockLight = info.z;
	ocsNorm = bitfieldExtract(info.y, 0, 16);
}
