#version 430

layout (location=0) in vec3 position;

#include "uniformBlock.glsl"

void main(void)
{
	gl_Position = projMatrix * mvMatrix * vec4(position, 1);
}
