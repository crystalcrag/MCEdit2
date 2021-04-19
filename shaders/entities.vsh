#version 430

/*
 * vertex shader for drawing entities (mobs, falling block, ...).
 */
layout (location=0) in vec3  position;
layout (location=1) in ivec2 tex;
layout (location=2) in vec3  offsets;

#include "uniformBlock.glsl"

out vec2 texcoord;
flat out float shade;
flat out int   isBlock;

void main(void)
{
	gl_Position = projMatrix * mvMatrix * vec4(position + offsets, 1);
	int U = tex.x & 511;
	int V = (tex.x >> 9) | ((tex.y & 24) << 4);
	if (U == 511)  U = 512;
	if (V == 1023) V = 1024;
	texcoord = vec2(float(U) * 0.001953125, float(V) * 0.0009765625);
	shade = shading[tex.y & 7].x;
	isBlock = (tex.y & 64);
}
