/*
 * waypoints.fsh: fragment shader for waypoints.
 */
#version 430

#include "uniformBlock.glsl"

out vec4 color;
in vec4 vtx_color;
in vec4 vtx_coord;
flat in vec4 vtx_center;
flat in uint vtx_type;

#define CIRCLE_RADIUS      0.6
#define BEAM_WIDTH         0.5

void main(void)
{
	/* fill a circle at the base */
	vec4 diff = vtx_coord - vtx_center;
	diff.y /= shading[0].y;
	if (diff.x*diff.x + diff.y*diff.y > CIRCLE_RADIUS*CIRCLE_RADIUS)
	{
		/* connection to the beam */
		if (! (-(BEAM_WIDTH/2) <= diff.x && diff.x <= (BEAM_WIDTH/2) && diff.y > 0))
			discard;
	}
	color = vtx_color;
}
