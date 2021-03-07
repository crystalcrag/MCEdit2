/*
 * ViewImage.c : little widget based on a canvas to display an image of arbitrary size
 * and zoom into or out of it. Key points:
 * - Fast transition using only CPU operations.
 * - High quality anti-aliassing using linear resize.
 *
 * Written by T.Pierron, Apr 2011, with ideas from Sean Barrett (http://nothings.org/)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <math.h>
#include "SIT.h"
#include "graphics.h"
#include "ViewImage.h"


/* Smoother transitions */
static int factors[] = {100, 200, 300, 400, 800, 1100, 1600, 2300, 3200};
static uint8_t mask[] = {0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01};

/* Better to remain private */
typedef struct ViewImage_t *     ViewImage;
typedef uint16_t *               DATA16;
typedef uint32_t *               DATA32;

struct ViewImage_t
{
	Image   original;    /* user-provided image (it is not a copy) */
	Image   curimg;      /* back buffer when fact < 1 */
	APTR    offgc, canvas, ud;
	Image   minimap;     /* thumb if image is bigger than viewport */
	Rect    src;         /* part to be extracted from 'bitmap' */
	Rect    dst;         /* extracted part will be of given size in the vp */
	Rect    zoom;        /* size and position of the image at current zoom level */
	Rect    marquee;     /* coord in screen space */
	int     cursor[4];   /* guides within minimap */
	Bool    hasminimap;  /* user want to use minimap */
	Bool    dispmini;    /* relevant to display it */
	Bool    dozoom;
	Bool    marqueeSel;
	int     marqueeHover;
	ULONG   marqueeCol;
	int     width,  height;
	int     mouseX, mouseY;
	int     offsetX, offsetY;
	int16_t offw, offh;  /* size of offscreen GC */
	double  fact;        /* current zoom level 0 < fact <= 32 */
	int8_t  zoomIdx;     /* integral zoom level if fact >= 1 */
	uint8_t margin;      /* minimum amount of px that must be visible from image */
	uint8_t waitconf;    /* SITE_OnPaint/SITE_OnResize */
	uint8_t setflags;    /* SITE_OnSetOrGet */
	int     magnetX, magnetY;
	SIT_CallProc cb;     /* user-provided paint callback for overlay */
};


#define	BITS         8
#define	VALUES       (1 << BITS)
#define	BG_COLOR     RGB(0x88, 0x88, 0x88)
#define	TILESHFT     7
#define	TILE         (1<<TILESHFT)
#define	TILEMASK     (TILE-1)
#define	TILESTRD(w)  ((((w + TILEMASK) >> TILESHFT) + 7) >> 3)
#define	TILESZ(i)    (TILESTRD(i->width)*((i->height+TILEMASK)>>TILESHFT))
#define VIT_UserData (SIT_TagUser + 0x10)

struct Iter_t
{
	int x, y, xe, ye;
	int dx, dy, err, sx, sy, oldy;
};

typedef struct Iter_t          Iter;

static void InitDDA(Iter * iter, int xs, int xe, int ys, int ye)
{
	/* Pre-condition: xe > xs >= 0 */
	div_t q = div(iter->dy = ye - ys, xe);
	iter->y   = ys;
	iter->ye  = ye;
	iter->x   = xs;
	iter->xe  = xe;
	iter->err = xe;
	iter->dx  = abs(q.rem);
	iter->sx  = q.quot;
	iter->sy  = (ys < ye ? 1 : -1);
	if (xs > 0)
	{
		q = div(xs * iter->dy + (xe >> 1), xe);
		iter->y   = ys + q.quot;
		iter->err = xe - q.rem;
	}
}

static inline void IterDDA(Iter * iter)
{
	iter->x ++;
	iter->y += iter->sx;
	iter->err -= iter->dx;
	if (iter->err <= 0)
		iter->y += iter->sy, iter->err += iter->xe;
}

