#version 430

/*
 * vertex shader for drawing selection.
 */
layout (location=0) in ivec3 position;

#include "uniformBlock.glsl"

uniform vec4 info;

flat out int selType;

void main(void)
{
	selType = int(info.w);

	/* same vertex data than blocks.vsh */
	gl_Position = projMatrix * mvMatrix * vec4(
		float(position.x - 15360) * 0.00048828125 + info.x, /* 1/2048 */
		float(position.y - 15360) * 0.00048828125 + info.y,
		float(position.z - 15360) * 0.00048828125 + info.z,
		1
	);
}
