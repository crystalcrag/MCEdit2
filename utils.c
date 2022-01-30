/*
 * utils.c: utility function to deal with opengl and 3d math
 *          (includes loading opengl functions).
 *
 * Written by T.Pierron, dec 2019
 */

#define STB_INCLUDE_LINE_NONE
#define STB_INCLUDE_IMPLEMENTATION
#include <glad.h>
#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include "SIT.h"
#include "utils.h"
#include "stb_include.h"

/*
 * GLSL shaders compilation, program linking
 */
static void printShaderLog(GLuint shader, const char * path)
{
	int len = 0, written;

	glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
	if (len > 0)
	{
		char * log = alloca(len);
		glGetShaderInfoLog(shader, len, &written, log);
		SIT_Log(SIT_ERROR, "%s: error compiling shader:\n%s\n", path, log);
	}
}

static void printProgramLog(GLuint program, const char * path)
{
	int len = 0, written;

	glGetProgramiv(program, GL_INFO_LOG_LENGTH, &len);
	if (len > 0)
	{
		char * log = alloca(len);
		glGetProgramInfoLog(program, len, &written, log);
		SIT_Log(SIT_ERROR, "%s: error linking program:\n%s\n", path, log);
	}
}

int checkOpenGLError(const char * function)
{
	int error = 0;
	int glErr = glGetError();
	while (glErr != GL_NO_ERROR)
	{
		SIT_Log(SIT_ERROR, "%s: glError: %d\n", function, glErr);
		error = 1;
		glErr = glGetError();
	}
	return error;
}

static int compileShader(const char * path, const char * inject, int type)
{
	char   error[256];
	char * source = alloca(strlen(path) + 32);

	strcpy(source, SHADERDIR);
	strcat(source, path);
	source = stb_include_file(source, inject, error);

	if (source)
	{
		GLint shader = glCreateShader(type);
		GLint compiled = 0;

		glShaderSource(shader, 1, (const char **)&source, NULL);
		free(source);
		glCompileShader(shader);
		checkOpenGLError("glCompileShader");
		glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
		if (compiled)
			return shader;

		printShaderLog(shader, path);
	}
	else SIT_Log(SIT_ERROR, "%s: %s\n", path, error);

	return 0;
}

int createGLSLProgram(const char * vertexShader, const char * fragmentShader, const char * geomShader)
{
	int linked;
	int vertex   = compileShader(vertexShader, NULL, GL_VERTEX_SHADER);      if (vertex   == 0) return 0;
	int fragment = compileShader(fragmentShader, NULL, GL_FRAGMENT_SHADER);  if (fragment == 0) return 0;
	int geometry = 0;

	if (geomShader)
	{
		geometry = compileShader(geomShader, NULL, GL_GEOMETRY_SHADER);
		if (geometry == 0) return 0;
	}

	GLuint program = glCreateProgram();
	glAttachShader(program, vertex);
	glAttachShader(program, fragment);
	if (geometry > 0)
		glAttachShader(program, geometry);
	glLinkProgram(program);
	checkOpenGLError("glLinkProgram");
	glGetProgramiv(program, GL_LINK_STATUS, &linked);
	if (! linked)
	{
		printProgramLog(program, vertexShader);
		return 0;
	}
	return program;
}

int createGLSLProgramCond(const char * vertexShader, const char * fragmentShader, const char * inject)
{
	int linked;
	int vertex   = compileShader(vertexShader, inject, GL_VERTEX_SHADER);      if (vertex   == 0) return 0;
	int fragment = compileShader(fragmentShader, inject, GL_FRAGMENT_SHADER);  if (fragment == 0) return 0;

	GLuint program = glCreateProgram();
	glAttachShader(program, vertex);
	glAttachShader(program, fragment);
	glLinkProgram(program);
	checkOpenGLError("glLinkProgram");
	glGetProgramiv(program, GL_LINK_STATUS, &linked);
	if (! linked)
	{
		printProgramLog(program, vertexShader);
		return 0;
	}
	return program;
}


void setShaderValue(int prog, const char * field, int args, float * array)
{
	int loc = glGetUniformLocation(prog, field);

	switch (args) {
	case 4: glProgramUniform4fv(prog, loc, 1, array); break;
	case 3: glProgramUniform3fv(prog, loc, 1, array); break;
	case 1: glProgramUniform1fv(prog, loc, 1, array);
	}
}


/*
 * javascript object notation parser (not exactly like JSON, but close)
 */
#define LINE_LENGTH(ptr)     ((((DATA8)ptr)[-1] << 8) | ((DATA8)ptr)[-2])

enum
{
	PARSE_COMMENT  = 1,
	PARSE_STARTOBJ = 2,
	PARSE_IDENT    = 4,
	PARSE_VALUE    = 8,
	PARSE_SEP      = 16,
	PARSE_ARRAY    = 32,
	PARSE_ENDOBJ   = 64,
	PARSE_ENDARRAY = 128,
	PARSE_ERROR    = 256,
};

static void accumPush(STRPTR * accum, STRPTR mem, int length, int split)
{
	/* remove quote from string: not needed anymore */
	if (mem[0] == '\"')
		mem ++, length -= 2;

	STRPTR buffer = *accum;
	DATA16 header = (DATA16) buffer;
	int    add    = split ? 2 + length : length;

	if (buffer == NULL || header[1] + add > header[0])
	{
		int size = ((buffer ? header[0] : 0) + add + 2047) & ~2047;
		buffer = realloc(buffer, size);
		if (buffer == NULL) return;
		if (header == NULL) ((DATA16)buffer)[1] = 4;
		header = (DATA16) buffer;
		header[0] = size;
		*accum = buffer;
	}
	buffer += header[1];
	header[1] += add;
	if (! split)
	{
		add = length + LINE_LENGTH(buffer);
		buffer -= 2;
	}
	else add = length;
	memcpy(buffer, mem, length);
	buffer += length;
	buffer[0] = add & 0xff;
	buffer[1] = add >> 8;
}

STRPTR jsonValue(STRPTR * keys, STRPTR key)
{
	int i;
	for (i = 0; keys[i]; i += 2)
		if (strcasecmp(keys[i], key) == 0) return keys[i+1];
	return NULL;
}

