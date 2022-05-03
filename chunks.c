/*
 * chunks.c : manage loading and saving chunks. TileEntities hash is also managed here
 *            meshing is done in chunkMesh.c
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
#include "meshBanks.h"
#include "blocks.h"
#include "render.h"
#include "entities.h"
#include "tileticks.h"
#include "NBT2.h"


/*
 * reading chunk from disk
 */
static void chunkFillData(Chunk chunk, int y, int offset)
{
	ChunkData cd = calloc(sizeof *cd, 1);

	cd->blockIds = NBT_Payload(&chunk->nbt, NBT_FindNode(&chunk->nbt, offset, "Blocks"));
//	cd->addId    = NBT_Payload(&chunk->nbt, NBT_FindNode(&chunk->nbt, offset, "Add"));
	cd->chunk    = chunk;
	cd->Y        = y * 16;

	chunk->layer[y] = cd;

	#if 0
	/* if vertex data generation is FUBAR, activate this block to limit amount of data to turn into a mesh */
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
	ChunkData cd = NULL;
	if (y >= CHUNK_LIMIT)
		return NULL;

	/* column of sub-chunk must be fully filled with ChunkData from 0 to c->maxy-1 (mostly needed by frustum/cave culling) */
	int i;
	for (i = c->maxy; i <= y; i ++)
	{
		cd = calloc(1, sizeof *cd + MIN_SECTION_MEM);

		DATA8 base = (DATA8) (cd+1);

		base += NBT_FormatSection(base, i);

		cd->blockIds = base;
		cd->cdFlags  = CDFLAG_CHUNKAIR;
		cd->chunk    = c;
		cd->Y        = i * 16;
		c->layer[i]  = cd;

		//fprintf(stderr, "creating air chunk at %d, %d, layer %d\n", c->X, c->Z, cd->Y);

		memset(cd->blockIds + SKYLIGHT_OFFSET, 255, 2048);
	}
	/* we will have to do add it manually to the NBT structure */
	chunkMarkForUpdate(c, CHUNK_NBT_SECTION);
	c->maxy = i;

	return cd;
}


/*
 * tile entities hash table: all tile entities within a chunk are stored in a hash table where
 * keys == tile entity coord (in chunk local coordinates) and value = NBT stream.
 */

#define chunkHashSize(count)     ((count) * sizeof (struct TileEntityEntry_t) + sizeof (struct TileEntityHash_t))
#define EOF_MARKER               0xffff

/* keep entities in a hash table */
static DATA8 chunkInsertTileEntity(TileEntityHash hash, TileEntityEntry ent)
{
	TileEntityEntry old;
	TileEntityEntry free;
	TileEntityEntry dest = (TileEntityEntry) (hash + 1);

	for (old = NULL, free = dest + ent->xzy % hash->max; free->data && (free->xzy & TILE_COORD) != ent->xzy; )
	{
		old = free;
		if (free->next == EOF_MARKER)
		{
			TileEntityEntry eof = dest + hash->max;
			do {
				free ++;
				if (free == eof) free = dest;
			} while (free->data);
			break;
		}
		free = dest + free->next;
	}
	DATA8 prev = free->data;
	if (old)
	{
		ent->prev = old - dest;
		old->next = free - dest;
	}
	else ent->prev = EOF_MARKER;
	ent->next = EOF_MARKER;
	*free = *ent;
	return prev;
}

static TileEntityHash chunkCreateTileEntityHash(Chunk c, int count)
{
	TileEntityHash hash;
	int nb = roundToUpperPrime(count);
	int size = chunkHashSize(nb);

	if (size < c->nbt.max - c->nbt.usage)
	{
		/* alloc at end of NBT: use all available space */
		nb = roundToLowerPrime((c->nbt.max - c->nbt.usage - sizeof *hash) / sizeof (struct TileEntityEntry_t));
		hash = (TileEntityHash) (c->nbt.mem + c->nbt.usage);
		size = chunkHashSize(nb);
		memset(hash, 0, size);
	}
	else hash = calloc(size, 1);
	hash->max = nb;
	c->tileEntities = hash;
	return hash;
}