Image ViewImageReduce(Image source, Image ret, Rect * from, Rect * to)
{
	struct Image_t temp;
	Image src;

	if (from == NULL)
	{
		from = alloca(sizeof *from*2); to = from + 1;
		from->x = from->y = to->x = to->y = 0;
		from->width = source->width;
		from->height = source->height;
		to->width = ret->width;
		to->height = ret->height;
	}

	ret->encoder = source->encoder; /* Keep GFX_Premultiplied flag */
	src = source;
	temp = *source;
	temp.bitmap = NULL;
	int wd = to->width;
	int hd = to->height;

//	fprintf(stderr, "=== from: %dx%d - %dx%d\n", from->x, from->y, from->width, from->height);

	/* First: resize image using a fast method */
	while (wd <= (from->width >> 1) && hd <= (from->height >> 1))
	{
		int i, j, stride = src->stride;
		temp.width  = from->width  >> 1;
		temp.height = from->height >> 1;
		temp.stride = (temp.width * (temp.bpp>>3) + 3) & ~3;

//		fprintf(stderr, "    temp: %dx%d - %d\n", temp.width, temp.height, temp.stride);

		if (temp.bitmap == NULL)
			temp.bitmap = calloc(temp.stride, temp.height);

		if (src->bpp == 24)
		{
			DATA8 src0, src1, dest;

			for (j = 0; j < temp.height; j ++)
			{
				src0 = src->bitmap + (j*2 + from->y) * stride + from->x * 3;
				src1 = src0 + stride;
				dest = temp.bitmap + j * temp.stride;

				for (i = temp.width; i > 0; i --, dest += 3)
				{
					dest[0] = (src0[0] + src0[3] + src1[0] + src1[3]) >> 2;
					dest[1] = (src0[1] + src0[4] + src1[1] + src1[4]) >> 2;
					dest[2] = (src0[2] + src0[5] + src1[2] + src1[5]) >> 2;
					src0 += 6;
					src1 += 6;
				}
			}
		}
		else
		{
			DATA32 src0, src1, dest = (DATA32) temp.bitmap;

			for (j = 0; j < temp.height; j ++)
			{
				src0 = (DATA32) (src->bitmap + (j*2 + from->y) * stride + from->x * (src->bpp>>3));
				src1 = (DATA32) ((DATA8)src0 + stride);
				for (i = temp.stride; i > 0; i -= 4, dest ++)
				{
					*dest = ((src0[0] >> 2) & 0x3f3f3f3f) +
							((src0[1] >> 2) & 0x3f3f3f3f) +
							((src1[0] >> 2) & 0x3f3f3f3f) +
							((src1[1] >> 2) & 0x3f3f3f3f);
					src0 += 2;
					src1 += 2;
				}
			}
		}
		from->x = from->y = 0;
		from->width = temp.width;
		from->height = temp.height;
		src = &temp;
	}
	/* Now we have to resize the image using a factor between 1 and 2 (not including) */
	if (wd < temp.width || hd < temp.height)
	{
		DATA32 sum;
		int    i, y, sz, surf, xerr, chan = src->bpp >> 3, hs, ws;

		DATA8 out = ret->bitmap + to->y * ret->stride + to->x * chan;
		DATA8 in  = src->bitmap + from->y * src->stride + from->x * chan;
		Iter  ypos, xpos, nerr;

		ws   = from->width;
		hs   = from->height;
		sz   = ws * chan;
		sum  = calloc(sz, sizeof *sum);
		surf = (unsigned long long) ws * hs * VALUES / (wd * hd);
		y    = 0;

		memset(&nerr, 0, sizeof nerr); nerr.err = 1;
		InitDDA(&ypos, 0, hd, 0, hs);
		InitDDA(&xpos, 0, wd, 0, ws); xerr = xpos.dx > 0;

		while (ypos.x < ypos.xe)
		{
			DATA8 d = out, p;
			int x, yerr;
			int total[4], tmp;
			DATA32 s;
			IterDDA(&ypos);
			yerr = (ypos.xe - ypos.err) * VALUES / ypos.xe;

			while (y < ypos.y)
			{
				for (p = in, i = sz, s = sum; i > 0; *s++ += (*p<<BITS), p++, i --);
				y ++; in += src->stride;
			}

			InitDDA(&xpos, 0, wd, 0, ws); x = 0;             IterDDA(&xpos);
			InitDDA(&nerr, 0, xpos.xe, 0, VALUES * xpos.dx); IterDDA(&nerr);
			memset(total, 0, sizeof total);
			if (yerr > 0)
			{
				#define MAX_255(ptr, val) { int z = val; *ptr = (z >= 255 ? 255 : z); ptr ++; }
				for (p = in, i = wd, s = sum; i > 0; )
				{
					if (x < xpos.y) {
						/* Vertical error compensation */
						switch (chan) {
						case 4: tmp = *p * yerr; total[3] += tmp + *s; *s = (*p<<BITS) - tmp; s ++; p ++;
						case 3: tmp = *p * yerr; total[2] += tmp + *s; *s = (*p<<BITS) - tmp; s ++; p ++;
								tmp = *p * yerr; total[1] += tmp + *s; *s = (*p<<BITS) - tmp; s ++; p ++;
						case 1: tmp = *p * yerr; total[0] += tmp + *s; *s = (*p<<BITS) - tmp; s ++; p ++;
						}
						x ++;
					} else {
						int err = nerr.y & (VALUES-1);
						if (xerr == 0 || err == 0) {
							switch (chan) {
							case 4: MAX_255(d, (total[3] + (surf>>1)) / surf);
							case 3: MAX_255(d, (total[2] + (surf>>1)) / surf);
									MAX_255(d, (total[1] + (surf>>1)) / surf);
							case 1: MAX_255(d, (total[0] + (surf>>1)) / surf);
							}
							memset(total, 0, sizeof total);
						} else {
							int k;
							/* Vertical and horizontal error compensation */
							for (k = chan-1; k >= 0; k --, p ++, s++) {
								tmp = *p * yerr;
								int tmp2 = tmp * err >> BITS;
								int right = *s * err >> BITS;
								MAX_255(d, (total[k] + tmp2 + (surf>>1) + right) / surf);
								total[k] = *s - right + tmp - tmp2;
								*s = (*p<<BITS) - tmp;
							}
							x++;
						}
						IterDDA(&nerr);
						IterDDA(&xpos); i --;
					}
				}
				y ++; in += src->stride;
			}
			else /* No vertical error (maybe horizontal) */
			{
				for (i = wd, s = sum; i > 0; ) {
					if (x < xpos.y) {
						/* No error compensation */
						switch (chan) {
						case 4: total[3] += *s; s ++;
						case 3: total[2] += *s; s ++;
								total[1] += *s; s ++;
						case 1: total[0] += *s; s ++;
						}
						x ++;
					} else {
						int err = nerr.y & (VALUES-1);
						if (xerr == 0 || err == 0) {
							switch (chan) {
							case 4: MAX_255(d, (total[3] + (surf>>1)) / surf);
							case 3: MAX_255(d, (total[2] + (surf>>1)) / surf);
									MAX_255(d, (total[1] + (surf>>1)) / surf);
							case 1: MAX_255(d, (total[0] + (surf>>1)) / surf);
							}
						} else {
							/* Horizontal error compensation */
							switch (chan) {
							case 4: tmp = *s * err >> BITS; MAX_255(d, (total[3] + tmp + (surf>>1)) / surf); *s -= tmp; s++;
							case 3: tmp = *s * err >> BITS; MAX_255(d, (total[2] + tmp + (surf>>1)) / surf); *s -= tmp; s++;
									tmp = *s * err >> BITS; MAX_255(d, (total[1] + tmp + (surf>>1)) / surf); *s -= tmp; s++;
							case 1: tmp = *s * err >> BITS; MAX_255(d, (total[0] + tmp + (surf>>1)) / surf); *s -= tmp; s++;
							}
							s -= chan;
						}
						IterDDA(&nerr);
						IterDDA(&xpos); i --;
						memset(total, 0, sizeof total);
					}
				}
				#undef MAX_255
				memset(sum, 0, sz<<2);
			}
			out += ret->stride;
		}
		free(sum);
	}
	else /* Factor = 1 */
	{
		DATA8 s, d;
		int   chan = ret->bpp>>3;
		for (d = ret->bitmap + to->y * ret->stride + to->x * chan,
		     s = src->bitmap + from->y * src->stride + from->x * chan, chan *= to->width; to->height > 0;
		     d += ret->stride, s += src->stride, to->height --)
			memcpy(d, s, chan);
	}
	if (temp.bitmap) free(temp.bitmap);

	return ret;
}

static void SET_DST(ViewImage v)
{
	v->dst.x = MAX(v->zoom.x, 0);
	v->dst.y = MAX(v->zoom.y, 0);
	v->dst.width  = MIN(v->width,  v->zoom.width);
	v->dst.height = MIN(v->height, v->zoom.height);
	int x = v->zoom.x + v->zoom.width;  if (x > v->width)  x = v->width;
	int y = v->zoom.y + v->zoom.height; if (y > v->height) y = v->height;
	if (v->dst.x + v->dst.width  > x) v->dst.width  = x-v->dst.x;
	if (v->dst.y + v->dst.height > y) v->dst.height = y-v->dst.y;
	if (v->fact > 1)
	{
		/* round dst.width and dst.height to an integer number of pixels (from source image) */
		v->dst.width  = round(ceil(v->dst.width  / v->fact) * v->fact);
		v->dst.height = round(ceil(v->dst.height / v->fact) * v->fact);
	}
}

