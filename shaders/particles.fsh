#version 430

out vec4 color;

     in vec2  texCoord;
flat in vec2  skyBlockLight;
flat in int   ptype;
flat in uvec2 pattern;

layout (binding=0) uniform sampler2D blockTex;

void main(void)
{
	int bit;
	switch (ptype) {
	case 0: // exploding
		color = texture(blockTex, texCoord);
		break;
	case 1: // sparks
		bit = int(texCoord.x*7) + int(texCoord.y*7) * 8;
		if (bit >= 32)
			bit = int(pattern.x & (1 << (bit-32)));
		else
			bit = int(pattern.y & (1 << bit));

		color = bit > 0 ? vec4(1, 0, 0, 1) : vec4(0, 0, 0, 0);
	}
	float sky = 0.80 * skyBlockLight.x * skyBlockLight.x + 0.2; if (sky < 0) sky = 0;
	float block = skyBlockLight.y * skyBlockLight.y * (1 - sky);

	color *= vec4(sky, sky, sky, 1) + vec4(1.5 * block, 1.2 * block, 1 * block, 0);
}