/* add a tile entity at given iterator "coordinates", will free()'ed the one that is already there, if any */
Bool chunkAddTileEntity(ChunkData cd, int offset, DATA8 mem)
{
	struct TileEntityEntry_t entry = {.data = mem};

	Chunk c = cd->chunk;
	TileEntityHash hash = c->tileEntities;

	if (hash == NULL)
		/* create on the fly */
		hash = chunkCreateTileEntityHash(c, 1);

	int count = hash->count + 1;

	/* encode pos in 3 bytes */
	entry.xzy = offset + (cd->Y << 8);

	if (count == hash->max)
	{
		/* table is full: need to be enlarged */
		TileEntityHash reloc;
		TileEntityEntry ent;
		int i;

		count = roundToUpperPrime(hash->count);
		reloc = calloc(chunkHashSize(count), 1);
		//fprintf(stderr, "enlarging hash table from %d to %d at %d, %d\n", hash->count, count, c->X, c->Z);
		if (! reloc) return False;
		c->tileEntities = reloc;

		reloc->max = count;
		reloc->count = i = count - 1;

		/* relocate entries */
		for (ent = (TileEntityEntry) (hash + 1); i > 0; i --, ent ++)
		{
			if (ent->data && chunkInsertTileEntity(reloc, ent))
				reloc->count --; /* hmm, duplicate tile entity in NBT: not good */
		}
		if (! STATIC_HASH(hash, c->nbt.mem, c->nbt.mem + c->nbt.usage))
			free(hash);
		hash = reloc;
	}
	mem = chunkInsertTileEntity(hash, &entry);
	if (mem > TILE_OBSERVED_DATA)
	{
		/* tile entity was replaced instead of added */
		if (! (c->nbt.mem <= mem && mem < c->nbt.mem + c->nbt.usage))
			free(mem);
	}
	else hash->count ++;

	return True;
}

