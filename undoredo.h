/*
 * undoredo.h: public functions to manage journal.
 *
 * written by T.Pierron, jan 2022
 */

#ifndef MCUNDOREDO_H
#define MCUNDOREDO_H

enum
{
	LOG_SELECTION = 1,         /* selection changed */
	LOG_BLOCK,                 /* single block changed (possibly with tile entity) */
	LOG_ENTITY,                /* single entity changed */
	LOG_REGION,                /* entire region changed */
};

void undoLog(int type, ...);
void undoOperation(void);
void undoDebug(void);

#define UNDO_LINK              0x80

#ifdef UNDOREDO_IMPL
#define UNDO_LOG_SIZE          4096
#define HAS_TILEENTITY         0x80000000

typedef struct UndoLogBuf_t *          UndoLogBuf;
typedef struct UndoSelection_t *       UndoSelection;
typedef struct UndoBlock_t *           UndoBlock;

struct UndoSelection_t         /* LOG_SELECTION */
{
	int32_t  start[3];
	int32_t  end[3];
	uint32_t typeSize;
};

struct UndoBlock_t             /* LOG_BLOCK */
{
	uint32_t itemId;
	int32_t  loc[3];
	uint32_t typeSize;
};

struct UndoLogBuf_t            /* memory storage for undo/redo */
{
	ListNode node;             /* linked list of blocks */
	int      usage;            /* how many bytes in this block */
	uint8_t  buffer[4];        /* array of UNDO_LOG_SIZE (but gdb doesn't like big array) */
};

struct UndoPrivate_t           /* global private */
{
	uint8_t  inUndo;
	ListHead undoLog;          /* list of memory blocks to undo */
	ListHead redoLog;
};

#endif
#endif
