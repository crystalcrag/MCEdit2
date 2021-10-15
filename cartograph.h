/*
 * cartograph.h : public functions to render in-game maps.
 *
 * Written by T.Pierron, oct 2021.
 */

#ifndef CARTOGRAPH_H
#define CARTOGRAPH_H

#include "utils.h"

void cartoAddMap(int entityId, float coord[6], int mapId, DATA32 light);
void cartoDelMap(int entityId);
void cartoSetSelect(int entityId, Bool set);
void cartoRender(void);


#ifdef CARTOGRAPH_IMPL
#define CARTO_HEIGHT   128
#define CARTO_WIDTH    128
#define CBANK_WIDTH    8
#define CBANK_HEIGHT   8
#define CBANK_MAX      (CBANK_WIDTH * CBANK_HEIGHT)
#define CBANK_NUM(b)   ((b) & 0x3ff)
#define CBANK_SLOT(b)  ((b) >> 10)

typedef struct Cartograph_t *      Cartograph;
typedef struct CartoBank_t *       CartoBank;

struct Cartograph_t                /* one in-game map */
{
	int      entityId;             /* reference entity */
	int      mapId;                /* id used to locate file on disk */
	uint8_t  light[4];             /* sky+block light */
	uint8_t  normal;               /* S,E,N,W,T,B */
	int16_t  bank;                 /* first 10bits: index of bank in cartograph.banks, next: [0-63] slot in bank */
	float    points[6];            /* coord of quad in world space (2 * vec3) */
};

struct CartoBank_t                 /* group 64 maps in a texture */
{
	int      glTex;                /* offscreen tex (32bits) */
	uint32_t usage[2];             /* 64 slots (bitfield) */
	uint8_t  inBank;               /* usage */
	uint8_t  inMDA;                /* draw count */
	uint8_t  update;               /* recompute mipmaps */
	int      vbo;
	int      vao;
	int *    mdaFirst;             /* array of CBANK_MAX int (used by glMultiDrawArrays) */
};

struct CartoPrivate_t              /* mostly the same fields than signs */
{
	Cartograph maps;               /* array of <count> items */
	DATA32     usage;              /* array of <count>/32+1 items */
	CartoBank  banks;              /* array of <maxBank> items */
	int        count;
	int        max, maxBank;
	int        toRender;           /* maps to render */
	int *      mdaCount;           /* static array for glMultiDrawArrays */
	int        shader;             /* decals.vsh */
};


#endif
#endif
