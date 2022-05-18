#version 430
out vec4 color;
uniform float hidden;

void main(void)
{
	if (hidden > 0)
		color = vec4(1,1,1,0.5);
	else
		color = vec4(1,1,1,1);
}