/* update X, Y, Z field of tile NBT record */
void chunkUpdateTilePosition(ChunkData cd, int offset, DATA8 tile)
{
	NBTFile_t nbt = {.mem = tile};
	NBTIter_t iter;
	uint8_t   flags = 0;
	int       XYZ[] = {cd->chunk->X + (offset & 15), cd->Y + (offset >> 8), cd->chunk->Z + ((offset >> 4) & 15)};
	int       off;
	NBT_IterCompound(&iter, tile);
	while ((off = NBT_Iter(&iter)) >= 0 && flags != 7)
	{
		switch (FindInList("X,Y,Z", iter.name, 0)) {
		case 0: NBT_SetInt(&nbt, off, XYZ[VX]); flags |= 1; break;
		case 2: NBT_SetInt(&nbt, off, XYZ[VZ]); flags |= 2; break;
		case 1: NBT_SetInt(&nbt, off, XYZ[VY]); flags |= 4;
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

	if (off < 0) return;
	/* sometimes this node is saved as a TAG_List_End or TAG_List_Byte :-/ */
	c->cflags |= CFLAG_HAS_TE;
	hdr->type = TAG_List_Compound;
	if (hdr->count == 0) return;

	NBT_InitIter(&nbt, off, &iter);

	if (! c->tileEntities)
		chunkCreateTileEntityHash(c, hdr->count);

	while ((off = NBT_Iter(&iter)) > 0)
	{
		NBTIter_t sub;
		int       XYZ[3];
		int       i, flag = 0;;

		NBT_IterCompound(&sub, nbt.mem+off);

		while ((i = NBT_Iter(&sub)) >= 0)
		{
			if (sub.name[1] == 0)
			switch (sub.name[0]) {
			case 'X': case 'x': XYZ[0] = NBT_GetInt(&nbt, off+i, 0) - c->X; flag |= 1; break;
			case 'Z': case 'z': XYZ[2] = NBT_GetInt(&nbt, off+i, 0) - c->Z; flag |= 2; break;
			case 'Y': case 'y': XYZ[1] = NBT_GetInt(&nbt, off+i, 0); flag |= 4;
			}
		}
		if (flag == 7 && XYZ[1] < (c->maxy << 4) && (unsigned) XYZ[0] < 15 && (unsigned) XYZ[2] < 15)
		{
			chunkAddTileEntity(c->layer[XYZ[1] >> 4], XYZ[0] | (XYZ[2] << 4) | ((XYZ[1] & 15) << 8), nbt.mem + off);
		}
	}
}

void chunkExpandEntities(Chunk c)
{
	int off = NBT_FindNode(&c->nbt, 0, "Entities");
	c->cflags |= CFLAG_HASENTITY;
	if (off > 0)
	{
		c->cflags |= CFLAG_HAS_ENT;
		NBTHdr hdr = (NBTHdr) (c->nbt.mem + off);
		if (hdr->count == 0) return; /* empty list */
		NBTIter_t list;
		Entity    prev = NULL;
		NBT_InitIter(&c->nbt, off, &list);
		while ((off = NBT_Iter(&list)) >= 0)
			prev = entityParse(c, &c->nbt, off, prev);
	}
}

static TileEntityEntry chunkGetTileEntry(ChunkData cd, int offset)
{
	TileEntityHash  hash = cd->chunk->tileEntities; if (! hash) return NULL;
	TileEntityEntry base = (TileEntityEntry) (hash + 1);
	uint32_t        xzy  = offset + (cd->Y << 8);
	TileEntityEntry ent  = base + xzy % hash->max;

	if (ent->data == NULL) return NULL;
	while ((ent->xzy & TILE_COORD) != xzy)
	{
		if (ent->next == EOF_MARKER) return NULL;
		ent = base + ent->next;
	}
	return ent;
}

/* get tile entity data based on its iterator "coordinates" */
DATA8 chunkGetTileEntity(ChunkData cd, int offset)
{
	TileEntityEntry entry = chunkGetTileEntry(cd, offset);
	return entry && entry->data != TILE_OBSERVED_DATA ? entry->data : NULL;
}

/* tile data will be potentially realloc()'ed */
DATA8 chunkUpdateTileEntity(ChunkData cd, int offset)
{
	TileEntityEntry entry = chunkGetTileEntry(cd, offset);
	if (entry && entry->data != TILE_OBSERVED_DATA)
	{
		DATA8 tile = entry->data;
		/* will prevent free()'ing a realloc()'ed block */
		entry->data = TILE_OBSERVED_DATA;
		return tile;
	}
	return NULL;
}

/* tile entity has been deleted: remove ref from hash */
DATA8 chunkDeleteTileEntity(ChunkData cd, int offset, Bool extract, DATA8 observed)
{
	TileEntityHash  hash = cd->chunk->tileEntities; if (! hash) return NULL;
	TileEntityEntry base = (TileEntityEntry) (hash + 1);
	uint32_t        xzy  = offset + (cd->Y << 8);
	TileEntityEntry ent  = base + xzy % hash->max;
	DATA8           data = ent->data;

	if (data == NULL) return False;

	while ((ent->xzy & TILE_COORD) != xzy)
	{
		if (ent->next == EOF_MARKER) return False;
		ent = base + ent->next;
		data = ent->data;
	}
	if ((ent->xzy & ~TILE_COORD) == 0)
	{
		if (ent->prev != EOF_MARKER)
		{
			TileEntityEntry prev = base + ent->prev;
			prev->next = ent->next;
			if (ent->next != EOF_MARKER)
			{
				TileEntityEntry next = base + ent->next;
				next->prev = ent->prev;
			}
		}
		else if (ent->next != EOF_MARKER)
		{
			/* removing first link in the chain: need to move next item here */
			TileEntityEntry next = base + ent->next;
			memmove(ent, base, sizeof *ent);
			ent->prev = EOF_MARKER;
			ent = next;
		}
		hash->count --;
		ent->data = NULL;
	}
	else ent->data = TILE_OBSERVED_DATA;

	// fprintf(stderr, "deleting tile entity at %d, %d, %d\n", c->X + XYZ[0], XYZ[1], c->Z + XYZ[2]);
	/* mapUpdate() will need this information to check if there are observers who need to be notified */
	if (observed)
		observed[0] = ent->xzy >> (TILE_OBSERVED_OFFSET-4);

	if (data > TILE_OBSERVED_DATA)
	{
		Chunk c = cd->chunk;
		if (extract)
		{
			if (c->nbt.mem <= data && data < c->nbt.mem + c->nbt.usage)
				data = NBT_Copy(data);
		}
		else if (! (c->nbt.mem <= data && data < c->nbt.mem + c->nbt.usage))
			free(data);
		return data;
	}
	return NULL;
}

/* only free memory related to tile/entity */
void chunkDeleteTile(Chunk c, DATA8 tile)
{
	if (! (c->nbt.mem <= tile && tile < c->nbt.mem + c->nbt.usage))
		free(tile);
}

/* iterate over all tile entities defined in this chunk (*offset needs to be initially set to 0) */
DATA8 chunkIterTileEntity(Chunk c, int XYZ[3], int * offset)
{
	if (! c->tileEntities) return NULL;
	TileEntityHash  hash = c->tileEntities;
	TileEntityEntry base = (TileEntityEntry) (hash + 1);
	TileEntityEntry ent  = base + *offset;
	int i;

	for (i = *offset; i < hash->max; i ++, ent ++)
	{
		if (ent->data && ent->data != TILE_OBSERVED_DATA)
		{
			if (XYZ)
			{
				XYZ[VX] = c->X + (ent->xzy & 15);
				XYZ[VZ] = c->Z + ((ent->xzy >> 4) & 15);
				XYZ[VY] = (ent->xzy >> 8) & 0xffff;
			}
			*offset = i + 1;
			return ent->data;
		}
	}
	return NULL;
}

/*
 * cheap trick to handle observer: add a fake tile entity to the observed location
 * if there is already a tile entity, mark it as "observable".
 * whenever there is an update in this block, the mapUpdate() function will have to retrieve the
 * tile entity anyway, without having to scan 6 surrounding blocks for every block update.
 */
void chunkMakeObservable(ChunkData cd, int offset, int side)
{
	struct BlockIter_t iter;
	mapInitIterOffset(&iter, cd, offset);
	mapIter(&iter, relx[side], rely[side], relz[side]);
	TileEntityEntry entry = chunkGetTileEntry(iter.cd, iter.offset);
	if (entry == NULL)
	{
		chunkAddTileEntity(iter.cd, iter.offset, TILE_OBSERVED_DATA);
		entry = chunkGetTileEntry(iter.cd, iter.offset);
	}
	/* the block can be observed on multiple side */
	entry->xzy |= 1 << (opp[side] + TILE_OBSERVED_OFFSET);
}

void chunkUnobserve(ChunkData cd, int offset, int side)
{
	struct BlockIter_t iter;
	mapInitIterOffset(&iter, cd, offset);
	mapIter(&iter, relx[side], rely[side], relz[side]);
	TileEntityEntry entry = chunkGetTileEntry(iter.cd, iter.offset);

	if (entry)
	{
		entry->xzy &= ~(1 << (opp[side] + TILE_OBSERVED_OFFSET));
		if ((entry->xzy & ~TILE_COORD) == 0 && entry->data == TILE_OBSERVED_DATA)
			chunkDeleteTileEntity(iter.cd, iter.offset, False, NULL);
	}
}

/*
 * chunk loading/saving
 */

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
			/* these fields will be repurposed (they are not needed anymore) */
			nbt.alloc = nbt.page = 0;
			chunk->signList       = -1;
			chunk->nbt            = nbt;
			chunk->heightMap      = NBT_Payload(&nbt, NBT_FindNode(&nbt, 0, "HeightMap"));
			chunk->entityList     = ENTITY_END;
			//chunk->lightPopulated = NBT_GetInt(&nbt, NBT_FindNode(&nbt, 0, "LightPopulated"), 0);
			//chunk->terrainDeco    = NBT_GetInt(&nbt, NBT_FindNode(&nbt, 0, "TerrainPopulated"), 0);
			//chunk->biomeMap       = NBT_Payload(&nbt, NBT_FindNode(&nbt, 0, "Biomes"));

			int secOffset = NBT_FindNode(&nbt, 0, "Sections");
			if (secOffset > 0)
			{
				NBTIter_t iter;
				NBT_InitIter(&nbt, secOffset, &iter);
				chunk->cflags |= CFLAG_HAS_SEC;
				while ((secOffset = NBT_Iter(&iter)) >= 0)
				{
					int y = NBT_GetInt(&nbt, NBT_FindNode(&nbt, secOffset, "Y"), 0);
					if (y < CHUNK_LIMIT && chunk->layer[y] == NULL)
						chunkFillData(chunk, y, secOffset);
				}
			}

			chunkExpandTileEntities(chunk);

			return True;
		}
		else fclose(in);
	}

	return False;
}

