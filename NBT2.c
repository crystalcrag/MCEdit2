/*
 * NBT2.c : parse a NBT (Named Binary Tag) file format. requires zlib from zlib.net
 *          this is version 2, using a different API with an improved memory management.
 *
 * This API is described more in details in doc/internals.html.
 *
 * Written by T.Pierron, aug 2020.
 */

#define NBT_IMPL
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <malloc.h>
#include "NBT2.h"
#include "utils.h"

/* only scalar types */
static uint8_t sizeof_type[] = {0, 1, 2, 4, 8, 4, 8};

static ZStream gzOpen(APTR in, int type, int offset)
{
	ZStream io = malloc(sizeof *io + (type > 0 ? SZ_CHUNK : 0));

	if (io)
	{
		uint8_t header[5];
		memset(io, 0, sizeof *io);
		io->type = type;
		switch (type) {
		case 0: /* gzip-compressed file (level.dat) */
			io->gzin = gzopen(in, "rb");
			if (io->gzin == NULL) { free(io); return NULL; }
			break;
		case 1: /* zlib-compressed region file */
			io->in = in;
			fseek(io->in, offset, SEEK_SET);
			/* header before chunk in region: 4bytes size and 1byte for type */
			fread(header, 1, sizeof header, io->in);
			if (header[4] == 2)
			{
				io->bout = (APTR) (io + 1);
				io->bin = io->bout + SZ_CHUNK/2;
				io->remain = (((((header[0] << 8) | header[1]) << 8) | header[2]) << 8) | header[3];
				io->strm.next_out = io->bout;
				io->strm.avail_out = SZ_CHUNK/2;
				io->strm.next_in = io->bin;
				if (inflateInit(&io->strm) == Z_OK)
					return io;
			}
			free(io);
			return NULL;
		case 2: /* read from memory - suppose zlib compression */
			io->remain = 0;
			io->bout = (APTR) (io + 1);
			io->bin = in;
			io->strm.next_out = io->bout;
			io->strm.avail_out = SZ_CHUNK;
			io->strm.next_in = io->bin;
			io->strm.avail_in = offset;
			if (inflateInit(&io->strm) != Z_OK)
				free(io), io = NULL;
		}
	}
	return io;
}

static int gzRead(ZStream in, APTR buffer, int len)
{
	int left, ret = 0;
	if (in->type > 0)
	{
		while (len > 0)
		{
			left = in->strm.next_out - (in->bout + in->read);
			if (left > 0)
			{
				if (left > len) left = len;
				memcpy(buffer, in->bout + in->read, left);
				buffer += left; len -= left;
				in->read += left;
				ret += left;
				if (len == 0) return ret;
			}
			/* caller want more stuff, but we are out of data */
			in->strm.avail_out += in->strm.next_out - in->bout;
			in->strm.next_out = in->bout;
			in->read = 0;
			if (in->strm.avail_in < SZ_CHUNK/4 && in->remain) {
				left = SZ_CHUNK/2 - in->strm.avail_in;
				if (left > in->remain) left = in->remain;
				if (left > 0) {
					memmove(in->bin, in->strm.next_in, in->strm.avail_in);
					in->strm.next_in = in->bin;
					left = fread(in->bin + in->strm.avail_in, 1, left, in->in);
					in->strm.avail_in += left;
					in->remain -= left;
				}
			}
			if (in->strm.avail_in <= 0 || inflate(&in->strm, 0) == Z_DATA_ERROR)
				return -1;
		}
		return ret;
	}
	else return gzread(in->gzin, buffer, len);
}

static int gzGetC(ZStream in)
{
	uint8_t c;
	if (gzRead(in, &c, 1) == 1)
	{
		in->strm.total_in ++;
		return c;
	}
	return -1;
}

static void gzClose(ZStream in)
{
	switch (in->type) {
	case 0: gzclose(in->gzin); break;
	case 1:
	case 2: inflateEnd(&in->strm);
	}
	free(in);
}

/* numbers over 1 byte long are stored in BE format */
static void NBT_SwapArray(APTR data, int bpp /* 2, 4 or 8 */, int size)
{
	DATA8 p;
	int   i;
	switch (bpp) {
	case 2:
		for (p = data, i = size; i > 0; i -= 2, swap(p[0], p[1]), p += 2);
		break;
	case 4:
		for (p = data, i = size; i > 0; i -= 4, swap(p[0], p[3]), swap(p[1], p[2]), p += 4);
		break;
	case 8:
		for (p = data, i = size; i > 0; i -= 8, swap(p[0], p[7]), swap(p[1], p[6]), swap(p[2], p[5]), swap(p[3], p[4]), p += 8);
	}
}

/* the entire NBT tree will be contained in a single buffer */
static void * NBT_AddBytes(NBTFile nbt, int size)
{
	int alloc = (size + 3) & ~3;
	int total = nbt->usage + alloc;
	if (total > nbt->max)
	{
		int   mod = nbt->page; /* 1Kb or 4Kb */
		int   max = (total + mod) & ~mod;
		DATA8 mem = realloc(nbt->mem, max);
		if (! mem) return NULL;
		nbt->max = max;
		nbt->mem = mem;
	}
	total = nbt->usage;
	nbt->usage += alloc;
	nbt->alloc  = alloc;
	return nbt->mem + total;
}

#define HDR(file, offset)    ((NBTHdr) (file->mem + offset))

