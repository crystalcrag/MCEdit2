/*
 * UtilityLibLite.h : main (and only) header for this simple utility library.
 *                    Trimmed down version of UtilityLib.h specifically designed for SIT.
 *
 * Written by T.Pierron, nov 2013.
 */

#ifndef	UTILITYLIBLITE_H
#define	UTILITYLIBLITE_H

#include <stdarg.h>
#include <stdint.h>

#ifndef DLLIMP
#ifdef DEBUG_SIT
# define DLLIMP
#elif BUILDING_DLL
# define DLLIMP __declspec (dllexport)
#else
# define DLLIMP __declspec (dllimport)
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Basic datatypes */
typedef char                     TEXT;
typedef char *                   STRPTR;
typedef void *                   APTR;
typedef uint8_t *                DATA8;
typedef unsigned long            ULONG;
typedef unsigned long long       ULLONG;
typedef struct ListHead_t        ListHead;
typedef struct ListNode_t        ListNode;
typedef struct ScanDirData_t     ScanDirData;
typedef struct Lang_t *          Lang;
typedef struct vector_t *        vector;
typedef struct vector_t          vector_t;
typedef APTR                     Mutex;
typedef APTR                     Semaphore;
typedef ULONG                    Thread;

#ifndef	False
typedef	enum { False, True }     Bool;
#endif

/* List datatype */
struct ListHead_t
{
	ListNode * lh_Head;
	ListNode * lh_Tail;
};

struct ListNode_t
{
	ListNode * ln_Next;
	ListNode * ln_Prev;
};

struct ScanDirData_t
{
	APTR     path;
	int      isDir;
	uint64_t size;
	TEXT     date[24];
	STRPTR   type;
	STRPTR   name;
	APTR     handle;
	int      error;
};

/* List.c */
typedef int (*ListSortFunc)(ListNode *, ListNode *);

DLLIMP void       ListAddHead(ListHead *, ListNode *);
DLLIMP void       ListAddTail(ListHead *, ListNode *);
DLLIMP void       ListInsert(ListHead *, ListNode * item, ListNode * insert_after);
DLLIMP void       ListInsertSort(ListHead *, ListNode *, ListSortFunc);
DLLIMP void       ListSort(ListHead *, ListSortFunc);
DLLIMP void       ListRemove(ListHead *, ListNode *);
DLLIMP ListNode * ListRemHead(ListHead *);
DLLIMP ListNode * ListRemTail(ListHead *);

#define	ListNew(ptr)        memset(ptr, 0, sizeof *ptr)
#define	NEXT(ptr)           (ptr = (APTR) ((ListNode *)(ptr))->ln_Next)
#define	PREV(ptr)           (ptr = (APTR) ((ListNode *)(ptr))->ln_Prev)
#define	HEAD(list)          ((APTR)(list).lh_Head)
#define	TAIL(list)          ((APTR)(list).lh_Tail)

/* needed if node field is not at the begining of struct */
#define START_OF(node, ptrType, field)   ((ptrType) ((DATA8)(node) - (ULONG) &((ptrType)0L)->field))

/* DOS.c prototypes */
DLLIMP int    ScanDirInit(ScanDirData * ret, STRPTR path);
DLLIMP int    ScanDirNext(ScanDirData *);
DLLIMP void   ScanDirCancel(ScanDirData *);
DLLIMP Bool   AddPart(STRPTR dir, STRPTR file, int max);
DLLIMP int    ParentDir(STRPTR path);
DLLIMP Bool   IsDir(STRPTR path);
DLLIMP STRPTR CreatePath(STRPTR path, Bool not_last);
DLLIMP STRPTR BaseName(STRPTR path);
DLLIMP void   SafePath(STRPTR path);
DLLIMP ULONG  TimeStamp(STRPTR path, int type);
DLLIMP int    Execute(STRPTR cmd, int timeout);
DLLIMP int    OpenDocument(STRPTR filename);
DLLIMP ULLONG FileSize(STRPTR path);
DLLIMP void   GetErrorMsg(int code, STRPTR buffer, int max);
DLLIMP int    GetErrorCode(void);
DLLIMP STRPTR GetError(void);
DLLIMP Bool   DeleteDOS(STRPTR path);
DLLIMP Bool   FileExists(STRPTR file);
DLLIMP Bool   FileRename(STRPTR from, STRPTR to, Bool overwrite);
DLLIMP Bool   FileCopy(STRPTR from, STRPTR to, Bool overwrite);
DLLIMP Bool   IsSameFile(STRPTR file1, STRPTR file2);
DLLIMP Bool   IsRelativePath(STRPTR file);
DLLIMP Bool   TruncateFile(STRPTR file, uint64_t offset);
DLLIMP STRPTR ExpandEnvVar(STRPTR str);
DLLIMP void   ExpandEnvVarBuf(STRPTR str, STRPTR utf8, int max);
DLLIMP void * fopen_enc(const char * file, const char * mode);
DLLIMP void   GetSelectionColor(DATA8 fg, DATA8 bg);
DLLIMP ULONG  TimeMS(void);
DLLIMP float  RandRange(float min, float max);
DLLIMP int    GetDefaultPath(int type, STRPTR out, int max);

enum /* possible values for <type> parameter of GetDefaultPath() */
{
	FOLDER_APPDATA,
	FOLDER_MYDOCUMENTS,
	FOLDER_MYPICTURES
};

