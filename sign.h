/*
 * sign.h: public functions to manage text displayed inside sign.
 *
 * Written by T.Pierron, jan 2021
 */

#ifndef MCSIGN_H
#define MCSIGN_H

#include "chunks.h"

Bool signInitStatic(int font);
int  signAddToList(int blockId, ChunkData cd, int offset, int prev, uint8_t light);
void signFillVertex(int blockId, float pt[6], int uv[4]);
void signPrepare(vec4 camera);
void signDel(DATA8 tile);
void signDelAll(void);
void signRender(void);
void signGetText(vec4 pos, DATA8 text, int max);
void signSetText(Chunk, vec4 pos, DATA8 text);

extern char signMinText[];

/*
 * private stuff below
 */
#ifdef SIGN_IMPL
#define SIGN_HEIGHT    64
#define SIGN_WIDTH     128
#define SIGN_MAX_DIST  80
#define SIGN_TEXTS     128         /* need to be multiple of 32 */
#define BANK_WIDTH     8
#define BANK_HEIGHT    16
#define BANK_MAX       (BANK_WIDTH * BANK_HEIGHT)


typedef struct SignText_t *        SignText;
typedef struct SignText_t          SignText_t;
typedef struct SignBank_t *        SignBank;
typedef struct SignTextBank_t *    SignTextBank;
typedef struct NVGLUframebuffer *  NVGFBO;

struct SignText_t
{
	int      XYZ[3];               /* coord of tile entity (world space) */
	uint8_t  light;                /* unused since tex is black :-/ */
	uint8_t  empty;                /* no text to render: don't render a texture quad */
	DATA8    tile;                 /* raw ptr to tile entity */
	int16_t  next;                 /* linked list for signs of a chunk */
	int16_t  bank;                 /* first 8bits: index of bank in signs.banks, next: [0-127] slot in bank, or -1 */
	uint16_t text[4];              /* offset in tile entity where to find text */
	float    pt1[3];               /* coord of quad in world space */
	float    pt2[3];
};

struct SignBank_t                  /* 56 bytes */
{
	NVGFBO   nvgFBO;               /* offscreen tex (8bit) */
	uint32_t usage[4];             /* 128 slots (bitfield) */
	uint8_t  inBank;               /* usage */
	uint8_t  inMDA;                /* draw count */
	uint8_t  update;               /* recompute mipmaps */
	int      vbo;
	int      vao;
	int *    mdaFirst;
};

/*
 * signs are added to the list in a separate thread: cannot realloc SignText buffers
 */
struct SignTextBank_t
{
	ListNode   node;
	SignText_t signs[SIGN_TEXTS];  /* 7168 bytes (128 * 56) */
	uint32_t   usage[SIGN_TEXTS>>5];
	int        count;
};

struct SignPrivate_t
{
	ListHead list;                 /* SignTextBank */
	SignBank banks;                /* array of <maxBank> items */
	int      count, maxBank;       /* count == total signs in <list> */
	int      toRender;             /* signs in render dist */
	int      listDirty;
	int      curXYZ[3];
	int      mdaCount[BANK_MAX];   /* should be property of SignBank, but will always be 6 */
	int      font;
	int      shader;
};

#endif
#endif
