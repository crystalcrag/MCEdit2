/*
 * pngwrite.h : simple PNG encoder based on zlib; original code taken from stb_image_write.h
 *              possibility to convert a RGB image into a dithered colormap to save some disk space.
 *
 * Written by T.Pierron, nov 2021.
 */


#ifdef PNGWRITE_IMPL
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <math.h>
#include <zlib.h>

typedef uint8_t *       DATA8;
typedef uint16_t *      DATA16;
typedef uint32_t *      DATA32;
typedef int8_t *        DATAS8;
typedef int16_t *       DATAS16;

#define STREAM_SIZE     4096
#define FLIP_VERTICALLY

#define STBIW_UCHAR(x)    ((x) & 0xff)

static uint8_t stbiw__paeth(int a, int b, int c)
{
   int p = a + b - c, pa = abs(p-a), pb = abs(p-b), pc = abs(p-c);
   if (pa <= pb && pa <= pc) return STBIW_UCHAR(a);
   if (pb <= pc) return STBIW_UCHAR(b);
   return STBIW_UCHAR(c);
}

static void stbiw__encode_png_line(unsigned char *pixels, int stride, int width, int height, int y, int bpp, int filter, DATAS8 line)
{
	static uint8_t mapping[] = { 0,1,2,3,4 };
	static uint8_t firstmap[] = { 0,1,0,5,6 };

	int i;
	int type = (y > 0 ? mapping : firstmap) [filter];

	#ifdef FLIP_VERTICALLY
	DATA8 src = pixels + stride * (height-1-y);
	stride = -stride;
	#else
	DATA8 src = pixels + stride * y;
	#endif

	if (type == 0)
	{
		/* no filtering: simple copy */
		memcpy(line, src, width * bpp);
		return;
	}

	/* first loop isn't optimized since it's just one pixel */
	for (i = 0; i < bpp; ++i)
	{
		switch (type) {
		case 1: line[i] = src[i]; break;
		case 2: line[i] = src[i] - src[i - stride]; break;
		case 3: line[i] = src[i] - (src[i - stride]>>1); break;
		case 4: line[i] = (signed char) (src[i] - stbiw__paeth(0, src[i-stride], 0)); break;
		case 5: line[i] = src[i]; break;
		case 6: line[i] = src[i]; break;
		}
	}
	width *= bpp;
	switch (type) {
	case 1: for (; i < width; ++i) line[i] = src[i] - src[i - bpp]; break;
	case 2: for (; i < width; ++i) line[i] = src[i] - src[i - stride]; break;
	case 3: for (; i < width; ++i) line[i] = src[i] - ((src[i - bpp] + src[i - stride])>>1); break;
	case 4: for (; i < width; ++i) line[i] = src[i] - stbiw__paeth(src[i-bpp], src[i-stride], src[i-stride-bpp]); break;
	case 5: for (; i < width; ++i) line[i] = src[i] - (src[i-bpp]>>1); break;
	case 6: for (; i < width; ++i) line[i] = src[i] - stbiw__paeth(src[i-bpp], 0, 0); break;
	}
}

#define ToBE32(dst, value) \
	(dst)[0] = (value) >> 24, \
	(dst)[1] = ((value) >> 16) & 0xff, \
	(dst)[2] = ((value) >>  8) & 0xff, \
	(dst)[3] = (value) & 0xff

