/*
 * Uniform buffer object.
 * Variables shared among all shaders through a GL buffer.
 * Preventing to set uniforms in each shader.
 */

layout (std140, binding = 2) uniform param
{
	mat4 projMatrix;
	mat4 mvMatrix;
	vec4 lookAt;
	vec4 camera;
	vec4 sunDir;
	vec4 normals[6];
	vec4 shading[6];
};

#define ORIGINVTX      15360
#define BASEVTX        0.00048828125
#define MIDVTX         4
#define SCR_WIDTH      shading[0].y     // width of wnd/screen in px
#define SCR_HEIGHT     shading[0].z     // height
#define INVENTORY_ITEM shading[0].w > 0 // see invShading[] in render.c
#define FOG_DISTANCE   shading[1].y     // max distance in blocks
#define FULL_BRIGHT    shading[1].z > 0 // debug
#define TEX_COORD_X    (1/512.)
#define TEX_COORD_Y    (1/1024.)
#define M_PI           3.14159265       // seriously, not defined?
