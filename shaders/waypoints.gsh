/*
 * geometry shader for waypoints: convert points into quads
 */

#version 430

layout (points) in;
layout (triangle_strip, max_vertices = 4) out;

#include "uniformBlock.glsl"

flat in  uint wp_Color[];
flat out vec4 vtx_center;
flat out uint vtx_select;
     out vec4 vtx_color;
	 out vec4 vtx_coord;

#define CIRCLE_RADIUS      0.6
#define BEAM_WIDTH         0.5

void main(void)
{
	vec3 perp = normalize(cross(lookAt.xyz, vec3(0, 1, 0))) * CIRCLE_RADIUS;
	vec4 pt1  = gl_in[0].gl_Position;
	vec4 pt2  = vec4(pt1.x, 256, pt1.z, 1);

	vtx_color  = unpackUnorm4x8(wp_Color[0]);
	vtx_center = MVP * pt1;
	vtx_select = vtx_color.a > 0.4 ? 1 : 0;

	gl_Position = vtx_coord = MVP * vec4(pt1.x - perp.x, pt1.y - perp.y - CIRCLE_RADIUS, pt1.z - perp.z, 1);
	EmitVertex();

	gl_Position = vtx_coord = MVP * vec4(pt1.x + perp.x, pt1.y + perp.y - CIRCLE_RADIUS, pt1.z + perp.z, 1);
	EmitVertex();

	vtx_color.a = 0;
	gl_Position = vtx_coord = MVP * (pt2 - vec4(perp.xyz, 0));
	EmitVertex();

	gl_Position = vtx_coord = MVP * (pt2 + vec4(perp.xyz, 0));
	EmitVertex();
	EndPrimitive();
}