int textureSavePNG(const char * path, DATA8 pixels, int stride, int width, int height, int bpp)
{
	static int8_t  ctype[5] = {0, 0, 4, 2, 6};
	static uint8_t header[] = {0x89, 'P','N','G','\r','\n',0x1a,0x0a};

	DATAS8 line;
	FILE * out;
	int    palette;
	int    j, filter;

	out = fopen(path, "wb+");
	if (out == NULL)
		return 0;

	palette = 0;
	if (bpp < 0)
		palette = -bpp*3, bpp = 1;

	if (stride == 0)
		stride = width * bpp;

	line = malloc(width * bpp + 1 + STREAM_SIZE);
	if (! line) {
		fclose(out);
		return 0;
	}

	/* check for best filtering to use for lines */
	int best_filter = 0;
	int best_filter_val = 0x7fffffff;
	int szline = width * bpp;
	for (filter = 0; filter < 5; filter ++)
	{
		stbiw__encode_png_line(pixels, stride, width, height, 0, bpp, filter, line);

		/* estimate the entropy of the line using this filter; the less, the better */
		int est, i;
		for (est = i = 0; i < szline; ++i)
			est += abs(line[i]);

		if (est < best_filter_val)
		{
			best_filter_val = est;
			best_filter = filter;
		}
	}

	filter = best_filter;
	fwrite(header, 1, sizeof header, out);

	/* each tag requires 12 bytes of overhead */
	uint8_t  IHDR[25];
	uint32_t crc;
	memset(IHDR, 0xff, sizeof IHDR);
	ToBE32(IHDR, 13);
	memcpy(IHDR+4, "IHDR", 4);
	ToBE32(IHDR+8, width);
	ToBE32(IHDR+12, height);
	IHDR[16] = 8;
	IHDR[17] = palette ? 3 : ctype[bpp];
	IHDR[18] = 0;
	IHDR[19] = 0;
	IHDR[20] = 0;
	crc = crc32(0, IHDR+4, 17);
	ToBE32(IHDR+21, crc);

	fwrite(IHDR, 1, sizeof IHDR, out);

	/* PLTE chunk if palette mode */
	if (palette > 0)
	{
		DATA8 cmap = pixels + stride * height;
		ToBE32(IHDR, palette);
		memcpy(IHDR+4, "PLTE", 4);
		fwrite(IHDR, 1, 8, out);
		crc = crc32(crc32(0, IHDR+4, 4), cmap, palette);
		fwrite(cmap, 1, palette, out);
		ToBE32(IHDR, crc);
		fwrite(IHDR, 1, 4, out);
	}

	/* single IDAT chunk: we will have to seek back to overwrite length field */
	palette = ftell(out);
	memset(IHDR+0, 0, 4);
	memcpy(IHDR+4, "IDAT", 4);
	fwrite(IHDR, 1, 8, out);

	DATA8 stream = line + szline + 1;
	z_stream zlib = {
		.next_out = stream,
		.avail_out = STREAM_SIZE,
		.next_in = line,
		.avail_in = szline + 1
	};

	if (deflateInit(&zlib, 9) == Z_OK)
	{
		for (j = 0, crc = crc32(0, IHDR+4, 4); j < height; )
		{
			line[0] = filter;
			stbiw__encode_png_line(pixels, stride, width, height, j, bpp, filter, line + 1);
			j ++;
			zlib.next_in = line;
			zlib.avail_in = szline + 1;
			for (;;)
			{
				if (deflate(&zlib, j == height ? Z_FINISH : Z_NO_FLUSH) == Z_STREAM_ERROR)
					break;

				if (zlib.avail_out < STREAM_SIZE)
				{
					crc = crc32(crc, stream, STREAM_SIZE - zlib.avail_out);
					fwrite(stream, STREAM_SIZE - zlib.avail_out, 1, out);
					int left = zlib.avail_out;
					zlib.next_out = stream;
					zlib.avail_out = STREAM_SIZE;
					if (left > 0) break;
				}
				else break;
			}
		}
		deflateEnd(&zlib);
	}
	free(line);

	ToBE32(IHDR, zlib.total_out);
	fseek(out, palette, SEEK_SET);
	fwrite(IHDR, 1, 4, out);
	fseek(out, 0, SEEK_END);

	ToBE32(IHDR, crc);
	memset(IHDR+4, 0, 4);
	memcpy(IHDR+8, "IEND", 4); crc = crc32(0, IHDR+8, 4);
	ToBE32(IHDR+12, crc);

	fwrite(IHDR, 1, 16, out);
	fclose(out);

	return 1;
}

/*
 * convert to colormap helper function
 *
 * only works for RGB image and nothing else (will overwrite <bitmap> with colormap at the end)
 *
 * return number of palette entries or 0 if something went wrong (in which case original pixels
 * won't be modified)
 */
