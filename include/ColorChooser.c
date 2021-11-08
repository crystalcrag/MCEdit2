/*
 * ColorChooser.h : simple widget to select an RGB color
 *
 * Written by T.Pierron, May 2021
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include "SIT.h"


typedef struct ColorChooser_t *      ColorChooser;
typedef uint16_t *                   DATA16;

struct ColorChooser_t
{
	SIT_CallProc cb;
	SIT_Widget   SV;
	SIT_Widget   hue;
	SIT_Widget   val;
	SIT_Widget   SVindic;
	SIT_Widget   Hindic;
	uint8_t      rgb[4];
	uint16_t     hsv[3];
	APTR         userData;
};


/* Normalized in range 255-255-255 from range 360-100-100 */
static void HSV_to_RGB(DATA16 hsv, DATA8 rgb)
{
	int i, f, m, n, v;

	i = hsv[0] / 60;
	v = hsv[2];
	f = ((hsv[0] - 60*i) << 8) / 60;
	if ((i&1) == 0) f = 256 - f; // if i is even
	m = v * (100 - hsv[1]) * 255 / 10000;
	n = v * (100 - (hsv[1] * f >> 8)) * 255 / 10000;
	v = v * 255 / 100;
	switch (i) {
	case 6:
	case 0: rgb[0] = v; rgb[1] = n; rgb[2] = m; break;
	case 1: rgb[0] = n; rgb[1] = v; rgb[2] = m; break;
	case 2: rgb[0] = m; rgb[1] = v; rgb[2] = n; break;
	case 3: rgb[0] = m; rgb[1] = n; rgb[2] = v; break;
	case 4: rgb[0] = n; rgb[1] = m; rgb[2] = v; break;
	case 5: rgb[0] = v; rgb[1] = m; rgb[2] = n;
	}
}

#define MAX3(x,y,z)  ((y) >= (z) ? ((x) >= (y) ? (x) : (y)) : \
                                   ((x) >= (z) ? (x) : (z)))
#define MIN3(x,y,z)  ((y) <= (z) ? ((x) <= (y) ? (x) : (y)) : \
                                   ((x) <= (z) ? (x) : (z)))

/* Normalized in range 360-100-100 from range 255-255-255 */
static void RGB_to_HSV(DATA16 rgb, DATA16 hsv)
{
	int max = MAX3(rgb[0], rgb[1], rgb[2]);
	int min = MIN3(rgb[0], rgb[1], rgb[2]);
	hsv[0] = hsv[1] = 0;
	hsv[2] = 100 * max / 255;
	if (max > 0)
	{
		int diff = max - min;
		hsv[1] = 100 * diff / max;
		if (hsv[1] > 0)
		{
			if (max == rgb[0]) {
				hsv[0] = 60 * (rgb[1] - rgb[2]) / diff;
				if ((short) hsv[0] < 0) hsv[0] += 360;
			} else if (max == rgb[1]) {
				hsv[0] = 120 + 60 * (rgb[2] - rgb[0]) / diff;
			} else {
				hsv[0] = 240 + 60 * (rgb[0] - rgb[1]) / diff;
			}
		}
	}
}

static void CCSetColorRGB(ColorChooser cc, DATA8 rgb)
{
	TEXT styles[128];
	TEXT rgbtxt[8];

	sprintf(rgbtxt, "#%02x%02x%02x", rgb[0], rgb[1], rgb[2]);
	sprintf(styles, "background: %s; color: %s", rgbtxt, ((77*rgb[0]+151*rgb[1]+28*rgb[2]) >> 8) < 140 ? "white" : "black");

	SIT_SetValues(cc->val, SIT_Title, rgbtxt, SIT_Style, styles, NULL);
}