/* will have to do some post-processing when saving this chunk */
void chunkMarkForUpdate(Chunk c, int type)
{
	int done = type << 6;
	if ((c->cflags & done) == 0)
	{
		STRPTR key;
		switch (type) {
		default:
		/* these keys must be children of "Level" */
		case CHUNK_NBT_SECTION:      key = "/Sections"; break;
		case CHUNK_NBT_TILEENTITIES: key = "/TileEntities"; break;
		case CHUNK_NBT_ENTITIES:     key = "/Entities"; break;
		case CHUNK_NBT_TILETICKS:    key = "/TileTicks"; break;
		}
		int level = NBT_FindNode(&c->nbt, 0, "Level");
		int tile  = NBT_FindNode(&c->nbt, level, key);
		NBT_MarkForUpdate(&c->nbt, tile < 0 ? level : tile, type);
		c->cflags |= done;
	}
}

/* insert <nbt> fragment at the location of tile entity pointed by <blockOffset> (ie: coord of tile entity within chunk) */
Bool chunkUpdateNBT(ChunkData cd, int offset, NBTFile nbt)
{
	NBT_SetHdrSize(nbt, 0);

	Chunk c = cd->chunk;
	DATA8 tile = chunkGetTileEntity(cd, offset);

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
	chunkMarkForUpdate(c, CHUNK_NBT_TILEENTITIES);

	/* add or update pointer in hash */
	return chunkAddTileEntity(cd, offset, nbt->mem);
}

