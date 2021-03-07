#version 430

out vec4 color;
in  vec2 texCoord;
in  vec2 skyBlockLight;

layout (binding=0) uniform sampler2D blockTex;

void main(void)
{
	color = texture(blockTex, texCoord);

	float sky = 0.80 * skyBlockLight.x * skyBlockLight.x + 0.2; if (sky < 0) sky = 0;
	float block = skyBlockLight.y * skyBlockLight.y * (1 - sky);

	color *= vec4(sky, sky, sky, 1) + vec4(1.5 * block, 1.2 * block, 1 * block, 0);
}
