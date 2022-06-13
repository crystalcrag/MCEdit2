/*
 * vertex shader for drawing items in inventories (in world items are rendered in entities shader).
 *
 * vertex data is based on the 10 bytes per vertex model (check doc/internals.html for details).
 */
#version 430 core

layout (location=0) in uvec3 position;
layout (location=1) in uvec2 info;
layout (location=2) in vec3  offsets;
layout (binding=0) uniform sampler2D blockTex;

/* extension provided by stb_include.h */
#include "uniformBlock.glsl"

out vec2 tc;
out float skyLight;
out float blockLight;
flat out int rswire;

void main(void)
{
	vec4 pos = vec4(
		(float(position.x) - 15360) * BASEVTX,
		(float(position.y) - 15360) * BASEVTX,
		(float(position.z) - 15360) * BASEVTX,
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

	rswire     = normal == 7 ? 1 : 0;
	tc         = vec2(U * TEX_COORD_X, V * TEX_COORD_Y);
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
			pos = MVP * ((pos - vec4(0.5, 0.5, 0.5, 0)) * vec4(scale,scale,scale,1)) + vec4(offsets.x + 0.85 * scale, offsets.y + 0.8 * scale, 25, 0);
		}
		else pos = pos * vec4(scale,scale,1,1) + vec4(offsets.x, offsets.y, 20, 0); /* quad */
		gl_Position = projMatrix * pos;
	}
	else /* preview block (show block before placing) */
	{
		gl_Position = MVP * (pos + vec4(offsets, 0));
	}
}
