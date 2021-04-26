#version 430

/*
 * vertex shader for drawing entities (mobs, falling block, ...).
 */
layout (location=0) in ivec3 position;
layout (location=1) in ivec2 info;
layout (location=2) in vec4  offsets;

#include "uniformBlock.glsl"

out vec2 texcoord;
flat out float shade;
flat out int   isBlock;
flat out int   isSelected;

void main(void)
{
	vec3 pos = vec3(
		float(position.x - 1920) * 0.00026041666666666666,
		float(position.y - 1920) * 0.00026041666666666666,
		float(position.z - 1920) * 0.00026041666666666666
	);
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