/* low-level NBT parse */
static int NBT_ParseFile(NBTFile nbt, ZStream in, int flags)
{
	NBTHdr  hdr;
	uint8_t type;
	int     len, off;
	DATA8   mem;

	/* first byte: node type */
	type = gzGetC(in);
	if (type == TAG_End || type == 255 /* -1 returned by gzGetC */) return -1;

	/* get node name */
	len = UINT16(in);
	off = nbt->usage;
	hdr = NBT_AddBytes(nbt, sizeof *hdr + (len > 3 ? len - 3 : 0));

	hdr->size = nbt->alloc;
	hdr->minNameSz = len > 255 ? 255 : len;
	hdr->type = type;
	hdr->count = 0;
	mem = (DATA8) hdr + 8;
	mem[len] = 0;
	if (len > 0)
		gzRead(in, mem, len);

	if ((flags & NBT_SECTION_FLAG) && len > 0)
	{
		/* keep some tables in a specific order */
		static uint16_t offset[] = {0, 0, DATA_OFFSET, SKYLIGHT_OFFSET, BLOCKLIGHT_OFFSET, ADDID_OFFSET};
		DATA8 sub = strstr("\1""Blocks\2""Data\3""SkyLight\4""BlockLight\5""Y", hdr->name);
		if (sub && sub[len] < 10)
		{
			/* copy hdr at specific location */
			mem = nbt->mem + (flags >> 3) + offset[sub[-1]];
			len = nbt->usage - off;
			memcpy(mem, hdr, len);
			nbt->usage = off;
			hdr = (NBTHdr) mem;
			mem += len;
			if (type == TAG_Byte_Array)
			{
				len = hdr->count = UINT32(in);
				gzRead(in, mem, len);
				hdr->size += len;
			}
			else
				gzRead(in, mem, 1), hdr->size += 4;
			return hdr->size;
		}
	}

	switch (type) {
	case TAG_Byte:
		hdr->size += 4;
		mem = NBT_AddBytes(nbt, 1);
		gzRead(in, mem, 1);
		break;
	case TAG_Short:
		hdr->size += 4;
		mem = NBT_AddBytes(nbt, 2);
		gzRead(in, mem, 2);
		swap(mem[0], mem[1]);
		break;
	case TAG_Float:
	case TAG_Int:
		hdr->size += 4;
		mem = NBT_AddBytes(nbt, 4);
		gzRead(in, mem, 4);
		swap(mem[0], mem[3]);
		swap(mem[1], mem[2]);
		break;
	case TAG_Double:
	case TAG_Long:
		hdr->size += 8;
		mem = NBT_AddBytes(nbt, 8);
		gzRead(in, mem, 8);
		swap(mem[0], mem[7]); swap(mem[1], mem[6]);
		swap(mem[2], mem[5]); swap(mem[3], mem[4]);
		break;
	case TAG_Byte_Array:
		len = hdr->count = UINT32(in);
		mem = NBT_AddBytes(nbt, len);
		gzRead(in, mem, len);
		HDR(nbt, off)->size += nbt->alloc;
		break;
	case TAG_String:
		len = hdr->count = UINT16(in);
		mem = NBT_AddBytes(nbt, len+1);
		gzRead(in, mem, len);
		mem[len] = 0;
		HDR(nbt, off)->size += nbt->alloc;
		break;
	case TAG_List:
		type = gzGetC(in);
		len  = hdr->count = UINT32(in);
		hdr->type |= type << 4;
		switch (type) {
		case TAG_Byte:
		case TAG_Short:
		case TAG_Int:
		case TAG_Long:
		case TAG_Float:
		case TAG_Double:
			type = sizeof_type[type];
			len *= type;
			mem = NBT_AddBytes(nbt, len);
			gzRead(in, mem, len);
			NBT_SwapArray(mem, type, len);
			HDR(nbt, off)->size += nbt->alloc;
			break;
		case TAG_Byte_Array:
		case TAG_List:
			fprintf(stderr, "unsupported nested list.\n");
			break;
		case TAG_String:
		{	int unused = 0;
			int total  = 0;
			int offset = nbt->usage;
			while (len > 0)
			{
				int length = UINT16(in);
				NBT_AddBytes(nbt, length-unused+1);
				mem = nbt->mem + offset + total;
				gzRead(in, mem, length);
				mem[length] = 0;
				total += length+1;
				unused = nbt->alloc - (length-unused+1);
				len --;
			}
			HDR(nbt, off)->size += total + unused;
		}	break;
		case TAG_Compound:
			if ((flags & NBT_REGION_FLAG) && strcasecmp(hdr->name, "Sections") == 0)
				flags |= NBT_SECTION_FLAG;
			while (len > 0)
			{
				int sz;
				if (flags & NBT_SECTION_FLAG)
				{
					/* pre-alloc mem for each sub-chunk tables */
					flags &= 7;
					flags |= nbt->usage << 3;
					NBT_AddBytes(nbt, MIN_SECTION_MEM);
				}
				while ((sz = NBT_ParseFile(nbt, in, flags)) > 0)
				{
					HDR(nbt, off)->size += sz;
				}
				HDR(nbt, off)->size += 4;
				mem = NBT_AddBytes(nbt, 1);
				SET_NULL(mem);
				len --;
			}
		}
		break;
	case TAG_Compound:
	{	int sz;
		while ((sz = NBT_ParseFile(nbt, in, flags)) > 0)
		{
			HDR(nbt, off)->size += sz;
		}
		HDR(nbt, off)->size += 4;
		mem = NBT_AddBytes(nbt, 1);
		SET_NULL(mem);
	}	break;
	case TAG_Int_Array:
		hdr->count = len = UINT32(in);
		len *= 4;
		mem = NBT_AddBytes(nbt, len);
		gzRead(in, mem, len);
		NBT_SwapArray(mem, 4, len);
		HDR(nbt, off)->size += nbt->alloc;
	}
	return nbt->usage - off;
}

