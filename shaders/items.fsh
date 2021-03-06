/*
 * fragment shader for drawing opaque and transparent blocks.
 */
#version 430 core

out vec4 color;

in vec2 tc;
in float skyLight;
in float blockLight;
flat in int rswire;

/*
 * Main texture for blocks
 */
layout (binding=0) uniform sampler2D blockTex;

void main(void)
{
	color = texture(blockTex, tc);
	/* prevent writing to the depth buffer: easy way to handle opacity for transparent block */
	if (color.a < 0.004)
		discard;

	if (rswire == 1)
	{
		/* use color from terrain to shade wire: coord are located at tile 31x3.5 to 32x3.5 */
		color *= texture(blockTex, vec2(0.96875, 0.0556640625));
	}
	else
	{
		float sky = 0.9 * skyLight * skyLight + 0.1; if (sky < 0) sky = 0;
		float block = blockLight * blockLight * (1 - sky);
		color *= vec4(sky, sky, sky, 1) + vec4(1.5 * block, 1.2 * block, 1 * block, 0);
	}
}