static void SET_BITMAP(Image i, Rect * r, DATA8 tiles, int rop)
{
	int   x1, x2, y1, y2, sz, x;
	DATA8 line = alloca(sz = TILESTRD(i->width));

	memset(line, 0, sz);
	for (x1 = x = r->x >> TILESHFT, x2 = (r->width + TILEMASK) >> TILESHFT; x < x2; x ++)
		line[x>>3] |= mask[x&7];

	y1 = r->y >> TILESHFT, x1 >>= 3, x2 = (x2 + 7) >> 3, tiles += y1*sz;
	for (y2 = (r->height + TILEMASK) >> TILESHFT; y1 < y2; y1 ++, tiles += sz)
		for (x = x1; x < x2; x ++)
			if (rop) tiles[x] &= ~line[x]; else tiles[x] |= line[x];
}

static Bool IterTile(Image i, Rect * from, Rect * sub, DATA8 tiles)
{
	Rect r = {.x = from->x >> TILESHFT, .width  = from->width  >> TILESHFT,
	          .y = from->y >> TILESHFT, .height = from->height >> TILESHFT};

	DATA8 line;
	int   x, x2, y2, sz;

	for (sz = TILESTRD(i->width), tiles += r.y * sz; r.y < r.height; r.y ++, tiles += sz)
		for (x = r.x; x < r.width; x ++)
			if ((tiles[x>>3] & mask[x&7]) == 0) goto break_all;

	return False; /* all tiles processed */
	break_all:
	memset(line = alloca(sz), 0, sz); x2 = x;
	/* horizontal expand */
	do { line[x2>>3] |= mask[x2&7]; x2 ++; } while(x2 < r.width && (tiles[x2>>3] & mask[x2&7]) == 0);
	/* vertical expand */
	for (y2 = r.y+1, r.x = x; y2 < r.height; y2 ++) {
		for (tiles += sz, x = 0; x < sz && (tiles[x] & line[x]) == 0; x ++);
		if (x < sz) break;
	}
	sub->x = r.x << TILESHFT; sub->width  = sub->x + ((x2 - r.x) << TILESHFT);
	sub->y = r.y << TILESHFT; sub->height = sub->y + ((y2 - r.y) << TILESHFT);
	return True;
}

static void RENDER_TILE(ViewImage v, Image i, Rect * from)
{
	Rect to;
	if (from->width  > i->width)  from->width  = i->width;
	if (from->height > i->height) from->height = i->height;

	to.x = (from->x * v->zoom.width  + (i->width>>1))  / i->width;
	to.y = (from->y * v->zoom.height + (i->height>>1)) / i->height;

	to.width  = (from->width  * v->zoom.width  + (i->width>>1))  / i->width  - to.x;
	to.height = (from->height * v->zoom.height + (i->height>>1)) / i->height - to.y;
	SET_BITMAP(i, from, (DATA8) (v->curimg+1), 0);
	from->width  -= from->x;
	from->height -= from->y;

	ViewImageReduce(v->original, v->curimg, from, &to);
	memcpy(from, &to, sizeof to);
}

static void SET_LAYER(ViewImage v)
{
	if (v->curimg) GFX_FreeImage(v->curimg), v->curimg = NULL;
	if (v->fact < 1)
	{
		Rect from;
		Image i = v->original;

		/* lazy rendering: only resize part that is visible (rounded to nearest tile size) */
		from.x = (v->src.x & ~TILEMASK);
		from.y = (v->src.y & ~TILEMASK);
		from.width  = (v->src.x + v->src.width  + TILE - 1) & ~TILEMASK;
		from.height = (v->src.y + v->src.height + TILE - 1) & ~TILEMASK;

		v->curimg = GFX_CreateImageEx(v->zoom.width, v->zoom.height, v->original->bpp, TILESZ(i));
		memset(v->curimg+1, 0, TILESZ(i));
		RENDER_TILE(v, i, &from);
	}
}

static void ViewImageAdjustZoomIdx(ViewImage v)
{
	v->zoomIdx = 0;
	int diff = v->fact * 100, i;
	if (v->fact > 1)
	{
		for (i = 1; i < DIM(factors); i ++)
			if (abs(diff - factors[v->zoomIdx]) > abs(diff - factors[i]))
				v->zoomIdx = i;
	}
}

/* be sure image is not completely outside viewport */
static void ViewImageClampVP(ViewImage v)
{
	int margin = v->margin;
	if (v->zoom.x + v->zoom.width < margin)  v->zoom.x = margin - v->zoom.width;
	if (v->zoom.x > v->width - margin)       v->zoom.x = v->width - margin;
	if (v->zoom.y + v->zoom.height < margin) v->zoom.y = margin - v->zoom.height;
	if (v->zoom.y > v->height - margin)      v->zoom.y = v->height - margin;
}

enum /* possible values for <rel> parameter */
{
	FactorRelative,
	FactorAbsolute,
	FactorKeepAsIs
};

static void ViewImageScale(ViewImage v, double f, int x, int y, int cx, int cy, int rel)
{
	Image  i = v->original;
	double oldfact = v->fact;

	if (v->width == 0 || v->height == 0)
	{
		if (f != 1) v->waitconf &= ~2;
		v->fact = f;
		return;
	}

	switch (rel) {
	case FactorRelative:
		v->fact *= f;

		if (f > 1 && v->fact < 1 && v->fact*1.5 > 1)
			v->fact = 1;

		/* avoid having 100.021345% zoom factor above 100% */
		if (f > 1 && v->fact > 1.1 && v->zoomIdx < DIM(factors))
			v->zoomIdx ++, v->fact = factors[v->zoomIdx] / 100.;

		if (f < 1 && v->zoomIdx > 0)
			v->zoomIdx --, v->fact = factors[v->zoomIdx] / 100.;
		break;
	case FactorAbsolute:
		v->fact = f;
		ViewImageAdjustZoomIdx(v);
	}

	if (v->fact <= 0) v->fact = 1;
	if (v->fact > 32) v->fact = 32;
	/* no point in having a smaller than 10x10px picture */
	if (i->width  * v->fact < MIN_IMAGE_SIZE) v->fact = MIN_IMAGE_SIZE / (double) i->width;
	if (i->height * v->fact < MIN_IMAGE_SIZE) v->fact = MIN_IMAGE_SIZE / (double) i->height;

	if (fabs(v->fact-oldfact) < 0.001 && rel != FactorKeepAsIs) return;

	/* image dimension according to zoom level */
	int oldw = v->zoom.width;  if (oldw == 0) oldw = 1;
	int oldh = v->zoom.height; if (oldh == 0) oldh = 1;

	if (cx < 0) cx = x;
	if (cy < 0) cy = y;
	v->zoom.width  = i->width * v->fact + 0.5;
	v->zoom.height = i->height * v->fact + 0.5;
	v->zoom.x      = cx - (int64_t)(x-v->zoom.x)*v->zoom.width/oldw;
	v->zoom.y      = cy - (int64_t)(y-v->zoom.y)*v->zoom.height/oldh;
	ViewImageClampVP(v);

	/* which translate into this rectangle in the viewport */
	SET_DST(v);

	/* what part of the image need to be extracted */
	v->src.x = - MIN(v->zoom.x, 0) * i->width / v->zoom.width;
	v->src.y = - MIN(v->zoom.y, 0) * i->height / v->zoom.height;
	v->src.width = v->dst.width * i->width / v->zoom.width;
	v->src.height = v->dst.height * i->height / v->zoom.height;
	v->dispmini = v->hasminimap && (v->zoom.width > 3*v->width/2 || v->zoom.height > 3*v->height/2);

	#if 0
	v->magnetX = (v->width  - v->dst.width)  >> 1; if (v->magnetX < 0) v->magnetX = 0;
	v->magnetY = (v->height - v->dst.height) >> 1; if (v->magnetY < 0) v->magnetY = 0;
	#else
	v->magnetX = v->magnetY = 0;
	#endif

	if (f == 1 && v->fact < 1 && v->curimg) /* only window size changed */
	{
		Rect from, sub;

		from.x = (v->src.x & ~TILEMASK);
		from.y = (v->src.y & ~TILEMASK);
		from.width  = (v->src.x + v->src.width  + TILE - 1) & ~TILEMASK;
		from.height = (v->src.y + v->src.height + TILE - 1) & ~TILEMASK;

		while (IterTile(i, &from, &sub, (DATA8) (v->curimg+1)))
			RENDER_TILE(v, i, &sub);
	}
	else /* Window and image size changed */
	{
		SET_LAYER(v);

		v->waitconf &= ~2;
	}
	{
		struct ViewImageOnChange_t msg = {.type = VIT_Factor, .f = v->fact};
		SIT_ApplyCallback(v->canvas, &msg, SITE_OnChange);
	}
}