Bool jsonParse(const char * file, JSONParseCb_t cb)
{
	STRPTR accum;
	char   buffer[256];
	int    nbKeys;
	int    expect;
	int    array;
	int    line;
	int    token;
	FILE * in;

	line   = 0;
	expect = PARSE_STARTOBJ;
	token  = PARSE_ERROR;
	accum  = NULL;
	nbKeys = 0;
	array  = 0;
	in     = fopen(file, "rb");

	if (in == NULL)
	{
		SIT_Log(SIT_ERROR, "Fail to open %s: %s", file, GetError());
		return False;
	}

	/* syntax is very to similar to the javascript object notation (NOT to be confused with JSON format) */
	while (fgets(buffer, sizeof buffer, in))
	{
		STRPTR p, ident;

		for (p = buffer, line ++; *p; )
		{
			if (token & PARSE_COMMENT)
			{
				while (*p && ! (p[0] == '*' && p[1] == '/')) p ++;
				if (p[0]) p += 2;
				if (p[0] == 0) break;
			}
			token = PARSE_ERROR;
			while (isspace(*p)) p ++;
			if (p[0] == 0) break;
			ident = p;

			/* lexical analyzer */
			if (('0' <= *p && *p <= '9') || *p == '-') /* number */
			{
				strtod(p, &p);
				if (p > ident)
				{
					token = PARSE_VALUE;
					while (isspace(*p)) p ++;
					if (*p == '+')
					{
						/* special array value */
						while (('A' <= *p && *p <= 'Z') || *p == '_' || *p == '+')
							p ++;
					}
				}
			}
			else if ('a' <= *p && *p <= 'z') /* identifier: must start with a lowercase letter */
			{
				/* first letter needs to be lower case */
				for (p ++; isalpha(*p) || *p == '_'; p ++);
				if (p > ident && *p == ':')
				{
					p ++;
					token = PARSE_IDENT;
				}
			}
			else if ('A' <= *p && *p <= 'Z') /* special constant: must start with a uppercase letter */
			{
				for (p ++; ('A' <= *p && *p <= 'Z') || ('0' <= *p && *p <= '9') || *p == '|' || *p == '_'; p ++);
				if (p > ident)
				{
					token = PARSE_VALUE;
					if (p - ident == 2 && strncmp(ident, "ID", 2) == 0)
					{
						/* readable number: convert it to raw number */
						int id, meta, n;
						if (sscanf(p, "(%d, %d%n", &id, &meta, &n) >= 2 && p[n] == ')')
						{
							STRPTR next = ident;
							p += n+1;
							next += sprintf(ident, "%d", (id << 4) | meta);
							while (next < p) *next++ = ' ';
						}
						else token = PARSE_ERROR;
					}
				}
			}
			else switch (*p) {
			case '{':
				token = PARSE_STARTOBJ;
				p ++;
				break;
			case '\"': /* string: must remain on one line */
				for (p ++; *p && *p != '\"'; p ++);
				if (*p == '\"')
				{
					p ++;
					token = PARSE_VALUE;
				}
				break;
			case '[':
				p ++;
				token = PARSE_ARRAY;
				break;
			case '/':
				if (p[1] == '*')
				{
					p += 2;
					token = PARSE_COMMENT;
				}
				else if (p[1] == '/')
				{
					/* line comment: ignore rest of line */
					*p = 0;
					continue;
				}
				break;
			case ',':
				p ++;
				token = PARSE_SEP;
				break;
			case ']':
				p ++;
				token = PARSE_ENDARRAY;
				break;
			case '}':
				if (p[1] == ',')
				{
					p ++;
					token = PARSE_ENDOBJ;
				}
			}

			/* grammatical analyzer */
			switch (token) {
			case PARSE_COMMENT:
				while (*p && ! (p[0] == '*' && p[1] == '/')) p ++;
				if (*p == 0) break;
				p += 2;
				token = 0;
				break;
			case PARSE_STARTOBJ:
				if (expect & PARSE_STARTOBJ)
				{
					if (accum)
						((DATA16)accum)[1] = 4;
					expect = PARSE_IDENT;
					nbKeys = 0;
				}
				else goto case_ERROR;
				break;
			case PARSE_ENDOBJ:
				if (expect & PARSE_ENDOBJ)
				{
					STRPTR * keys;
					STRPTR   table[33]; /* 16 key/value pairs per object, ought to be enough(TM) */
					STRPTR   header;

					/* convert accum into a key/value pairs table */
					for (keys = EOT(table)-1, *keys = NULL, header = accum + ((DATA16)accum)[1]; header > accum+4; )
					{
						int length = LINE_LENGTH(header) + 2;
						if (table == keys)
						{
							SIT_Log(SIT_ERROR, "%s: object with too many keys on line %d", file, line);
							return False;
						}
						keys --;
						*keys = header - length;
						header[-2] = 0;
						header -= length;
					}
					if ((keys - table) & 1)
						keys ++;
					if (! cb(file, keys, line))
						return False;
					expect = PARSE_SEP;
				}
				else goto case_ERROR;
				nbKeys = -1;
				break;
			case PARSE_IDENT:
				if (expect & PARSE_IDENT)
				{
					accumPush(&accum, ident, p - ident - 1, True);
					expect = PARSE_VALUE|PARSE_ARRAY;
					nbKeys ++;
				}
				else goto case_ERROR;
				break;
			case PARSE_SEP:
				if (expect & PARSE_SEP)
				{
					if (array)
					{
						expect = PARSE_VALUE;
						accumPush(&accum, ",", 1, False);
					}
					else expect = nbKeys < 0 ? PARSE_STARTOBJ :
					              nbKeys & 1 ? PARSE_VALUE|PARSE_ARRAY : PARSE_IDENT;
				}
				else goto case_ERROR;
				break;
			case PARSE_VALUE:
				if (expect & PARSE_VALUE)
				{
					if (array)
					{
						accumPush(&accum, ident, p - ident, False);
						expect = PARSE_SEP | PARSE_ENDARRAY;
					}
					else
					{
						accumPush(&accum, ident, p - ident, True);
						expect = PARSE_SEP | PARSE_ENDOBJ;
						nbKeys ++;
					}
				}
				else goto case_ERROR;
				break;
			case PARSE_ARRAY:
				if (expect & PARSE_ARRAY)
				{
					accumPush(&accum, "[", 1, True);
					array = 1;
					expect |= PARSE_VALUE;
				}
				else goto case_ERROR;
				break;
			case PARSE_ENDARRAY:
				array = 0;
				expect = PARSE_SEP | PARSE_ENDOBJ;
				nbKeys ++;
				break;
			case PARSE_ERROR:
			case_ERROR:
				*p = 0;
				if (expect & PARSE_VALUE) p = "value"; else
				if (expect & PARSE_IDENT) p = "identifier"; else
				if (expect & PARSE_SEP)   p = "separator"; else p = "";
				SIT_Log(SIT_ERROR, "%s: unexpected token %s on line %d, col %d (expected %s): aborting\n",
					file, ident, line, ident - buffer, p);
				return False;
			}
		}
	}
	free(accum);
	fclose(in);
	return True;
}

