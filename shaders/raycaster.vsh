/*
 * raycaster.vsh: use to render distant chunks using raycasting.
 */
#version 430 core

#include "uniformBlock.glsl"

layout (location=0) in vec3 point;

out vec3 worldPos;

void main()
{
	worldPos = point;
	gl_Position = MVP * vec4(point, 1);
}
