/*
 * NBT2.h : datatypes to read/write/modify NBT files
 *
 * written by T.Pierron, aug 2020.
 */

#ifndef	NBT_LIB_V2_H
#define	NBT_LIB_V2_H

#include <stdint.h>
#include "UtilityLibLite.h"

typedef struct NBTFile_t *      NBTFile;
typedef struct NBTFile_t        NBTFile_t;
typedef struct NBTIter_t *      NBTIter;
typedef struct NBTIter_t        NBTIter_t;
typedef struct NBTHdr_t *       NBTHdr;

typedef int (*NBT_WriteCb_t)(int tag, APTR ud, NBTFile);

int NBT_Parse(NBTFile, STRPTR path);
int NBT_ParseIO(NBTFile, FILE *, int offset);
int NBT_FindNode(NBTFile, int offset, STRPTR name);
int NBT_FindNodeFromStream(DATA8 nbt, int offset, STRPTR name);
int NBT_Save(NBTFile, STRPTR path, NBT_WriteCb_t cb, APTR cbparam);
int NBT_Iter(NBTIter iter);
int NBT_FormatSection(DATA8 mem, int y);
int NBT_SetHdrSize(NBTFile, int offset);
int NBT_Insert(NBTFile, STRPTR loc, int type, NBTFile fragment);
int NBT_ToInt(NBTFile, int offset, int def);
int NBT_Size(DATA8 fragment);
Bool NBT_ToFloat(NBTFile, int offset, float * array, int nb);
void NBT_MarkForUpdate(NBTFile, int offset, int tag);
void NBT_InitIter(NBTFile, int offset, NBTIter);
void NBT_IterCompound(NBTIter, DATA8 mem);
APTR NBT_Payload(NBTFile, int offset);
APTR NBT_ArrayStart(NBTFile root, int offset, int * size);
APTR NBT_PayloadFromStream(DATA8 stream, int offset, STRPTR name);
Bool NBT_SetFloat(NBTFile, int offset, float * array, int nb);
Bool NBT_SetInt(NBTFile, int offset, int64_t val);
Bool NBT_Add(NBTFile nbt, ...);
Bool NBT_Delete(NBTFile nbt, int offset, int nth);
DATA8 NBT_Copy(DATA8 mem);
DATA8 NBT_Compress(NBTFile, int * size, int page, NBT_WriteCb_t cb, APTR cbparam);

/* only available in debug */
int  NBT_Dump(NBTFile, int offset, int level, FILE * out);
void NBT_DumpCompound(NBTFile);

#define NBT_Free(ptr)        free((ptr)->mem)
#define MIN_SECTION_MEM      10328
#ifndef DEBUG
#define NBT_Dump(x,y,z,t)
#endif

struct NBTFile_t
{
	int   usage, max;
	int   alloc, page;
	DATA8 mem;
};

struct NBTIter_t
{
	DATA8 buffer;
	DATA8 name;
	int   offset;
	int   state;
};

struct NBTHdr_t
{
	uint8_t  type;           /* TAG_* */
	uint8_t  minNameSz;      /* min name len: if 255 need to scan up to 0 byte */
	uint16_t count;          /* number of entries after this node (TAG_List only) */
	uint32_t size;           /* size of entire hierarchy under this node (including header + name) */
	uint8_t  name[4];        /* name of node (4-byte alignment) */
};

/* use with caution */
#define NBT_HdrSize(mem)     ((NBTHdr)(mem))->size
#define NBT_MemPayload(mem)  (((NBTHdr)mem)->name + ((((NBTHdr)mem)->minNameSz + 4) & ~3))
#define NBT_Hdr(file,off)    ((NBTHdr)((file)->mem + off))

/* tags for NBT_Add() */
#define TAG_End              0
#define	TAG_Byte             1
#define	TAG_Short            2
#define	TAG_Int              3
#define	TAG_Long             4
#define	TAG_Float            5
#define	TAG_Double           6
#define	TAG_Byte_Array       7
#define	TAG_String           8
#define TAG_List             9
#define TAG_Compound         10
#define TAG_Int_Array        11
#define TAG_Raw_Data         12   /* NBT_Add() only */
#define TAG_Raw_Ptr          13   /* NBT_Add() only */
#define TAG_Compound_End     14   /* NBT_Add() only */
#define	TAG_List_Byte        (TAG_List | (TAG_Byte << 4))
#define	TAG_List_Short       (TAG_List | (TAG_Short << 4))
#define	TAG_List_Int         (TAG_List | (TAG_Int << 4))
#define	TAG_List_Long        (TAG_List | (TAG_Long << 4))
#define	TAG_List_Float       (TAG_List | (TAG_Float << 4))
#define	TAG_List_Double      (TAG_List | (TAG_Double << 4))
#define	TAG_List_String      (TAG_List | (TAG_String << 4))
#define	TAG_List_Compound    (TAG_List | (TAG_Compound << 4))
#define NBT_WithInit         0x1000000

#ifdef NBT_IMPL			     /* private stuff below */
#define DATA_OFFSET          4112 /* size of all (previous table + previous NBTHdr) */
#define SKYLIGHT_OFFSET      6176
#define BLOCKLIGHT_OFFSET    8244
#define ADDID_OFFSET         10312

#define SET_NULL(mem)        * (uint32_t *) mem = 0

#include "zlib.h"
typedef struct ZStream_t *      ZStream;

typedef union NBTVariant_t
{
	uint8_t  byte;
	int16_t  word;
	int32_t  dword;
	int64_t  qword;
	uint64_t u64;
	uint32_t u32;
	float    real32;
	double   real64;
	STRPTR   string;
	APTR *   array;
} * NBTVariant;

/* possible values for NBTHdr.type */
#define TAG_Deleted          13
#define TAG_Replaced         14
#define TAG_Int_Array_Conv   15
#define SZ_CHUNK             4096

/* special value for NBTHdr_t.count */
#define NBT_NODE_CHANGED     0xff00

#define NBT_REGION_FLAG      1
#define NBT_SECTION_FLAG     2

struct ZStream_t
{
	FILE *   in;
	int      type, remain;
	gzFile   gzin;
	DATA8    bout, bin;
	int      read;
	z_stream strm;
};

#define	UINT32(in)     (((((gzGetC(in) << 8) | gzGetC(in)) << 8) | gzGetC(in)) << 8) | gzGetC(in)
#define	UINT16(in)     ((gzGetC(in) << 8) | gzGetC(in))

#else /* !NBT_IMPL */
#define DATA_OFFSET           4112 /* offset from start of blockId ... */
#define SKYLIGHT_OFFSET       6180 /* ... and each table being 2048 bytes + NBTHdr */
#define BLOCKLIGHT_OFFSET     8248
#endif
#endif
