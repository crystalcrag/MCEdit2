/*
 * raycasting.h: public functions to handle raycasting chunk.
 *
 * written by T.Pierron, may 2022.
 */


#ifndef MC_RAYCASTING_H
#define MC_RAYCASTING_H


APTR raycastConvertToCMap(DATA8 * data, int * width, int * height, int bpp);
void raycastWorld(Map map, mat4 invMVP, vec4 pos);


struct RaycastPrivate_t
{
	DATA8   palette;
	int     paletteStride;
	uint8_t shading[6];
};


#endif