/* String.c */
DLLIMP void   CopyString(STRPTR destination, STRPTR source, int max);
DLLIMP int    StrCat(STRPTR dest, int max, int pos, STRPTR cat);
DLLIMP int    FindInList(STRPTR list, STRPTR word, int len);
DLLIMP STRPTR StripCRLF(STRPTR in);
DLLIMP STRPTR NextLine(STRPTR start, Bool cut);
DLLIMP int    Split(STRPTR * table, STRPTR str, int max, int chr);
DLLIMP int    StrToUpper16(STRPTR str, int len); /* UTF-16 */
DLLIMP int    StrToLower16(STRPTR str, int len); /* UTF-16 */
DLLIMP int    StrToLower(STRPTR str, int len);
DLLIMP int    StrCount(STRPTR list, int chr);
DLLIMP int    FormatNumber(STRPTR buffer, int max, STRPTR fmt, int num);

/* Encodings.c */
DLLIMP int    CP2UTF8(DATA8 dest, int cp);
DLLIMP STRPTR NthChar(STRPTR str, int nth);
DLLIMP int    UTF8ToUTF16(STRPTR out, int max, STRPTR in, int len);
DLLIMP int    UTF16ToUTF8(STRPTR out, int max, STRPTR in, int len);

/* high performance frame counter */
DLLIMP int    FrameSetFPS(int fps);
DLLIMP double FrameGetFPS(void);
DLLIMP double FrameGetTime(void);
DLLIMP void   FrameWaitNext(void);
DLLIMP void   FramePauseUnpause(Bool pause);
DLLIMP void   FrameSaveRestoreTime(Bool save);

/* Some useful macros */
#define	IsDef(val)         ((val) && (val)[0])
#define	STRDUPA(str)       strcpy(alloca(strlen(str)+1), str)
#define	DIM(table)         (sizeof(table) / sizeof(table[0]))
#define	EOT(table)         (table + DIM(table))
#define	swap(a, b)         ((a) ^= (b), (b) ^= (a), (a) ^= (b))
#define	swap_tmp(a,b,t)    (t = a, a = b, b = t)
#define	offsetp(p,f)       ((ULONG) (&((p)0L)->f))
#define STRINGIFY(x)       #x
#define TOSTRING(x)        STRINGIFY(x)
#ifndef	MIN
#define	MIN(a, b)          ((a) < (b) ? (a) : (b))
#endif
#ifndef	MAX
#define	MAX(a, b)          ((a) > (b) ? (a) : (b))
#endif
#define BE24(data)         (((((data)[0]<<8)|(data)[1])<<8)|(data)[2])
#define TOBE24(data, num)  (data)[2] = num&0xff, num>>=8, (data)[1] = num&0xff, num>>= 8, (data)[0] = num

/* Thread.c */
typedef void (*ThreadCb)(void *);

DLLIMP Mutex     MutexCreate(void);
DLLIMP void      MutexEnter(Mutex);
DLLIMP void      MutexLeave(Mutex);
DLLIMP Bool      MutexTryEnter(Mutex);
DLLIMP void      MutexDestroy(Mutex);
DLLIMP Thread    ThreadCreate(ThreadCb func, APTR arg);
DLLIMP void      ThreadPause(int delay);
DLLIMP Semaphore SemInit(int count);
DLLIMP Bool      SemWait(Semaphore);
DLLIMP Bool      SemWaitTimeout(Semaphore, ULONG ms);
DLLIMP void      SemAdd(Semaphore, int count);
DLLIMP void      SemClose(Semaphore);

/* vector.c */
struct vector_t
{
	int    itemsize;
	int    max;
	int    count;
	APTR   clearwith;
	STRPTR buffer;
};

DLLIMP APTR vector_nth(vector v, int i);

/* free the entire table (suppose v is stack allocated) */
#define vector_free(v)             if ((v).buffer) free((v).buffer)

/* init by memset'ing to 0 and setting itemsize, clearwith optionnally */
#define vector_init(v, sz)         memset(&(v), 0, sizeof (v)), (v).itemsize = sz
#define vector_init_fill(v, item)  memset(&(v), 0, sizeof (v)), (v).itemsize = sizeof(item), (v).clearwith = &(item)
#define vector_init_zero(v, sz)    memset(&(v), 0, sizeof (v)), (v).itemsize = sz, (v).clearwith = VECTOR_ZERO_FILL
#define vector_first(v)            (APTR) ((v).buffer)
#define vector_reset(v)            { vector_free(v); (v).count = (v).max = 0; (v).buffer = NULL; }

#define VECTOR_ZERO_FILL           ((APTR) 1)

/* INI.c */
typedef struct INIFile_t *    INIFile;

DLLIMP INIFile ParseINI(STRPTR file);
DLLIMP INIFile ParseINIStr(STRPTR buf);
DLLIMP STRPTR  GetINIValue(INIFile, STRPTR section);
DLLIMP STRPTR  GetINIValueStr(INIFile ini, STRPTR key, STRPTR def);
DLLIMP int     GetINIValueInt(INIFile ini, STRPTR key, int def);
DLLIMP Bool    SetINIValue(STRPTR path, STRPTR key, STRPTR val);
DLLIMP Bool    SetINIValueInt(STRPTR path, STRPTR key, int val);
DLLIMP Bool    IterINI(INIFile, STRPTR * key, STRPTR * value);
DLLIMP void    IterINIReset(INIFile ini);
DLLIMP void    FreeINI(INIFile);

/* Lang.c */
DLLIMP Lang   LangParse(STRPTR path);
DLLIMP STRPTR LangStr(STRPTR msg);
DLLIMP STRPTR LangStrPlural(int nb, STRPTR sing, STRPTR plur);
DLLIMP void   LangFree(Lang lang);

#define	LANG(str)              LangStr(str)
#define	DLANG(str)             str

#ifdef __cplusplus
}
#endif

#endif
