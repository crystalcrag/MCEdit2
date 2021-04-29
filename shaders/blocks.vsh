#version 430 core

/*
 * vertex shader for drawing opaque and transparent blocks only.
 *
 * position and info encode the following values:
 * - position.x
 * - position.y
 * - position.z : position relative to offsets + (position.xyz - 1024) / 2048.
 * - info.x[bit0  -  8] : U tex coord (0 - 511)
 * - info.x[bit9  - 15] : V tex coord (7bits, hi part).
 * - info.y[bit0  -  2] : V tex coord (3bits, lo part).
 * - info.y[bit3  -  5] : side (3bits, normal vector): 0 = south, 1 = east, 2 : north, 3 = west, 4 = top, 5 = bottom, 6 = don't care
 * - info.y[bit6  -  7] : ambient occlusion
 * - info.y[bit8  - 11] : block light value
 * - info.y[bit12 - 15] : sky light value
 */
layout (location=0) in ivec3 position;
layout (location=1) in ivec2 info;
layout (location=2) in vec3  offsets;
layout (binding=0) uniform sampler2D blockTex;

uniform vec3 biomeColor;

/* extension provided by stb_include.h */
#include "uniformBlock.glsl"

out vec2 tc;
out float skyLight;
out float blockLight;
out float shadeOCS;
flat out int biomeCol;
flat out int rswire;


void main(void)
{
	vec4 pos = vec4(
		float(position.x - 15360) * 0.00048828125,
		float(position.y - 15360) * 0.00048828125,
		float(position.z - 15360) * 0.00048828125,
		1
	);
	float U = float(info.x & 511);
	float V = float(((info.x >> 6) & ~7) | (info.y & 7));
	float shade = 1;
	int   normal = (info.y >> 3) & 7;

	if (V == 1023) V = 1024;
	if (U == 511)  U = 512;

	/* last tex line: first 16 tex are biome dep, next are offset tex */
	biomeCol = V >= 62*16 ? 1 : 0;
	rswire = 0;
	if (normal < 6)
		shade = shading[normal].x;

	tc = vec2(U * 0.001953125, V * 0.0009765625);

	/* ambient occlusion */
	shadeOCS = 0;
	switch ((info.y >> 6) & 3) {
	case 1: shade -= 0.2; shadeOCS = 0.025; break;
	case 2: shade -= 0.3; shadeOCS = 0.05; break;
	case 3: shade -= 0.5; shadeOCS = 0.1; 
	}
	skyLight   = float((info.y >> 12) & 15) / 15;
	blockLight = float((info.y >> 8)  & 15) / 15;
	if (normal == 7)
	{
		shadeOCS = blockLight;
		rswire = 1;
	}
	else if (blockLight > skyLight)
	{
		/* diminish slightly ambient occlusion if there is blockLight overpowering skyLight */
		shade += (blockLight - skyLight) * 0.35;
		if (shade > 1) shade = 1;
	}
	blockLight *= shade;
	skyLight   *= shade;

	if (shading[0].w > 0)
	{
		/* inventory items: using an orthogonal projection on XY plane */
		float scale = offsets.z;
		/* <offsets> is relative to bottom left corner of rendering box */
		if (normal < 6)
		{
			/* cube: due to rotation, scale and offset need to be slightly adjusted */
			scale *= 0.625;
			pos = mvMatrix * ((pos - vec4(0.5, 0.5, 0.5, 0)) * vec4(scale,scale,scale,1)) + vec4(offsets.x + 0.85 * scale, offsets.y + 0.8 * scale, 25, 0);
		}
		else pos = pos * vec4(scale,scale,1,1) + vec4(offsets.x, offsets.y, 20, 0); /* quad */
		gl_Position = projMatrix * pos;
	}
	else
	{
		gl_Position = projMatrix * mvMatrix * (pos + vec4(offsets, 0));
	}
}