/* add NBT node at end: used to construct a sub-tree and then merge with final NBT */
Bool NBT_Add(NBTFile nbt, ...)
{
	va_list args;
	int     type, nested, compound;
	DATA8   mem;
	NBTHdr  hdr;

	/* opened compound that needs closing */
	compound = nbt->alloc;
	for (va_start(args, nbt), nested = 0; ; )
	{
		type = va_arg(args, int);
		if (type == TAG_End)
		{
			break; /* done */
		}
		else if (type == TAG_Compound_End)
		{
			mem = NBT_AddBytes(nbt, 1);
			hdr = NBT_Hdr(nbt, compound);
			if (hdr->type == TAG_Compound)
				hdr->size = nbt->usage - compound;
			compound = 0;
			nested --;
			SET_NULL(mem);
			if (nested <= 0) break;
			else continue;
		}
		else if (type == TAG_Raw_Data)
		{
			type = va_arg(args, int);
			mem = NBT_AddBytes(nbt, type);
			memcpy(mem, va_arg(args, APTR), type);
			continue;
		}

		STRPTR name = va_arg(args, STRPTR);
		int    len  = name ? strlen(name) : 0;
		int    off  = nbt->usage;

		hdr = NBT_AddBytes(nbt, sizeof *hdr + (len > 3 ? len - 3 : 0));
		hdr->type = type;
		hdr->minNameSz = len;
		hdr->count = 0;
		hdr->size = nbt->alloc;
		strcpy(hdr->name, name);

		switch (type&15) {
		case TAG_Byte:
			mem = NBT_AddBytes(nbt, 1);
			mem[0] = va_arg(args, int);
			break;
		case TAG_Short:
			mem = NBT_AddBytes(nbt, 2);
			((uint16_t *)mem)[0] = va_arg(args, unsigned);
			break;
		case TAG_Int:
		case TAG_Float:
			mem = NBT_AddBytes(nbt, 4);
			((uint32_t *)mem)[0] = va_arg(args, unsigned);
			break;
		case TAG_Long:
		case TAG_Double:
			mem = NBT_AddBytes(nbt, 8);
			((uint64_t *)mem)[0] = va_arg(args, uint64_t);
			break;
		case TAG_Byte_Array:
			len = hdr->count = va_arg(args, int);
			mem = NBT_AddBytes(nbt, len);
			memset(mem, va_arg(args, int), len);
			break;
		case TAG_Int_Array:
			hdr->count = va_arg(args, int);
			len = hdr->count * 4;
			mem = NBT_AddBytes(nbt, len);
			memset(mem, 0, len);
			break;
		case TAG_String:
			name = va_arg(args, STRPTR);
			hdr->count = strlen(name);
			mem = NBT_AddBytes(nbt, hdr->count+1);
			strcpy(mem, name);
			break;
		case TAG_List:
			type >>= 4;
			switch (type) {
			case TAG_Byte:
			case TAG_Short:
			case TAG_Int:
			case TAG_Float:
			case TAG_Long:
			case TAG_Double:
				len = va_arg(args, int);
				hdr->count = len & (NBT_WithInit-1);
				mem = NBT_AddBytes(nbt, hdr->count * sizeof_type[type]);
				if (len >= NBT_WithInit) memcpy(mem, va_arg(args, APTR), nbt->alloc);
				else memset(mem, 0, nbt->alloc);
				break;
			case TAG_String:
				// TODO
				return False;
			case TAG_Compound:
				hdr->count = va_arg(args, int);
				if (hdr->count > 0) nested ++;
				/* will need a terminator at some point */
				nbt->alloc = 4;
				break;
			default: return False;
			}
			break;
		case TAG_Compound:
			compound = (DATA8) hdr - nbt->mem;
			nbt->alloc = 4;
			nested ++;
			break;
		case TAG_Raw_Ptr:
			/* tile entity pushed by pistons */
			name = va_arg(args, APTR);
			mem = NBT_AddBytes(nbt, sizeof name);
			memcpy(mem, &name, sizeof name);
		}
		HDR(nbt, off)->size += nbt->alloc;
	}
	nbt->alloc = compound;
	return True;
}

/* copy a chunk of a NBT, mem is actually a NBTHdr */
DATA8 NBT_Copy(DATA8 mem)
{
	DATA8 dup;
	int   size;

	if (mem == NULL) return NULL;

	/* count bytes first */
	for (size = 0; mem[size]; size += ((NBTHdr)(mem+size))->size);

	dup = malloc(size+1);
	if (! dup) return NULL;
	memcpy(dup, mem, size);
	dup[size] = TAG_End;
	return dup;
}

/* bytes have been added in between NBT stream: change NBTHdr.size to keep reference pointing to correct data */
static void NBT_UpdateHdrSize(NBTFile nbt, int diff, int offset)
{
	NBTHdr hdr;
	DATA8  mem, eof;
	int    i;

	/* change size of higher level nodes */
	for (hdr = HDR(nbt, 0), mem = nbt->mem, eof = mem + offset; mem < eof; )
	{
		hdr->size += diff;
		mem = hdr->name + ((hdr->minNameSz + 4) & ~3); /* payload */

		switch (hdr->type) {
		case TAG_Compound: hdr->count = 1; // no break;
		case TAG_List_Compound:
			for (i = hdr->count; i > 0; i --)
			{
				/* parse <count> compound */
				for (;;)
				{
					NBTHdr sub = (NBTHdr) mem;
					if (sub->type == 0) { mem += 4; break; }
					mem += sub->size;
					if (mem == eof) return;
					if (mem > eof)
					{
						hdr = sub;
						mem = (DATA8) sub;
						i = 1;
						break;
					}
				}
			}
			break;
		default: return; /* shouldn't get here */
		}
	}
}

/* delete a node from a nbt stream */
Bool NBT_Delete(NBTFile nbt, int offset, int nth)
{
	NBTHdr hdr  = HDR(nbt, offset);
	DATA8  mem  = (DATA8) hdr;
	int    size = hdr->size, i;

	/* NBT_FindNode failed */
	if (offset < 0) return False;

	if (hdr->type == TAG_List_Compound && nth > 0)
	{
		mem = hdr->name + ((hdr->minNameSz + 4) & ~3);
		if (nth > hdr->count) return False;
		for (i = nth - 1; i > 0; i --)
		{
			/* there are no TAG_Compound headers: only properties separated by TAG_End, therefore no hdr->size */
			for (;;)
			{
				NBTHdr sub = (NBTHdr) mem;
				if (sub->type == TAG_End) { mem += 4; break; }
				mem += sub->size;
			}
		}
		offset = mem - nbt->mem;
		DATA8 p;
		for (size = 0, p = mem;;)
		{
			NBTHdr sub = (NBTHdr) p;
			if (sub->type == TAG_End) { size += 4; break; }
			p += sub->size;
			size += sub->size;
		}
		hdr->count --;
	}

	nbt->usage -= size;
	memmove(mem, mem + size, nbt->usage - offset);
	if (nth >= 0)
		NBT_UpdateHdrSize(nbt, -size, offset);

	return True;
}

