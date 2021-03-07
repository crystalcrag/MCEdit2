/*
 * Uniform buffer object.
 * Variables shared among all shaders through a GL buffer.
 * Preventing to set uniforms in each shader.
 */

layout (std140, binding = 2) uniform param
{
	mat4 projMatrix;
	mat4 mvMatrix;
	vec4 lightPos;
	vec4 camera;
	vec4 normals[6];
	vec4 shading[6];
};