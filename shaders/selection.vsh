#version 430

/*
 * vertex shader for drawing selection.
 */
layout (location=0) in ivec3 position;

/* note: include is an extension provided by stb_include.h */
#include "uniformBlock.glsl"

uniform vec4 info;

flat out int selType;

void main(void)
{
	selType = int(info.w);

	/* same vertex data than blocksVert.glsl */
	gl_Position = projMatrix * mvMatrix * vec4(
		float(position.x - 1920) * 0.00026041666666666666 + info.x, /* 1/3840 */
		float(position.y - 1920) * 0.00026041666666666666 + info.y,
		float(position.z - 1920) * 0.00026041666666666666 + info.z,
		1
	);
}
