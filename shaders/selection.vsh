/*
 * vertex shader for drawing selection.
 */
#version 430

layout (location=0) in vec3 position;
layout (location=1) in vec2 texCoord;

#include "uniformBlock.glsl"

uniform vec4 info;

flat out int selType;
out vec2 tex;

void main(void)
{
	selType = int(info.w);
	tex = texCoord;
	gl_Position = projMatrix * mvMatrix * vec4(position.xyz + info.xyz, 1);
}
