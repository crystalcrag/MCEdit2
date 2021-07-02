/*
 * Chunks.c : manage loading, saving and meshing of chunks.
 *
 * Written by T.Pierron, jan 2020
 */

#define CHUNK_IMPL
#include <stdio.h>
#include <stddef.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "maps.h"
#include "blocks.h"
#include "render.h"
#include "entities.h"
#include "NBT2.h"
#include "sign.h"

/*
 * reading chunk from disk
 */
static void chunkFillData(Chunk chunk, int y, int offset)
{
	ChunkData cd = calloc(sizeof *cd, 1);
	DATA8 base = NBT_Payload(&chunk->nbt, NBT_FindNode(&chunk->nbt, offset, "Blocks"));

	cd->blockIds = base;
//	cd->addId    = NBT_Payload(&chunk->nbt, NBT_FindNode(&chunk->nbt, offset, "Add"));
	cd->chunk    = chunk;
	cd->Y        = y * 16;

	chunk->layer[y] = cd;

//	cd->blockIds[8+8*16+256*8] = 0;

	#if 0
	memset(cd->blockIds, 0, 4096);
	memset(cd->blockIds + DATA_OFFSET, 0, 2048);
	memset(cd->blockIds + SKYLIGHT_OFFSET, 255, 2048);
	memset(cd->blockIds + BLOCKLIGHT_OFFSET, 0, 2048);
	cd->blockIds[8+8*16+256*8] = 1;
	cd->blockIds[DATA_OFFSET + ((8+8*16+256*8) >> 1)] = 1;
	#endif

	if (chunk->maxy <= y)
		chunk->maxy =  y+1;
}

/* from mapUpdate: create chunk on the fly */
ChunkData chunkCreateEmpty(Chunk c, int y)
{
	if (y >= CHUNK_LIMIT)
		return NULL;

	ChunkData cd = calloc(1, sizeof *cd + MIN_SECTION_MEM);

	DATA8 base = (DATA8) (cd+1);

	base += NBT_FormatSection(base, y);

	cd->blockIds = base;
	cd->chunk    = c;
	cd->Y        = y * 16;
	c->layer[y]  = cd;

	/* we will have to do add it manually to the NBT strucutre */
	NBT_MarkForUpdate(&c->nbt, c->secOffset, CHUNK_NBT_SECTION);

	memset(cd->blockIds + SKYLIGHT_OFFSET, 255, 2048);

	if (c->maxy <= y)
		c->maxy =  y+1;

	return cd;
}

#define chunkHashSize(count)     ((count) * sizeof (struct EntityEntry_t) + sizeof (struct EntityHash_t))
#define EOF_MARKER               0xffff

/* keep entities in a hash table */
static DATA8 chunkInsertTileEntity(EntityHash hash, EntityEntry ent)
{
	EntityEntry old;
	EntityEntry free;
	EntityEntry dest = (EntityEntry) (hash + 1);
	int         slot = ent->xzy % hash->max;

	for (old = NULL, free = dest + slot; free->data && free->xzy != ent->xzy; )
	{
		old = free;
		if (free->next == EOF_MARKER)
		{
			EntityEntry eof = dest + hash->max;
			do {
				free ++;
				if (free == eof) free = dest;
			} while (free->data);
			break;
		}
		free = dest + free->next;
	}
	DATA8 prev = free->data;
	ent->prev = old ? old - dest : EOF_MARKER;
	ent->next = EOF_MARKER;
	*free = *ent;
	return prev;
}

static EntityHash chunkCreateTileEntityHash(Chunk c, int count)
{
	EntityHash hash;
	int nb = roundToUpperPrime(count);
	int size = chunkHashSize(nb);

	if (size < c->nbt.max - c->nbt.usage)
	{
		/* alloc at end of NBT: use all available space */
		nb = roundToLowerPrime((c->nbt.max - c->nbt.usage - sizeof *hash) / sizeof (struct EntityEntry_t));
		hash = (EntityHash) (c->nbt.mem + c->nbt.usage);
		size = chunkHashSize(nb);
		memset(hash, 0, size);
	}
	else hash = calloc(size, 1);
	hash->max = nb;
	c->tileEntities = hash;
	return hash;
}

/*
 * pre-conditions:
 *    0 <= XYZ[0] <= 15    &&
 *    0 <= XYZ[1] <= 65535 &&
 *    0 <= XYZ[2] <= 15
 * ie: X, Z are relative to chunk <c>, Y is absolute coord
 */
Bool chunkAddTileEntity(Chunk c, int * XYZ, DATA8 mem)
{
	struct EntityEntry_t entry = {.data = mem};

	EntityHash hash = c->tileEntities;

	if (hash == NULL)
		/* create on the fly */
		hash = chunkCreateTileEntityHash(c, 1);

	int count = hash->count + 1;

	/* encode pos in 3 bytes */
	entry.xzy = (XYZ[1]<<16) | (XYZ[0] << 4) | XYZ[2];

	if (count == hash->max)
	{
		/* table is full: need to be enlarged */
		EntityHash reloc;
		EntityEntry ent;
		int i;

		count = roundToUpperPrime(hash->count);
		reloc = calloc(chunkHashSize(count), 1);
		if (! reloc) return False;
		c->tileEntities = reloc;

		reloc->max = count;
		reloc->count = i = count - 1;

		/* relocate entries */
		for (ent = (EntityEntry) (hash + 1); i > 0; i --, ent ++)
		{
			if (ent->data && chunkInsertTileEntity(reloc, ent))
				reloc->count --; /* hmm, duplicate tile entity in NBT: not good */
		}
		if (! STATIC_HASH(hash, c->nbt.mem, c->nbt.mem + c->nbt.usage))
			free(hash);
		hash = reloc;
	}
	mem = chunkInsertTileEntity(hash, &entry);
	if (mem)
	{
		/* tile entity was replaced instead of added */
		if (! (c->nbt.mem <= mem && mem < c->nbt.mem + c->nbt.usage))
			free(mem);
	}
	else hash->count ++;
	return True;
}

/* update X, Y, Z field of tile NBT record */
void chunkUpdateTilePosition(Chunk c, int * XYZ, DATA8 tile)
{
	NBTFile_t nbt = {.mem = tile};
	NBTIter_t iter;
	uint8_t   flags = 0;
	int       i;
	NBT_IterCompound(&iter, tile);
	while ((i = NBT_Iter(&iter)) >= 0 && flags != 7)
	{
		switch (FindInList("X,Y,Z", iter.name, 0)) {
		case 0: NBT_SetInt(&nbt, i, c->X + XYZ[0]); flags |= 1; break;
		case 2: NBT_SetInt(&nbt, i, c->Z + XYZ[2]); flags |= 2; break;
		case 1: NBT_SetInt(&nbt, i, XYZ[1]);        flags |= 4;
		}
	}
}

/* transfer NBT tile entites into a hash table for (way) faster access */
static void chunkExpandTileEntities(Chunk c)
{
	NBTFile_t nbt = c->nbt;
	int       off = NBT_FindNode(&c->nbt, 0, "TileEntities");
	NBTHdr    hdr = (NBTHdr) (nbt.mem + off);
	NBTIter_t iter;

	c->teOffset = off;
	if (off < 0) return;
	/* sometimes this node is saved as a TAG_List_End or TAG_List_Byte :-/ */
	hdr->type = TAG_List_Compound;
	if (hdr->count == 0) return;

	NBT_InitIter(&nbt, off, &iter);

	if (! c->tileEntities)
		chunkCreateTileEntityHash(c, hdr->count);

	while ((off = NBT_Iter(&iter)) > 0)
	{
		NBTIter_t sub;
		int       XYZ[3];
		int       i;

		NBT_IterCompound(&sub, nbt.mem+off);

		while ((i = NBT_Iter(&sub)) >= 0)
		{
			if (sub.name[1] == 0)
			switch (sub.name[0]) {
			case 'X': case 'x': XYZ[0] = NBT_ToInt(&nbt, off+i, 0) - c->X; break;
			case 'Z': case 'z': XYZ[2] = NBT_ToInt(&nbt, off+i, 0) - c->Z; break;
			case 'Y': case 'y': XYZ[1] = NBT_ToInt(&nbt, off+i, 0);
			}
		}
		chunkAddTileEntity(c, XYZ, nbt.mem + off);
	}
}

void chunkExpandEntities(Chunk c)
{
	int off = c->entOffset;
	c->cflags |= CFLAG_HASENTITY;
	if (off >= 0)
	{
		NBTHdr hdr = (NBTHdr) (c->nbt.mem + off);
		if (hdr->count == 0) return; /* empty list */
		entityParse(c, &c->nbt, off);
	}
}