static void CCSetColor(ColorChooser cc, DATA8 rgb)
{
	TEXT     bg[128];
	uint16_t rgb16[3];
	uint8_t  rgb8[3];

	memcpy(cc->rgb, rgb, 4);
	rgb16[0] = rgb[0];
	rgb16[1] = rgb[1];
	rgb16[2] = rgb[2];
	RGB_to_HSV(rgb16, cc->hsv);

	uint16_t hsv[] = {cc->hsv[0], 100, 100};
	HSV_to_RGB(hsv, rgb8);

	sprintf(bg,
		"background: linear-gradient(to right, black, transparent), "
		"linear-gradient(to bottom, #%02x%02x%02x, white)", rgb8[0], rgb8[1], rgb8[2]
	);
	SIT_SetValues(cc->SV, SIT_Style, bg, NULL);
	SIT_SetValues(cc->Hindic,  SIT_TopObject, SITV_AttachPos(100 - cc->hsv[0] / 3.60), NULL);
	SIT_SetValues(cc->SVindic, SIT_TopObject, SITV_AttachPos(100 - cc->hsv[1]), SIT_LeftObject, SITV_AttachPos(cc->hsv[2]), NULL);
	CCSetColorRGB(cc, rgb);
}

static void CCSetSVCursor(ColorChooser cc, int x, int y)
{
	float width, height;
	SIT_GetValues(cc->SV, SIT_Width, &width, SIT_Height, &height, NULL);

	if (x < 0) x = 0; if (x >= width)  x = width-1;
	if (y < 0) y = 0; if (y >= height) y = height-1;
	y = 100 - (y / (height-1)) * 100;
	x = (x / (width-1)) * 100;

	if (cc->hsv[1] != y || cc->hsv[2] != x)
	{
		uint8_t rgb[4];
		cc->hsv[1] = y;
		cc->hsv[2] = x;
		HSV_to_RGB(cc->hsv, rgb);
		CCSetColorRGB(cc, rgb);
		SIT_SetValues(cc->SVindic, SIT_TopObject, SITV_AttachPos(100-y), SIT_LeftObject, SITV_AttachPos(x), NULL);
	}
}

static void CCSetHueCursor(ColorChooser cc, int y)
{
	float height;
	SIT_GetValues(cc->hue, SIT_Height, &height, NULL);
	if (y < 0) y = 0;
	if (y >= height) y = height-1;
	y = 359 - (y * 359 / height);

	if (y != cc->hsv[0])
	{
		cc->hsv[0] = y;
		HSV_to_RGB(cc->hsv, cc->rgb);
		CCSetColor(cc, cc->rgb);
	}
}

static int CCMoveHS(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_OnMouse * msg = cd;
	switch (msg->state) {
	case SITOM_ButtonPressed:
		if (msg->button == SITOM_ButtonLeft)
		{
			CCSetSVCursor(ud, msg->x, msg->y);
			return 2;
		}
		break;
	case SITOM_CaptureMove:
		CCSetSVCursor(ud, msg->x, msg->y);
	default:
		break;
	}
	return 1;
}

static int CCMoveHue(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_OnMouse * msg = cd;
	switch (msg->state) {
	case SITOM_ButtonPressed:
		if (msg->button == SITOM_ButtonLeft)
		{
			CCSetHueCursor(ud, msg->y);
			return 2;
		}
		break;
	case SITOM_CaptureMove:
		CCSetHueCursor(ud, msg->y);
	default:
		break;
	}
	return 1;
}

static int CCParseColor(SIT_Widget w, APTR cd, APTR ud)
{
	ColorChooser cc = ud;
	STRPTR       str;
	uint8_t      rgb[4];
	SIT_GetValues(w, SIT_Title, &str, NULL);
	if (SIT_ParseCSSColor(str, rgb) && memcmp(cc->rgb, rgb, 3))
		CCSetColor(ud, rgb);
	return 0;
}

static int CCSelect(SIT_Widget w, APTR cd, APTR ud)
{
	ColorChooser cc = ud;
	STRPTR       color;
	uint8_t      rgb[4];

	SIT_GetValues(cc->val, SIT_Title, &color, SIT_Parent, &w, NULL);
	if (SIT_ParseCSSColor(color, rgb))
	{
		rgb[3] = 255;
		cc->cb(w, rgb, cc->userData);
		SIT_CloseDialog(w);
	}
	return 1;
}

