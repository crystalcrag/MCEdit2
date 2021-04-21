#version 430

/*
 * fragment shader for entities
 */
out vec4 color;
in  vec2 texcoord;
flat in float shade;
flat in int   isBlock;
flat in int   isSelected;

layout (binding=0) uniform sampler2D blocksTex;
layout (binding=1) uniform sampler2D mobTex;

void main(void)
{
	if (isBlock > 0)
		color = texture(blocksTex, texcoord) * shade;
	else
		color = texture(mobTex, texcoord) * shade;
	if (isSelected > 0)
		color = mix(color, vec4(1,1,1,1), 0.5);
}