/* get tile entity data based on its coordinates within chunk (same as chunkAddTileEntity()) */
DATA8 chunkGetTileEntity(Chunk c, int * XYZ)
{
	if (! c->tileEntities) return NULL;
	EntityHash  hash = c->tileEntities;
	EntityEntry base = (EntityEntry) (hash + 1);
	uint32_t    xzy  = (XYZ[1] << 16) | (XYZ[0] << 4) | XYZ[2];
	EntityEntry ent  = base + xzy % hash->max;

	if (ent->data == NULL) return NULL;
	while (ent->xzy != xzy)
	{
		if (ent->next == EOF_MARKER) return NULL;
		ent = base + ent->next;
	}
	return ent->data;
}

/* tile entity has been deleted: remove ref from hash */
DATA8 chunkDeleteTileEntity(Chunk c, int * XYZ)
{
	if (! c->tileEntities) return False;
	EntityHash  hash = c->tileEntities;
	EntityEntry base = (EntityEntry) (hash + 1);
	uint32_t    xzy  = (XYZ[1] << 16) | (XYZ[0] << 4) | XYZ[2];
	EntityEntry ent  = base + xzy % hash->max;
	DATA8       data = ent->data;

	if (data == NULL) return False;
	while (ent->xzy != xzy)
	{
		if (ent->next == EOF_MARKER) return False;
		ent = base + ent->next;
	}
	if (ent->prev != EOF_MARKER)
	{
		EntityEntry prev = base + ent->prev;
		prev->next = ent->next;
		if (ent->next != EOF_MARKER)
		{
			EntityEntry next = base + ent->next;
			next->prev = ent->prev;
		}
	}
	else if (ent->next != EOF_MARKER)
	{
		/* removing first link in the chain: need to move next item here */
		EntityEntry next = base + ent->next;
		memmove(ent, base, sizeof *ent);
		ent->prev = EOF_MARKER;
		ent = next;
	}
	fprintf(stderr, "deleting tile entity at %d, %d, %d\n", c->X + XYZ[0], XYZ[1], c->Z + XYZ[2]);
	if (! (c->nbt.mem <= data && data < c->nbt.mem + c->nbt.usage))
		free(data);
	hash->count --;
	ent->data = NULL;
	return data;
}

Bool chunkLoad(Chunk chunk, const char * path, int x, int z)
{
	STRPTR region = alloca(strlen(path) + 32);

	chunk->X = x;
	chunk->Z = z;
	/* convert to chunk coordinate */
	x >>= 4;
	z >>= 4;

	/* convert to region coordinate */
	sprintf(region, "%s/r.%d.%d.mca", path, x >> 5, z >> 5);

	FILE * in = fopen(region, "rb");

	if (in)
	{
		NBTFile_t nbt;
		uint8_t   offset[4];
		fseek(in, 4 * ((x&31) + (z&31) * 32), SEEK_SET);
		fread(offset, 1, 4, in);

		if (NBT_ParseIO(&nbt, in, 4096 * ((((offset[0] << 8) | offset[1]) << 8) | offset[2])))
		{
			fclose(in);
			nbt.alloc = 0;
			chunk->signList       = -1;
			chunk->nbt            = nbt;
			chunk->lightPopulated = NBT_ToInt(&nbt, NBT_FindNode(&nbt, 0, "LightPopulated"), 0);
			chunk->terrainDeco    = NBT_ToInt(&nbt, NBT_FindNode(&nbt, 0, "TerrainPopulated"), 0);
			chunk->heightMap      = NBT_Payload(&nbt, NBT_FindNode(&nbt, 0, "HeightMap"));
			chunk->biomeMap       = NBT_Payload(&nbt, NBT_FindNode(&nbt, 0, "Biomes"));
			chunk->entOffset      = NBT_FindNode(&nbt, 0, "Entities");
			chunk->entityList     = ENTITY_END;

			chunkExpandTileEntities(chunk);

			int secOffset = NBT_FindNode(&nbt, 0, "Sections");
			if (secOffset > 0)
			{
				NBTIter_t iter;
				NBT_InitIter(&nbt, secOffset, &iter);
				chunk->secOffset = secOffset;
				while ((secOffset = NBT_Iter(&iter)) >= 0)
				{
					int y = NBT_ToInt(&nbt, NBT_FindNode(&nbt, secOffset, "Y"), 0);
					if (y < CHUNK_LIMIT && chunk->layer[y] == NULL)
						chunkFillData(chunk, y, secOffset);
				}
			}
			return True;
		}
		else fclose(in);
	}

	return False;
}

/* will have to do some post-processing when saving this chunk */
void chunkMarkForUpdate(Chunk c)
{
	NBT_MarkForUpdate(&c->nbt, c->teOffset < 0 ? NBT_FindNode(&c->nbt, 0, "Level") : c->teOffset, CHUNK_NBT_TILEENTITES);
	c->cflags |= CFLAG_MARKMODIF;
}

/* insert <nbt> fragment at the location of tile entity pointed by <blockOffset> (ie: coord of tile entity within chunk) */
Bool chunkUpdateNBT(Chunk c, int blockOffset, NBTFile nbt)
{
	DATA8 tile;
	int   XYZ[3];

	NBT_SetHdrSize(nbt, 0);

	XYZ[0] = blockOffset & 15; blockOffset >>= 4;
	XYZ[2] = blockOffset & 15; blockOffset >>= 4;
	XYZ[1] = blockOffset;

	tile = chunkGetTileEntity(c, XYZ);

	/* small optimization: if size is same as what's currently stored, overwrite data */
	if (tile && c->nbt.mem <= tile && tile < c->nbt.mem + c->nbt.usage)
	{
		NBTIter_t iter;
		NBT_IterCompound(&iter, tile);
		while (NBT_Iter(&iter) >= 0);
	    if (nbt->usage == iter.offset)
		{
			memcpy(tile, nbt->mem, iter.offset);
			NBT_Free(nbt);
			c->cflags |= CFLAG_NEEDSAVE;
			return True;
		}
	}

	/* we will need to modify NBT structure when saving this chunk */
	if ((c->cflags & CFLAG_MARKMODIF) == 0)
		chunkMarkForUpdate(c);
	c->cflags |= CFLAG_NEEDSAVE;

	/* add or update pointer in hash */
	return chunkAddTileEntity(c, XYZ, nbt->mem);
}

/*
 * saving chunk to disk
 */

/* find a place inside a region file to allocate the number of requested 4Kb pages */
static int chunkAllocSpace(FILE * in, int pages)
{
	uint32_t usedSpace[1024]; /* worst case scenario: each chunk has its own slot */
	uint8_t  header[256];
	int      usage = 0;

	if (fseek(in, 0, SEEK_SET) == 0)
	{
		int total = 0, offset, pos, cnt, i;
		/* build a map of the used space in the region file: try to fit the <pages> in between to limit unused space */
		while (total < 4096 && fread(header, 1, sizeof header, in) == sizeof header)
		{
			DATA8 p;
			for (p = header; p < EOT(header); p += 4)
			{
				/* minus 2 comes from region header: 4Kb for offset+page, 4Kb for timestamp = 2 pages */
				offset = BE24(p) - 2;
				if (p[3] == 0) continue; /* chunk not generated yet */
				for (i = 0; i < usage; i ++)
				{
					/* generate a sorted list, with partial merge */
					pos = usedSpace[i];
					cnt = pos & 0xff; pos >>= 8;
					if (offset + p[3] < pos)
					{
						DATA32 insert;
						insert_here:
						insert = usedSpace + i;
						if (i < usage)
							memmove(insert + 1, insert, (usage - i) * 4);
						insert[i] = (pos << 8) | cnt;
						break;
					}
					else if (offset == pos + cnt)
					{
						/* enlarge this slot */
						cnt += p[3];
						if (cnt >= 256)
						{
							/* split insert here */
							p[3] = 255;
							cnt -= 255;
							pos += 255;
							i ++;
							usage ++;
							goto insert_here;
						}
						else p[3] = cnt;
						break;
					}
				}
				if (i == usage)
				{
					/* insert at the end */
					usedSpace[i] = (offset << 8) | p[3];
					usage ++;
				}
			}
			total += sizeof header;
		}

		/* now we can scan for a free slot */
		for (i = 0, total = 0; i < usage; i ++)
		{
			pos = usedSpace[i];
			cnt = pos & 0xff; pos >>= 8;
			if (total - pos >= pages)
				return (total + 2) << 12;

			total = pos + cnt;
		}
		/* insert at end */
		return (total + 2) << 12;
	}
	return -1;
}

