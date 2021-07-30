/*
 * fragment shader for drawing opaque and transparent blocks.
 */
#version 430 core

#include "uniformBlock.glsl"

out vec4 color;

in  vec2 tc;
in  float skyLight;
in  float blockLight;
flat in int rswire;
flat in vec2 texOrigin;
flat in uint ocsmap;
flat in int normal;

/*
 * Main texture for blocks
 */
layout (binding=0) uniform sampler2D blockTex;

uniform vec3 biomeColor;

void main(void)
{
	color = texture(blockTex, tc);
	/* prevent writing to the depth buffer: easy way to handle opacity for transparent block */
	if (color.a < 0.004)
		discard;
	if (rswire >= 1)
	{
		/* use color from terrain to shade wire: coord are located at tile 31x3.5 to 32x3.5 */
		color *= texture(blockTex, vec2(0.96875 + float(rswire-1) * 0.03125, 0.0556640625));
		return;
	}

	/* ambient occlusion */
	float shade = 0;
	float shadeLight = 0;
	if (ocsmap > 0)
	{
		/* ambient occlusion for normal blocks */
		const float intensity[] = float[4](0, 0.2, 0.35, 0.5);
		float dx = (tc.x - texOrigin.x) * 32; if (dx < 0) dx = -dx;
		float dy = (tc.y - texOrigin.y) * 64; if (dy < 0) dy = -dy;
		float ocsval = (normal == 4 ? 1.3 : 1) * (skyLight * 0.5 + 0.5);
		float pt1 = intensity[bitfieldExtract(ocsmap, 0, 2)] * ocsval;
		float pt2 = intensity[bitfieldExtract(ocsmap, 2, 2)] * ocsval;
		float pt3 = intensity[bitfieldExtract(ocsmap, 4, 2)] * ocsval;
		float pt4 = intensity[bitfieldExtract(ocsmap, 6, 2)] * ocsval;
		uint extend = ocsmap >> 9;

		if ((ocsmap & 256) > 0)
		{
			/* half-block ocs: a bit more expensive to process: need to compute contribution from 4 sides */
			#define CORNERSET(pt1, pt2)         (ocsset & ((1 << (pt1*2-2)) | (1 << (pt2*2-2)))) == ((1 << (pt1*2-2)) | (1 << (pt2*2-2)))
			#define CORNERUNSET(pt1, pt2, pt3)  (ocsset & ((1 << (pt1*2-2)) | (1 << (pt2*2-2)) | (1 << (pt3*2-2)))) != (1 << (pt1*2-2))
			uint ocsset = (ocsmap | ((ocsmap & 0xaa) >> 1)) & 0x55;
			/* doc/internals.html or get lost */
			shade = clamp(CORNERSET(1, 4) ?
				mix(mix(pt1, 0, (extend & 2)   > 0 ? dy : dy*2),
				    mix(pt4, 0, (extend & 128) > 0 ? dy : dy*2), dx) : CORNERUNSET(1, 2, 4) ? 0 :
				mix(mix(pt1, 0, (extend & 1) > 0 ? dx : dx*2), 0, (extend & 2) > 0 ? dy : min(dy*2, 1)), 0, 1); /* top left corner */
			float left = clamp(CORNERSET(1, 2) ?
				mix(mix(pt1, 0, (extend & 1) > 0 ? dx : dx*2),
				    mix(pt2, 0, (extend & 4) > 0 ? dx : dx*2), dy) : CORNERUNSET(2, 1, 3) ? 0 :
				mix(0, mix(pt2, 0, (extend & 4) > 0 ? dx : dx*2), (extend & 8) > 0 ? dy : max(dy*2-1, 0)), 0, 1) /* bottom left corner */;
			float right = clamp(CORNERSET(4, 3) ?
				mix(mix(0, pt4, (extend & 64) > 0 ? dx : dx*2-1),
			        mix(0, pt3, (extend & 16) > 0 ? dx : dx*2-1), dy) : CORNERUNSET(3, 2, 4) ? 0 :
				mix(0, mix(0, pt3, (extend & 16) > 0 ? dx : max(dx*2-1, 0)), (extend & 32) > 0 ? dy : max(dy*2-1, 0)), 0, 1); /* bottom right corner */
			float bottom = clamp(CORNERSET(2, 3) ?
				mix(mix(0, pt2, (extend & 8)  > 0 ? dy : dy*2-1),
			        mix(0, pt3, (extend & 32) > 0 ? dy : dy*2-1), dx) : CORNERUNSET(4, 1, 3) ? 0 :
				mix(mix(0, pt4, (extend & 64) > 0 ? dx : max(dx*2-1, 0)), 0, (extend & 128) > 0 ? dy : min(dy*2, 1)), 0, 1); /* top right corner */
			if (left   > shade) shade = left;
			if (right  > shade) shade = right;
			if (bottom > shade) shade = bottom;
		}
		else
		{
			if ((extend & 1) > 0) dy *= 2;
			shade = mix(mix(pt1, pt2, dy), mix(pt4, pt3, dy), (extend & 2) > 0 ? dx*2 : dx);
			if (shade > 1) shade = 1;
		}

		if (blockLight > skyLight)
		{
			/* diminish slightly ambient occlusion if there is blockLight overpowering skyLight */
			shadeLight = (blockLight - skyLight) * shade * 0.8;
		}
	}

	/* last tex line: first 16 tex are biome dep */
	if (tc.y >= 0.96875 && color.x == color.y && color.y == color.z)
	{
		color.x *= biomeColor.x;
		color.y *= biomeColor.y;
		color.z *= biomeColor.z;
	}

	float sky = 0.9 * skyLight * skyLight + 0.1 - shade; if (sky < 0) sky = 0;
	float block = (blockLight * blockLight - shadeLight) * (1 - sky);  if (block < 0) block = 0;
	color *= vec4(sky, sky, sky, 1) + vec4(1.5 * block, 1.2 * block, 1 * block, 0);
}