SIT_Widget CCOpen(SIT_Widget app, DATA8 rgb, SIT_CallProc cb, APTR ud, int arrowUp)
{
	static TEXT bgSV[] =
		"background: linear-gradient(to bottom, #f00, #f0f 17%, #00f 33%, #0ff 50%, #0f0 67%, #ff0 83%, #f00)";
	static TEXT bgCursorSV[] =
		"background: url(data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAA8AAAAPAgMAAABGuH3ZAAAACVBMVEUAAAD///8AAABzxoNxA"
		"AAAAXRSTlMAQObYZgAAABxJREFUCNdjYGBzYMBNhIaFhjCsWrVqBZiFXzEAQP4Ilyq2pVsAAAAASUVORK5CYII=); top: -7px; left: -7px";
	static TEXT bgCursorHue[] =
		"background: url(data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAcAAAALAgMAAADO/n87AAAACVBMVEUAAAAAAAD///+D3c/SAAAAAX"
		"RSTlMAQObYZgAAACNJREFUCNdjWMDAMIOBYRoDw9QGhqkJDFMjgCSIPQ0kDpQFAJB2CJ7cfMrnAAAAAElFTkSuQmCC); top: -5px; left: -7px";

	SIT_Widget diag = SIT_CreateWidget("colorchooser.mc", SIT_DIALOG + SIT_EXTRA(sizeof (struct ColorChooser_t)), app,
		SIT_Title,        "Select color",
		SIT_DialogStyles, SITV_Plain | SITV_Transcient,
		NULL
	);
	ColorChooser cc;

	SIT_GetValues(diag, SIT_UserData, &cc, NULL);
	SIT_CreateWidgets(diag,
		"<canvas name=SV height=10em>"
		" <canvas name=SVindic width=15 height=15 style=", bgCursorSV, "top=POSITION left=POSITION/>"
		"</canvas>"
		"<canvas name=hue style=", bgSV, "width=1em height=10em right=FORM top=FORM bottom=OPPOSITE,SV>"
		" <canvas name=Hindic width=7 height=11 top=POSITION style=", bgCursorHue, "/>"
		"</canvas>"
		"<editbox name=color width=5em bottom=FORM>"
		"<button name=ok.act title=Select top=MIDDLE,color left=WIDGET,color,0.3em buttonType=", SITV_DefaultButton, ">"
		"<button name=ko.act title=Cancel top=MIDDLE,color left=WIDGET,ok,0.3em buttonType=", SITV_CancelButton, ">"
	);
	SIT_SetAttributes(diag, "<SV left=FORM top=FORM right=WIDGET,hue,0.8em bottom=WIDGET,color,0.3em>");

	if (arrowUp > 0)
	{
		/* add an arrow pointing up (user will have to manually place the popup though) */
		uint8_t bg[4];
		TEXT    style[96];
		SIT_GetCSSValue(diag, "background-color", bg);
		sprintf(style, "border-bottom: 1em solid #%02x%02x%02x; border-width: 0 1em 1em 1em; top: -1em", bg[0], bg[1], bg[2]);
		SIT_CreateWidget("arrow", SIT_CANVAS, diag,
			SIT_Left,   SITV_AttachForm, NULL, arrowUp,
			SIT_Style,  style,
			SIT_Width,  SITV_Em(2),
			SIT_Height, SITV_Em(1),
			NULL
		);
	}

	cc->SV       = SIT_GetById(diag, "SV");
	cc->hue      = SIT_GetById(diag, "hue");
	cc->val      = SIT_GetById(diag, "color");
	cc->SVindic  = SIT_GetById(diag, "SVindic");
	cc->Hindic   = SIT_GetById(diag, "Hindic");
	cc->cb       = cb;
	cc->userData = ud;

	SIT_AddCallback(cc->SV,  SITE_OnClickMove, CCMoveHS, cc);
	SIT_AddCallback(cc->hue, SITE_OnClickMove, CCMoveHue, cc);
	SIT_AddCallback(cc->val, SITE_OnBlur,      CCParseColor, cc);

	SIT_AddCallback(SIT_GetById(diag, "ok"), SITE_OnActivate, CCSelect, cc);

	CCSetColor(cc, rgb);

	return diag;
}