/* text must point to the first character (following the first double-quote) */
int jsonParseString(DATA8 dst, DATA8 src, int max)
{
	DATA8 s, d;
	for (s = src, d = dst; *s && *s != '\"' && max > 0; s ++, d ++, max --)
	{
		if (*s == '\\')
		{
			int i, cp;
			switch (s[1]) {
			case '\"': *d = '\"'; break;
			case '\\': *d = '\\'; break;
			case '/':  *d = '/';  break;
			case 'b':  *d = '\b'; break;
			case 'f':  *d = '\f'; break;
			case 'n':  *d = '\n'; break;
			case 'r':  *d = '\r'; break;
			case 't':  *d = '\t'; break;
			case 'u':
				/* unicode code point */
				for (i = cp = 0, s += 2; i < 4; i ++, s ++)
				{
					uint8_t chr = *s;
					if ('A' <= chr && chr <= 'Z') chr = 10 + (chr - 'A'); else
					if ('a' <= chr && chr <= 'z') chr = 10 + (chr - 'a'); else
					if ('0' <= chr && chr <= '9') chr -= '0'; else break;
					cp = (cp << 8) | chr;
				}
				d += CP2UTF8(d, cp) - 1;
				s --;
				break;
			default: /* invalid seq, but copy as is */
				*d = *s;
			}
		}
		else *d = *s;
	}
	if (max > 0) *d = 0;
	else d[-1] = 0;
	return d - dst;
}

/*
 * base64 encoder/decoder
 */
static uint8_t base64chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static uint8_t base64rev[128] = {
	0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,
	0,0,0,62,0,0,0,63,52,53,
	54,55,56,57,58,59,60,61,0,0,
	0,0,0,0,0,0,1,2,3,4,
	5,6,7,8,9,10,11,12,13,14,
	15,16,17,18,19,20,21,22,23,24,
	25,0,0,0,0,0,0,26,27,28,
	29,30,31,32,33,34,35,36,37,38,
	39,40,41,42,43,44,45,46,47,48,
	49,50,51
};

#define WORDWRAP        80  /* wrap base64 every WORDWRAP chars (need to be multiple of 4) */

/* give a slightly upper estimate of the size it would take to encode the number of <bytes> */
int base64EncodeLength(int bytes)
{
	/* size is deterministic and does not depend on source content: for every 3 input bytes => 4 output bytes */
	int size = (bytes + 2) * 4 / 3;

	/* we will also add newlines to prevent the string from being too long */
	return size + (size / WORDWRAP) + 2;
}

/* encode <source> as a base64 string */
int base64Encode(DATA8 dest, int dstMax, DATA8 source, int srcMax)
{
	DATA8 d, s;
	int   wrap;

	for (wrap = WORDWRAP, s = source, d = dest; srcMax > 0; srcMax -= 3, s += 3)
	{
		uint32_t triple;
		switch (srcMax) {
		default: /* 3 bytes or more */
			triple = (s[0] << 16) | (s[1] << 8) | s[2];
			*d ++ = base64chars[(triple >> 18) & 0x3f];
			*d ++ = base64chars[(triple >> 12) & 0x3f];
			*d ++ = base64chars[(triple >>  6) & 0x3f];
			*d ++ = base64chars[triple & 0x3f];
			break;
		case 2:
			triple = (s[0] << 16) | (s[1] << 8);
			*d ++ = base64chars[triple >> 18];
			*d ++ = base64chars[(triple >> 12) & 0x3f];
			*d ++ = base64chars[(triple >>  6) & 0x3f];
			*d ++ = '=';
			break;
		case 1:
			*d ++ = base64chars[s[0] >> 2];
			*d ++ = base64chars[(s[0] & 3) << 4];
			*d ++ = '=';
			*d ++ = '=';
		}

		wrap -= 4;
		if (wrap == 0)
			*d ++ = '\n', wrap = WORDWRAP;
	}
	if (d > dest && d[-1] != '\n')
		*d++ = '\n';
	return d - dest;
}

int base64Decode(DATA8 source, int length)
{
	DATA8 src, dst, max;

	for (src = dst = source, max = src + length; src < max; )
	{
		/* try to extract 4 bytes */
		uint8_t quad[4] = {0, 0, 0, 0}, i;
		for (i = 0; src < max && i < 4; src ++)
		{
			uint8_t chr = src[0];
			if ((chr & 0x80) == 0 && ((quad[i] = base64rev[chr]) > 0 || chr == 'A'))
				i ++;
		}

		*dst ++ = (quad[0] << 2) | (quad[1] >> 4);
		*dst ++ = (quad[1] << 4) | (quad[2] >> 2);
		*dst ++ = (quad[2] << 6) | quad[3];
	}
	return dst - source;
}

/* convert <src> into HTML stream suitable to be displayed, without overflowing <dest> */
void escapeHTML(DATA8 dest, int max, DATA8 src)
{
	DATA8 end = dest + max;
	DATA8 p   = strchr(dest, 0);
	DATA8 s;

	for (s = src; *s && p < end; s ++)
	{
		DATA8   text;
		uint8_t chr = *s;
		uint8_t len;
		switch (chr) {
		case '<': text = "&lt;"; len = 4; break;
		case '>': text = "&gt;"; len = 4; break;
		case '&': text = "&amp;"; len = 5; break;
		default:  text = &chr; len = 1;
		}
		if (p + len >= end)
		{
			memcpy(end-5, "...", 4);
			return;
		}
		else memcpy(p, text, len), p += len;
	}
	if (p < end-1) p[0] = 0;
	else p[-1] = 0;
}

/*
 * classical matrix/vector related operations
 */
void matTranspose(mat4 A)
{
	float tmp;

	tmp = A[A10]; A[A10] = A[A01]; A[A01] = tmp;
	tmp = A[A20]; A[A20] = A[A02]; A[A02] = tmp;
	tmp = A[A30]; A[A30] = A[A03]; A[A03] = tmp;
	tmp = A[A12]; A[A12] = A[A21]; A[A21] = tmp;
	tmp = A[A13]; A[A13] = A[A31]; A[A31] = tmp;
	tmp = A[A23]; A[A23] = A[A32]; A[A32] = tmp;
}

void matAdd(mat4 res, mat4 A, mat4 B)
{
	int i;
	for (i = 0; i < 16; i ++)
		res[i] = A[i] + B[i];
}

