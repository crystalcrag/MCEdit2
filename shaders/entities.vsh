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
flat out float shade;
flat out int   isBlock;
flat out int   isSelected;

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

	gl_Position = projMatrix * mvMatrix * vec4(pos + offsets.xyz, 1);
	float U = float(info.x & 511);
	float V = float(((info.x >> 6) & ~7) | (info.y & 7));
	if (V == 1023) V = 1024;
	if (U == 511)  U = 512;
	texcoord = vec2(U * 0.001953125, V * 0.0009765625);
	shade = shading[(info.y >> 3) & 7].x;
	isBlock = 1;
	isSelected = int(offsets.w) & 256;
}
