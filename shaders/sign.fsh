#version 430

out vec4  color;
in  float shade;
in  vec2  tex;

layout (binding=0) uniform sampler2D signBank;

void main(void)
{
	vec4 col = texture(signBank, tex);

	if (col.x < 0.004)
		discard;

	// xxx shade not necessary ?
	color = vec4(0, 0, 0, col.x);
}