/* res = A x B */
void matMult(mat4 res, mat4 A, mat4 B)
{
	mat4 tmp;

	tmp[A00] = A[A00]*B[A00] + A[A01]*B[A10] + A[A02]*B[A20] + A[A03]*B[A30];
	tmp[A10] = A[A10]*B[A00] + A[A11]*B[A10] + A[A12]*B[A20] + A[A13]*B[A30];
	tmp[A20] = A[A20]*B[A00] + A[A21]*B[A10] + A[A22]*B[A20] + A[A23]*B[A30];
	tmp[A30] = A[A30]*B[A00] + A[A31]*B[A10] + A[A32]*B[A20] + A[A33]*B[A30];
	tmp[A01] = A[A00]*B[A01] + A[A01]*B[A11] + A[A02]*B[A21] + A[A03]*B[A31];
	tmp[A11] = A[A10]*B[A01] + A[A11]*B[A11] + A[A12]*B[A21] + A[A13]*B[A31];
	tmp[A21] = A[A20]*B[A01] + A[A21]*B[A11] + A[A22]*B[A21] + A[A23]*B[A31];
	tmp[A31] = A[A30]*B[A01] + A[A31]*B[A11] + A[A32]*B[A21] + A[A33]*B[A31];
	tmp[A02] = A[A00]*B[A02] + A[A01]*B[A12] + A[A02]*B[A22] + A[A03]*B[A32];
	tmp[A12] = A[A10]*B[A02] + A[A11]*B[A12] + A[A12]*B[A22] + A[A13]*B[A32];
	tmp[A22] = A[A20]*B[A02] + A[A21]*B[A12] + A[A22]*B[A22] + A[A23]*B[A32];
	tmp[A32] = A[A30]*B[A02] + A[A31]*B[A12] + A[A32]*B[A22] + A[A33]*B[A32];
	tmp[A03] = A[A00]*B[A03] + A[A01]*B[A13] + A[A02]*B[A23] + A[A03]*B[A33];
	tmp[A13] = A[A10]*B[A03] + A[A11]*B[A13] + A[A12]*B[A23] + A[A13]*B[A33];
	tmp[A23] = A[A20]*B[A03] + A[A21]*B[A13] + A[A22]*B[A23] + A[A23]*B[A33];
	tmp[A33] = A[A30]*B[A03] + A[A31]*B[A13] + A[A32]*B[A23] + A[A33]*B[A33];

	memcpy(res, tmp, sizeof tmp);
}

/* ame as matMult, but only consider 3x3 elements */
void matMult3(mat4 res, mat4 A, mat4 B)
{
	mat4 tmp;
	tmp[A00] = A[A00]*B[A00] + A[A01]*B[A10] + A[A02]*B[A20];
	tmp[A10] = A[A10]*B[A00] + A[A11]*B[A10] + A[A12]*B[A20];
	tmp[A20] = A[A20]*B[A00] + A[A21]*B[A10] + A[A22]*B[A20];
	tmp[A30] = 0;
	tmp[A01] = A[A00]*B[A01] + A[A01]*B[A11] + A[A02]*B[A21];
	tmp[A11] = A[A10]*B[A01] + A[A11]*B[A11] + A[A12]*B[A21];
	tmp[A21] = A[A20]*B[A01] + A[A21]*B[A11] + A[A22]*B[A21];
	tmp[A31] = 0;
	tmp[A02] = A[A00]*B[A02] + A[A01]*B[A12] + A[A02]*B[A22];
	tmp[A12] = A[A10]*B[A02] + A[A11]*B[A12] + A[A12]*B[A22];
	tmp[A22] = A[A20]*B[A02] + A[A21]*B[A12] + A[A22]*B[A22];
	tmp[A32] = 0;
	/* don't copy last column */
	memcpy(res, tmp, sizeof tmp - 16);
}

/* res = A x B */
void matMultByVec(vec4 res, mat4 A, vec4 B)
{
	vec4 tmp;

	tmp[VX] = A[A00]*B[VX] + A[A01]*B[VY] + A[A02]*B[VZ] + A[A03]*B[VT];
	tmp[VY] = A[A10]*B[VX] + A[A11]*B[VY] + A[A12]*B[VZ] + A[A13]*B[VT];
	tmp[VZ] = A[A20]*B[VX] + A[A21]*B[VY] + A[A22]*B[VZ] + A[A23]*B[VT];
	tmp[VT] = A[A30]*B[VX] + A[A31]*B[VY] + A[A32]*B[VZ] + A[A33]*B[VT];

	memcpy(res, tmp, sizeof tmp);
}

/* assume translation vector is 0 */
void matMultByVec3(vec4 res, mat4 A, vec4 B)
{
	vec4 tmp;

	tmp[VX] = A[A00]*B[VX] + A[A01]*B[VY] + A[A02]*B[VZ];
	tmp[VY] = A[A10]*B[VX] + A[A11]*B[VY] + A[A12]*B[VZ];
	tmp[VZ] = A[A20]*B[VX] + A[A21]*B[VY] + A[A22]*B[VZ];

	memcpy(res, tmp, 3 * sizeof (float));
}

/*
 * taken from glm library: convert a matrix intended for vertex so that it can be applied to a vector
 * (has to ignore translation); note: normalization will still be required if used on a normal.
 */
void matInverseTranspose(mat4 res, mat4 m)
{
	float SubFactor00 = m[A22] * m[A33] - m[A32] * m[A23];
	float SubFactor01 = m[A21] * m[A33] - m[A31] * m[A23];
	float SubFactor02 = m[A21] * m[A32] - m[A31] * m[A22];
	float SubFactor03 = m[A20] * m[A33] - m[A30] * m[A23];
	float SubFactor04 = m[A20] * m[A32] - m[A30] * m[A22];
	float SubFactor05 = m[A20] * m[A31] - m[A30] * m[A21];
	float SubFactor06 = m[A12] * m[A33] - m[A32] * m[A13];
	float SubFactor07 = m[A11] * m[A33] - m[A31] * m[A13];
	float SubFactor08 = m[A11] * m[A32] - m[A31] * m[A12];
	float SubFactor09 = m[A10] * m[A33] - m[A30] * m[A13];
	float SubFactor10 = m[A10] * m[A32] - m[A30] * m[A12];
	float SubFactor11 = m[A10] * m[A31] - m[A30] * m[A11];
	float SubFactor12 = m[A12] * m[A23] - m[A22] * m[A13];
	float SubFactor13 = m[A11] * m[A23] - m[A21] * m[A13];
	float SubFactor14 = m[A11] * m[A22] - m[A21] * m[A12];
	float SubFactor15 = m[A10] * m[A23] - m[A20] * m[A13];
	float SubFactor16 = m[A10] * m[A22] - m[A20] * m[A12];
	float SubFactor17 = m[A10] * m[A21] - m[A20] * m[A11];

	mat4 Inverse;
	Inverse[A00] = + (m[A11] * SubFactor00 - m[A12] * SubFactor01 + m[A13] * SubFactor02);
	Inverse[A01] = - (m[A10] * SubFactor00 - m[A12] * SubFactor03 + m[A13] * SubFactor04);
	Inverse[A02] = + (m[A10] * SubFactor01 - m[A11] * SubFactor03 + m[A13] * SubFactor05);
	Inverse[A03] = - (m[A10] * SubFactor02 - m[A11] * SubFactor04 + m[A12] * SubFactor05);
	Inverse[A10] = - (m[A01] * SubFactor00 - m[A02] * SubFactor01 + m[A03] * SubFactor02);
	Inverse[A11] = + (m[A00] * SubFactor00 - m[A02] * SubFactor03 + m[A03] * SubFactor04);
	Inverse[A12] = - (m[A00] * SubFactor01 - m[A01] * SubFactor03 + m[A03] * SubFactor05);
	Inverse[A13] = + (m[A00] * SubFactor02 - m[A01] * SubFactor04 + m[A02] * SubFactor05);
	Inverse[A20] = + (m[A01] * SubFactor06 - m[A02] * SubFactor07 + m[A03] * SubFactor08);
	Inverse[A21] = - (m[A00] * SubFactor06 - m[A02] * SubFactor09 + m[A03] * SubFactor10);
	Inverse[A22] = + (m[A00] * SubFactor07 - m[A01] * SubFactor09 + m[A03] * SubFactor11);
	Inverse[A23] = - (m[A00] * SubFactor08 - m[A01] * SubFactor10 + m[A02] * SubFactor11);
	Inverse[A30] = - (m[A01] * SubFactor12 - m[A02] * SubFactor13 + m[A03] * SubFactor14);
	Inverse[A31] = + (m[A00] * SubFactor12 - m[A02] * SubFactor15 + m[A03] * SubFactor16);
	Inverse[A32] = - (m[A00] * SubFactor13 - m[A01] * SubFactor15 + m[A03] * SubFactor17);
	Inverse[A33] = + (m[A00] * SubFactor14 - m[A01] * SubFactor16 + m[A02] * SubFactor17);

	float Determinant =
		+ m[A00] * Inverse[A00]
		+ m[A01] * Inverse[A01]
		+ m[A02] * Inverse[A02]
		+ m[A03] * Inverse[A03];

	int i;
	for (i = 0; i < 16; i ++)
		Inverse[i] /= Determinant;

	memcpy(res, Inverse, sizeof Inverse);
}

