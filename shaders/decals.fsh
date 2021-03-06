/*
 * decals.fsh: handle texture printed on top of terrain, which includes: signs and maps (item frame).
 */
#version 430

flat in  uint  selected;
flat in  uint  colorDecal;
     in  vec2  tex;
     in  float skyLight;
     in  float blockLight;
     out vec4  color;

layout (binding=0) uniform sampler2D signBank;

void main(void)
{
	vec4 col = texture(signBank, tex);

	if (colorDecal > 0)
	{
		if (col.a < 0.004)
			discard;
		color = col;
	}
	else /* B&W texture */
	{
		if (col.x < 0.004)
			discard;
		color = vec4(0, 0, 0, col.x);
	}

	float sky = 0.9 * skyLight * skyLight + 0.1; if (sky < 0) sky = 0;
	float block = (blockLight * blockLight) * (1 - sky);
	color *= vec4(sky, sky, sky, 1) + vec4(1.5 * block, 1.2 * block, 1 * block, 0);

	if (selected > 0)
		color = mix(color, vec4(1,1,1,1), 0.5);
}
