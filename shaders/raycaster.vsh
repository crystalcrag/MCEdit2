/*
 * raycaster.vsh: use to render distant chunks using raycasting.
 */
#version 430 core

// input vertices will be a flat quad over the entire screen
layout (location=0) in vec3 point;

void main()
{
	// yep, that's it
	gl_Position = vec4(point, 1);
}
