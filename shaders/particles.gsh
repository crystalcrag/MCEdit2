/*
 * geometry shader for particles: simply convert VBO points into quads
 */

#version 430

layout (points) in;
layout (triangle_strip, max_vertices = 4) out;

#include "uniformBlock.glsl"
flat in float size[];
flat in float skyLight[];
flat in float blockLight[];
flat in int texbase[];

out vec2 texCoord;
out vec2 skyBlockLight;

vec2 getTexCoord(int base, int offX, int offY)
{
	int baseU = base & 511;
	int baseV = base >> 9;
	int minU  = baseU & ~15;
	int minV  = baseV & ~15;

	return vec2(float(clamp(baseU-offX, minU, minU + 15)) * (1/512.),
	            float(clamp(baseV-offY, minV, minV + 15)) * (1/1024.));
}

void main(void)
{
	vec4  pos  = gl_in[0].gl_Position;
	int   sztx = int(size[0]); if (sztx > 4) sztx = 4;
	float szX  = size[0] / 64;
	float szY  = szX * shading[0].y; /* shading.y == aspect ratio of screen */

	/* that way we will get a billboard effect at the same time */
	texCoord = getTexCoord(texbase[0], - sztx, -sztx);
	skyBlockLight = vec2(skyLight[0], blockLight[0]);
	gl_Position = vec4(pos.x - szX, pos.y - szY, pos.z, pos.w);
	EmitVertex();

	texCoord = getTexCoord(texbase[0], sztx, -sztx);
	gl_Position = vec4(pos.x + szX, pos.y - szY, pos.z, pos.w);
	EmitVertex();

	texCoord = getTexCoord(texbase[0], -sztx, sztx);
	gl_Position = vec4(pos.x - szX, pos.y + szY, pos.z, pos.w);
	EmitVertex();

	texCoord = getTexCoord(texbase[0], sztx, sztx);
	gl_Position = vec4(pos.x + szX, pos.y + szY, pos.z, pos.w);
	EmitVertex();

	EndPrimitive();
}
