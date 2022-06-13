/*
 * library.h: public functions to handle library of schematics.
 *
 * Written by T.Pierron, sep 2021.
 */


#ifndef MCLIBRARY_H
#define MCLIBRARY_H

void libraryCopySelection(Map brush);
void libraryShow(int type);
int  libraryImport(SIT_Widget w, APTR cd, APTR ud);

#ifdef MCLIBRARY_IMPL /* private stuff below */
#include "NBT2.h"
#include "nanovg.h"
#include "nanovg_gl_utils.h"

typedef struct LibBrush_t *        LibBrush;
typedef struct NVGLUframebuffer *  NVGFBO;

void libraryFreeBrush(LibBrush);

struct MCLibrary_t
{
	ListHead   brushes;         /* LibBrush */
	mat4       matPerspective;
	int        uboShader;
	int        nbBrushes;
	uint8_t    saveBrush;       /* action when select callback is triggered */
	uint8_t    saveFromLib;     /* otherwise save clone selection */
	uint8_t    confirm;         /* expensive operation about to be done, ask confirmation first */
	SIT_Widget copyWnd;         /* list of copied brush (top right corner of screen) */
	SIT_Widget copyList, save;
	SIT_Widget use, del;
	SIT_Widget exportTo;
};

struct LibBrush_t
{
	ListNode   node;
	Map        data;            /* brush as originally copied */
	int        size;            /* in bytes */
	uint16_t   thumbSz;         /* in px */
	uint8_t    staticStruct;    /* LibBrush cannot be free()'ed */
	NVGFBO     nvgFBO;          /* preview of brush */
	NBTFile_t  nbt;             /* if brush has been read from a schematic file */
};

#endif
#endif
