#version 430

/*
 * fragment shader for selection
 */
out vec4 color;
flat in int selType;

void main(void)
{
	float a = 1;
	switch (selType >> 2) {
	case 1: a = 0.5; break; /* hidden edges: less opaque */
	case 2: a = 0.25;       /* hidden surface */
	}
	switch (selType & 3) {
	default: color = vec4(0, 0.00, 0, a); break; /* block highlight: black */
	case 1:  color = vec4(1, 1.00, 1, a); break; /* selection cube: white */
	case 2:  color = vec4(1, 0.94, 0, a); break; /* first block sel: yellow */
	case 3:  color = vec4(0, 0.07, 1, a);        /* second block sel: blue */
	}
}