/* mark a node as requiring a callback when saved */
void NBT_MarkForUpdate(NBTFile nbt, int offset, int tag)
{
	if (offset < 0 || nbt->usage == 0) return;

	NBTHdr hdr = HDR(nbt, offset);

	if ((hdr->type & 15) != TAG_List && hdr->type != TAG_Compound)
		return;

	if (hdr->count < NBT_NODE_CHANGED)
		hdr->count = NBT_NODE_CHANGED;
	hdr->count |= tag;
}


/* get raw payload of given node at offset */
APTR NBT_Payload(NBTFile root, int offset)
{
	if (offset < 0) return NULL;
	NBTHdr hdr = HDR(root, offset);
	return hdr->name + ((hdr->minNameSz + 4) & ~3);
}

APTR NBT_ArrayStart(NBTFile root, int offset, int * size)
{
	if (offset < 0) return NULL;
	NBTHdr hdr = HDR(root, offset);
	if (size) *size = hdr->count;
	return hdr->name + ((hdr->minNameSz + 4) & ~3);
}

void NBT_InitIter(NBTFile root, int offset, NBTIter iter)
{
	iter->buffer = root->mem;

	NBTHdr  hdr  = HDR(root, offset);
	uint8_t type = hdr->type;
	switch (type&15) {
	case TAG_Compound: iter->state = -1; /* iter over properties */ break;
	case TAG_End:      iter->state =  0; return;
	case TAG_List:     if (type == TAG_List_Compound) { iter->state = hdr->count; /* iter over items */ break; }
	default:           iter->state = -1; iter->offset = offset; return; /* assume middle of compound */
	}
	/* move to payload */
	iter->name   = hdr->name;
	iter->offset = (hdr->name + ((hdr->minNameSz + 4) & ~3)) - root->mem;
}

void NBT_IterCompound(NBTIter iter, DATA8 mem)
{
	iter->buffer = mem;
	iter->state  = -1;
	iter->offset = 0;
}

int NBT_Iter(NBTIter iter)
{
	int    ret = iter->offset, offset;
	NBTHdr hdr = (NBTHdr) (iter->buffer + ret);
	DATA8  mem;
	iter->name = hdr->name;
	/* move to next item */
	switch (iter->state) {
	case  0: return -1;
	case -1:
		/* iterate until node type == 0 */
		if (iter->buffer[ret] == 0) { iter->offset += 4; return -1; }
		iter->offset += hdr->size;
		break;
	default:
		for (offset = ret, mem = iter->buffer; mem[offset]; offset += ((NBTHdr)(mem + offset))->size);
		iter->offset = offset + 4;
		iter->state --;
	}
	return ret;
}

/* return the size (in bytes) of fragment (usually an NBT_Compound) */
int NBT_Size(DATA8 fragment)
{
	NBTIter_t iter;
	int i, size = 0;
	NBT_IterCompound(&iter, fragment);
	while ((i = NBT_Iter(&iter)) >= 0)
		size += NBT_HdrSize(fragment+i);

	return size;
}

/* update NBTHdr.size of TAG_Compound and TAG_List_Compound nodes */
int NBT_SetHdrSize(NBTFile nbt, int offset)
{
	while (offset < nbt->usage)
	{
		NBTHdr hdr = HDR(nbt, offset);
		DATA8  mem = hdr->name + ((hdr->minNameSz + 4) & ~3); /* payload */
		int    i;

		switch (hdr->type) {
		case TAG_Compound: hdr->count = 1; // no break;
		case TAG_List_Compound:
			for (i = hdr->count; i > 0; i --)
			{
				/* parse <count> compound */
				for (;;)
				{
					NBTHdr sub = (NBTHdr) mem;
					if (sub->type == 0) { mem += 4; break; }
					if (sub->type == TAG_List_Compound || sub->type == TAG_Compound)
						mem += NBT_SetHdrSize(nbt, mem - nbt->mem);
					else
						mem += sub->size;
				}
			}
			return hdr->size = mem - (DATA8) hdr;
		case TAG_End:
			return offset + 4;
		default:
			offset += hdr->size;
		}
	}
	return offset;
}

/* find node within using breadth-first exploration */
int NBT_FindNode(NBTFile root, int offset, STRPTR name)
{
	NBTHdr hdr, sub;
	int    i, old;
	int    recursive;
	STRPTR next;

	recursive = 1;
	hdr = HDR(root, offset);
	old = hdr->type;
	if (old == 0) return -1;
	for (next = name; *next && *next != '.'; next ++);
	if (*next)
	{
		i = next - name;
		name = STRDUPA(name);
		next = name + i;
		*next++ = 0;
		for (;;)
		{
			offset = NBT_FindNode(root, offset, name);
			if (offset < 0) return -1;
			name = next;
			if (name == NULL) break;
			next = strchr(next, '.');
			if (next) *next++ = 0;
		}
		return offset;
	}
	if (name[0] == '/') recursive = 0, name ++;
	if (strcasecmp(hdr->name, name) == 0) return offset;
	if (old == TAG_Compound || old == TAG_List_Compound)
	{
		offset += sizeof *hdr - 4 + ((hdr->minNameSz + 4) & ~3);
	}
	/* else assume middle of a TAG_Compound */

	if (old != TAG_List_Compound)
	{
		for (old = offset, sub = HDR(root, offset); sub->type; offset += sub->size, sub = HDR(root, offset))
			if (strcasecmp(sub->name, name) == 0) return offset;

		if (recursive)
		for (offset = old, sub = HDR(root, offset); sub->type; offset += sub->size, sub = HDR(root, offset))
		{
			if (sub->type == TAG_List_Compound || sub->type == TAG_Compound)
			{
				i = NBT_FindNode(root, offset, name);
				if (i > 0) return i;
			}
		}
	}
	else
	{
		for (i = hdr->count; i > 0; i --)
		{
			uint8_t hasChild = 0;
			/* each compound is TAG_End terminated */
			for (old = offset;;)
			{
				sub = HDR(root, offset);
				if (sub->type == 0) { offset += 4; break; }
				if (strcasecmp(sub->name, name) == 0) return offset;
				hasChild |= sub->type == TAG_List_Compound || sub->type == TAG_Compound;
				offset += sub->size;
			}
			/* not found yet, check children */
			if (hasChild && recursive)
			for (offset = old;;)
			{
				sub = HDR(root, offset);
				if (sub->type == 0) { offset += 4; break; }
				if (sub->type == TAG_List_Compound || sub->type == TAG_Compound)
				{
					int find = NBT_FindNode(root, offset, name);
					if (find > 0) return find;
				}
				offset += sub->size;
			}
		}
	}
	return -1;
}