/* save all the extra NBT tags we added to a chunk */
static int chunkSaveExtra(int tag, APTR cbparam, NBTFile nbt)
{
	ChunkData cd;
	EntityHash hash;
	EntityEntry ent;
	Chunk chunk = cbparam;
	int i;

	switch (tag) {
	case CHUNK_NBT_TILEENTITES:
		/* list of tile entities modified */
		hash = chunk->tileEntities;
		if (nbt == NULL)
			/* return total count of tile entities */
			return hash ? hash->count : 0;

		if (hash->save == 0)
		{
			if (chunk->teOffset < 0)
			{
				static NBTFile_t header;
				static uint8_t   content[64];
				if (header.mem == NULL)
				{
					header.mem = content;
					header.max = sizeof content;
					NBT_Add(&header,
						TAG_List_Compound, "TileEntities", 0,
						TAG_Compound_End
					);
					NBT_MarkForUpdate(&header, 0, CHUNK_NBT_TILEENTITES);
				}
				nbt->mem = content;
				nbt->usage = header.usage;
				hash->save ++;
				return 1;
			}
			else hash->save ++;
		}

		/* this will certainly change the order of tile entities each time it is saved :-/ */
		for (i = hash->save - 1, ent = (EntityEntry) (hash + 1) + i; i < hash->max; i ++, ent ++)
		{
			if (ent->data == NULL) continue;
			NBTIter_t iter;
			NBT_IterCompound(&iter, ent->data);
			while (NBT_Iter(&iter) >= 0);
			nbt->mem = ent->data;
			nbt->usage = iter.offset-4;
			hash->save = i + 2;
			return 1;
		}
		break;

	case CHUNK_NBT_SECTION:
		/* has to return TAG_List count */
		if (nbt == NULL)
			return chunk->maxy;

		if (chunk->cdIndex < chunk->maxy)
		{
			/* this will point to original NBT struct or a newly allocated chunk */
			cd = chunk->layer[chunk->cdIndex];
			nbt->usage  = MIN_SECTION_MEM;
			nbt->mem    = cd->blockIds - 16;
			chunk->cdIndex ++;
			return 1;
		}
	}
	return 0;
}

Bool chunkSave(Chunk chunk, const char * path)
{
	STRPTR region = alloca(strlen(path) + 32);

	/* convert to chunk coordinate */
	int x = chunk->X >> 4;
	int z = chunk->Z >> 4;

	/* convert to region coordinate */
	sprintf(region, "%s/r.%d.%d.mca", path, x >> 5, z >> 5);

	FILE * io = fopen(region, "rb+");
	Bool   ret = False;

	if (! io && ! FileExists((STRPTR) path))
	{
		/* does not exist yet: create it */
		io = fopen(path, "wb+");

		if (io)
		{
			/* init with 8Kb of zero */
			uint8_t zero = 0;
			fseek(io, 8192-1, SEEK_SET);
			fwrite(&zero, 1, 1, io);
		}
	}

	if (io)
	{
		uint8_t offset[4];
		int     hdrOffset = 4 * ((x&31) + (z&31) * 32);

		if (fseek(io, hdrOffset, SEEK_SET) == 0 && fread(offset, 1, 4, io) == 4)
		{
			DATA8 zstream;
			int   chunkOffset;
			int   chunkSize;
			int   chunkPage;

			fprintf(stderr, "saving chunk %d, %d\n", chunk->X, chunk->Z);
			if (chunk->tileEntities)
				((EntityHash)chunk->tileEntities)->save = 0;
			chunk->cdIndex = 0;

			chunkOffset = BE24(offset) << 12;
			zstream     = NBT_Compress(&chunk->nbt, &chunkSize, offset[3], chunkSaveExtra, chunk);
			chunkPage   = (chunkSize + 4100) >> 12;

			/* note +4100 comes from: +4095 to round the number of pages up, and +5 to add the 5 bytes header before z-stream */
			if (chunkPage > 255)
			{
				/* someone is dropping too many tile entities in this chunk it seems :-/ */
				ret = False;
				free(zstream);
			}
			else if (zstream)
			{
				if (chunkPage > offset[3])
				{
					/* argh, need to be rellocated */
					chunkOffset = chunkAllocSpace(io, chunkPage);
				}

				if (chunkOffset > 0 && fseek(io, hdrOffset, SEEK_SET) == 0)
				{
					/* this part is critical: be super cautious around here, because it can corrupt the world save */
					uint32_t curTime = time(NULL);
					uint8_t  header[5];
					uint8_t  oldhdr[5];
					/* offset and page of chunk in region header */
					x = chunkOffset >> 12;
					TOBE24(header, x);
					header[3] = chunkPage;
					/* unlikely to fail */
					if (fwrite(header, 1, 4, io) != 4)
						goto bail;

					/* timestamp, not sure if this is still used (probably never was...) */
					if (fseek(io, hdrOffset + 4096, SEEK_SET) == 0)
						/* don't care if this fails */
						fwrite(&curTime, 1, 4, io);

					/* finally z-stream content of chunk */
					x = chunkSize;
					header[3] = x & 0xff;  x >>= 8;
					header[2] = x & 0xff;  x >>= 8;
					header[1] = x & 0xff;  x >>= 8;
					header[0] = x;
					header[4] = 2; /* zlib compressed stream will follow */

					/* might fail if chunk is added */
					memset(oldhdr, 0, sizeof oldhdr);
					if (fseek(io, chunkOffset, SEEK_SET) == 0)
						fread(oldhdr, 1, 5, io);

					if (fseek(io, chunkOffset, SEEK_SET) == 0 &&
						/* the most likely scenario is to run out of disk space */
						fwrite(header, 1, 5, io) == 5 && fwrite(zstream, 1, chunkSize, io) == chunkSize)
					{
						/* chunk size cannot be more than 1Mb */
						int pad = BE24(oldhdr+1) - chunkSize;

						/* pad unused space with zeroes: technically not necessery, but world size will be smaller if zipped */
						while (pad > 0)
						{
							int size = MIN(pad, chunkSize);
							memset(zstream, 0, size);
							fwrite(zstream, 1, size, io);
							pad -= size;
						}

						/* yay, success */
						chunk->cflags &= ~CFLAG_NEEDSAVE;
						ret = True;
					}
					/* argh, something's fubar, try to restore header at least */
					else if (fseek(io, hdrOffset, SEEK_SET) == 0)
					{
						/* if chunk was partially overwritten, it is going to be fubar though */
						fwrite(offset, 1, 4, io);
					}
				}
				bail:
				free(zstream);
			}
		}
		fclose(io);
	}
	return ret;
}

/*
 * Very important part: convert a sub-chunk into a mesh of triangles.
 * See internals.html for a quick explanation on how this part works.
 *
 * Reader warning: Look up table hell.
 */

uint8_t vertex[] = { /* 8 vertices of a 1x1x1 cube */
	0,0,1,  1,0,1,  1,1,1,  0,1,1,
	0,0,0,  1,0,0,  1,1,0,  0,1,0,
};
uint8_t cubeIndices[6*4] = { /* face (quad) of cube: S, E, N, W, T, B */
	9, 0, 3, 6,    6, 3, 15, 18,     18, 15, 12, 21,     21, 12, 0, 9,    21, 9, 6, 18,      0, 12, 15, 3
/*  3, 0, 1, 2,    2, 1,  5,  6,      6,  5,  4,  7,      7,  4, 0, 3,     7, 3, 2,  6,      0,  4,  5, 1 */
};
uint8_t skyBlockOffset[] = { /* where to get skylight to shade a vertex of a cube: grab max of 4 values per vertex */
	15, 16, 24, 25,    6,  7, 15, 16,    7,  8, 16, 17,    16, 17, 25, 26,
	14, 17, 23, 26,    5,  8, 14, 17,    2,  5, 11, 14,    11, 14, 20, 23,
	10, 11, 19, 20,    1,  2, 10, 11,    0,  1,  9, 10,     9, 10, 18, 19,
	 9, 12, 18, 21,    0,  3,  9, 12,    3,  6, 12, 15,    12, 15, 21, 24,
	18, 19, 21, 22,   21, 22, 24, 25,   22, 23, 25, 26,    19, 20, 22, 23,
	 3,  4,  6,  7,    0,  1,  3,  4,    1,  2,  4,  5,     4,  5,  7,  8
};
uint8_t quadIndices[] = { /* coord within <vertex> to make a quad from a QUAD block type */
	 9, 0,  15, 18,       /* QUAD_CROSS */
	21, 12,  3,  6,       /* QUAD_CROSS (2nd part) */
	21, 12, 15, 18,       /* QUAD_NORTH */
	 6,  3,  0,  9,       /* QUAD_SOUTH */
	18, 15,  3,  6,       /* QUAD_EAST */
	 9,  0, 12, 21,       /* QUAD_WEST */
	12,  0,  3, 15,       /* QUAD_BOTTOM */
	18, 12,  0,  6,       /* QUAD_ASCE */
	 9,  3, 15, 21,       /* QUAD_ASCW */
	21,  0,  3, 18,       /* QUAD_ASCN */
	 6, 15, 12,  9,       /* QUAD_ASCS */
};

