/*
 * vertex shader for drawing opaque and transparent blocks only.
 *
 * check doc/internals.html for vertex format: abandon all hope without reading this first.
 */
#version 430 core

layout (location=0) in uvec4 position;
layout (location=1) in uvec4 info;
layout (location=2) in vec3  offsets;

// extension provided by stb_include.h
#include "uniformBlock.glsl"

// GL_POINT needs to be converted to quad
out vec3 vertex1;
out vec3 vertex2;
out vec3 vertex3;
out vec4 texCoord;
out uint normFlags;
out uint lightingTexBank;

void main(void)
{
	// simply extract value from vertex buffers, and let geometry shader output real value for fsh
	texCoord = vec4(
		float(bitfieldExtract(info.y, 0, 9))  * TEX_COORD_X, float(bitfieldExtract(info.z, 0, 9))  * TEX_COORD_X,
		float(bitfieldExtract(info.y, 9, 10)) * TEX_COORD_Y, float(bitfieldExtract(info.z, 9, 10)) * TEX_COORD_Y
	);

	vertex1 = vec3(
		(float(bitfieldExtract(position.x,  0, 16)) - ORIGINVTX) * BASEVTX + offsets.x,
		(float(bitfieldExtract(position.x, 16, 16)) - ORIGINVTX) * BASEVTX + offsets.y,
		(float(bitfieldExtract(position.y,  0, 16)) - ORIGINVTX) * BASEVTX + offsets.z
	);

	vertex2 = vec3(
		(float(bitfieldExtract(position.y, 16, 16)) - ORIGINVTX) * BASEVTX + offsets.x,
		(float(bitfieldExtract(position.z,  0, 16)) - ORIGINVTX) * BASEVTX + offsets.y,
		(float(bitfieldExtract(position.z, 16, 16)) - ORIGINVTX) * BASEVTX + offsets.z
	);
	vertex3 = vec3(
		(float(bitfieldExtract(position.w,  0, 16)) - ORIGINVTX) * BASEVTX + offsets.x,
		(float(bitfieldExtract(position.w, 16, 16)) - ORIGINVTX) * BASEVTX + offsets.y,
		(float(bitfieldExtract(info.x,     16, 16)) - ORIGINVTX) * BASEVTX + offsets.z
	);

	// last tex line: first 16 tex are biome dependant
	if (texCoord.z >= 0.96875)
	{
		// ignore biome color for now
		texCoord.z += 0.015625;
		texCoord.w += 0.015625;
	}

	lightingTexBank = bitfieldExtract(info.x, 0, 16);
	normFlags = bitfieldExtract(info.y, 19, 13);
}
