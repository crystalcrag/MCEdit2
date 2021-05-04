#version 430

/*
 * vertex shader for drawing entities (mobs, falling block, paintings, ...).
 */
layout (location=0) in ivec3 position;
layout (location=1) in ivec2 info;
layout (location=2) in vec4  offsets;
layout (location=3) in vec2  rotation;

#include "uniformBlock.glsl"

out vec2 texcoord;
flat out int   isBlock;
flat out int   isSelected;
flat out float skyLight;
flat out float blockLight;

void main(void)
{
	vec3 pos = vec3(
		float(position.x - 15360) * 0.00048828125,
		float(position.y - 15360) * 0.00048828125,
		float(position.z - 15360) * 0.00048828125
	);

	if (rotation.x > 0.001)
	{
		/* yaw: rotate along Y axis actually :-/ */
		float ca = cos(rotation.x);
		float sa = sin(rotation.x);
		pos = (vec4(pos, 1) * mat4(
			ca, 0, sa, 0,
			0, 1, 0, 0,
			-sa, 0, ca, 0,
			0, 0, 0, 1
		)).xyz;
	}

	float shade = shading[(info.y >> 3) & 7].x;
	int meta = int(offsets.w);
	gl_Position = projMatrix * mvMatrix * vec4(pos + offsets.xyz, 1);
	float U = float(info.x & 511);
	float V = float(((info.x >> 6) & ~7) | (info.y & 7));
	if (V == 1023) V = 1024;
	if (U == 511)  U = 512;
	texcoord = vec2(U * 0.001953125, V * 0.0009765625);
	blockLight = float(meta & 15) / 15.;
	skyLight = float(meta & 0xf0) / 240.;
	if (blockLight > skyLight)
	{
		/* diminish slightly ambient occlusion if there is blockLight overpowering skyLight */
		shade += (blockLight - skyLight) * 0.35;
		if (shade > 1) shade = 1;
	}
	blockLight *= shade;
	skyLight   *= shade;
	isBlock = 1;
	isSelected = meta & 256;
}