int NBT_FindNodeFromStream(DATA8 nbt, int offset, STRPTR name)
{
	NBTFile_t file = {.mem = nbt};
	if (nbt == NULL) return -1;
	return NBT_FindNode(&file, offset, name);
}

APTR NBT_PayloadFromStream(DATA8 stream, int offset, STRPTR name)
{
	NBTFile_t file = {.mem = stream};
	if (stream == NULL) return NULL;
	int off = NBT_FindNode(&file, offset, name);
	if (off < 0) return NULL;
	return NBT_Payload(&file, off);
}

int NBT_ToInt(NBTFile root, int offset, int def)
{
	if (offset < 0) return def;

	NBTHdr     hdr = HDR(root, offset);
	NBTVariant val = (NBTVariant) (hdr->name + ((hdr->minNameSz + 4) & ~3));

	switch (hdr->type) {
	case TAG_Byte:   return val->byte;
	case TAG_Short:  return val->word;
	case TAG_Int:    return val->dword;
	case TAG_Long:   return val->qword;
	case TAG_Float:  return val->real32;
	case TAG_Double: return val->real64;
	case TAG_String: return strtol((STRPTR) val, NULL, 10);
	default:         return def;
	}
}

/*
 * modification functions
 */

Bool NBT_ToFloat(NBTFile root, int offset, float * array, int nb)
{
	if (offset < 0) return 0;
	NBTHdr  hdr  = HDR(root, offset);
	DATA8   mem  = hdr->name + ((hdr->minNameSz + 4) & ~3);
	uint8_t type = hdr->type;
	int     sz   = sizeof_type[type >= TAG_List ? type>>4 : type];

	if ((type & 15) == TAG_List)
	{
		type >>= 4;
		if (nb > hdr->count) return False;
	}
	else if (nb > 1) return False;

	while (nb > 0)
	{
		union NBTVariant_t buffer;
		memcpy(&buffer, mem, sz);
		switch (type) {
		case TAG_Byte:   array[0] = buffer.byte; break;
		case TAG_Short:  array[0] = buffer.word; break;
		case TAG_Int:    array[0] = buffer.dword; break;
		case TAG_Long:   array[0] = buffer.qword; break;
		case TAG_Float:  array[0] = buffer.real32; break;
		case TAG_Double: array[0] = buffer.real64;
		}
		array ++;
		nb --;
		mem += sz;
	}
	return True;
}

Bool NBT_SetFloat(NBTFile root, int offset, float * array, int nb)
{
	if (offset < 0) return 0;
	NBTHdr  hdr  = HDR(root, offset);
	DATA8   mem  = hdr->name + ((hdr->minNameSz + 4) & ~3);
	uint8_t type = hdr->type;
	int     sz   = sizeof_type[type>>4];

	if ((type & 15) == TAG_List)
	{
		type >>= 4;
		if (nb > hdr->count) return 0;
	}
	else if (nb > 1) return 0;

	while (nb > 0)
	{
		union NBTVariant_t * buffer = (APTR) mem;
		switch (type) {
		case TAG_Byte:   buffer->byte   = array[0]; break;
		case TAG_Short:  buffer->word   = array[0]; break;
		case TAG_Double: buffer->real64 = array[0]; break;
		case TAG_Float:  buffer->real32 = array[0]; break;
		case TAG_Int:    buffer->dword  = array[0]; break;
		case TAG_Long:   return False;
		}
		array ++;
		nb --;
		mem += sz;
	}
	return 1;
}

Bool NBT_SetInt(NBTFile root, int offset, int64_t val)
{
	NBTHdr hdr = HDR(root, offset);

	union NBTVariant_t * buffer = (APTR) (hdr->name + ((hdr->minNameSz + 4) & ~3));

	switch (hdr->type) {
	case TAG_Byte:   buffer->byte   = val; break;
	case TAG_Short:  buffer->word   = val; break;
	case TAG_Double: buffer->real64 = val; break;
	case TAG_Float:  buffer->real32 = val; break;
	case TAG_Int:    buffer->dword  = val; break;
	case TAG_Long:   buffer->qword  = val;
	}
	return True;
}

int NBT_FormatSection(DATA8 mem, int y)
{
	static struct NBTHdr_t compound[] = {
		{TAG_Byte_Array,  6, 4096, SKYLIGHT_OFFSET},
		{TAG_Byte_Array,  8, 2048, BLOCKLIGHT_OFFSET - SKYLIGHT_OFFSET},
		{TAG_Byte_Array, 10, 2048, DATA_OFFSET - BLOCKLIGHT_OFFSET},
		{TAG_Byte_Array,  4, 2048, ADDID_OFFSET - DATA_OFFSET},
		{TAG_Byte,        1, 0,    16},
	};
	static STRPTR names[] = {
		"Blocks", "SkyLight", "BlockLight", "Data", "Y"
	};
	int i;

	for (i = 0; i < DIM(compound); i ++)
	{
		memcpy(mem, compound + i, sizeof compound[0]);
		strcpy(mem + offsetp(NBTHdr, name), names[i]);
		mem += compound[i].size;
	}
	mem[-4] = y;
	/* return payload start of first table */
	return 16;
}