/* move image using mouse or keyboard */
static void ViewImageTranslate(ViewImage v, int x, int y, Bool magnet)
{
	Image i = v->original;
	if (! magnet)
		v->mouseX = v->mouseY = 0;
	v->zoom.x += x - v->mouseX;
	v->zoom.y += y - v->mouseY;
	if (magnet)
	{
		if (abs(v->zoom.x - v->magnetX) < 8) x += v->magnetX - v->zoom.x, v->zoom.x = v->magnetX;
		if (abs(v->zoom.y - v->magnetY) < 8) y += v->magnetY - v->zoom.y, v->zoom.y = v->magnetY;
	}
	ViewImageClampVP(v);
	v->dst.x  = MAX(v->zoom.x, 0);
	v->dst.y  = MAX(v->zoom.y, 0);
	v->src.x  = - MIN(v->zoom.x, 0) * i->width  / v->zoom.width;
	v->src.y  = - MIN(v->zoom.y, 0) * i->height / v->zoom.height;
	v->mouseX = x;
	v->mouseY = y;
	SET_DST(v);
	v->src.width  = (v->dst.width  * i->width)  / v->zoom.width;
	v->src.height = (v->dst.height * i->height) / v->zoom.height;

	/* Load unrendered tiles */
	if (v->zoom.width < i->width)
	{
		Rect from, sub;

		from.x = (v->src.x & ~TILEMASK);
		from.y = (v->src.y & ~TILEMASK);
		from.width  = (v->src.x + v->src.width  + TILE - 1) & ~TILEMASK;
		from.height = (v->src.y + v->src.height + TILE - 1) & ~TILEMASK;

		while (IterTile(i, &from, &sub, (DATA8) (v->curimg+1)))
			RENDER_TILE(v, i, &sub);
	}
}

/* Check if user clicked within minimap, move vp accordingly if so */
static Bool ViewImageHandleMini(ViewImage v, int x, int y)
{
	if (v->dispmini && v->minimap)
	{
		Image i  = v->minimap;
		x -= v->width - i->width;
		y -= v->height - i->height;
		if (x >= 0 && y > 0)
		{
			/* Click inside, move vp */
			x = - (x - ((v->cursor[2] - v->cursor[0])>>1)) * v->zoom.width  / i->width  - v->zoom.x;
			y = - (y - ((v->cursor[3] - v->cursor[1])>>1)) * v->zoom.height / i->height - v->zoom.y;
			ViewImageTranslate(v, x, y, False);
			SIT_Refresh(v->canvas, 0, 0, 0, 0, False);
			return True;
		}
	}
	return False;
}


static void ViewImageSetMini(ViewImage v, Bool set, Bool refresh)
{
	Image i;
	int   w = 0, h = 0;
	v->hasminimap = set;
	if (set && ! v->minimap && (i = v->original))
	{
		int mw, mh;
		if (i->width > i->height)
			mw = 100, mh = i->height * 100 / i->width;
		else
			mh = 100, mw = i->width * 100 / i->height;
		if (mw > i->width || mh > i->height)
			mw = i->width, mh = i->height;
		if (mw <= 0) mw = 1;
		if (mh <= 0) mh = 1;
		v->minimap = ViewImageReduce(i, GFX_CreateImage(mw, mh, i->bpp), NULL, NULL);
		GFX_FlattenImage(v->minimap, BG_COLOR);
		memset(v->cursor, 0, sizeof v->cursor);
		w = mw+2; h = mh+2;
		v->dispmini = (v->zoom.width > 3*v->width/2 || v->zoom.height > 3*v->height/2);
	}
	else if (! set && v->minimap)
	{
		i = v->minimap;
		w = i->width+2; h = i->height+2;
		GFX_FreeImage(v->minimap);
		v->minimap = NULL;
	}
	if (refresh && w > 0)
		SIT_Refresh(v->canvas, v->width - w, v->height - h, w, h, False);
}

/* Direct blit using xOR within the image */
void ViewImageDrawCursor(Image img, int * coords)
{
	int i, j, s, e, dir, chan = img->bpp>>3;
	for (i = 0; i < 4; i ++)
	{
		DATA8 p;
		switch (i) {
		case 0: s = coords[0]; e = img->height; dir = 0; break;
		case 1: s = coords[1]; e = img->width;  dir = 1; break;
		case 2: s = coords[2]; e = img->height; dir = 0; break;
		case 3: s = coords[3]; e = img->width;  dir = 1; break;
		default: return;
		}
		if (dir == 0) // Vertical
		{
			if (s < 0 || s >= img->width) return;
			for (p = img->bitmap + s * chan, j = 0; j < e; p += img->stride, j ++)
				p[0] ^= 0xff, p[1] ^= 0xff, p[2] ^= 0xff;
		} else {
			if (s < 0 || s >= img->height) return;
			for (p = img->bitmap + s * img->stride, j = 0; j < e; p += chan, j ++)
				p[0] ^= 0xff, p[1] ^= 0xff, p[2] ^= 0xff;
		}
	}
}

