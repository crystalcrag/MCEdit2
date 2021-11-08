/*
 * geometry shader for waypoints: convert points into quads
 */

#version 430

layout (points) in;
layout (triangle_strip, max_vertices = 4) out;

#include "uniformBlock.glsl"

flat in  uint wp_Color[];
flat out vec4 vtx_center;
     out vec4 vtx_color;
	 out vec4 vtx_coord;

#define CIRCLE_RADIUS      0.6
#define BEAM_WIDTH         0.5

void main(void)
{
	vec4  pt1 = gl_in[0].gl_Position;
	vec4  pt2 = projMatrix * mvMatrix * vec4(pt1.x, 256, pt1.z, 1);
	float dy = CIRCLE_RADIUS * shading[0].y;
	pt1 = projMatrix * mvMatrix * pt1;

	/* we want a billboard effect for the beam, just like particles */
	vtx_color = unpackUnorm4x8(wp_Color[0]);
	vtx_center = pt1;
	
	/*
	 * Note: there will be some distortion of the circle at the base when it is at the edge of the screen.
	 * This is because pt1.w and pt2.w are only valid for pt1 and pt2.
	 * Here the points are shifted to form a quad, which means w component should change, but it is not that much of a big deal.
	 */
	gl_Position = vtx_coord = vec4(pt1.x - CIRCLE_RADIUS, pt1.y - dy, 1, pt1.w);
	EmitVertex();

	gl_Position = vtx_coord = vec4(pt1.x + CIRCLE_RADIUS, pt1.y - dy, 1, pt1.w);
	EmitVertex();

	vtx_color.a = 0;
	gl_Position = vtx_coord = vec4(pt2.x - CIRCLE_RADIUS, pt2.y, 1, pt2.w);
	EmitVertex();

	gl_Position = vtx_coord = vec4(pt2.x + CIRCLE_RADIUS, pt2.y, 1, pt2.w);
	EmitVertex();
	EndPrimitive();
}