/* normal vector for given quad type (QUAD_*); note: 6 = none */
uint8_t quadSides[] = {6, 6, 0, 2, 3, 1, 4, 1, 3, 2, 0};

uint8_t openDoorDataToModel[] = {
	5, 6, 7, 4, 3, 0, 1, 2
};

uint8_t texCoord[] = { /* tex coord for each face: each line is a rotation, indexed by (Block.rotate&3)*8 */
	0,0,    0,1,    1,1,    1,0,
	0,1,    1,1,    1,0,    0,0,
	1,1,    1,0,    0,0,    0,1,
	1,0,    0,0,    0,1,    1,1,
};
static int offsets[] = { /* neighbors: S, E, N, W, T, B */
	16, 1, -16, -1, 256, -256
};
static int8_t offsetConnected[] = { /* T, E, B, W */
	9+13, 1+13, -9+13, -1+13,     9+13, -3+13, -9+13,  3+13,    9+13, -1+13, -9+13,  1+13,
	9+13, 3+13, -9+13, -3+13,    -3+13,  1+13,  3+13, -1+13,    3+13,  1+13, -3+13, -1+13
};
int8_t normals[] = { /* normal per face */
	 0,  0,  1, 0,
	 1,  0,  0, 0,
	 0,  0, -1, 0,
	-1,  0,  0, 0,
	 0,  1,  0, 0,
	 0, -1,  0, 0
};

/* check which face has a hole in it */
uint8_t slotsY[] = {64,0,0,0,0,0,0,0,0,0,0,0,0,0,0,32};
uint8_t slotsXZ[256];

#define VTX_1      (BASEVTX + ORIGINVTX)
#define VTX_0      ORIGINVTX
uint8_t  axisCheck[] = {2, 0, 2, 0, 1, 1};
uint16_t axisAlign[] = {VTX_1, VTX_1, VTX_0, VTX_0, VTX_1, VTX_0};
#undef VTX_0
#undef VTX_1

/*
 * 26 surrounding blocks that can impact shading around one block: all blocks except center one
 * offsets are ordered in increasing XZY (like chunks) into a 3x3x3 cube (middle one being empty).
 */
static int16_t occlusionNeighbors[27];

/* check if neighbor is in another chunk (bitfield from S,E,N,W,T,B) */
static uint8_t occlusionSides[27];
static int8_t  subChunkOff[64];
static uint8_t oppositeMask[64];
static int16_t blockOffset[64];
static int16_t blockOffset2[64];

#define IDS(id1,id2)   (1<<id1)|(1<<id2)
#define IDC(id)        (1<<id)
static int occlusionIfNeighbor[] = { /* indexed by cube indices */
	IDS(15,25),  IDS(15, 7),  IDS(17, 7),  IDS(25,17),
	IDS(23,17),  IDS(17, 5),  IDS(11, 5),  IDS(23,11),
	IDS(19,11),  IDS(11, 1),  IDS( 9, 1),  IDS(19,9),
	IDS(21, 9),  IDS( 9, 3),  IDS(15, 3),  IDS(21,15),
	IDS(21,19),  IDS(25,21),  IDS(23,25),  IDS(23,19),
	IDS( 7, 3),  IDS( 3, 1),  IDS( 5, 1),  IDS( 7, 5)
};
static int occlusionIfCorner[] = {
	IDC(24),  IDC( 6),  IDC( 8),  IDC(26),
	IDC(26),  IDC( 8),  IDC( 2),  IDC(20),
	IDC(20),  IDC( 2),  IDC( 0),  IDC(18),
	IDC(18),  IDC( 0),  IDC( 6),  IDC(24),
	IDC(18),  IDC(24),  IDC(26),  IDC(20),
	IDC( 6),  IDC( 0),  IDC( 2),  IDC(8),
};
#undef IDC
#undef IDS

static int occlusionForSlab[] = { /* indexed by face id: S,E,N,W,T,B */
	1<<16, 1<<14, 1<<10, 1<<12, 1<<22, 1<<4
};

#if 0 /* XXX FACEVEC not good: need 33 bits, only 32 available */
#define FACE(x,v1,v2)                       ((x-1) | (v1<<4) | (v2<<7))
#define FACEVEC(x,y,z,v1,v2,v3,v4,v5,v6)    (FACE(x,v1,v2) | (FACE(y,v3,v4) << 10) | (FACE(z,v5,v6)<<20))
static uint32_t lineDefOCS[] = {
	FACEVEC(16,25,26,6,2,5,1,4,0), FACEVEC(16, 7, 8,5,1,6,2,7,3), FACEVEC( 8, 9,18,6,2,7,3,4,0), FACEVEC(26,27,18,5,1,4,0,7,3), // S
	FACEVEC(24,27,18,0,1,4,5,7,6), FACEVEC(18, 9, 6,4,5,7,6,3,2), FACEVEC( 3, 6,12,3,2,7,6,0,1), FACEVEC(12,21,24,3,2,0,1,7,6), // E
	FACEVEC(20,21,12,1,5,0,4,3,7), FACEVEC( 2, 3,12,2,6,3,7,0,4), FACEVEC( 1, 2,10,2,6,3,7,0,4), FACEVEC(10,19,20,2,6,1,5,0,4), // N
	FACEVEC(10,19,22,2,3,1,0,5,4), FACEVEC( 1, 4,10,2,3,6,7,1,0), FACEVEC( 4, 7,16,2,3,6,7,5,4), FACEVEC(16,25,22,6,7,5,4,1,0), // W
	FACEVEC(19,20,22,1,2,0,3,5,6), FACEVEC(22,25,26,1,2,5,6,4,7), FACEVEC(24,26,27,0,3,5,6,4,7), FACEVEC(20,21,24,1,2,0,3,4,7), // T
	FACEVEC( 2, 3, 6,2,1,3,0,7,4), FACEVEC( 6, 8, 9,3,0,6,5,7,4), FACEVEC( 4, 7, 8,2,1,6,5,7,4), FACEVEC( 1, 2, 4,2,1,3,0,6,5), // B
};
#undef FACEVEC
#undef FACE
#endif

/* indicates whether we can find the neighbor block in the current chunk (sides&1)>0 or in the neighbor (sides&1)==0 */
static uint8_t xsides[] = { 2, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,  8};
static uint8_t zsides[] = { 1,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  4};
static uint8_t ysides[] = {16, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 32};

/* these tables are used to list neighbor chunks, if a block is updated at a boundary (383 bytes) */
uint8_t updateChunk[] = {
	0, 43, 1, 71, 3, 43, 26, 112, 5, 96, 88, 71, 60, 43, 26, 0, 8, 169, 164, 153,
	148, 140, 129, 0, 107, 96, 88, 71, 60, 43, 26, 226, 17, 223, 220, 213, 210,
	205, 198, 0, 195, 188, 183, 71, 176, 43, 26, 0, 174, 169, 164, 153, 148, 140,
	129, 112, 107, 96, 88, 71, 60, 43, 26, 0,
};

uint8_t updateLength[] = {
	0, 1, 1, 3, 1, 2, 3, 5, 1, 3, 2, 5, 3, 5, 5, 8, 1, 3, 3, 7, 3, 5, 7, 165, 3, 7,
	5, 11, 7, 11, 11, 17, 1, 3, 3, 7, 3, 5, 7, 133, 3, 7, 5, 101, 7, 69, 37, 8, 2,
	5, 5, 11, 5, 8, 11, 17, 5, 11, 8, 17, 11, 17, 17, 26,
};

uint16_t updateMore[] = {
	2313, 1542, 1542, 1542, 1548, 1539
};

uint8_t updateChunks[243] = { /* bitfield S, E, N, W, T, B */
	 1,  2,  3,  4,  6,  8,  9, 12, 16, 17, 18, 19, 20, 22, 24, 25, 28, 32, 33, 34,
	35, 36, 38, 40, 41, 44,  2,  4,  6,  8, 12, 16, 18, 20, 22, 24, 28, 32, 34, 36,
	38, 40, 44,  1,  4,  8,  9, 12, 16, 17, 20, 24, 25, 28, 32, 33, 36, 40, 41, 44,
	 4,  8, 12, 16, 20, 24, 28, 32, 36, 40, 44,  1,  2,  3,  8,  9, 16, 17, 18, 19,
	24, 25, 32, 33, 34, 35, 40, 41,  2,  8, 16, 18, 24, 32, 34, 40,  1,  8,  9, 16,
	17, 24, 25, 32, 33, 40, 41,  8, 16, 24, 32, 40,  1,  2,  3,  4,  6, 16, 17, 18,
	19, 20, 22, 32, 33, 34, 35, 36, 38,  2,  4,  6, 16, 18, 20, 22, 32, 34, 36, 38,
	 1,  4, 16, 17, 20, 32, 33, 36,  4, 16, 20, 32, 36,  1,  2,  3, 16, 17, 18, 19,
	32, 33, 34, 35,  2, 16, 18, 32, 34,  1, 16, 17, 32, 33, 16, 32,  4,  8, 12, 32,
	36, 40, 44,  2,  8, 32, 34, 40,  1,  8,  9, 32, 33, 40, 41,  8, 32, 40,  2,  4,
	 6, 32, 34, 36, 38,  1,  4, 32, 33, 36,  4, 32, 36,  1,  2,  3, 32, 33, 34, 35,
	 2, 32, 34,  1, 32, 33,  1,  2,  3,  4,  6,  8,  9, 12, 16, 17, 18, 19, 20, 22,
	24, 25, 28,
};

