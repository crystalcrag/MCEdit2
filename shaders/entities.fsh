#version 430

/*
 * fragment shader for entities
 */

flat in  int   isBlock;
flat in  int   isSelected;
     out vec4  color;
     in  vec2  texcoord;
     in  float skyLight;
     in  float blockLight;

layout (binding=0) uniform sampler2D blocksTex;
layout (binding=1) uniform sampler2D mobTex;

void main(void)
{
	if (isBlock > 0)
		color = texture(blocksTex, texcoord);
	else
		color = texture(mobTex, texcoord);

	float sky = 0.9 * skyLight * skyLight + 0.1; if (sky < 0) sky = 0;
	float block = blockLight * blockLight * (1 - sky);
	color *= vec4(sky, sky, sky, 1) + vec4(1.5 * block, 1.2 * block, 1 * block, 0);

	if (isSelected > 0)
		color = mix(color, vec4(1,1,1,1), 0.5);
}
