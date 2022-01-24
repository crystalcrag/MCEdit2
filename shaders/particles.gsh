/*
 * geometry shader for particles: simply convert VBO points into quads
 */

#version 430

layout (points) in;
layout (triangle_strip, max_vertices = 4) out;

#include "uniformBlock.glsl"

flat in float size[];
flat in float angle[];
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

	return vec2(float(clamp(baseU-offX, minU, minU + 16)) * TEX_COORD_X,
	            float(clamp(baseV-offY, minV, minV + 16)) * TEX_COORD_Y);
}

void main(void)
{
	vec4  pos  = gl_in[0].gl_Position;
	int   tmax = int(size[0]); if (tmax > 4) tmax = 4;
	int   tmin = - tmax;
	int   base = texbase[0];
	float szX  = size[0] / 64;
	float szY  = szX;
	vec2  pt1  = vec2(-szX, -szY);
	vec2  pt2  = vec2( szX, -szY);
	vec2  pt3  = vec2(-szX,  szY);
	vec2  pt4  = vec2( szX,  szY);

	if (angle[0] > 0)
	{
		float ca = cos(angle[0]);
		float sa = sin(angle[0]);
		mat2  rotate = mat2(ca, -sa, sa, ca);
		pt1 = rotate * pt1;
		pt2 = rotate * pt2;
		pt3 = rotate * pt3;
		pt4 = rotate * pt4;
	}
	pt1.y *= ASPECT_RATIO;
	pt2.y *= ASPECT_RATIO;
	pt3.y *= ASPECT_RATIO;
	pt4.y *= ASPECT_RATIO;

	/* that way we will get a billboard effect at the same time */
	ptype = type[0];
	skyBlockLight = vec2(float(light[0] >> 4) / 15., float(light[0] & 15) / 15.);
	switch (ptype) {
	case 2: /* smoke */
		texColor = vec2(float((color[0] & 15) + 31*16) * TEX_COORD_X, float(color[0] >> 4) * TEX_COORD_Y);
		tmin = 0;
		tmax = -8;
		break;
	case 3: /* dust */
		texColor = vec2(float(base & 511) * TEX_COORD_X, float(base >> 9) * TEX_COORD_Y);
		base = 31 * 16 + ((9*16+color[0]*8) << 9);
		tmin = 0;
		tmax = -8;
	}

	/* adjust skyight value according to day/night cycle */
	if (sunDir.y < 0.4)
	{
		float sky = (sunDir.y + 0.4) * 1.25;
		if (sky < 0) sky = 0; /* night time */
		sky = sqrt(sky);
		skyBlockLight.x *= sky;
	}

	texCoord = getTexCoord(base, tmin, tmin);
	gl_Position = vec4(pos.x + pt1.x, pos.y + pt1.y, pos.z, pos.w);
	EmitVertex();

	texCoord = getTexCoord(base, tmax, tmin);
	gl_Position = vec4(pos.x + pt2.x, pos.y + pt2.y, pos.z, pos.w);
	EmitVertex();

	texCoord = getTexCoord(base, tmin, tmax);
	gl_Position = vec4(pos.x + pt3.x, pos.y + pt3.y, pos.z, pos.w);
	EmitVertex();

	texCoord = getTexCoord(base, tmax, tmax);
	gl_Position = vec4(pos.x + pt4.x, pos.y + pt4.y, pos.z, pos.w);
	EmitVertex();

	EndPrimitive();
}