static void ViewImageMarqueeViewRect(ViewImage v, Rect * out)
{
	Rect rect;
	if (v->zoom.x < 0)
	{
		int offx = v->zoom.x / v->fact;
		rect.x     = (offx + v->marquee.x) * v->fact;
		rect.width = (offx + v->marquee.width) * v->fact;
	}
	else
	{
		rect.x     = v->marquee.x * v->fact + v->zoom.x;
		rect.width = v->marquee.width * v->fact + v->zoom.x;
	}
	if (v->zoom.y < 0)
	{
		int offy = v->zoom.y / v->fact;
		rect.y      = (offy + v->marquee.y) * v->fact;
		rect.height = (offy + v->marquee.height) * v->fact;
	}
	else
	{
		rect.y      = v->marquee.y * v->fact + v->zoom.y;
		rect.height = v->marquee.height * v->fact + v->zoom.y;
	}

	if (rect.x <= rect.width)  rect.width  += v->fact; else rect.x += v->fact;
	if (rect.y <= rect.height) rect.height += v->fact; else rect.y += v->fact;
//	if (rect.width < rect.x) rect.x += v->fact;
//	if (rect.height < rect.y) rect.y += v->fact;

	*out = rect;
}

int ViewImagePaint(SIT_Widget w, APTR gc, APTR ud)
{
	ViewImage v = ud;

	if (v->original == NULL)
	{
		GFX_FillRect(gc, 0, 0, v->width, v->height);
		return 0;
	}

	if (v->dispmini)
	{
		Image i = v->minimap;
		int   x, y;
		if (i == NULL)
			ViewImageSetMini(v, True, False), i = v->minimap;
		ViewImageDrawCursor(i, v->cursor);
		x = -MIN(v->zoom.x, 0);
		y = -MIN(v->zoom.y, 0);
		v->cursor[0] = x * i->width / v->zoom.width;
		v->cursor[1] = y * i->height / v->zoom.height;
		v->cursor[2] = (x+v->width) * i->width / v->zoom.width;
		v->cursor[3] = (y+v->height) * i->height / v->zoom.height;
		x = v->width-i->width;
		y = v->height-i->height;
		ViewImageDrawCursor(i, v->cursor);
		GFX_SetBgColor(gc, RGB(255,255,255));
		GFX_SetPixels(i, 0, 0, i->width, i->height, gc, x, y, i->width, i->height);
		GFX_FillRect(gc, x-2, y-2, v->width, y-1);
		GFX_FillRect(gc, x-2, y, x-1, v->height);
		GFX_ExcludeClipRect(gc, x-2, y-2, i->width+2, i->height+2);
	}

	GFX_SetBgColor(gc, BG_COLOR);

	if (v->original->bpp == 32)
	{
		#define	ROUND(x)    (((x) + 31) & ~31)

		/* Need to use offscreen gc to avoid flicker */
		Rect r;
		GFX_GetRefresh(v->offgc, &r);
		if (v->offgc == NULL || r.width < ROUND(v->width) || r.height < ROUND(v->height))
		{
			if (v->offgc) GFX_Free(v->offgc);
			v->offw  = ROUND(v->width);
			v->offh  = ROUND(v->height);
			v->offgc = GFX_AllocOffScreen(w, v->offw, v->offh);
		}
		GFX_SetBgColor(v->offgc, BG_COLOR);
		GFX_FillRect(v->offgc, 0, 0, v->width, v->height);
		if (v->fact < 1)
		{
			if (v->curimg)
				GFX_SetPixels(v->curimg, - MIN(v->zoom.x, 0), - MIN(v->zoom.y, 0), v->dst.width, v->dst.height,
							  v->offgc,  v->dst.x, v->dst.y, v->dst.width, v->dst.height);
			else
				GFX_SetPixels(v->original, v->src.x, v->src.y, v->src.width, v->src.height,
							  v->offgc,    v->dst.x, v->dst.y, v->dst.width, v->dst.height);
		}
		else
		{
//			fprintf(stderr, "refreshing area from %d,%d - %d,%d [%d,%d] to %d,%d - %dx%d [%dx%d], over: %d\n",
//				v->src.x, v->src.y, v->src.width, v->src.height, v->original->width, v->original->height,
//				v->dst.x, v->dst.y, v->dst.width, v->dst.height, v->offw, v->offh, v->curimg == NULL);
			GFX_SetPixels(v->original, v->src.x, v->src.y, v->src.width, v->src.height,
	              v->offgc, v->dst.x, v->dst.y, v->dst.width, v->dst.height);
		}

		if (v->cb) v->cb(w, v->offgc, v->ud);

	    GFX_CopyGC(v->offgc, 0, 0, v->width, v->height, gc, 0, 0);
	}
	else /* Direct blit */
	{
//		fprintf(stderr, "refreshing area from %d,%d - %d,%d to %d,%d - %dx%d, over: %d\n", v->src.x, v->src.y, v->src.width, v->src.height,
//			v->dst.x, v->dst.y, v->dst.width, v->dst.height, v->curimg == NULL);
		if (v->curimg)
			GFX_SetPixels(v->curimg, - MIN(v->zoom.x, 0), - MIN(v->zoom.y, 0), v->dst.width, v->dst.height,
						  gc, v->dst.x, v->dst.y, v->dst.width, v->dst.height);
		else
			GFX_SetPixels(v->original, v->src.x, v->src.y, v->src.width, v->src.height,
						  gc, v->dst.x, v->dst.y, v->dst.width, v->dst.height);

		/* Clear unused space */
		int x;
		if (v->dst.x > 0)  GFX_FillRect(gc, 0, 0, v->dst.x-1, v->height);
		if (v->dst.y > 0)  GFX_FillRect(gc, 0, 0, v->width, v->dst.y);  x = v->dst.x+v->dst.width-1;
		if (x < v->width)  GFX_FillRect(gc, x, 0, v->width, v->height); x = v->dst.y+v->dst.height-1;
		if (x < v->height) GFX_FillRect(gc, 0, x, v->width, v->height);

		if (v->cb) v->cb(w, gc, v->ud);
	}
	if (v->marquee.x >= 0)
	{
		Rect rect;
		ViewImageMarqueeViewRect(v, &rect);
		GFX_SetPenEx(gc, 0, v->marqueeCol, GFX_PenSolid);
		GFX_DrawRect(gc, rect.x, rect.y, rect.width, rect.height);
	}
	return 1;
}

static int round2px(ViewImage v, int val, int axis)
{
	if (v->fact > 1)
	{
		int orig = (&v->zoom.x)[axis&1];
		int ret;

		if (orig > 0)
			ret = (val - orig) / v->fact;
		else
			ret = (int) (val / v->fact) - (int) (orig / v->fact);

//		if (axis >= 2 && (&v->marquee.x)[axis&1] <= ret)
//			ret ++;

		return ret;
	}
	return val;
}

