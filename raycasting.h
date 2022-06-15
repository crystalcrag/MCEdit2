/*
 * raycasting.h: public functions to handle raycasting chunk.
 *
 * written by T.Pierron, may 2022.
 */


#ifndef MC_RAYCASTING_H
#define MC_RAYCASTING_H


Bool raycastInitStatic(void);
void raycastInitMap(Map map);
APTR raycastConvertToCMap(DATA8 * data, int * width, int * height, int bpp);
void raycastWorld(Map map, mat4 invMVP, vec4 pos);
void raycastRebuiltPriority(Map map);
void raycastFreeAll(void);
void raycastRender(void);
void raycastUpdateTexMap(void);
Bool raycastNextChunk(int XZId[3]);
void raycastFlushChunk(DATA8 rgbaTex, int XZ, int Y, int maxy);
void chunkConvertToRGBA(ChunkData, DATA8 rgba);

#define TEXTURE_SLOTS			1024

#ifdef RAYCASTING_IMPL
struct ChunkTexture_t
{
	ListNode node;
	uint32_t usage[TEXTURE_SLOTS/32];
	int      textureId;
	int      total;
	#ifdef DEBUG
	DATA8    data;
	#endif
};


struct RaycastPrivate_t
{
	ListHead texBanks;               /* ChunkTexture */
	DATA8    palette;                /* derived from terrain.png (RGBA 32x64px) */
	int      paletteStride;          /* bytes per scanline for <palette> */
	uint8_t  shading[6];             /* CPU rendering */
	int      distantChunks;          /* distance in chunks distant chunk extends */
	int      rasterChunks;           /* distance in chunks for rasterizaed chunks */
	int      texMapWidth;
	DATA16   texMap;                 /* will contain in which <texBanks> distant chunks are */
	DATA16   priorityMap;            /* to load chunk that are visible first */
	int      priorityMax;
	int      priorityIndex;
	int      priorityFrame;          /* update priority map if frustum has changed */
	DATA8    bitmapMap;              /* to avoid adding twice the same chunk when building priorityMap */
	int      bitmapMax;
	int      shader;                 /* raycasting.vsh */
	int      vao, vbo, vboCount;
	int      uniformINVMVP;          /* shader param */
	int      uniformChunk;
	int      uniformSize;
	float    chunkLoc[4];            /* shader uniform */
	float    chunkSize[4];
	int      texMapId;               /* texture id, copy of texMap but on GPU */
	Map      map;
	int      Xmin, Zmin;             /* world coord of north-west corner of raster chunks */
	int      Xdist, Zdist;           /* distant chunk coord (north-west) */
	int      maxSlot;                // DEBUG
	int      chunkMaxHeight;         /* max height (in chunks) of all chunks in distant region */
	uint16_t chunkVisible[2];        /* to limit the work to be done by raycastRebuiltPriority() */
};

typedef struct ChunkTexture_t *      ChunkTexture;
#endif
#endif