/* insert NBT fragment directly into NBT original structure */
int NBT_Insert(NBTFile nbt, STRPTR loc, int type, NBTFile fragment)
{
	/* should be at least 2 levels of indirection in case of missing leaf */
	STRPTR next = strchr(loc, '.');
	int    offset;

	NBT_SetHdrSize(fragment, 0);

	if (next)
	{
		loc = STRDUPA(loc);

		for (next = loc, offset = 0; next; loc = next)
		{
			next = strchr(loc, '.');
			if (next) *next++ = 0;
			int off = NBT_FindNode(nbt, offset, loc);
			if (off < 0) break;
			offset = off;
		}
		if (loc)
		{
			/* missing entry: add it on the fly */
			NBTHdr hdr;
			int    len = strlen(loc);
			len = sizeof *hdr + (len > 3 ? len - 3 : 0);
			hdr = alloca(len);
			hdr->type  = type;
			hdr->size  = fragment->usage;
			hdr->count = 0;
			strcpy(hdr->name, loc);
			fragment->usage += len;
			fragment->page = len;
			next = (STRPTR) hdr;

			// XXX hdr->count if type == TAG_List_Compound
		}
	}
	else offset = NBT_FindNode(nbt, 0, loc); /* hmm, no indirection */

	if (offset < 0)
		return -1;

	if (offset >= 0)
	{
		DATA8 mem  = nbt->mem + offset;
		int   size = ((NBTHdr)mem)->size;
		int   diff = fragment->usage - size;
		int   nb   = nbt->usage - (offset + size);

		if (diff > 0)
		{
			if (! NBT_AddBytes(nbt, diff))
				return False;
			mem = nbt->mem + offset;
		}
		else nbt->usage += diff;

		memmove(mem + fragment->usage, mem + size, nb);

		if (next)
		{
			/* add missing property name */
			memcpy(mem, next, fragment->page);
			mem += fragment->page;
			fragment->usage -= fragment->page;
		}
		memcpy(mem, fragment->mem, fragment->usage);

		NBT_UpdateHdrSize(nbt, diff, offset);

		//NBT_Dump(nbt, 0, 0, stderr);

		return offset;
	}
	return -1;
}

/*
 * save functions: write to gzip file (level.dat)
 */
static int NBT_WriteToGZ(APTR out, APTR buffer, int size, int be)
{
	if (be)
	{
		uint8_t tmp[8];
		DATA8   s, d;

		for (s = buffer + size - 1, d = tmp; s >= (DATA8) buffer; *d ++ = *s --);

		return gzwrite(out, tmp, size);
	}
	else return gzwrite(out, buffer, size);
}

/* save to a deflated memory chunk (region chunk) */
static int NBT_WriteToZip(APTR out, APTR buffer, int size, int be)
{
	z_stream * zip = out;
	uint8_t    bebuf[8];

	if (be)
	{
		DATA8 d, s;
		for (s = buffer + size - 1, d = bebuf; s >= (DATA8) buffer; *d ++ = *s --);
		buffer = bebuf;
	}

	while (size > 0)
	{
		int max = zip->avail_in;
		if (max + size > 1024)
		{
			DATA8 start = zip->next_in;
			deflate(zip, Z_NO_FLUSH);
			if (zip->avail_out == 0)
			{
				DATA8 outbuf = zip->next_out - zip->total_out;
				outbuf = realloc(outbuf, zip->total_out + 4096);
				if (outbuf == NULL) return -1;
				zip->avail_out = 4096;
				zip->next_out = outbuf + zip->total_out;
			}
			memmove(start, zip->next_in, zip->avail_in);
			zip->next_in = start;
		}

		max = 1024 - zip->avail_in;
		if (max > size) max = size;
		memcpy(zip->next_in + zip->avail_in, buffer, max);
		buffer += max;
		size -= max;
		zip->avail_in += max;
	}
	return 1;
}

typedef struct NBTWriteParam_t *     NBTWriteParam;
typedef int (*write_t)(APTR, APTR, int, int);
struct NBTWriteParam_t
{
	write_t       puts;
	NBT_WriteCb_t cb;
	APTR          cbdata;
};

static void NBT_WriteArray(APTR out, write_t cb, DATA8 mem, int items, int size)
{
	uint8_t buffer[1024];
	int     max = sizeof buffer / size;
	int     nb, i, j;

	for (i = 0; i < items; i += nb)
	{
		DATA8 d;
		nb = MIN(items, max);
		switch (size) {
		default:
			for (j = nb, d = buffer; j > 0; d[0] = mem[1], d[1] = mem[0], j --, mem += 2, d += 2);
			break;
		case 4:
			for (j = nb, d = buffer; j > 0; j --, mem += 4, d += 4)
				d[0] = mem[3], d[1] = mem[2], d[2] = mem[1], d[3] = mem[0];
			break;
		case 8:
			for (j = nb, d = buffer; j > 0; j --, mem += 8, d += 8)
			{
				d[0] = mem[7]; d[1] = mem[6]; d[2] = mem[5]; d[3] = mem[4];
				d[4] = mem[3]; d[5] = mem[2]; d[6] = mem[1]; d[7] = mem[0];
			}
		}
		cb(out, buffer, d - buffer, 0);
	}
}

#define putc(out, c)   chr = c, puts(out, &chr, 1, 0)

