/*
 * TileFinder.h : public function for main interface
 *
 * Written by T.Pierron, Feb 2020
 */

#ifndef TILEFINDER_H
#define TILEFINDER_H

#include "utils.h"

typedef struct MainCtrl_t   MainCtrl;
typedef struct Block_t      Block;

#define PRIMITIVES   20

struct Block_t
{
	float    vertex[3*4*6];
	uint16_t texUV[48];
	uint8_t  texTrans[6];
	uint8_t  vtxCount;
	float    size[3];
	float    trans[3];
	float    rotate[3];
	float    rotCascade[3];
	uint8_t  faces;
	uint8_t  detailMode;
	uint8_t  detailFaces;
};

struct MainCtrl_t
{
	SIT_Widget dialog, list, app, full, detail;
	SIT_Widget canvas, label, coords, cube, tex;
	SIT_Widget faces[7], lab90;
	mat4       rotation;
	float      scale, trans[2]; /* translate and/or scale cube preview */
	Image      back;
	int        showBBox, detailSel, swapView, cullFace;
	int        faceEdit, editBlock, lastFaceSet;
	int        curCX, curCY;
	int        texId;
	uint8_t    defU, defV;
	int8_t     rot90;
	float      line[3];
	int        nbBlocks;
	TEXT       oldSize[32];
	Block      primitives[PRIMITIVES];
};

enum
{
	MENU_COPY      = 101,
	MENU_PASTE     = 102,
	MENU_RESETVIEW = 103,
	MENU_RESETTEX  = 104,
	MENU_ROT90TEX  = 105,
	MENU_MIRRORTEX = 106,
	MENU_COPYTEX   = 107,
	MENU_SWITCHSEL = 108,
	MENU_SWAPVIEW  = 109,
	MENU_NEXTFACE  = 110,
	MENU_PREVFACE  = 111,
	MENU_ABOUT     = 112,
	MENU_EXIT      = 113
};

#endif