/*
 * saving chunk to disk
 */

/* find a place inside a region (.mca) file to allocate the number of requested 4Kb pages */
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

static void chunkAddNBTEntry(NBTFile nbt, STRPTR name, int tag)
{
	static uint8_t content[64];

	nbt->mem = content;
	nbt->max = sizeof content;
	nbt->usage = 0;

	NBT_Add(nbt,
		TAG_List_Compound, name, 0,
		TAG_Compound_End
	);
	NBT_MarkForUpdate(nbt, 0, tag);
}

Bool entityGetNBT(NBTFile, int * id);

typedef struct SaveParam_t *     SaveParam;
struct SaveParam_t
{
	Chunk chunk;
	int   flags;
};

/* save all the extra NBT tags we added to a chunk */
static int chunkSaveExtra(int tag, APTR cbparam, NBTFile nbt)
{
	ChunkData cd;
	TileEntityHash hash;
	TileEntityEntry ent;
	SaveParam save = cbparam;
	Chunk chunk = save->chunk;
	int i;

	switch (tag) {
	case CHUNK_NBT_TILEENTITIES:
		/* list of tile entities modified */
		hash = chunk->tileEntities;
		if (nbt == NULL)
		{
			/* return total count of tile entities */
			int count;
			/* dummy tile entities for observed blocks must not be saved in NBT */
			for (i = hash->max-1, count = 0, ent = (TileEntityEntry) (hash + 1); i >= 0; i --, ent ++)
				if (ent->data > TILE_OBSERVED_DATA) count ++;
			return count;
		}

		if ((save->flags & CHUNK_NBT_TILEENTITIES) == 0)
		{
			save->flags |= CHUNK_NBT_TILEENTITIES;
			chunk->cdIndex = 1;
			/* check for missing TileEntities entries within NBT */
			if ((chunk->cflags & CFLAG_HAS_TE) == 0)
			{
				/* MCEditv1 doesn't like when this is missing, even if it is empty :-/ */
				chunkAddNBTEntry(nbt, "TileEntities", CHUNK_NBT_TILEENTITIES);
				return -1;
			}
		}

		/* this will certainly change the order of tile entities each time it is saved (not that big of a deal though) */
		for (i = chunk->cdIndex - 1, ent = (TileEntityEntry) (hash + 1) + i; i < hash->max; i ++, ent ++)
		{
			if (ent->data <= TILE_OBSERVED_DATA) continue;
			NBTIter_t iter;
			NBT_IterCompound(&iter, ent->data);
			while (NBT_Iter(&iter) >= 0);
			nbt->mem = ent->data;
			nbt->usage = iter.offset-4;
			chunk->cdIndex = i + 2;
			return 1;
		}
		break;

	case CHUNK_NBT_ENTITIES:
		#define curEntity    nbt.alloc
		/* list of entities modified */
		if (nbt == NULL)
		{
			/* total count of entities for this chunk */
			return entityCount(chunk->entityList);
		}
		if ((save->flags & CHUNK_NBT_ENTITIES) == 0)
		{
			save->flags |= CHUNK_NBT_ENTITIES;
			chunk->curEntity = chunk->entityList;
			if ((chunk->cflags & CFLAG_HAS_ENT) == 0)
			{
				/* missing "Entities" TAG_List_Compound entry in NBT */
				chunkAddNBTEntry(nbt, "Entities", CHUNK_NBT_ENTITIES);
				return -1;
			}
		}
		if (entityGetNBT(nbt, &chunk->curEntity))
			return 1;
		#undef curEntity
		break;

	case CHUNK_NBT_TILETICKS:
		if (nbt == NULL)
		{
			return updateCount(chunk);
		}
		if ((save->flags & CHUNK_NBT_TILETICKS) == 0)
		{
			chunk->cdIndex = 0;
			save->flags |= CHUNK_NBT_TILETICKS;
			if ((chunk->cflags & CFLAG_HAS_TT) == 0)
			{
				/* highly likely */
				if (updateCount(chunk) == 0) return 0;
				/* missing "TileTicks" TAG_List_Compound entry in NBT */
				chunkAddNBTEntry(nbt, "TileTicks", CHUNK_NBT_ENTITIES);
				return -1;
			}
		}
		if (updateGetNBT(chunk, nbt, &chunk->cdIndex))
			return 1;
		break;

	case CHUNK_NBT_SECTION:
		/* has to return TAG_List count */
		if (nbt == NULL)
			return chunk->maxy;
		if ((save->flags & CHUNK_NBT_SECTION) == 0)
		{
			save->flags |= CHUNK_NBT_SECTION;
			chunk->cdIndex = 0;
			if ((chunk->cflags & CFLAG_HAS_SEC) == 0)
			{
				chunkAddNBTEntry(nbt, "Sections", CHUNK_NBT_SECTION);
				return -1;
			}
		}

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

/* write chunk into region file */
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
			//NBT_Dump(&chunk->nbt, 0, 0, 0);
			struct SaveParam_t param = {.chunk = chunk, .flags = 0};

			/* compress file in memory first, then write it to disk */
			chunkOffset = BE24(offset) << 12;
			zstream     = NBT_Compress(&chunk->nbt, &chunkSize, offset[3], chunkSaveExtra, &param);
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
					uint32_t secTime = time(NULL);
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
						fwrite(&secTime, 1, 4, io);

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


void chunkFreeHash(TileEntityHash hash, DATA8 min, DATA8 max)
{
	TileEntityEntry ent = (TileEntityEntry) (hash + 1);
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

int chunkFree(Chunk c, Bool clear)
{
	int i, max, ret;
	for (i = ret = 0, max = c->maxy; max > 0; max --, i ++)
	{
		ChunkData cd = c->layer[i];
		if (cd)
		{
			if (cd->glBank && clear) meshFreeGPU(cd), ret ++;
			if (cd->emitters) free(cd->emitters);
			free(cd);
		}
	}
	if (c->tileEntities)
	{
		chunkFreeHash((TileEntityHash) c->tileEntities, c->nbt.mem, c->nbt.mem + c->nbt.max);
		c->tileEntities = NULL;
	}
	if (clear)
	{
		if (c->cflags & CFLAG_HASENTITY)
			entityUnload(c);
		memset(c->layer, 0, c->maxy * sizeof c->layer[0]);
		memset(&c->nbt, 0, sizeof c->nbt);
		c->cflags = 0;
		c->maxy = 0;
	}
	NBT_Free(&c->nbt);
	return ret;
}