static int NBT_WriteFile(NBTFile nbt, APTR out, int offset, NBTWriteParam param)
{
	NBTHdr   hdr;
	uint8_t  type, chr;
	uint16_t size;
	uint32_t dword;
	int      off, i;
	DATA8    mem, p;
	write_t  puts = param->puts;

	hdr = HDR(nbt, offset);
	type = hdr->type;
	size = hdr->minNameSz;
	putc(out, type&15);
	if (type == TAG_End) return 0;
	puts(out, &size, 2, 1);
	puts(out, hdr->name, size, 0);
	mem = hdr->name + ((hdr->minNameSz + 4) & ~3);

	off = 0;
	switch (type & 15) {
	case TAG_Byte: case TAG_Short: case TAG_Int:
	case TAG_Long: case TAG_Float: case TAG_Double:
		puts(out, mem, off = sizeof_type[type], 1);
		break;
	case TAG_Byte_Array:
		dword = off = hdr->count;
		puts(out, &dword, 4, 1);
		puts(out, mem, dword, 0);
	    break;
	case TAG_String:
		off = hdr->count + 1;
		puts(out, &hdr->count, 2, 1);
		puts(out, mem, hdr->count, 0);
	    break;
	case TAG_List:
		type >>= 4;
		dword = hdr->count;
		p = (DATA8) hdr + hdr->size;
		if (hdr->type == TAG_List_Compound && hdr->count >= NBT_NODE_CHANGED && param->cb)
		{
			/* node has been modified: ask callback for NBT stream of each item */
			struct NBTFile_t sub = {0};
			/* get count first */
			dword = param->cb(hdr->count & 0xff, param->cbdata, NULL);
			putc(out, type);
			puts(out, &dword, 4, 1);
			while (dword > 0 && param->cb(hdr->count & 0xff, param->cbdata, &sub))
			{
				sub.alloc = 0;
				do {
					sub.alloc += NBT_WriteFile(&sub, out, sub.alloc, param);
				} while (sub.alloc < sub.usage);
				/* compound terminator */
				type = 0;
				puts(out, &type, 1, 0);
				dword --;
			}
			/* skip entire content of NBT */
			off = p - mem;
			break;
		}

		putc(out, type);
		puts(out, &dword, 4, 1);
		switch (type) {
		case TAG_Byte: case TAG_Short: case TAG_Int:
		case TAG_Long: case TAG_Float: case TAG_Double:
			NBT_WriteArray(out, puts, mem, hdr->count, sizeof_type[type]);
			mem += hdr->count * sizeof_type[type];
			break;
		case TAG_String:
			/* hdr->count NULL-terminated strings in <mem> */
			for (p = mem, i = hdr->count; i > 0; i --, off += size + 1, p += size + 1)
			{
				size = strlen(p);
				puts(out, &size, 2, 1);
				puts(out, p, size, 0);
			}
			break;
		case TAG_Compound:
			for (i = hdr->count, p = mem; i > 0; i --)
			{
				while ((off = NBT_WriteFile(nbt, out, p - nbt->mem, param)) > 0)
					p += off;
				p += 4; /* byte terminator */
			}
			off = p - mem;
		}
		break;
	case TAG_Compound:
		off = 0;
		offset += mem - (DATA8) hdr;
		p = mem;
		while ((i = NBT_WriteFile(nbt, out, offset, param)) > 0)
		{
			offset += i; off += i; p += i;
			/* node has been modified, other properties might follow, do not add 0 yet */
			if (p[0] == 0 && p[1]) break;
		}
		off += 4;
		if (hdr->count >= NBT_NODE_CHANGED && param->cb)
		{
			/* want to extend this node */
			struct NBTFile_t sub = {0};
			uint8_t tag, tags;
			for (tag = 1, tags = hdr->count & ~ NBT_NODE_CHANGED; tags; tags >>= 1, tag <<= 1)
			{
				if ((tags & 1) == 0) continue;
				while (param->cb(tag, param->cbdata, &sub))
				{
					sub.alloc = 0;
					do {
						sub.alloc += NBT_WriteFile(&sub, out, sub.alloc, param);
					} while (sub.alloc < sub.usage);
					type = 0;
					puts(out, &type, 1, 0);
				}
			}
		}
		break;
	case TAG_Int_Array:
		dword = hdr->count;
		puts(out, &dword, 4, 1);
		NBT_WriteArray(out, puts, mem, hdr->count, 4);
		mem += hdr->count * 4;
	}
	return mem - (DATA8) hdr + ((off + 3) & ~3);
}

/* compress <nbt> using deflate method, page is a rough estimate on how big the compressed data will be (enlarged if necessary) */
DATA8 NBT_Compress(NBTFile nbt, int * size, int page, NBT_WriteCb_t cb, APTR cbparam)
{
	struct NBTWriteParam_t params = {
		NBT_WriteToZip, cb, cbparam
	};
	z_stream zip;
	uint8_t  chunk[1024];
	DATA8    buffer;

	page <<= 12;
	buffer = malloc(page);
	if (buffer == NULL) return NULL;
	memset(&zip, 0, sizeof zip);
	zip.next_in = chunk;
	zip.next_out = buffer;
	zip.avail_out = page;
	deflateInit(&zip, 9);

	NBT_WriteFile(nbt, &zip, 0, &params);

	if (zip.avail_in > 0)
	{
		while (deflate(&zip, Z_FINISH) == Z_OK)
		{
			/* not enough space in output buffer */
			DATA8 reloc = realloc(buffer, zip.total_out + 4096);
			if (reloc == NULL) { zip.avail_in = 1; break; }
			zip.avail_out = 4096;
			zip.next_out = reloc + zip.total_out;
			buffer = reloc;
		}
	}
	deflateEnd(&zip);

	if (zip.avail_in == 0)
	{
		*size = zip.total_out;
		return buffer;
	}
	free(buffer);
	*size = 0;
	return NULL;
}

/* write to a gzip-compressed file */
int NBT_Save(NBTFile nbt, STRPTR path, NBT_WriteCb_t cb, APTR cbparam)
{
	struct NBTWriteParam_t params = {
		NBT_WriteToGZ, cb, cbparam
	};
	gzFile out = gzopen(path, "wb");

	if (out)
	{
		int ret = NBT_WriteFile(nbt, out, 0, &params);
		gzclose(out);
		return ret;
	}
	return 0;
}