/* taken from Mesa3d */
Bool matInverse(mat4 res, mat4 m)
{
	float det;
	mat4  inv;
	int   i;

	inv[0] = m[5]  * m[10] * m[15] -
	         m[5]  * m[11] * m[14] -
	         m[9]  * m[6]  * m[15] +
	         m[9]  * m[7]  * m[14] +
	         m[13] * m[6]  * m[11] -
	         m[13] * m[7]  * m[10];

    inv[4] = -m[4]  * m[10] * m[15] +
	          m[4]  * m[11] * m[14] +
	          m[8]  * m[6]  * m[15] -
	          m[8]  * m[7]  * m[14] -
	          m[12] * m[6]  * m[11] +
	          m[12] * m[7]  * m[10];

    inv[8] = m[4]  * m[9] * m[15] -
	         m[4]  * m[11] * m[13] -
	         m[8]  * m[5] * m[15] +
	         m[8]  * m[7] * m[13] +
	         m[12] * m[5] * m[11] -
	         m[12] * m[7] * m[9];

    inv[12] = -m[4]  * m[9] * m[14] +
	           m[4]  * m[10] * m[13] +
	           m[8]  * m[5] * m[14] -
	           m[8]  * m[6] * m[13] -
	           m[12] * m[5] * m[10] +
	           m[12] * m[6] * m[9];

    inv[1] = -m[1]  * m[10] * m[15] +
	          m[1]  * m[11] * m[14] +
	          m[9]  * m[2] * m[15] -
	          m[9]  * m[3] * m[14] -
	          m[13] * m[2] * m[11] +
	          m[13] * m[3] * m[10];

    inv[5] = m[0]  * m[10] * m[15] -
	         m[0]  * m[11] * m[14] -
	         m[8]  * m[2] * m[15] +
	         m[8]  * m[3] * m[14] +
	         m[12] * m[2] * m[11] -
	         m[12] * m[3] * m[10];

    inv[9] = -m[0]  * m[9] * m[15] +
	          m[0]  * m[11] * m[13] +
	          m[8]  * m[1] * m[15] -
	          m[8]  * m[3] * m[13] -
	          m[12] * m[1] * m[11] +
	          m[12] * m[3] * m[9];

    inv[13] = m[0]  * m[9] * m[14] -
	          m[0]  * m[10] * m[13] -
	          m[8]  * m[1] * m[14] +
	          m[8]  * m[2] * m[13] +
	          m[12] * m[1] * m[10] -
	          m[12] * m[2] * m[9];

    inv[2] = m[1]  * m[6] * m[15] -
	         m[1]  * m[7] * m[14] -
	         m[5]  * m[2] * m[15] +
	         m[5]  * m[3] * m[14] +
	         m[13] * m[2] * m[7] -
	         m[13] * m[3] * m[6];

    inv[6] = -m[0]  * m[6] * m[15] +
	          m[0]  * m[7] * m[14] +
	          m[4]  * m[2] * m[15] -
	          m[4]  * m[3] * m[14] -
	          m[12] * m[2] * m[7] +
	          m[12] * m[3] * m[6];

    inv[10] = m[0]  * m[5] * m[15] -
	          m[0]  * m[7] * m[13] -
	          m[4]  * m[1] * m[15] +
	          m[4]  * m[3] * m[13] +
	          m[12] * m[1] * m[7] -
	          m[12] * m[3] * m[5];

    inv[14] = -m[0]  * m[5] * m[14] +
	           m[0]  * m[6] * m[13] +
	           m[4]  * m[1] * m[14] -
	           m[4]  * m[2] * m[13] -
	           m[12] * m[1] * m[6] +
	           m[12] * m[2] * m[5];

    inv[3] = -m[1] * m[6] * m[11] +
	          m[1] * m[7] * m[10] +
	          m[5] * m[2] * m[11] -
	          m[5] * m[3] * m[10] -
	          m[9] * m[2] * m[7] +
	          m[9] * m[3] * m[6];

    inv[7] = m[0] * m[6] * m[11] -
	         m[0] * m[7] * m[10] -
	         m[4] * m[2] * m[11] +
	         m[4] * m[3] * m[10] +
	         m[8] * m[2] * m[7] -
	         m[8] * m[3] * m[6];

    inv[11] = -m[0] * m[5] * m[11] +
	           m[0] * m[7] * m[9] +
	           m[4] * m[1] * m[11] -
	           m[4] * m[3] * m[9] -
	           m[8] * m[1] * m[7] +
	           m[8] * m[3] * m[5];

    inv[15] = m[0] * m[5] * m[10] -
	          m[0] * m[6] * m[9] -
	          m[4] * m[1] * m[10] +
	          m[4] * m[2] * m[9] +
	          m[8] * m[1] * m[6] -
	          m[8] * m[2] * m[5];

	det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];

	if (det == 0)
		return False;

	det = 1.0f / det;

	for (i = 0; i < 16; i++)
        res[i] = inv[i] * det;

	return True;
}

