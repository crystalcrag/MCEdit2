/*
 * library.h: public functions to handle library of schematics.
 *
 * Written by T.Pierron, sep 2021.
 */


#ifndef MCLIBRARY_H
#define MCLIBRARY_H

void libraryCopySelection(Map brush);

#ifdef MCLIBRARY_IMPL /* private stuff below */
#include "NBT2.h"
#include "nanovg.h"
#include "nanovg_gl_utils.h"

typedef struct LibBrush_t *        LibBrush;
typedef struct NVGLUframebuffer *  NVGFBO;

void libraryDelBrush(LibBrush);

struct MCLibrary_t
{
	ListHead   brushes;         /* LibBrush */
	int        uboShader;
	int        nbBrushes;
	SIT_Widget copyWnd;
	SIT_Widget copyList, save, use, del;
	SIT_Widget schematicsWnd;
	NVGCTX     nvgCtx;
};

struct LibBrush_t
{
	ListNode   node;
	Map        data;            /* brush as originally copied */
	int        size;            /* in bytes */
	int        thumbSz;         /* in px */
	NVGFBO     nvgFBO;          /* preview of brush */
	NBTFile_t  nbt;             /* if brush has been read from a schematic file */
};

#endif
#endif