static void UnionRect(Rect * out, Rect * rect)
{
	if (out->x > rect->x) out->x = rect->x;
	if (out->y > rect->y) out->y = rect->y;
	if (out->width < rect->width) out->width = rect->width;
	if (out->height < rect->height) out->height = rect->height;
}

/* handle marquee drawing for quickly zooming in */
static void ViewImageDrawMarquee(ViewImage v, int x, int y)
{
	int x2 = round2px(v, x, 2);
	int y2 = round2px(v, y, 3);

	if (v->marquee.width != x2 || v->marquee.height != y2)
	{
		Rect refresh;

		x = v->marquee.x;
		y = v->marquee.y;

		memset(&refresh, 0, sizeof refresh);
		if (v->marquee.width >= 0)
			ViewImageMarqueeViewRect(v, &refresh);

		v->marquee.width = x2;
		v->marquee.height = y2;

		if (refresh.width > refresh.x)
		{
			Rect rect;
			ViewImageMarqueeViewRect(v, &rect);
			UnionRect(&refresh, &rect);
		}
		else ViewImageMarqueeViewRect(v, &refresh);

		GC gc = GFX_GetGC(v->canvas);
		refresh.width  -= refresh.x;
		refresh.height -= refresh.y;
		GFX_SetRefresh(gc, &refresh);
		ViewImagePaint(v->canvas, gc, v);
		GFX_Free(gc);
	}
}

/* zoom in the rectangle drawn by the user */
static void ViewImageZoomMarquee(ViewImage v, int type)
{
	struct ViewImageOnChange_t msg = {.type = type, .rect = v->marquee};

	if (msg.rect.x <= msg.rect.width)  msg.rect.width  ++; else msg.rect.x ++;
	if (msg.rect.y <= msg.rect.height) msg.rect.height ++; else msg.rect.y ++;

	int tmp;
	if (msg.rect.x > msg.rect.width)
		swap_tmp(msg.rect.x, msg.rect.width, tmp);

	if (msg.rect.y > msg.rect.height)
		swap_tmp(msg.rect.y, msg.rect.height, tmp);

	SIT_ApplyCallback(v->canvas, &msg, SITE_OnChange);
}

static ULONG ViewImageGetRGB(ViewImage v, int x, int y)
{
	uint8_t rgba[4];
	Image img = v->original;

	if (x < 0) x = 0;
	if (y < 0) y = 0;
	if (x >= v->original->width)  x = v->original->width-1;
	if (y >= v->original->height) y = v->original->height-1;

	memcpy(rgba, img->bitmap + y * img->stride + x * (img->bpp>>3), 4);

	if (img->bpp == 32) /* pre-multiplied :-/ */
	{
		/* rounded, but better than nothing ... */
		rgba[0] *= 255 / rgba[3];
		rgba[1] *= 255 / rgba[3];
		rgba[2] *= 255 / rgba[3];
	}
	return RGB(rgba[0], rgba[1], rgba[2]);
}

static int IsIn(ViewImage v, int x, int y, int rw, int rh)
{
	x -= rw; rw = x + rw*2;
	y -= rh; rh = y + rh*2;
	return x <= v->mouseX && v->mouseX < rw &&
	       y <= v->mouseY && v->mouseY < rh;
}

#define AREA    5
static int ViewImageGetCursor(ViewImage v)
{
	if (v->marquee.width >= 0)
	{
		Rect rect;
		int  tmp;
		ViewImageMarqueeViewRect(v, &rect);
		if (rect.x > rect.width)  swap_tmp(rect.x, rect.width,  tmp);
		if (rect.y > rect.height) swap_tmp(rect.y, rect.height, tmp);

		if (IsIn(v, rect.x,     rect.y,      AREA, AREA)) { v->marqueeHover = 1; return SITV_CursorSizeNWSE; }
		if (IsIn(v, rect.width, rect.height, AREA, AREA)) { v->marqueeHover = 4; return SITV_CursorSizeNWSE; }
		if (IsIn(v, rect.x,     rect.height, AREA, AREA)) { v->marqueeHover = 3; return SITV_CursorSizeNESW; }
		if (IsIn(v, rect.width, rect.y,      AREA, AREA)) { v->marqueeHover = 2; return SITV_CursorSizeNESW; }
	}
	v->marqueeHover = 0;
	return SITV_CursorNormal;
}

/* SITE_OnClick and SITE_OnMouseMove handler */
static int ViewImageMouse(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_OnMouse * msg = cd;
	ViewImage     v   = ud;

	if (v->original == NULL || ! v->dozoom) return 0;
	if (msg->state == SITOM_Move)
	{
		v->mouseX = msg->x;
		v->mouseY = msg->y;
		SIT_SetValues(w, SIT_Cursor, ViewImageGetCursor(v), NULL);
	}
	if ((msg->flags & SITK_FlagShift) && msg->button == 0)
		msg->button = 2;
	switch (msg->button) {
	case 3: /* Wheel */
		ViewImageScale(ud, msg->state < 0 ? 1/1.5 : 1.5, msg->x, msg->y, -1, -1, FactorRelative);
		SIT_Refresh(w, 0, 0, 0, 0, False);
		break;
	case 2: /* MMB */
		if (v->marqueeSel)
		switch (msg->state) {
		case SITOM_ButtonPressed:
			v->marquee.x = round2px(v, msg->x, 0);
			v->marquee.y = round2px(v, msg->y, 1);
			v->marqueeCol = ViewImageGetRGB(v, v->marquee.x, v->marquee.y);
			if ((RGB_GETR(v->marqueeCol) + RGB_GETG(v->marqueeCol) + RGB_GETB(v->marqueeCol)) / 3 < 200)
				v->marqueeCol = RGB(255,255,255);
			else
				v->marqueeCol = 0;
			v->marquee.width = -1;
			return 1;
		case SITOM_CaptureMove:
			if (v->marquee.x >= 0)
			{
				ViewImageDrawMarquee(v, msg->x, msg->y);
				ViewImageZoomMarquee(v, VIT_MarqueeNotif);
			}
			break;
		case SITOM_ButtonReleased:
			if (v->marquee.x >= 0)
				ViewImageZoomMarquee(v, VIT_Marquee);
		}
		break;
	case 0: /* LMB */
		switch (msg->state) {
		case SITOM_ButtonPressed:
			if (v->marqueeHover > 0)
			{
				Rect rect = v->marquee;
				int  inv  = v->marqueeHover - 1;
				int  tmp;
				if (rect.x > rect.width)  swap_tmp(rect.x, rect.width,  tmp);
				if (rect.y > rect.height) swap_tmp(rect.y, rect.height, tmp);
				if ((inv & 1) == 0) swap_tmp(rect.x, rect.width,  tmp);
				if ((inv & 2) == 0) swap_tmp(rect.y, rect.height, tmp);
				v->marquee = rect;
				return 1;
			}
			if (! ViewImageHandleMini(v, msg->x, msg->y))
			{
				v->mouseX = msg->x;
				v->mouseY = msg->y;
				return 1;
			}
			break;
		case SITOM_CaptureMove:
			if (v->marqueeHover > 0)
			{
				ViewImageDrawMarquee(v, msg->x, msg->y);
				ViewImageZoomMarquee(v, VIT_MarqueeNotif);
			}
			else
			{
				ViewImageTranslate(v, msg->x, msg->y, True);
				SIT_Refresh(w, 0, 0, 0, 0, False);
			}
			break;
		case SITOM_ButtonReleased:
			if (v->marqueeHover > 0)
			{
				ViewImageZoomMarquee(v, VIT_Marquee);
			}
		}
	}
	return 0;
}

