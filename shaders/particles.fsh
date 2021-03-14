#version 430

out vec4 color;

     in vec2  texCoord;
flat in vec2  skyBlockLight;
flat in int   ptype;
flat in vec2  texColor;

layout (binding=0) uniform sampler2D blockTex;

void main(void)
{
	int bit;
	switch (ptype) {
	case 1: // bits
		color = texture(blockTex, texCoord);
		break;
	case 2: // smoke
		color = texture(blockTex, texCoord) * texture(blockTex, texColor);
	}
	float sky = 0.80 * skyBlockLight.x * skyBlockLight.x + 0.2; if (sky < 0) sky = 0;
	float block = skyBlockLight.y * skyBlockLight.y * (1 - sky);

	color *= vec4(sky, sky, sky, 1) + vec4(1.5 * block, 1.2 * block, 1 * block, 0);
}
