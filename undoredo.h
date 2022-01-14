/*
 * undoredo.h: public functions to manage journal.
 *
 * written by T.Pierron, jan 2022
 */

#ifndef MCUNDOREDO_H
#define MCUNDOREDO_H

enum
{
	LOG_SELECTION = 1,             /* selection changed */
	LOG_BLOCK,                     /* single block changed (possibly with tile entity) */
	LOG_ENTITY_CHANGED,            /* single entity changed */
	LOG_ENTITY_ADDED,
	LOG_ENTITY_DEL,
	LOG_REGION_START,              /* one block in the region */
	LOG_REGION_END,                /* trailer of the region changed */
};

void undoLog(int type, ...);
void undoOperation(int redo);
void undoDebug(void);

#define UNDO_LINK                  0x80

#ifdef UNDOREDO_IMPL
#define UNDO_LOG_SIZE              4096
#define HAS_TILEENTITY             0x80000000

typedef struct UndoLogBuf_t *      UndoLogBuf;
typedef struct UndoSelection_t *   UndoSelection;
typedef struct UndoBlock_t *       UndoBlock;

struct UndoSelection_t             /* LOG_SELECTION, LOG_REGION */
{
	int32_t  start[3];
	int32_t  size[3];
	uint32_t typeSize;
};

struct UndoBlock_t                 /* LOG_BLOCK */
{
	uint32_t itemId;
	int32_t  loc[3];
	uint32_t typeSize;
};

struct UndoEntity_t                /* LOG_ENTITY */
{
	float    loc[3];
	uint32_t entityId;
	uint32_t typeSize;
};

struct UndoLogBuf_t                /* memory storage for undo/redo */
{
	ListNode node;                 /* linked list of blocks */
	int      usage;                /* how many bytes in this block */
	uint8_t  buffer[4];            /* array of UNDO_LOG_SIZE (but gdb doesn't like big array) */
};

struct UndoPrivate_t               /* global private */
{
	uint8_t  inUndo;
	uint8_t  inSelection;
	int      regionRepeatId;       /* repeat this block id */
	DATA16   regionRepeatLoc;      /* need to modify this location to UNDO_BLOCK_REPEAT if repeat */
	uint16_t regionRepeat;         /* repeat this many times */
	int      regionOffset;         /* offset within regionLoc and regionSize of last block added */
	int      regionLoc[3];         /* starting point of region */
	int      regionSize[3];
	int      regionBytes;          /* size in bytes of all the modifications done by region */
	ListHead undoLog;              /* list of memory blocks to undo */
	ListHead redoLog;
};

enum                               /* special blockId stored in undo buffer stream (instead of actual blockId) */
{
	UNDO_BLOCK_TILEENT = 0xffff,   /* next uint16 is size, then <size> bytes of tile entity data */
	UNDO_BLOCK_SKIP    = 0xfffe,   /* next uint16 is amount to add to regionOffset (if other than 1) */
	UNDO_BLOCK_SKIP32  = 0xfffd,   /* next 2 uint16 is amount to skip */
	UNDO_BLOCK_REPEAT  = 0xfffc    /* next uint16 is count of blocks to repeat at offset <regionOffset+1> to <regionOffset+count> */
};

#endif
#endif
