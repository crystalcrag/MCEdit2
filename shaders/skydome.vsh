/*
 * render the sky in MCEditv2.
 *
 * orignal idea from Simon Rodriguez, https://github.com/kosua20/opengl-skydome
 *
 * check doc/internals.html for changes that have been done.
 */
#version 430 core

// input vertices will be a sphere
layout (location=0) in vec3 vpoint;
#include "uniformBlock.glsl"

uniform float sun_angle;
uniform vec4  overlay;

out vec3 pos;
out vec3 star_pos;
out vec3 sun_norm;

void main()
{
	gl_Position = projMatrix * mvMatrix * (vec4(vpoint, 0) + vec4(camera.x, 64, camera.z, 1));
	pos = vpoint;

	// sun pos being a constant vector, we can normalize it in the vshader
	// and pass it to the fshader without having to re-normalize it
	sun_norm = normalize(sunDir.xyz);

	// and we compute an approximate star position using the special rotation matrix
	float ca = cos(sun_angle);
	float sa = sin(sun_angle);
	star_pos = mat3(ca, -sa, 0, sa, ca, 0, 0, 0, 1) * pos;
}