/* perspective projection (https://www.khronos.org/registry/OpenGL-Refpages/gl2.1/xhtml/gluPerspective.xml) */
void matPerspective(mat4 res, float fov_deg, float aspect, float znear, float zfar)
{
	memset(res, 0, sizeof (mat4));
	float q = 1 / tanf(fov_deg * DEG_TO_RAD * 0.5f);
	res[A00] = q / aspect;
	res[A11] = q;
	res[A22] = (znear + zfar) / (znear - zfar);
	res[A23] = 2 * znear * zfar / (znear - zfar);
	res[A32] = -1;
}

/* orthographic projection */
void matOrtho(mat4 res, float left, float right, float bottom, float top, float znear, float zfar)
{
	memset(res, 0, sizeof (mat4));
	res[A00] = 2 / (right - left);
	res[A11] = 2 / (top - bottom);
	res[A22] = 1 / (zfar - znear);
	res[A03] = - (right + left) / (right - left);
	res[A13] = - (top + bottom) / (top - bottom);
	res[A23] = - znear / (zfar - znear);
	res[A33] = 1;
}

/* similar to gluLookAt */
void matLookAt(mat4 res, vec4 eye, vec4 center, vec4 up)
{
	vec4 fwd = {center[VX] - eye[VX], center[VY] - eye[VY], center[VZ] - eye[VZ]};
	vec4 side;

	memset(res, 0, sizeof (mat4));

	vecNormalize(fwd, fwd);
	vecCrossProduct(side, fwd, up);
	vecNormalize(side, side);
	vecCrossProduct(up, side, fwd);
	vecNormalize(up, up);

	/* from book */
	res[A00] = side[VX];
	res[A01] = side[VY];
	res[A02] = side[VZ];
	res[A03] = - vecDotProduct(side, eye);
	res[A10] = up[VX];
	res[A11] = up[VY];
	res[A12] = up[VZ];
	res[A13] = - vecDotProduct(up, eye);
	res[A20] = -fwd[VX];
	res[A21] = -fwd[VY];
	res[A22] = -fwd[VZ];
	res[A23] = vecDotProduct(fwd, eye);
	res[A33] = 1;
}

/* generate a transformation matrix in res */
void matIdent(mat4 res)
{
	memset(res, 0, sizeof (mat4));
	res[A00] = res[A11] = res[A22] = res[A33] = 1;
}

void matTranslate(mat4 res, float x, float y, float z)
{
	matIdent(res);
	res[A03] = x;
	res[A13] = y;
	res[A23] = z;
}

void matScale(mat4 res, float x, float y, float z)
{
	memset(res, 0, sizeof *res);
	res[A00] = x;
	res[A11] = y;
	res[A22] = z;
	res[A33] = 1;
}

void matRotate(mat4 res, float theta, int axis_0X_1Y_2Z)
{
	float fcos = cosf(theta);
	float fsin = sinf(theta);
	matIdent(res);
	switch (axis_0X_1Y_2Z) {
	case 0: /* along X axis */
		res[A11] = fcos;
		res[A21] = fsin;
		res[A12] = -fsin;
		res[A22] = fcos;
		break;
	case 1: /* along Y axis */
		res[A00] = fcos;
		res[A20] = -fsin;
		res[A02] = fsin;
		res[A22] = fcos;
		break;
	case 2: /* along Z axis */
		res[A00] = fcos;
		res[A10] = fsin;
		res[A01] = -fsin;
		res[A11] = fcos;
	}
}

void matPrint(mat4 A)
{
	static uint8_t num[] = {0, 4, 8, 12, 1, 5, 9, 13, 2, 6, 10, 14, 3, 7, 11, 15};
	int i;
	fputc('[', stderr);
	for (i = 0; i < 16; i ++)
	{
		fprintf(stderr, "\t%g", (double) A[num[i]]);
		if ((i & 3) == 3) fputc('\n', stderr);
	}
	fputs("];\n", stderr);
}

/*
 * classical vector operations
 */
void vecAdd(vec4 res, vec4 A, vec4 B)
{
	res[VX] = A[VX] + B[VX];
	res[VY] = A[VY] + B[VY];
	res[VZ] = A[VZ] + B[VZ];
}

void vecSub(vec4 res, vec4 A, vec4 B)
{
	res[VX] = A[VX] - B[VX];
	res[VY] = A[VY] - B[VY];
	res[VZ] = A[VZ] - B[VZ];
}

float vecLength(vec4 A)
{
	return sqrtf(A[VX]*A[VX] + A[VY]*A[VY] + A[VZ]*A[VZ]);
}

float vecDistSquare(vec4 A, vec4 B)
{
	vec4 diff;
	diff[VX] = A[VX] - B[VX];
	diff[VY] = A[VY] - B[VY];
	diff[VZ] = A[VZ] - B[VZ];
	return diff[VX] * diff[VX] + diff[VY] * diff[VY] + diff[VZ] * diff[VZ];
}


void vecNormalize(vec4 res, vec4 A)
{
	float len = vecLength(A);
	res[VX] = A[VX] / len;
	res[VY] = A[VY] / len;
	res[VZ] = A[VZ] / len;
}

float vecDotProduct(vec4 A, vec4 B)
{
	return A[VX]*B[VX] + A[VY]*B[VY] + A[VZ]*B[VZ];
}

/* get perpendicular vector to A and B */
void vecCrossProduct(vec4 res, vec4 A, vec4 B)
{
	vec4 tmp;

	tmp[VX] = A[VY]*B[VZ] - A[VZ]*B[VY];
	tmp[VY] = A[VZ]*B[VX] - A[VX]*B[VZ];
	tmp[VZ] = A[VX]*B[VY] - A[VY]*B[VX];

	memcpy(res, tmp, 12);
}

/* keep angle between 0 and 2*M_PI */
float normAngle(float angle)
{
	if (angle < 0) angle += 2*M_PIf; else
	if (angle >= 2*M_PIf) angle -= 2*M_PIf;
	if (angle == -0.0f) angle = 0;
	return angle;
}

/* compiler without builtin support for popcount */
#ifndef popcount
int popcount(uint32_t x)
{
	/* taken from https://github.com/BartMassey/popcount, function popcount_2() */
	uint32_t m1 = 0x55555555;
	uint32_t m2 = 0x33333333;
	uint32_t m4 = 0x0f0f0f0f;
	x -= (x >> 1) & m1;
	x = (x & m2) + ((x >> 2) & m2);
	x = (x + (x >> 4)) & m4;
	x += x >>  8;
	return (x + (x >> 16)) & 0x3f;
}
#endif

static uint16_t primes[] = {
	11,   23,   43,   71,   97,
	113, 149,  173,  193,  251,
	307, 353,  401,  457,  557,
	659, 769, 1009, 1543, 3079,
	6151, 12289, 24593, 49193, 65521,
};

int roundToUpperPrime(int n)
{
	int i;

	/* get nearest prime number */
	for (i = 0; i < DIM(primes); i ++)
		if (primes[i] >= n) return primes[i];

	return n;
}

