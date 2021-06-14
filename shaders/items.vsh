#version 430 core

/*
 * vertex shader for drawing items (inventory and in world)
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
layout (location=0) in uvec3 position;
layout (location=1) in uvec2 info;
layout (location=2) in vec3  offsets;
layout (binding=0) uniform sampler2D blockTex;

/* extension provided by stb_include.h */
#include "uniformBlock.glsl"

out vec2 tc;
out float skyLight;
out float blockLight;

void main(void)
{
	vec4 pos = vec4(
		(float(position.x) - 15360) * 0.00048828125,
		(float(position.y) - 15360) * 0.00048828125,
		(float(position.z) - 15360) * 0.00048828125,
		1
	);
	float U = float(bitfieldExtract(info.x, 0, 9));
	float V = float(bitfieldExtract(info.y, 0, 3) | (bitfieldExtract(info.x, 9, 7) << 3));
	uint  normal = bitfieldExtract(info.y, 3, 3);
	float shade = 1/15.;

	if (V == 1023) V = 1024;
	if (U == 511)  U = 512;

	if (normal < 6)
		shade = shading[normal].x / 15;

	tc         = vec2(U * 0.001953125, V * 0.0009765625);
	skyLight   = float(bitfieldExtract(info.y, 12, 4)) * shade;
	blockLight = float(bitfieldExtract(info.y,  8, 4)) * shade;

	if (shading[0].w > 0)
	{
		/* inventory items: using an orthogonal projection on XY plane */
		float scale = offsets.z;
		/* <offsets> is relative to bottom left corner of rendering box */
		if (normal < 6)
		{
			/* cube: due to rotation, scale and offset need to be slightly adjusted */
			scale *= 0.625;
			pos = mvMatrix * ((pos - vec4(0.5, 0.5, 0.5, 0)) * vec4(scale,scale,scale,1)) + vec4(offsets.x + 0.85 * scale, offsets.y + 0.8 * scale, 25, 0);
		}
		else pos = pos * vec4(scale,scale,1,1) + vec4(offsets.x, offsets.y, 20, 0); /* quad */
		gl_Position = projMatrix * pos;
	}
	else
	{
		gl_Position = projMatrix * mvMatrix * (pos + vec4(offsets, 0));
	}
}
