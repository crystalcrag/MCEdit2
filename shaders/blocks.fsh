/*
 * fragment shader for drawing opaque and transparent blocks.
 */
#version 430 core

out vec4 color;

in  vec2 tc;
in  float skyLight;
in  float blockLight;
in  float shadeOCS;
flat in int rswire;
flat in vec2 texOrigin;
flat in uint ocsmap;

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
	if (rswire == 1)
	{
		/* use color from terrain to shade wire: coord are located at tile 31x3.5 to 32x3.5 */
		color *= texture(blockTex, vec2(0.96875 + shadeOCS * 0.03125, 0.0556640625));
		return;
	}

	float shade = shadeOCS;
	if (ocsmap > 0)
	{
		/*
		 * ambient occlusion of half-slab applied on a full-voxel: cheaper than splitting the face
		 * into 4 half voxels.
		 */
		float dx = tc.x - texOrigin.x; if (dx < 0) dx = -dx;
		float dy = tc.y - texOrigin.y; if (dy < 0) dy = -dy;
		int   origin = 0;
		if (dx >= 0.015625)  { origin += 1; dx -= 0.015625; }
		if (dy >= 0.0078125) { origin += 3; dy -= 0.0078125; }
		shade += mix(
			mix((ocsmap & (1<<origin))   > 0 ? 0.25 : 0, (ocsmap & (1<<origin+3)) > 0 ? 0.25 : 0, dy*128),
			mix((ocsmap & (1<<origin+1)) > 0 ? 0.25 : 0, (ocsmap & (1<<origin+4)) > 0 ? 0.25 : 0, dy*128),
			dx*64
		);
		if (shade > 1) shade = 1;
	}

	/* last tex line: first 16 tex are biome dep */
	if (tc.y >= 0.96875 && color.x == color.y && color.y == color.z)
	{
		color.x *= biomeColor.x;
		color.y *= biomeColor.y;
		color.z *= biomeColor.z;
	}

	float sky = 0.9 * skyLight * skyLight + 0.1 - shade; if (sky < 0) sky = 0;
	float block = blockLight * blockLight * (1 - sky);
	color *= vec4(sky, sky, sky, 1) + vec4(1.5 * block, 1.2 * block, 1 * block, 0);
}