int roundToLowerPrime(int n)
{
	int i;
	for (i = 0; i < DIM(primes) && primes[i] < n; i ++);
	if (i == DIM(primes)) return n;
	if (i == 0 || n == primes[i]) return primes[i];
	return primes[i-1];
}

void DOS2Unix(STRPTR path)
{
	while ((path = strchr(path, '\\')))
		*path ++ = '/';
}

/*
 * dynamic loading of opengl functions needed for this program (only a subset from glad.h)
 */
#define UNICODE
#include <windows.h>

PFNGLGETSHADERINFOLOGPROC glad_glGetShaderInfoLog;
PFNGLGETPROGRAMINFOLOGPROC glad_glGetProgramInfoLog;
PFNGLCREATEPROGRAMPROC glad_glCreateProgram;
PFNGLCREATESHADERPROC glad_glCreateShader;
PFNGLSHADERSOURCEPROC glad_glShaderSource;
PFNGLCOMPILESHADERPROC glad_glCompileShader;
PFNGLGETSHADERIVPROC glad_glGetShaderiv;
PFNGLATTACHSHADERPROC glad_glAttachShader;
PFNGLBINDATTRIBLOCATIONPROC glad_glBindAttribLocation;
PFNGLLINKPROGRAMPROC glad_glLinkProgram;
PFNGLGETPROGRAMIVPROC glad_glGetProgramiv;
PFNGLDELETEPROGRAMPROC glad_glDeleteProgram;
PFNGLDELETESHADERPROC glad_glDeleteShader;
PFNGLGETUNIFORMLOCATIONPROC glad_glGetUniformLocation;
PFNGLGETUNIFORMBLOCKINDEXPROC glad_glGetUniformBlockIndex;
PFNGLGENVERTEXARRAYSPROC glad_glGenVertexArrays;
PFNGLGENBUFFERSPROC glad_glGenBuffers;
PFNGLUNIFORMBLOCKBINDINGPROC glad_glUniformBlockBinding;
PFNGLGENERATEMIPMAPPROC glad_glGenerateMipmap;
PFNGLUSEPROGRAMPROC glad_glUseProgram;
PFNGLACTIVETEXTUREPROC glad_glActiveTexture;
PFNGLBINDBUFFERPROC glad_glBindBuffer;
PFNGLBUFFERDATAPROC glad_glBufferData;
PFNGLBINDVERTEXARRAYPROC glad_glBindVertexArray;
PFNGLENABLEVERTEXATTRIBARRAYPROC glad_glEnableVertexAttribArray;
PFNGLVERTEXATTRIBPOINTERPROC glad_glVertexAttribPointer;
PFNGLUNIFORM1IPROC glad_glUniform1i;
PFNGLUNIFORM2FVPROC glad_glUniform2fv;
PFNGLDELETEBUFFERSPROC glad_glDeleteBuffers;
PFNGLDELETEVERTEXARRAYSPROC glad_glDeleteVertexArrays;
PFNGLMAPBUFFERRANGEPROC glad_glMapBufferRange;
PFNGLSCISSORPROC glad_glScissor;

PFNGLBINDTEXTUREPROC glad_glBindTexture;
PFNGLDELETETEXTURESPROC glad_glDeleteTextures;
PFNGLGETERRORPROC glad_glGetError;
PFNGLGETINTEGERVPROC glad_glGetIntegerv;
PFNGLGENTEXTURESPROC glad_glGenTextures;
PFNGLPIXELSTOREIPROC glad_glPixelStorei;
PFNGLTEXIMAGE2DPROC glad_glTexImage2D;
PFNGLTEXPARAMETERIPROC glad_glTexParameteri;
PFNGLTEXSUBIMAGE2DPROC glad_glTexSubImage2D;
PFNGLENABLEPROC glad_glEnable;
PFNGLDISABLEPROC glad_glDisable;
PFNGLDRAWARRAYSPROC glad_glDrawArrays;
PFNGLCULLFACEPROC glad_glCullFace;
PFNGLFRONTFACEPROC glad_glFrontFace;
PFNGLVIEWPORTPROC glad_glViewport;
PFNGLCLEARCOLORPROC glad_glClearColor;
PFNGLCLEARPROC glad_glClear;
PFNGLGETSTRINGPROC glad_glGetString;
PFNGLMULTIDRAWARRAYSPROC glad_glMultiDrawArrays;
PFNGLPOLYGONOFFSETPROC glad_glPolygonOffset;
PFNGLGETTEXIMAGEPROC glad_glGetTexImage;

PFNGLBUFFERSUBDATAPROC glad_glBufferSubData;
PFNGLDEBUGMESSAGECALLBACKPROC glad_glDebugMessageCallback;
PFNGLVERTEXATTRIBIPOINTERPROC glad_glVertexAttribIPointer;
PFNGLVERTEXATTRIBDIVISORPROC glad_glVertexAttribDivisor;
PFNGLBINDBUFFERBASEPROC glad_glBindBufferBase;
PFNGLPROGRAMUNIFORM4FVPROC glad_glProgramUniform4fv;
PFNGLPROGRAMUNIFORM3FVPROC glad_glProgramUniform3fv;
PFNGLPROGRAMUNIFORM1FVPROC glad_glProgramUniform1fv;
PFNGLPROGRAMUNIFORM1UIPROC glad_glProgramUniform1ui;
PFNGLDRAWELEMENTSPROC glad_glDrawElements;
PFNGLPOLYGONMODEPROC glad_glPolygonMode;
PFNGLBLENDFUNCPROC glad_glBlendFunc;
PFNGLDEPTHFUNCPROC glad_glDepthFunc;
PFNGLMULTIDRAWARRAYSINDIRECTPROC glad_glMultiDrawArraysIndirect;
PFNGLREADPIXELSPROC glad_glReadPixels;
PFNGLDEPTHMASKPROC glad_glDepthMask;
PFNGLGETBUFFERSUBDATAPROC glad_glGetBufferSubData;
PFNGLMAPBUFFERPROC glad_glMapBuffer;
PFNGLUNMAPBUFFERPROC glad_glUnmapBuffer;
PFNGLCOPYTEXIMAGE2DPROC glad_glCopyTexImage2D;
PFNGLREADBUFFERPROC glad_glReadBuffer;
PFNGLDRAWARRAYSINSTANCEDPROC glad_glDrawArraysInstanced;
PFNGLGENFRAMEBUFFERSPROC glad_glGenFramebuffers;
PFNGLBINDFRAMEBUFFERPROC glad_glBindFramebuffer;
PFNGLFRAMEBUFFERTEXTURE2DPROC glad_glFramebufferTexture2D;

typedef void* (APIENTRYP PFNGLXGETPROCADDRESSPROC_PRIVATE)(const char*);
PFNGLXGETPROCADDRESSPROC_PRIVATE gladGetProcAddressPtr;

HANDLE opengl;
void * load(const char * func)
{
	void * result = NULL;
	if (gladGetProcAddressPtr)
		result = gladGetProcAddressPtr(func);
	if (result == NULL)
		result = GetProcAddress(opengl, func);
	return result;
}

