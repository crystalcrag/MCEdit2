/*
 * waypoints.vsh: used to display in-game map marker using some kind of beam of light from position to sky limit.
 */

#version 430

#include "uniformBlock.glsl"

layout (location=0) in  vec3 position;
layout (location=1) in uvec2 info;

flat out uint wp_Color;

void main(void)
{
	/* note: info.y is used by CPU */
	gl_Position = vec4(position, 1);
	wp_Color = info.x;
}
