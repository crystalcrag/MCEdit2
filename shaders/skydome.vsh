#version 430 core
//---------IN------------
layout (location=0) in vec3 vpoint;
//---------UNIFORM------------
#include "uniformBlock.glsl"

uniform vec4 sun_pos;//sun position in world space
uniform mat3 rot_stars;//rotation matrix for the stars

//---------OUT------------
out vec3 pos;
out vec3 star_pos;
out vec3 sun_norm;

//---------MAIN------------
void main()
{
	gl_Position = projMatrix * mvMatrix * (vec4(vpoint, 1.0) + camera);
	pos = vpoint;

	//Sun pos being a constant vector, we can normalize it in the vshader
	//and pass it to the fshader without having to re-normalize it
	sun_norm = normalize(sun_pos.xyz);

	//And we compute an approximate star position using the special rotation matrix
	star_pos = /*rot_stars **/ normalize(pos);
}