/*
 * cave culling tables: see internals.html to know how these tables work
 */

/* given a S,E,N,W,T,B bitfield, will give what face connections we can reach */
uint16_t faceCnx[] = {
0, 0, 0, 1, 0, 2, 32, 35, 0, 4, 64, 69, 512, 518, 608, 615, 0, 8, 128, 137, 1024,
1034, 1184, 1195, 4096, 4108, 4288, 4301, 5632, 5646, 5856, 5871, 0, 16, 256, 273,
2048, 2066, 2336, 2355, 8192, 8212, 8512, 8533, 10752, 10774, 11104, 11127, 16384,
16408, 16768, 16793, 19456, 19482, 19872, 19899, 28672, 28700, 29120, 29149, 32256,
32286, 32736, 32767
};

/* given two faces (encoded as bitfield S,E,N,W,T,B), return connection bitfield */
uint16_t hasCnx[] = {
0, 0, 0, 1, 0, 2, 32, 0, 0, 4, 64, 0, 512, 0, 0, 0, 0, 8, 128, 0, 1024, 0, 0, 0,
4096, 0, 0, 0, 0, 0, 0, 0, 0, 16, 256, 0, 2048, 0, 0, 0, 8192, 0, 0, 0, 0, 0, 0,
0, 16384, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

/* yep, more look-up table init */
void chunkInitStatic(void)
{
	int8_t i, x, y, z;
	int    pos;

	for (i = 0; i < 64; i ++)
	{
		int8_t layer = 0;
		if (i & 16) layer ++;
		if (i & 32) layer --;
		subChunkOff[i] = layer;
		pos = 0;
		if (i & 1) pos -= 15*16;
		if (i & 2) pos -= 15;
		if (i & 4) pos += 15*16;
		if (i & 8) pos += 15;
		if (i & 16) pos -= 15*256;
		if (i & 32) pos += 15*256;
		blockOffset[i] = pos;

		pos = 0;
		if (i & 1) pos += 16;
		if (i & 2) pos ++;
		if (i & 4) pos -= 16;
		if (i & 8) pos --;
		if (i & 16) pos += 256;
		if (i & 32) pos -= 256;
		blockOffset2[i] = pos;

		pos = 0;
		if (i & 1)  pos |= 4;
		if (i & 2)  pos |= 8;
		if (i & 4)  pos |= 1;
		if (i & 8)  pos |= 2;
		if (i & 16) pos |= 32;
		if (i & 32) pos |= 16;
		oppositeMask[i] = pos;
	}
	for (i = 0, x = -1, y = -1, z = -1; i < 27; i ++)
	{
		occlusionNeighbors[i] = x + y * 256 + z * 16;
		occlusionSides[i] = (x < 0 ? 8 : x > 0 ? 2 : 0) | (z < 0 ? 4 : z > 0 ? 1 : 0) | (y < 0 ? 32 : y > 0 ? 16 : 0);
		x ++;
		if (x > 1)
		{
			x = -1; z ++;
			if (z > 1) z = -1, y ++;
		}
	}
	occlusionSides[13] = 0; /* center block */

	for (pos = 0; pos < 256; pos ++)
	{
		x = pos & 15;
		z = pos >> 4;
		slotsXZ[pos] = (x == 0 ? 1 << (SIDE_WEST+1)  : x == 15 ? 1 << (SIDE_EAST+1)  : 0) |
		               (z == 0 ? 1 << (SIDE_NORTH+1) : z == 15 ? 1 << (SIDE_SOUTH+1) : 0);
	}
}

#if 0
static uint8_t chunkBlockOcclusion(DATA16 blockIds, uint32_t lineDef)
{
	int i;
	for (i = 0; i < 3; i ++, lineDef >>= 10)
	{
		uint16_t id = blockIds[lineDef&31];
		Block b = blockGetByIdData(id>>4, id&15);
		if (b->special == BLOCK_STAIRS || b->special == BLOCK_HALF)
		{
			static uint8_t flags[] = {4, 8, 128, 64, 1, 2, 32, 16};
			uint8_t model = *halfBlockGetModel(b, 2, NULL);
			id = 0;
			if (model & flags[(lineDef>>4) & 3]) id += 0x80;
			if (model & flags[(lineDef>>7) & 3]) id += 0x40;
			return id;
		}
		else if (b->type == SOLID)
		{
			return 0xc0;
		}
	}
	return 0;
}
#endif

static void chunkAddEmitters(ChunkData cd, int pos, int type)
{
	DATA16 list = cd->emitters;
	if (list == NULL || list[0] == list[1])
	{
		if (list == NULL)
		{
			list = malloc(32);
			list[0] = 0;
			list[1] = 14;
		}
		else list = realloc(list, list[1] + 18), list[1] += 16;
		cd->emitters = list;
	}
	list[list[0]+2] = pos | (type << 12);
	list[0] ++;
}

extern int breakPoint;
static void chunkGenQuad(ChunkData neighbors[], WriteBuffer buffer, BlockState b, int pos);
static void chunkGenCust(ChunkData neighbors[], WriteBuffer opaque, BlockState b, int pos);
static void chunkGenCube(ChunkData neighbors[], WriteBuffer opaque, BlockState b, int pos);
int mapUpdateGetCnxGraph(ChunkData, int start, DATA8 visited);

/*
 * transform chunk data into something useful for the vertex shader (blocks.vsh)
 * this is the "meshing" function for our world
 */
void chunkUpdate(Chunk c, ChunkData empty, int layer)
{
	static uint8_t visited[512];
	struct WriteBuffer_t alpha, opaque;
	ChunkData neighbors[7];    /* S, E, N, W, T, B, current */
	ChunkData cur;
	int i, pos, air;

	renderInitBuffer(neighbors[6] = cur = c->layer[layer], &opaque, &alpha);

	/* 6 surrounding chunks (+center) */
	neighbors[5] = layer > 0 ? c->layer[layer-1] : NULL;
	neighbors[4] = layer+1 < c->maxy ? c->layer[layer+1] : empty;
	for (i = 0; i < 4; i ++)
	{
		neighbors[i] = (c + chunkNeighbor[c->neighbor + (1<<i)])->layer[layer];
		if (neighbors[i] == NULL) neighbors[i] = empty;
	}
	if (cur->emitters)
		cur->emitters[0] = 0;

	/* default sorting for alpha quads */
	cur->yaw = 3.14926535 * 1.5;
	cur->pitch = 0;

	memset(visited, 0, sizeof visited);

//	if (c->X == -224 && cur->Y == 64 && c->Z == -48)
//		breakPoint = 1;

	for (pos = air = 0; pos < 16*16*16; pos ++)
	{
		BlockState state;
		uint8_t    data;
		uint8_t    block;
		DATA8      blocks = cur->blockIds;

		data  = blocks[DATA_OFFSET + (pos >> 1)]; if (pos & 1) data >>= 4; else data &= 15;
		block = blocks[pos];
		state = blockGetByIdData(block, data);

//		if (breakPoint && pos == 45)
//			breakPoint = 2;

		/* 3d flood fill for cave culling */
		if (state->type != SOLID && (slotsXZ[pos & 0xff] || slotsY[pos >> 8]))
			cur->cnxGraph |= mapUpdateGetCnxGraph(cur, pos, visited);

		if (blockIds[block].particle)
			if (block != 55 || data > 0) // XXX needs to be declared in blockTable.js :-/
				chunkAddEmitters(cur, pos, blockIds[block].particle - 1);

		switch (state->type) {
		case QUAD:
			chunkGenQuad(neighbors, &opaque, state, pos);
			break;
		case CUST:
			if (state->custModel)
			{
				chunkGenCust(neighbors, STATEFLAG(state, ALPHATEX) ? &alpha : &opaque, state, pos);
				/* SOLIDOUTER: custom block with ambient occlusion */
				if (state->special != BLOCK_SOLIDOUTER)
					break;
			}
			/* else no break; */
		case TRANS:
			// no break;
		case SOLID:
			chunkGenCube(neighbors, STATEFLAG(state, ALPHATEX) ? &alpha : &opaque, state, pos);
			break;
		default:
			if (state->id == 0) air ++;
		}
	}

	/* entire sub-chunk is composed of air: check if we can get rid of it */
	if (air == 4096)
	{
		/* block light must be all 0 and skylight be all 15 */
		if (memcmp(cur->blockIds + BLOCKLIGHT_OFFSET, empty->blockIds + BLOCKLIGHT_OFFSET, 2048) == 0 &&
			memcmp(cur->blockIds + SKYLIGHT_OFFSET,   empty->blockIds + SKYLIGHT_OFFSET,   2048) == 0 &&
			(cur->Y >> 4) == c->maxy-1)
		{
			/* yes, can be freed */
			c->layer[cur->Y >> 4] = NULL;
			c->maxy --;
			/* cannot delete it now, but will be done after VBO has been cleared */
			cur->pendingDel = True;
			NBT_MarkForUpdate(&c->nbt, c->secOffset, CHUNK_NBT_SECTION);
			return;
		}
	}

	if (opaque.cur > opaque.start) opaque.flush(&opaque);
	if (alpha.cur  > alpha.start)  alpha.flush(&alpha);
}

/*
 * vertex format:
 * - 6 bytes for coord, using fixed point [0 - 65280] => [-0.5 - 16.5]
 * - 3 bytes for UV coord (19bits) + normal (3bits) + OCS (2bits)
 * - 1 byte for skylight + blocklight
 */

#define BUF_LESS_THAN(buffer,min)   (((DATA8)buffer->end - (DATA8)buffer->cur) < min)
#define META(cd,off)                ((cd)->blockIds[DATA_OFFSET + (off)])
#define LIGHT(cd,off)               ((cd)->blockIds[BLOCKLIGHT_OFFSET + (off)])
#define SKYLIT(cd,off)              ((cd)->blockIds[SKYLIGHT_OFFSET + (off)])

/* tall grass, flowers, rails, ladder, vines, ... */
static void chunkGenQuad(ChunkData neighbors[], WriteBuffer buffer, BlockState b, int pos)
{
	DATA8   tex   = &b->nzU;
	DATA8   sides = &b->pxU;
	Chunk   chunk = neighbors[6]->chunk;
	int     vtx   = b->special == BLOCK_NOSIDE || b->pxU == QUAD_CROSS ? VERTEX_DATA_SIZE*2 : VERTEX_DATA_SIZE;
	int     seed  = neighbors[6]->Y ^ chunk->X ^ chunk->Z;
	uint8_t x, y, z, light;

	x =  LIGHT(neighbors[6], pos>>1);
	y = SKYLIT(neighbors[6], pos>>1);

	if (pos & 1) light = (y & 0xf0) | (x >> 4);
	else         light = (y << 4)   | (x & 15);

	x = (pos & 15);
	z = (pos >> 4) & 15;
	y = (pos >> 8);

	if (b->special == BLOCK_TALLFLOWER && (b->id&15) == 10)
	{
		uint8_t data;
		/* state 10 is used for top part of all tall flowers :-/ need to look at bottom part to know which top part to draw */
		if (y == 0)
			data = neighbors[5]->blockIds[DATA_OFFSET + ((pos + 256*15) >> 1)];
		else
			data = neighbors[6]->blockIds[DATA_OFFSET + ((pos - 256) >> 1)];
		if (pos & 1) data >>= 4;
		else         data &= 15;
		b += data & 7;
		tex = &b->nzU;
	}

	do {
		static uint8_t vtxIndices[] = {3,0,2,    0,3,1}; /* front vertex, back vertex */

		uint8_t side, norm, i, j;
		DATA8   indices;
		DATA32  out;

		if (BUF_LESS_THAN(buffer, vtx))
			buffer->flush(buffer);

		out   = buffer->cur;
		side  = *sides;
		norm  = quadSides[side];
		for (indices = vtxIndices, i = vtx; i > 0; indices += 3, out += VERTEX_INT_SIZE, i -= VERTEX_DATA_SIZE)
		{
			uint16_t U, V, X1, Y1, Z1;
			DATA8    coord;

			coord = vertex + quadIndices[side*4+indices[0]];

			/* first vertex */
			X1 = VERTEX(coord[0] + x);
			Y1 = VERTEX(coord[1] + y);
			Z1 = VERTEX(coord[2] + z);

			j = (b->rotate&3) * 8;
			U = (texCoord[j]   + tex[0]) << 4;
			V = (texCoord[j+1] + tex[1]) << 4;

			/* second and third vertex */
			coord  = vertex + quadIndices[side*4+indices[1]];
			out[0] = X1 | (Y1 << 16);
			out[1] = Z1 | (RELDX(coord[0] + x) << 16);
			out[2] = RELDY(coord[1] + y) | (RELDZ(coord[2] + z) << 14);
			coord  = vertex + quadIndices[side*4+indices[2]];
			out[3] = RELDX(coord[0] + x) | (RELDY(coord[1] + y) << 14) | ((V & 512) << 19);
			out[4] = RELDZ(coord[2] + z) | (U << 14) | (V << 23);

			/* tex size, norm and ocs: none */
			out[5] = (((texCoord[j+4] + tex[0]) * 16 + 128 - U) << 16) |
					 (((texCoord[j+5] + tex[1]) * 16 + 128 - V) << 24) | (norm << 8);

			if (texCoord[j] == texCoord[j + 6]) out[5] |= 1 << 11;
			if (side >= QUAD_ASCE)              out[5] |= 1 << 12;
			/* skylight/blocklight: uniform on all vertices */
			out[6] = light | (light << 8) | (light << 16) | (light << 24);

			if (side <= QUAD_CROSS2)
			{
				/* add some jitter to X,Z coord for QUAD_CROSS */
				uint8_t jitter = seed ^ (x ^ y ^ z);
				if (jitter & 1) out[0] += BASEVTX/16;
				if (jitter & 2) out[1] += BASEVTX/16;
				if (jitter & 4) out[0] -= (BASEVTX/16) << 16;
				if (jitter & 8) out[0] -= (BASEVTX/32) << 16;
			}
			else if (norm < 6)
			{
				/* offset 1/16 of a block in the direction of their normal */
				int8_t * normal = normals + (side >= QUAD_ASCE ? 16 : norm * 4);
				out[0] += normal[0] * (BASEVTX/16);
				out[0] += normal[1] * (BASEVTX/16) << 16;
				out[1] += normal[2] * (BASEVTX/16);
			}
		}
		sides ++;
		buffer->cur = out;
	} while (*sides);
}

static DATA8 chunkGetTileEntityFromOffset(Chunk c, int Y, int offset)
{
	int XYZ[3];
	XYZ[0] = offset & 15; offset >>= 4;
	XYZ[2] = offset & 15;
	XYZ[1] = Y + (offset >> 4);
	return chunkGetTileEntity(c, XYZ);
}

/* custom model mesh: anything that doesn't fit quad or full/half block */
static void chunkGenCust(ChunkData neighbors[], WriteBuffer buffer, BlockState b, int pos)
{
	static uint8_t connect6blocks[] = {
		/*B*/7, 5, 1, 3, 4,   /*M*/16, 14, 10, 12,   /*T*/25, 23, 19, 21, 22
	};

	uint8_t blockIds[14 * 2];
	Chunk   c = neighbors[6]->chunk;
	DATA32  out;
	DATA16  model;
	DATA8   blocks = neighbors[6]->blockIds, p;
	int     sides, side, count, connect;
	int     x, y, z;
	uint8_t Y, data, light;

	x = (pos & 15);
	z = (pos >> 4) & 15;
	y = (pos >> 8);
	Y = neighbors[6]->Y >> 4;

	light = blocks[SKYLIGHT_OFFSET   + (pos >> 1)];
	sides = blocks[BLOCKLIGHT_OFFSET + (pos >> 1)];
	if (pos & 1) light = (light & 0xf0) | (sides >> 4);
	else         light = ((light & 15) << 4) | (sides & 15);

	sides = xsides[x] | ysides[y] | zsides[z];
	data  = blocks[DATA_OFFSET + (pos >> 1)];
	model = b->custModel;
	count = connect = 0;

	switch (b->special) {
	case BLOCK_DOOR:
		/*
		 * bottom part data:
		 * - bit0: orient
		 * - bit1: orient
		 * - bit2: 1 if open
		 * - bit3: 0 = bottom part
		 * top part data:
		 * - bit0: hinge on right
		 * - bit1: powered
		 * - bit2: unused
		 * - bit3: 1 = top part
		 */
		side = 0;
		if (pos & 1) data >>= 4;
		else         data &= 15;
		if (data & 8) /* top part: get bottom part */
		{
			count = 8;
			side = data;
			if (sides & 32)
				data = blocks[(pos>>1) - 128 + DATA_OFFSET];
			else if (neighbors[5])
				data = neighbors[5]->blockIds[DATA_OFFSET + (pos>>1) + 15*128];
			if (pos & 1) data >>= 4;
			else         data &= 15;
		}
		else /* bottom part: get top part */
		{
			if (sides & 16)
				side = blocks[(pos>>1) + 128 + DATA_OFFSET];
			else if (neighbors[4])
				side = neighbors[4]->blockIds[DATA_OFFSET + ((pos>>1) & 127)];
			if (pos & 1) side >>= 4;
			else         side &= 15;
		}
		side = (data & 3) | ((side&1) << 2);
		b -= b->id & 15;
		if (data & 4) side = openDoorDataToModel[side];
		model = b[side+count].custModel;
		count = 0;
		break;
	case BLOCK_CHEST:
	case BLOCK_FENCE:
	case BLOCK_FENCE2:
		p = connect6blocks + 5;
		count = 4;
		break;
	case BLOCK_RSWIRE:
		/* redstone wire: only use base model */
		light = b->id & 15;
		b -= light;
		// no break;
	case BLOCK_GLASS:
		/* need: 14 surrounding blocks (S, E, N, W): 5 bottom, 4 middle, 5 top */
		p = connect6blocks;
		count = 14;
		break;
	case BLOCK_WALL:
		/* need: 4 surrounding blocks (S, E, W, N), 1 bottom (only for face culling), 1 top */
		p = connect6blocks + 4;
		count = 10;
		break;
	case BLOCK_BED:
		p = chunkGetTileEntityFromOffset(c, neighbors[6]->Y, pos);
		if (p)
		{
			struct NBTFile_t nbt = {.mem = p};
			connect = 1 << NBT_ToInt(&nbt, NBT_FindNode(&nbt, 0, "color"), 14);
		}
		/* default color: red */
		else connect = 1 << 14;
		break;
	case BLOCK_SIGN:
		c->signList = signAddToList(b->id, chunkGetTileEntityFromOffset(c, neighbors[6]->Y, pos), c->signList, light);
		break;
	default:
		/* piston head with a tile entity: head will be rendered as an entity if it is moving */
		if ((b->id >> 4) == RSPISTONHEAD)
		{
			if (chunkGetTileEntity(c, (int[3]){x, (Y << 4) + y, z}))
				return;
		}
	}

	if (model == NULL)
		return;

	if (count > 0)
	{
		DATA8 ids;
		for (ids = blockIds; count > 0; p ++, ids += 2, count --)
		{
			uint8_t ocs = occlusionSides[side = *p] & ~sides;
			int     off = pos;
			if (ocs > 0)
			{
				/* neighbor is in another chunk: bits of OCS will tell where: 1:S, 2:E, 4:N, 8:W, 16:T, 32:B */
				Chunk sub = c + chunkNeighbor[c->neighbor + (ocs&15)];
				int   lay = Y + subChunkOff[ocs];
				if (lay < 0 || lay >= CHUNK_LIMIT) continue;
				ChunkData cd = sub->layer[lay];
				if (! cd) continue;
				/* translate pos into new chunk */
				off += blockOffset[ocs] + blockOffset2[occlusionSides[side] & sides];
				/* block and metadata */
				ids[0] = cd->blockIds[off];
				ids[1] = cd->blockIds[DATA_OFFSET + (off >> 1)];
			}
			else
			{
				off += occlusionNeighbors[side];
				ids[0] = blocks[off];
				ids[1] = blocks[DATA_OFFSET + (off >> 1)];
			}
			if (off & 1) ids[1] >>= 4;
			else         ids[1] &= 15;
		}
		connect = blockGetConnect(b, blockIds);
	}

	x *= BASEVTX;
	y *= BASEVTX;
	z *= BASEVTX;

	/* vertex and light info still need to be adjusted */
	for (count = model[-1]; count > 0; count -= 6, model += 6 * INT_PER_VERTEX)
	{
		uint8_t faceId = (model[4] >> 8) & 31;
		if (faceId > 0 && (connect & (1 << (faceId-1))) == 0)
		{
			/* discard vertex */
			continue;
		}
		/* check if we can eliminate even more faces */
		uint8_t norm = GET_NORMAL(model);
		if (model[axisCheck[norm]] == axisAlign[norm])
		{
			extern int8_t opp[];
			struct BlockIter_t iter;
			int8_t * normal = normals + norm * 4;
			mapInitIterOffset(&iter, neighbors[6], pos);
			mapIter(&iter, normal[0], normal[1], normal[2]);

			if (blockIsSideHidden(getBlockId(&iter), model, opp[norm]))
				/* skip entire face (6 vertices) */
				continue;
		}

		if (BUF_LESS_THAN(buffer, VERTEX_DATA_SIZE))
			buffer->flush(buffer);

		out = buffer->cur;
		DATA16 coord = model + INT_PER_VERTEX * 3;
		uint16_t X1 = coord[0] + x;
		uint16_t Y1 = coord[1] + y;
		uint16_t Z1 = coord[2] + z;
		uint16_t U  = GET_UCOORD(model);
		uint16_t V  = GET_VCOORD(model);

		#define RELX(x)     ((x) + MIDVTX - X1)
		#define RELY(x)     ((x) + MIDVTX - Y1)
		#define RELZ(x)     ((x) + MIDVTX - Z1)

		coord  = model;
		out[0] = X1 | (Y1 << 16);
		out[1] = Z1 | (RELX(coord[0]+x) << 16);
		out[2] = RELY(coord[1]+y) | (RELZ(coord[2]+z) << 14);
		coord  = model + INT_PER_VERTEX * 2;
		out[3] = RELX(coord[0]+x) | (RELY(coord[1]+y) << 14) | ((V & 512) << 19);
		out[4] = RELZ(coord[2]+z) | (U << 14) | (V << 23);
		out[5] = ((GET_UCOORD(coord) + 128 - U) << 16) |
		         ((GET_VCOORD(coord) + 128 - V) << 24) | (GET_NORMAL(model) << 8);
		out[6] = light | (light << 8) | (light << 16) | (light << 24);
		coord  = model + INT_PER_VERTEX * 3;

		/* flip tex */
		if (U == GET_UCOORD(coord)) out[5] |= 1<<11;

		if (STATEFLAG(b, CNXTEX))
		{
			/* glass pane (stained and normal): relocate to simulate connected textures (only middle parts) */
			if (5 <= faceId && faceId <= 8)
			{
				int face = (faceId - 1) & 3;
				int flag = 15;

				/* remove top/bottom part */
				if ((connect & (1 << face)) > 0)     flag &= ~4; /* bottom */
				if ((connect & (1 << (face+8))) > 0) flag &= ~1; /* top */

				/* left/right side */
				if ((connect & (1 << (face+12))) && /* connected to same block */
				    (connect & 0x0f0) > 0)
				{
					flag &= ((GET_NORMAL(model)+1) & 3) == face ? ~2 : ~8;
				}

				out[4] += flag << 18;
			}
			else if (13 <= faceId && faceId <= 16)
			{
				/* center piece connected to another part of glass pane */
				if (connect & (1 << (faceId-9)))
					continue;

				int flag = 0;
				if ((connect & (1<<16)) == 0) flag |= 1;
				if ((connect & (1<<17)) == 0) flag |= 4;

				out[4] += flag << 18;
			}
		}
		buffer->cur = out + VERTEX_INT_SIZE;
	}
}

/* most common block within a chunk */
static void chunkGenCube(ChunkData neighbors[], WriteBuffer buffer, BlockState b, int pos)
{
	uint16_t blockIds3x3[27];
	uint8_t  skyBlock[27];
	DATA32   out;
	DATA8    tex;
	DATA8    blocks = neighbors[6]->blockIds;
	int      side, sides, occlusion, slab, rotate;
	int      i, j, k, n;
	uint8_t  x, y, z, Y, data;
	Chunk    c;

	x = (pos & 15);
	z = (pos >> 4) & 15;
	y = (pos >> 8);
	c = neighbors[6]->chunk;
	Y = neighbors[6]->Y >> 4;

	sides = xsides[x] | ysides[y] | zsides[z];

	/* outer loop: iterate over each faces (6) */
	for (i = 0, side = 1, occlusion = -1, tex = &b->nzU, rotate = b->rotate, j = (rotate&3) * 8, slab = 0; i < DIM(cubeIndices);
		 i += 4, side <<= 1, rotate >>= 2, tex += 2, j = (rotate&3) * 8)
	{
		BlockState t;
		n = pos;

		if (b->special != BLOCK_LEAVES)
		{
			/* check if neighbor is opaque: discard face if yes */
			if ((sides&side) == 0)
			{
				/* neighbor is not in the same chunk */
				ChunkData cd = neighbors[i>>2];
				if (cd == NULL)
					continue; /* edge of map */
				n += blockOffset[side];
				data = META(cd, n>>1);
				t = blockGetByIdData(cd->blockIds[n], n & 1 ? data >> 4 : data & 0xf);
			}
			else
			{
				n += offsets[i>>2];
				data = blocks[DATA_OFFSET + (n >> 1)];
				t = blockGetByIdData(blocks[n], n & 1 ? data >> 4 : data & 0xf);
			}

			/* face hidden by another opaque block: 75% of SOLID blocks will be culled by this test */
			switch (t->type) {
			case SOLID:
				switch (t->special) {
				case BLOCK_HALF:
				case BLOCK_STAIRS:
					if (oppositeMask[*halfBlockGetModel(t, 0, NULL)] & side) continue;
					break;
				default: continue;
				}
				break;
			case TRANS:
				if (b->type == TRANS) continue;
			}
		}

		/* ambient occlusion neighbor: look after 26 surrounding blocks (only 20 needed by AO) */
		if (occlusion == -1)
		{
			memset(skyBlock,    0, sizeof skyBlock);
			memset(blockIds3x3, 0, sizeof blockIds3x3);

			/* only compute that info if block is visible (highly likely it is not) */
			for (k = occlusion = 0; k < DIM(occlusionNeighbors); k ++)
			{
				BlockState t;
				uint8_t    ocs = occlusionSides[k] & ~sides;
				int        off = pos;
				int        id;

				if (ocs > 0)
				{
					/* neighbor is in another chunk: bits of OCS will tell where: 1:S, 2:E, 4:N, 8:W, 16:T, 32:B */
					Chunk sub = c + chunkNeighbor[c->neighbor + (ocs&15)];
					int   lay = Y + subChunkOff[ocs];
					if (lay < 0 || lay >= CHUNK_LIMIT) continue;
					ChunkData cd = sub->layer[lay];
					if (! cd) { skyBlock[k] = 15<<4; continue; }
					/* translate pos into new chunk */
					off += blockOffset[ocs] + blockOffset2[occlusionSides[k] & sides];
					/* extract block+sky light */
					data =  LIGHT(cd, off >> 1); skyBlock[k]  = off & 1 ? data >> 4 : data & 0xf;
					data = SKYLIT(cd, off >> 1); skyBlock[k] |= off & 1 ? data & 0xf0 : data << 4;
					/* block and metadata */
					data = META(cd, off >> 1);
					id   = cd->blockIds[off];
				}
				else
				{
					off += occlusionNeighbors[k];
					data = blocks[BLOCKLIGHT_OFFSET + (off >> 1)]; skyBlock[k]  = off & 1 ? data >> 4 : data & 0xf;
					data = blocks[SKYLIGHT_OFFSET   + (off >> 1)]; skyBlock[k] |= off & 1 ? data & 0xf0 : data << 4;
					data = blocks[DATA_OFFSET       + (off >> 1)];
					id   = blocks[off];
				}
				t = blockGetByIdData(id, off & 1 ? data >> 4 : data & 0xf);
				blockIds3x3[k] = t->id;
				if (t->type == SOLID || (t->type == CUST && t->special == BLOCK_SOLIDOUTER))
				{
					occlusion |= 1<<k;
					if (t->special == BLOCK_HALF)
						slab |= 1<<k;
				}
			}
			/* CUST with no model: don't apply ambient occlusion, like CUST model will */
			if (b->type == CUST && b->special != BLOCK_SOLIDOUTER)
				occlusion = slab = 0;
			if (STATEFLAG(b, CNXTEX))
			{
				static uint8_t texUV[12];
				DATA8 cnx, uv;
				int   id = b->id;
				memcpy(texUV, &b->nzU, 12);
				/* check for connected textures */
				for (k = 0, uv = texUV, cnx = offsetConnected; k < DIM(offsetConnected); k += 4, cnx += 4, uv += 2)
				{
					uint8_t flags = 0;
					if (blockIds3x3[cnx[0]] == id) flags |= 1;
					if (blockIds3x3[cnx[1]] == id) flags |= 2;
					if (blockIds3x3[cnx[2]] == id) flags |= 4;
					if (blockIds3x3[cnx[3]] == id) flags |= 8;
					uv[0] += flags;
				}
				tex = texUV + (tex - &b->nzU);
			}
		}

		if (b->special == BLOCK_HALF || b->special == BLOCK_STAIRS)
		{
			uint8_t pos[3] = {x<<1, y<<1, z<<1};
			halfBlockGenMesh(buffer, halfBlockGetModel(b, 2, blockIds3x3), 2, pos, &b->nzU, blockIds3x3, skyBlock);
			break;
		}

		if (BUF_LESS_THAN(buffer, VERTEX_DATA_SIZE))
			buffer->flush(buffer);

		/* generate one quad (see internals.html for format) */
		{
			DATA8    coord = vertex + cubeIndices[i+3];
			uint16_t texU  = (texCoord[j]   + tex[0]) << 4;
			uint16_t texV  = (texCoord[j+1] + tex[1]) << 4;
			uint16_t X1, Y1, Z1;

			X1 = VERTEX(coord[0]+x);
			Y1 = VERTEX(coord[1]+y);
			Z1 = VERTEX(coord[2]+z);
			out = buffer->cur;

			/* write one quad */
			coord  = vertex + cubeIndices[i];
			out[0] = X1 | (Y1 << 16);
			out[1] = Z1 | (RELDX(coord[0]+x) << 16);
			out[2] = RELDY(coord[1]+y) | (RELDZ(coord[2]+z) << 14);
			coord  = vertex + cubeIndices[i+2];
			out[3] = RELDX(coord[0]+x) | (RELDY(coord[1]+y) << 14) | ((texV & 512) << 19);
			out[4] = RELDZ(coord[2]+z) | (texU << 14) | (texV << 23);
			out[5] = (((texCoord[j+4] + tex[0]) * 16 + 128 - texU) << 16) |
			         (((texCoord[j+5] + tex[1]) * 16 + 128 - texV) << 24) | (i << 6);
			out[6] = 0;

			/* flip tex */
			if (texCoord[j] == texCoord[j + 6]) out[5] |= 1<<11;

			/* sky/block light values: 2*4bits per vertex = 4 bytes needed, ambient occlusion: 2bits per vertex = 1 byte needed */
			for (k = 0; k < 4; k ++)
			{
				uint8_t skyval, blockval, off, ocs;
				for (n = skyval = blockval = 0, off = (i+k) * 4; n < 4; off ++, n ++)
				{
					uint8_t skyvtx = skyBlock[skyBlockOffset[off]];
					uint8_t light  = skyvtx & 15;
					skyvtx &= 0xf0;
					/* max for block light */
					if (blockval < light) blockval = light;
					if (skyvtx > 0 && (skyval > skyvtx || skyval == 0)) skyval = skyvtx;
				}
				out[6] |= (skyval | blockval) << (k << 3);

				switch (popcount((occlusion & occlusionIfNeighbor[i+k]) | (slab & occlusionForSlab[i>>2]))) {
				case 2: ocs = 3; break;
				case 1: ocs = 1; break;
				default: ocs = occlusion & occlusionIfCorner[i+k] ? 1 : 0;
				}

				if (b->special == BLOCK_LIQUID && i == SIDE_TOP * 4)
				{
					/* reduce ambient occlusion a bit */
					static uint8_t lessAmbient[] = {0, 1, 1, 1};
					ocs = lessAmbient[ocs];
					/* reduce Y by 0.2 unit */
					out[0] -= (BASEVTX/32) << 16;
				}
				out[5] |= ocs << k*2;
			}
		}
		buffer->cur = out + VERTEX_INT_SIZE;
	}
}

static void chunkFreeHash(EntityHash hash, DATA8 min, DATA8 max)
{
	EntityEntry ent = (EntityEntry) (hash + 1);
	int i;

	for (i = hash->max; i > 0; i --, ent ++)
	{
		DATA8 mem = ent->data;
		if (mem && ! (min <= mem && mem < max))
			free(mem);
	}
	if (! STATIC_HASH(hash, min, max))
		free(hash);
}

int chunkFree(Chunk c)
{
	int i, max, ret;
	for (i = ret = 0, max = c->maxy; max > 0; max --, i ++)
	{
		ChunkData cd = c->layer[i];
		if (cd)
		{
			if (cd->glBank) renderFreeArray(cd), ret ++;
			if (cd->emitters) free(cd->emitters);
			free(cd);
		}
	}
	if (c->tileEntities)
	{
		chunkFreeHash((EntityHash) c->tileEntities, c->nbt.mem, c->nbt.mem + c->nbt.usage);
		c->tileEntities = NULL;
	}
	if (c->cflags & CFLAG_HASENTITY)
		entityUnload(c);
	NBT_Free(&c->nbt);
	memset(c->layer, 0, c->maxy * sizeof c->layer[0]);
	memset(&c->nbt, 0, sizeof c->nbt);
	c->cflags = 0;
	c->maxy = 0;
	return ret;
}
