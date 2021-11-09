/*
 * geometry shader for particles: simply convert VBO points into quads
 */

#version 430

layout (points) in;
layout (triangle_strip, max_vertices = 4) out;

#include "uniformBlock.glsl"

flat in float size[];
flat in int   texbase[];
flat in int   type[];
flat in int   light[];
flat in int   color[];

     out vec2  texCoord;
flat out vec2  skyBlockLight;
flat out int   ptype;
flat out vec2  texColor;

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
	int   tmax = int(size[0]); if (tmax > 4) tmax = 4;
	int   tmin = - tmax;
	float szX  = size[0] / 64;
	float szY  = szX * ASPECT_RATIO;

	/* that way we will get a billboard effect at the same time */
	ptype = type[0];
	skyBlockLight = vec2(float(light[0] >> 4) / 15., float(light[0] & 15) / 15.);
	if (type[0] == 2) /* smoke */
	{
		texColor = vec2(float((color[0] & 15) + 31*16) * (1/512.), float(color[0] >> 4) * (1/1024.));
		tmin = 0;
		tmax = -8;
	}
	texCoord = getTexCoord(texbase[0], tmin, tmin);
	gl_Position = vec4(pos.x - szX, pos.y - szY, pos.z, pos.w);
	EmitVertex();

	texCoord = getTexCoord(texbase[0], tmax, tmin);
	gl_Position = vec4(pos.x + szX, pos.y - szY, pos.z, pos.w);
	EmitVertex();

	texCoord = getTexCoord(texbase[0], tmin, tmax);
	gl_Position = vec4(pos.x - szX, pos.y + szY, pos.z, pos.w);
	EmitVertex();

	texCoord = getTexCoord(texbase[0], tmax, tmax);
	gl_Position = vec4(pos.x + szX, pos.y + szY, pos.z, pos.w);
	EmitVertex();

	EndPrimitive();
}