/* make the image fit into the whole canvas */
static void ViewImageFullScr(ViewImage v)
{
	Image i = v->original;
	int   dstw, dsth, changes;
	if (i == NULL) return;

	changes = 0;
	if (i->width * v->height > i->height * v->width)
		dstw = v->width, dsth = i->height * v->width / i->width;
	else
		dsth = v->height, dstw = i->width * v->height / i->height;

	if (dstw > i->width * 32)
	{
		dstw = i->width * 32;
		dsth = i->height * 32;
	}

	if (dstw != v->zoom.width || dsth != v->zoom.height)
	{
		v->zoom.width  = v->dst.width  = dstw;
		v->zoom.height = v->dst.height = dsth;
		v->fact = v->zoom.width / (double) i->width;
		v->src.width  = i->width;
		v->src.height = i->height;
		v->dispmini = False;
		changes |= 1;
		ViewImageAdjustZoomIdx(v);
	}
	dstw = (v->width  - v->zoom.width)  / 2;
	dsth = (v->height - v->zoom.height) / 2;
	if (dstw != v->zoom.x || dsth != v->zoom.y)
	{
		v->dst.x = v->zoom.x = v->magnetX = dstw;
		v->dst.y = v->zoom.y = v->magnetY = dsth;
		v->src.x = v->src.y = 0;
		changes |= 2;
	}
	if (changes & 1)
	{
		SET_LAYER(v);
		struct ViewImageOnChange_t msg = {.type = VIT_Factor, .f = v->fact};
		SIT_ApplyCallback(v->canvas, &msg, SITE_OnChange);
	}
	else if (changes & 2)
	{
		ViewImageTranslate(v, 0, 0, False);
	}
}

/* SITE_OnVanillaKey and SITE_OnRawKey handler */
int ViewImageKbd(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_OnKey * msg = cd;
	ViewImage   v   = ud;
	int         s   = msg->flags & SITK_FlagShift ? 100 : 10;

	if (v->original == NULL || ! v->dozoom) return 0;

	switch (msg->keycode) {
	case 'f': // full-screen
		v->waitconf |= 2;
		ViewImageFullScr(v);
		break;
	case '1': case 'd': case 'D': // dot for dot
		ViewImageScale(v, 1, v->width/2, v->height/2, -1, -1, FactorAbsolute);
		break;
	case '=':
	case '+': ViewImageScale(v, 1.5,   v->width/2, v->height/2, -1, -1, FactorRelative); break; // Zoom in
	case '-': ViewImageScale(v, 1/1.5, v->width/2, v->height/2, -1, -1, FactorRelative); break; // Zoom out
	case SITK_Left:   ViewImageTranslate(v, -s,  0, False); break;
	case SITK_Right:  ViewImageTranslate(v,  s,  0, False); break;
	case SITK_Up:     ViewImageTranslate(v,  0, -s, False); break;
	case SITK_Down:   ViewImageTranslate(v,  0,  s, False); break;
	case SITK_Escape: v->marquee.x = -1; break;
	default: return 0;
	}
	SIT_Refresh(w, 0, 0, 0, 0, False);
	return 1;
}

/* OnResize event handler */
int ViewImageResize(SIT_Widget w, APTR cd, APTR ud)
{
	ViewImage v  = ud;
	int *     sz = cd;

	v->width  = sz[0];
	v->height = sz[1];
	if (v->original == NULL) { v->waitconf &= ~1; return 0; }

	if (v->waitconf)
	{
		if (v->waitconf & 2) ViewImageFullScr(v);
		else ViewImageScale(v, 1, 0, 0, -1, -1, FactorKeepAsIs);
		v->waitconf &= ~1;
	}
	else ViewImageScale(v, 1, 0, 0, -1, -1, FactorKeepAsIs);
	return 1;
}

int ViewImageFree(SIT_Widget w, APTR cd, APTR ud)
{
	ViewImage v = ud;
	if (v->minimap) GFX_FreeImage(v->minimap);
	if (v->curimg)  GFX_FreeImage(v->curimg);
	if (v->offgc)   GFX_Free(v->offgc);
	free(v);
	return 1;
}

/* Change or set the current image displayed */
void ViewImageSet(ViewImage v, Image i)
{
	v->original = i;

	memset(&v->src, 0, 3 * sizeof v->src);
	if (v->curimg) GFX_FreeImage(v->curimg), v->curimg = NULL;
	if (v->minimap) GFX_FreeImage(v->minimap), v->minimap = NULL;
	ViewImageSetMini(v, v->hasminimap, False);

	if ((v->waitconf & 1) == 0)
	{
		int sz[2];
		SIT_GetValues(v->canvas, SIT_Width, sz, SIT_Height, sz+1, NULL);
		ViewImageResize(v->canvas, sz, v);
		SIT_Refresh(v->canvas, 0, 0, 0, 0, False);
	}
}

/*
 * Properties : image, factor, allowzoom, autofit
 */
