/*
 * raycasting.h: public functions to handle raycasting chunk.
 *
 * written by T.Pierron, may 2022.
 */


#ifndef MC_RAYCASTING_H
#define MC_RAYCASTING_H


Bool raycastInit(Map map);
Bool raycastInitStatic(void);
APTR raycastConvertToCMap(DATA8 * data, int * width, int * height, int bpp);
void raycastWorld(Map map, mat4 invMVP, vec4 pos);
void raycastDelete(void);
void raycastRender(void);
void raycastMoveCenter(Map map);

struct ChunkTexture_t
{
	ListNode node;
	uint32_t usage[32];
	int      textureId;
	int      slots;
};

struct RaycastPrivate_t
{
	ListHead texBanks;             /* ChunkTexture */
	DATA8    palette;
	int      paletteStride;
	uint8_t  shading[6];
	int      texRadius;
	int      texHole;
	DATA16   texMap;
	int      shader;
	int      vao, vbo;
};

typedef struct ChunkTexture_t *    ChunkTexture;

#endif