int gladLoadGL(void)
{
	opengl = LoadLibrary(L"opengl32.dll");

	if (opengl)
	{
		STRPTR name;
		/* note: opengl context needs to be created before */
		gladGetProcAddressPtr = (void *) GetProcAddress(opengl, "wglGetProcAddress");
		if ((glad_glGetString          = load(name = "glGetString"))
		 && (glad_glGetShaderInfoLog   = load(name = "glGetShaderInfoLog"))
		 && (glad_glGetProgramInfoLog  = load(name = "glGetProgramInfoLog"))
		 && (glad_glCreateProgram      = load(name = "glCreateProgram"))
		 && (glad_glCreateShader       = load(name = "glCreateShader"))
		 && (glad_glShaderSource       = load(name = "glShaderSource"))
		 && (glad_glCompileShader      = load(name = "glCompileShader"))
		 && (glad_glGetShaderiv        = load(name = "glGetShaderiv"))
		 && (glad_glAttachShader       = load(name = "glAttachShader"))
		 && (glad_glBindAttribLocation = load(name = "glBindAttribLocation"))
		 && (glad_glLinkProgram        = load(name = "glLinkProgram"))
		 && (glad_glGetProgramiv       = load(name = "glGetProgramiv"))
		 && (glad_glDeleteProgram      = load(name = "glDeleteProgram"))
		 && (glad_glDeleteShader       = load(name = "glDeleteShader"))
		 && (glad_glGetUniformLocation = load(name = "glGetUniformLocation"))
		 && (glad_glGenVertexArrays    = load(name = "glGenVertexArrays"))
		 && (glad_glGenBuffers         = load(name = "glGenBuffers"))
		 && (glad_glGenerateMipmap     = load(name = "glGenerateMipmap"))
		 && (glad_glUseProgram         = load(name = "glUseProgram"))
		 && (glad_glActiveTexture      = load(name = "glActiveTexture"))
		 && (glad_glBindBuffer         = load(name = "glBindBuffer"))
		 && (glad_glBufferData         = load(name = "glBufferData"))
		 && (glad_glBindVertexArray    = load(name = "glBindVertexArray"))
		 && (glad_glUniform1i          = load(name = "glUniform1i"))
		 && (glad_glUniform2fv         = load(name = "glUniform2fv"))
		 && (glad_glDeleteBuffers      = load(name = "glDeleteBuffers"))
		 && (glad_glDeleteVertexArrays = load(name = "glDeleteVertexArrays"))
		 && (glad_glBindTexture        = load(name = "glBindTexture"))
		 && (glad_glDeleteTextures     = load(name = "glDeleteTextures"))
		 && (glad_glGetError           = load(name = "glGetError"))
		 && (glad_glGetIntegerv        = load(name = "glGetIntegerv"))
		 && (glad_glScissor            = load(name = "glScissor"))
		 && (glad_glGenTextures        = load(name = "glGenTextures"))
		 && (glad_glPixelStorei        = load(name = "glPixelStorei"))
		 && (glad_glTexImage2D         = load(name = "glTexImage2D"))
		 && (glad_glTexParameteri      = load(name = "glTexParameteri"))
		 && (glad_glTexSubImage2D      = load(name = "glTexSubImage2D"))
		 && (glad_glEnable             = load(name = "glEnable"))
		 && (glad_glDisable            = load(name = "glDisable"))
		 && (glad_glDrawArrays         = load(name = "glDrawArrays"))
		 && (glad_glCullFace           = load(name = "glCullFace"))
		 && (glad_glFrontFace          = load(name = "glFrontFace"))
		 && (glad_glViewport           = load(name = "glViewport"))
		 && (glad_glClearColor         = load(name = "glClearColor"))
		 && (glad_glClear              = load(name = "glClear"))
		 && (glad_glBufferSubData      = load(name = "glBufferSubData"))
		 && (glad_glDrawElements       = load(name = "glDrawElements"))
		 && (glad_glPolygonMode        = load(name = "glPolygonMode"))
		 && (glad_glBlendFunc          = load(name = "glBlendFunc"))
		 && (glad_glDepthFunc          = load(name = "glDepthFunc"))
		 && (glad_glReadPixels         = load(name = "glReadPixels"))
		 && (glad_glBindBufferBase     = load(name = "glBindBufferBase"))
		 && (glad_glDepthMask          = load(name = "glDepthMask"))
		 && (glad_glGetBufferSubData   = load(name = "glGetBufferSubData"))
		 && (glad_glMapBuffer          = load(name = "glMapBuffer"))
		 && (glad_glUnmapBuffer        = load(name = "glUnmapBuffer"))
		 && (glad_glCopyTexImage2D     = load(name = "glCopyTexImage2D"))
		 && (glad_glReadBuffer         = load(name = "glReadBuffer"))
		 && (glad_glPolygonOffset      = load(name = "glPolygonOffset"))
		 && (glad_glMultiDrawArrays    = load(name = "glMultiDrawArrays"))
		 && (glad_glGetTexImage        = load(name = "glGetTexImage"))
		 && (glad_glMapBufferRange     = load(name = "glMapBufferRange"))
		 && (glad_glGetUniformBlockIndex     = load(name = "glGetUniformBlockIndex"))
		 && (glad_glUniformBlockBinding      = load(name = "glUniformBlockBinding"))
		 && (glad_glEnableVertexAttribArray  = load(name = "glEnableVertexAttribArray"))
		 && (glad_glVertexAttribPointer      = load(name = "glVertexAttribPointer"))
		 && (glad_glDebugMessageCallback     = load(name = "glDebugMessageCallback"))
		 && (glad_glVertexAttribIPointer     = load(name = "glVertexAttribIPointer"))
		 && (glad_glVertexAttribDivisor      = load(name = "glVertexAttribDivisor"))
		 && (glad_glProgramUniform4fv        = load(name = "glProgramUniform4fv"))
		 && (glad_glProgramUniform3fv        = load(name = "glProgramUniform3fv"))
		 && (glad_glProgramUniform1fv        = load(name = "glProgramUniform1fv"))
		 && (glad_glProgramUniform1ui        = load(name = "glProgramUniform1ui"))
		 && (glad_glDrawArraysInstanced      = load(name = "glDrawArraysInstanced"))
		 && (glad_glMultiDrawArraysIndirect  = load(name = "glMultiDrawArraysIndirect"))
		 && (glad_glFramebufferTexture2D     = load(name = "glFramebufferTexture2D"))
		 && (glad_glGenFramebuffers          = load(name = "glGenFramebuffers"))
		 && (glad_glBindFramebuffer          = load(name = "glBindFramebuffer")))
		{
			return 1;
		}
		fprintf(stderr, "fail to load function '%s'\n", name);
	}
	return 0;
}