int ViewImageSetGet(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_OnVal * val = cd;
	ViewImage   v   = ud;
	int         ret = 0;

	switch (val->stage) {
	case SITV_Set:
		switch (val->tag) {
		case VIT_Image:     ViewImageSet(ud, SIT_GET(val, APTR)); break;
		case VIT_Factor:    ViewImageScale(ud, SIT_GET(val, double), 0, 0, -1, -1, FactorAbsolute); ret = 1; break;
		case VIT_AllowZoom: v->dozoom = SIT_GET(val, int); break;
		case VIT_MiniMap:   ViewImageSetMini(v, SIT_GET(val, int), True); break;
		case VIT_Overlay:   v->cb = SIT_GET(val, APTR); ret = 1; break;
		case VIT_UserData:  v->ud = SIT_GET(val, APTR);  break;
		case VIT_Marquee:   v->marqueeSel = SIT_GET(val, Bool); ret = 1; break;
		case VIT_OffsetX:   v->offsetX = SIT_GET(val, int); v->setflags |= 1; return 2;
		case VIT_OffsetY:   v->offsetY = SIT_GET(val, int); v->setflags |= 1; return 2;
		case VIT_AutoFit:
			if (SIT_GET(val, int)) v->waitconf |= 2, ViewImageFullScr(v), ret = 1;
			else v->waitconf &= ~2;
			break;
		case VIT_MarqueeRect:
			v->marquee = * SIT_GET(val, Rect *);
			v->marquee.width --;
			v->marquee.height --;
			v->marqueeCol = ViewImageGetRGB(v, v->marquee.x, v->marquee.y);
			if ((RGB_GETR(v->marqueeCol) + RGB_GETG(v->marqueeCol) + RGB_GETB(v->marqueeCol)) / 3 < 200)
				v->marqueeCol = RGB(255,255,255);
			else
				v->marqueeCol = 0;
			ret = 1;
		}
		if (ret && (v->waitconf & 1) == 0) SIT_Refresh(v->canvas, 0, 0, 0, 0, False);
		break;
	case SITV_Get:
		switch (val->tag) {
		case VIT_Image:     SIT_SET(val, v->original, APTR); break;
		case VIT_Factor:    SIT_SET(val, v->fact, double); break;
		case VIT_AllowZoom: SIT_SET(val, v->dozoom, int); break;
		case VIT_MiniMap:   SIT_SET(val, v->hasminimap, int); break;
		case VIT_Overlay:   SIT_SET(val, v->cb, APTR); break;
		case VIT_AutoFit:   SIT_SET(val, v->waitconf & 2, int); break;
		case VIT_UserData:  SIT_SET(val, v->ud, APTR); break;
		case VIT_OffsetY:   SIT_SET(val, v->offsetY, int); break;
		case VIT_OffsetX:   SIT_SET(val, v->offsetX, int); break;
		case VIT_Marquee:   SIT_SET(val, v->marqueeSel, int); break;
		case VIT_ZoomX:
			ret = v->zoom.x;
			if (ret < 0 && v->fact > 1) ret /= v->fact, ret *= v->fact;
			SIT_SET(val, ret, int);
			break;
		case VIT_ZoomY:
			ret = v->zoom.y;
			if (ret < 0 && v->fact > 1) ret /= v->fact, ret *= v->fact;
			SIT_SET(val, ret, int);
			break;
		}
		break;
	case SITV_PostProcess:
		/* OffsetX or Y changed */
		if (v->original && (v->setflags & 1))
		{
			v->zoom.x = - v->offsetX * v->zoom.width  / v->original->width;
			v->zoom.y = - v->offsetY * v->zoom.height / v->original->height;
			ViewImageTranslate(v, 0, 0, False);
			SIT_Refresh(v->canvas, 0, 0, 0, 0, False);
		}
		v->setflags = 0;
	}
	return 0;
}

void ViewImageInvalidate(SIT_Widget c, int x, int y, int w, int h)
{
	ViewImage v = NULL;
	Image     i;
	Rect      from;

	SIT_GetValues(c, VIT_UserData, &v, NULL); if (v == NULL) return;
	i = v->original;                          if (i == NULL) return;
	if (h == 0 && w == 0)
		x = y = 0, w = i->width, h = i->height; /* Whole area */

	from.x = x; from.width  = x + w; if (from.width  > i->width)  from.width  = i->width;
	from.y = y; from.height = y + h; if (from.height > i->height) from.height = i->height;

	if (v->curimg)
		SET_BITMAP(i, &from, (DATA8) (v->curimg + 1), 1);

	/* Check if part of invalidated region is visible: intersect rect between 'from' and 'v->src' */
	Rect r = {.x = MAX(from.x, v->src.x), .width  = v->src.x + v->src.width,
			  .y = MAX(from.y, v->src.y), .height = v->src.y + v->src.height};

	if (r.width  > from.width)  r.width  = from.width;
	if (r.height > from.height) r.height = from.height;

	if (r.x < r.width && r.y < r.height)
	{
		/* Round to nearest tile boundary */
		if (v->curimg) {
			r.x &= ~TILEMASK; r.width  = (r.width  + TILEMASK) & ~ TILEMASK;
			r.y &= ~TILEMASK; r.height = (r.height + TILEMASK) & ~ TILEMASK;
			/* Part of invalidated area is visible */
			RENDER_TILE(v, i, &r);
			r.x -= (v->src.x * v->zoom.width  + (i->width>>1)) / i->width;
			r.y -= (v->src.y * v->zoom.height + (i->height>>1)) / i->height;
		}
		else
		{
			r.x = (r.x - v->src.x) * v->zoom.width  / i->width;
			r.y = (r.y - v->src.y) * v->zoom.height / i->height;
			r.width  = (from.width  - v->src.x) * v->zoom.width  / i->width;
			r.height = (from.height - v->src.y) * v->zoom.height / i->height;
		}
		SIT_Refresh(c, r.x + v->dst.x, r.y + v->dst.y, r.width, r.height, False);
	}
	if ((i = v->minimap)) {
		int w = i->width + 2, h = i->height + 2;
		GFX_FreeImage(i); v->minimap = NULL;
		if (v->dispmini) SIT_Refresh(c, v->width - w, v->height - h, w, h, False);
	}
}

/* bitmap modified from another thread, refresh canvas */
static int ViewImageRefresh(SIT_Widget w, APTR cd, APTR ud)
{
	ViewImageInvalidate(w, 0, 0, 0, 0);
	return 1;
}

Bool ViewImageInit(SIT_Widget w, Image i)
{
	ViewImage v = calloc(sizeof *v, 1);

	if (v == NULL) return False;
	SIT_GetValues(w, SIT_Width, &v->width, SIT_Height, &v->height, NULL);
	v->waitconf = 3;
	v->fact = 1;
	v->hasminimap = v->dozoom = 1;
	v->canvas = w;
	v->ud = v;
	v->margin = GFX_GetFontHeight(NULL);
	v->marquee.x = -1;
	ViewImageSet(v, i);

	SIT_AddCallback(w, SITE_OnClickMove,  ViewImageMouse,   v);
	SIT_AddCallback(w, SITE_OnRawKey,     ViewImageKbd,     v);
	SIT_AddCallback(w, SITE_OnVanillaKey, ViewImageKbd,     v);
	SIT_AddCallback(w, SITE_OnResize,     ViewImageResize,  v);
	SIT_AddCallback(w, SITE_OnPaint,      ViewImagePaint,   v);
	SIT_AddCallback(w, SITE_OnFinalize,   ViewImageFree,    v);
	SIT_AddCallback(w, SITE_OnSetOrGet,   ViewImageSetGet,  v);
	SIT_AddCallback(w, SITE_OnUser,       ViewImageRefresh, v);
	SIT_SetFocus(w);
	return True;
}