int textureConvertToCMap(DATA8 bitmap, int width, int height)
{
	#ifdef DEBUG
	/* gdb you suck sometimes :-/ */
	DATA16   ccount = alloca((32+256)*2);
	DATA8    cmap   = alloca((32+256)*3);
	#else
	uint16_t ccount[32+256];
	uint8_t  cmap[(32+256)*3];
	#endif
	int      count, size, i, j, minDist;
	DATA8    p;

	for (minDist = 0; ; minDist += 3)
	{
		for (count = 0, p = bitmap, size = width * height; size > 0; size --, p += 3)
		{
			uint8_t r = p[0], g = p[1], b = p[2];

			/* check if color already exists in palette */
			DATA8 list, end;
			#define MIN_DIST(color)    (abs(color[0]-r) <= minDist && abs(color[1]-g) <= minDist && abs(color[2]-b) <= minDist)
			for (list = cmap, end = list + count*3, i = 0; list < end && ! MIN_DIST(list); list += 3, i ++);
			if (list == end)
			{
				if (count < 256+32)
				{
					list[0] = r;
					list[1] = g;
					list[2] = b;
					ccount[i] = 1;
					count ++;
				}
				else break;
			}
			else ccount[i] ++;
		}
		/* 12% loss is acceptable */
		if (count < 256+32) break;
	}

	/* original bitmap needs enough space */
	size = width * height;
	if (size * 3 < size + 3 * (count >= 256 ? 256 : count))
		/* that means you will save more space by saving an RGB image, instead of a colormap one */
		return 0;

	/* sort color by frequency: most frequent => lower index => better compression */
	DATA16 end;
	for (size = 1, end = ccount + 1, p = cmap + 3; size < count; size ++, end ++, p += 3)
	{
		if (end[-1] >= end[0]) continue;

		/* last element of array is not sorted: find its location using a binary search */
		uint16_t key = end[0];
		int      num = size;
		DATA16   pivot, base;

		for (base = ccount; num > 0; num >>= 1)
		{
			pivot = base + (num >> 1);
			if (pivot[0] > key)
				base = pivot + 1, num --;
		}

		uint8_t old[3] = {p[0], p[1], p[2]};
		num = base - ccount;
		memmove(base + 1, base, (DATA8) end - (DATA8) base); base[0] = key; num *= 3;
		memmove(cmap + num + 3, cmap + num, size * 3 - num);
		memcpy(cmap + num, old, 3);
	}

	/*
	 * apply a floyd-steinberg dithering with our current color map:
	 *      X  7
	 *   3  5  1
	 */
	DATA8 d;
	if (count > 256) count = 256;
	minDist *= minDist;
	size = width * 3;

	for (p = d = bitmap, j = height; j > 0; j --)
	{
		for (i = width; i > 0; i --, p += 3)
		{
			/* find nearest color in cmap */
			uint8_t best;
			int16_t r, g, b;
			DATA8   color;
			int     k, bestDist;

			r = p[0];
			g = p[1];
			b = p[2];

			for (best = 0, color = cmap, bestDist = 1e6, k = 0; k < count; k ++, color += 3)
			{
				int distr = (color[0] - r) * (color[0] - r);
				int distg = (color[1] - g) * (color[1] - g);
				int distb = (color[2] - b) * (color[2] - b);
				int dist  = distr + distg + distb;
				if (bestDist > dist)
				{
					best = k;
					//if (distr < minDist && distg < minDist && distb < minDist) break;
					bestDist = dist;
				}
			}

			/* diffuse error to nearby pixels */
			color = cmap + best * 3;
			/* perr == color of pixels without clamping */
			r -= color[0];
			g -= color[1];
			b -= color[2];

			int16_t tmp;
			#define CLAMP(x)    ((tmp = x) >= 255 ? 255 : (tmp <= 0 ? 0 : tmp))
			#define SPP         3
			if (i > 1)
			{
				p[SPP+0] = CLAMP(p[SPP+0] + (7 * r >> 4));
				p[SPP+1] = CLAMP(p[SPP+1] + (7 * g >> 4));
				p[SPP+2] = CLAMP(p[SPP+2] + (7 * b >> 4));
			}
			if (j > 1)
			{
				DATA8 d2 = p + size - SPP;
				if (i < width)
				{
					d2[0] = CLAMP(d2[0] + (3 * r >> 4));
					d2[1] = CLAMP(d2[1] + (3 * g >> 4));
					d2[2] = CLAMP(d2[2] + (3 * b >> 4));
				}
				d2 += SPP;
				d2[0] = CLAMP(d2[0] + (5 * r >> 4));
				d2[1] = CLAMP(d2[1] + (5 * g >> 4));
				d2[2] = CLAMP(d2[2] + (5 * b >> 4));

				d2 += SPP;
				if (i > 1)
				{
					d2[0] = CLAMP(d2[0] + (r >> 4));
					d2[1] = CLAMP(d2[1] + (g >> 4));
					d2[2] = CLAMP(d2[2] + (b >> 4));
				}
			}
			#undef SPP
			#undef CLAMP
			*d++ = best;
		}
	}
	/* write colormap at end in RGB format */
	memcpy(d, cmap, count * 3);

	return count;
}
#endif