#ifdef DEBUG
int NBT_Dump(NBTFile root, int offset, int level, FILE * out)
{
	static STRPTR tagNames[] = {
		"TAG_End",   "TAG_Byte",   "TAG_Short",     "TAG_Int",    "TAG_Long",
		"TAG_Float", "TAG_Double", "TAG_ByteArray", "TAG_String", "TAG_List",
		"TAG_Compound"
	};
	DATA8  p, mem;
	NBTHdr hdr;
	int    i, type, sz, old;

	if (out == NULL)
		out = stderr;

	hdr = HDR(root, offset);
	type = hdr->type;
	if (type == 0) return -1;
	fprintf(out, "%*s", level, "");
	p = hdr->name;
	mem = p + ((hdr->minNameSz + 4) & ~3);
//	while (*mem) mem ++;
	NBTVariant val = (NBTVariant) mem;
	old = offset;
	sz = 0;
	offset += mem - (DATA8) hdr;

	switch (type & 15) {
	case TAG_Byte:   fprintf(out, "TAG_Byte(\"%s\"): %d [%d]\n", p, val->byte, hdr->size);     sz = 1; break;
	case TAG_Short:  fprintf(out, "TAG_Short(\"%s\"): %d [%d]\n", p, val->word, hdr->size);    sz = 2; break;
	case TAG_Int:    fprintf(out, "TAG_Int(\"%s\"): %d [%d]\n", p, val->dword, hdr->size);     sz = 4; break;
	case TAG_Long:   fprintf(out, "TAG_Long(\"%s\"): %I64d [%d]\n", p, val->qword, hdr->size); sz = 8; break;
	case TAG_Float:  fprintf(out, "TAG_Float(\"%s\"): %f [%d]\n", p, (double) val->real32, hdr->size);  sz = 4; break;
	case TAG_Double: fprintf(out, "TAG_Double(\"%s\"): %g [%d]\n", p, val->real64, hdr->size); sz = 8; break;
	case TAG_String: fprintf(out, "TAG_String(\"%s\"): %s [%d]\n", p, mem, hdr->size);         sz = strlen(mem) + 1; break;
	case TAG_Byte_Array:
		sz = hdr->count;
		fprintf(out, "TAG_Byte_Array(\"%s\"): [%d bytes/%d] {", p, hdr->count, hdr->size);
		for (i = 0; i < 10 && i < hdr->count; i ++)
			fprintf(out, "%s%u", i ? ", " : "", mem[i]);
		fprintf(out, ", ...}\n");
		break;
	case TAG_List:
		type >>= 4;
		fprintf(out, "TAG_List(\"%s\"): %d entries of type %s [%d]\n%*s{\n", p, hdr->count,
			tagNames[type], hdr->size, level, "");
		switch (type) {
		case TAG_Byte:
		case TAG_Short:
		case TAG_Int:
		case TAG_Long:
		case TAG_Float:
		case TAG_Double:
			sz = hdr->count * sizeof_type[type];
			for (i = 0; i < hdr->count; i ++)
			{
				DATA8 data = mem + i * sizeof_type[type];
				fprintf(out, "%*s%s: ", level + 3, "", tagNames[type]);
				/* arrays are left encoded as big endian */
				switch (sizeof_type[type]) {
				case 1: fprintf(out, "%u", data[0]); break;
				case 2: fprintf(out, "%u", ((uint16_t *)data)[0]); break;
				case 4:
					if (type == TAG_Float) fprintf(out, "%g", (double) ((float *)data)[0]);
					else fprintf(out, "%d", ((uint32_t *)data)[0]);
					break;
				case 8:
					if (type == TAG_Double) fprintf(out, "%g", ((double *)data)[0]);
					else fprintf(out, "%I64d", ((uint64_t *)data)[0]);
				}
				fputc('\n', out);
			}
			break;
		case TAG_List:
		case TAG_Int_Array:
		case TAG_Byte_Array: break;
		case TAG_String:
			for (i = 0, p = mem; i < hdr->count; i ++)
				fprintf(out, "%*sTAG_String: %s\n", level + 3, "", p+sz), sz += strlen(p+sz) + 1;
			break;
		case TAG_Compound:
			level += 3;
			for (i = 0; i < hdr->count; i ++)
			{
				fprintf(out, "%*sTAG_Compound(\"\"):\n%*s{\n", level, "", level, "");
				while ((sz = NBT_Dump(root, offset, level + 3, out)) > 0)
					offset += sz;
				offset += 4;
				fprintf(out, "%*s}\n", level, "");
			}
			sz = 0;
			level -= 3;
		}
		fprintf(out, "%*s}\n", level, "");
		break;
	case TAG_Compound:
		fprintf(out, "TAG_Compound(\"%s\"): [%d]\n%*s{\n", p, hdr->size, level, "");
		while ((i = NBT_Dump(root, offset, level + 3, out)) > 0)
			offset += i;
		sz += 4;
		fprintf(out, "%*s}\n", level, "");
		break;
	case TAG_Int_Array:
		fprintf(out, "TAG_Int_Array(\"%s\"): [%d ints/%d] {", p, hdr->count, hdr->size);
		sz = hdr->count * 4;
		for (i = 0; i < 10 && i < hdr->count; i ++)
		{
			DATA8 data = mem + i * 4;
			fprintf(out, "%s%u", i ? ", " : "", (((((data[0]<<8)|data[1])<<8)|data[2])<<8)|data[3]);
		}
		fprintf(out, ", ...}\n");
	}
	return offset + ((sz + 3) & ~3) - old;
}

void NBT_DumpCompound(NBTFile nbt)
{
	NBTIter_t iter;
	int off;
	NBT_InitIter(nbt, 0, &iter);

	while ((off = NBT_Iter(&iter)) >= 0)
		NBT_Dump(nbt, off, 0, 0);
}
#endif

/* parse standalone nbt file */
int NBT_Parse(NBTFile file, STRPTR path)
{
	ZStream in = gzOpen(path, 0, 0);

	memset(file, 0, sizeof *file);
	if (in)
	{
		file->page = 1023;
		NBT_ParseFile(file, in, 0);
		gzClose(in);
		return file->usage > 0;
	}
	return 0;
}

/* parse region chunk */
int NBT_ParseIO(NBTFile file, FILE * in, int offset)
{
	ZStream io = gzOpen(in, 1, offset);

	memset(file, 0, sizeof *file);
	if (io)
	{
		/*
		 * small trick to avoid relocating potentially huge memory segments when reading region chunks:
		 * - we initially alloc a rather big memory chunk to be sure it is not allocated in the middle
		 *   of a free heap list (ie: end of heap).
		 * - at the end of heap, enlarging a chunk is basically free (no memmove needed).
		 * - if we allocated too much, we reduce the size of the chunk (which, again, should not cuase
		 *   a pointer relocation).
		 * - next chunk allocation will reuse that last chunk, without risking relocation either.
		 * With this simple trick, there should be *NO* pointer relocation when reading chunks.
		 */
		file->max = 100 * 1024;
		file->page = 4095;
		file->mem = malloc(file->max);
		if (file->mem)
		{
			NBT_ParseFile(file, io, NBT_REGION_FLAG);
			/* round to nearest page number, to avoid wasting too much memory */
			int max = (file->usage + 4095) & ~4095;
			if (max < file->max)
			{
				/* we are reducing the size of the block */
				file->mem = realloc(file->mem, max);
				file->max = max;
			}
			gzClose(io);
			return 1;
		}
	}
	return 0;
}
