/*
 * ViewImage.h: public functions to initialize/manage a picture viewer widget.
 *
 * Written by T.Pierron, Mar 2011.
 */

#ifndef	VIEW_IMAGE_H
#define	VIEW_IMAGE_H

#include "graphics.h"

enum {
	VIT_Image = SIT_TagUser,  /* Image */
	VIT_Factor,               /* double */
	VIT_AllowZoom,            /* Bool */
	VIT_MiniMap,              /* Bool */
	VIT_Overlay,              /* SIT_CallProc */
	VIT_AutoFit,              /* int */
	VIT_OffsetX,              /* int */
	VIT_OffsetY,              /* int */
	VIT_Marquee,              /* Bool */
	VIT_MarqueeRect,          /* Rect * */
	VIT_ZoomX,
	VIT_ZoomY,
};

#define VIT_MarqueeNotif       128

#define MIN_IMAGE_SIZE         64

Bool  ViewImageInit(SIT_Widget, Image i);
void  ViewImageInvalidate(SIT_Widget, int x, int y, int w, int h);
Image ViewImageReduce(Image source, Image ret, Rect * from, Rect * to);

typedef struct ViewImageOnChange_t *     ViewImageOnChange; /* OnChange notification */

struct ViewImageOnChange_t
{
	int    type; /* VIT_Factor, VIT_Minimap, VIT_Marquee, VIT_MarqueeNotif */
	union
	{
		double f;     /* only if type == VIT_Factor */
		Bool   map;   /* if type == VIT_Minimap */
		Rect   rect;  /* if type == VIT_Marquee */
	};
};

#endif
