// stb_include.h - v0.01 - parse and process #include directives - public domain
//
// To build this, in one source file that includes this file do
//      #define STB_INCLUDE_IMPLEMENTATION
//
// This program parses a string and replaces lines of the form
//         #include "foo"
// with the contents of a file named "foo". It also embeds the
// appropriate #line directives. Note that all include files must
// reside in the location specified in the path passed to the API;
// it does not check multiple directories.
//
// If the string contains a line of the form
//         #inject
// then it will be replaced with the contents of the string 'inject' passed to the API.
//
// Options:
//
//      Define STB_INCLUDE_LINE_GLSL to get GLSL-style #line directives
//      which use numbers instead of filenames.
//
//      Define STB_INCLUDE_LINE_NONE to disable output of #line directives.
//
// Standard libraries:
//
//      stdio.h     FILE, fopen, fclose, fseek, ftell
//      stdlib.h    malloc, realloc, free
//      string.h    strcpy, strncmp, memcpy

#ifndef STB_INCLUDE_STB_INCLUDE_H
#define STB_INCLUDE_STB_INCLUDE_H

// Do include-processing on the string 'str'. To free the return value, pass it to free()
char *stb_include_string(char *str, const char *inject, char *path_to_includes, const char *filename_for_line_directive, char error[256]);

// Load the file 'filename' and do include-processing on the string therein. note that
// 'filename' is opened directly; 'path_to_includes' is not used. To free the return value, pass it to free()
char *stb_include_file(const char *filename, const char *inject, char error[256]);

#endif


#ifdef STB_INCLUDE_IMPLEMENTATION

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char * stb_include_load_file(const char *filename, size_t * plen)
{
	char *text;
	size_t len;
	FILE *f = fopen(filename, "rb");
	if (f == 0) return 0;
	fseek(f, 0, SEEK_END);
	len = ftell(f);
	if (plen) *plen = len;
	text = malloc(len+1);
	if (text == 0) return 0;
	fseek(f, 0, SEEK_SET);
	fread(text, 1, len, f);
	fclose(f);
	text[len] = 0;
	return text;
}

typedef struct
{
	void * next;
	int    offset;
	int    end;
	int    next_line;
	char   filename[1];

}	include_info;

static void stb_include_append_include(include_info ** list, int len, int offset, int end, char * filename, int next_line)
{
	include_info * info = malloc(sizeof *info + (filename ? strlen(filename) : 0));
	include_info * last;

	info->next      = NULL;
	info->offset    = offset;
	info->end       = end;
	info->next_line = next_line;
	if (filename)
		strcpy(info->filename, filename);
	else
		info->filename[0] = 0;

	for (last = *list; last && last->next; last = last->next);
	if (last) last->next = info;
	else *list = info;
}

static int stb_include_isspace(int ch)
{
	return (ch == ' ' || ch == '\t' || ch == '\r' || ch == 'n');
}

// find location of all #include and #inject
static include_info * stb_include_find_includes(char * text)
{
	include_info * list = NULL;
	int            line_count = 1;
	int            inc_count  = 0;
	char *         s = text;
	char *         start;

	while (*s)
	{
		// parse is always at start of line when we reach here
		start = s;
		while (*s == ' ' || *s == '\t')
			++s;
		if (*s == '#')
		{
			++s;
			while (*s == ' ' || *s == '\t')
				++s;

			if (0 == strncmp(s, "include", 7) && stb_include_isspace(s[7]))
			{
				s += 7;
				while (*s == ' ' || *s == '\t')
					++s;
				if (*s == '"')
				{
					char * t = ++s;
					while (*t != '"' && *t != '\n' && *t != '\r' && *t != 0)
						++t;
					if (*t == '"')
					{
						for (*t++ = 0; *t != '\r' && *t != '\n' && *t != 0; ++t);
						// t points to the newline, so t-start is everything except the newline
						stb_include_append_include(&list, inc_count++, start-text, t-text, s, line_count+1);
					}
				}
			}
			else if (0 == strncmp(s, "inject", 6) && (stb_include_isspace(s[6]) || s[6] == 0))
			{
				while (*s != '\r' && *s != '\n' && *s != 0)
					++s;
				stb_include_append_include(&list, inc_count++, start-text, s-text, NULL, line_count+1);
			}
		}
		while (*s != '\r' && *s != '\n' && *s != 0)
			++s;
		if (*s == '\r' || *s == '\n') {
			s = s + (s[0] + s[1] == '\r' + '\n' ? 2 : 1);
		}
		++line_count;
	}
	return list;
}

#ifndef STB_INCLUDE_LINE_NONE
// avoid dependency on sprintf()
static void stb_include_itoa(char * str, int n)
{
	char temp[10];
	int  i;

	while (*str) str ++;

	for (i = 0; n > 0; i ++, n /= 10)
		temp[i] = n % 10 + '0';

	for (str[i] = 0, i --; i >= 0; *str = temp[i], i --, str ++);
}
#endif

static char *stb_include_append(char *str, size_t *curlen, const char *addstr, size_t addlen)
{
	str = (char *) realloc(str, *curlen + addlen);
	memcpy(str + *curlen, addstr, addlen);
	*curlen += addlen;
	return str;
}

char * stb_include_string(char *str, const char *inject, char *path_to_includes, const char *filename, char error[256])
{
	include_info * inc_list;
	char           temp[1024];
	size_t         source_len = strlen(str);
	char *         text = 0;
	size_t         textlen = 0, last = 0;

	inc_list = stb_include_find_includes(str);

	while (inc_list)
	{
		include_info * next = inc_list->next;
		text = stb_include_append(text, &textlen, str+last, inc_list->offset - last);
		// write out line directive for the include
		#ifndef STB_INCLUDE_LINE_NONE
		#ifdef STB_INCLUDE_LINE_GLSL
		if (textlen != 0)  // GLSL #version must appear first, so don't put a #line at the top
		#endif
		{
			strcpy(temp, "#line ");
			stb_include_itoa(temp+6, 1);
			strcat(temp, " ");
			#ifdef STB_INCLUDE_LINE_GLSL
			stb_include_itoa(temp+6, i+1);
			#else
			strcat(temp, "\"");
			if (inc_list->filename[0] == 0)
				strcat(temp, "INJECT");
			else
				strcat(temp, inc_list->filename);
			strcat(temp, "\"");
			#endif
			strcat(temp, "\n");
			text = stb_include_append(text, &textlen, temp, strlen(temp));
		}
		#endif
		if (inc_list->filename[0] == 0)
		{
			if (inject != 0)
				text = stb_include_append(text, &textlen, inject, strlen(inject));
		}
		else // parse include file in case there is other #include directives
		{
			char * inc;
			strcpy(temp, path_to_includes);
			strcat(temp, "/");
			strcat(temp, inc_list->filename);
			inc = stb_include_file(temp, inject, error);
			if (inc == NULL)
			{
				do
				{
					next = inc_list->next;
					free(inc_list);
					inc_list = next;
				} while (next);
				return NULL;
			}
			text = stb_include_append(text, &textlen, inc, strlen(inc));
			free(inc);
		}
		// write out line directive
		#ifndef STB_INCLUDE_LINE_NONE
		strcpy(temp, "\n#line ");
		stb_include_itoa(temp+6, inc_list->next_line);
		strcat(temp, " ");
		#ifdef STB_INCLUDE_LINE_GLSL
		stb_include_itoa(temp+6, 0);
		#else
		strcat(temp, filename != 0 ? filename : "source-file");
		#endif
		text = stb_include_append(text, &textlen, temp, strlen(temp));
		// no newlines, because we kept the #include newlines, which will get appended next
		#endif
		last = inc_list->end;
		free(inc_list);
		inc_list = next;
	}
	text = stb_include_append(text, &textlen, str+last, source_len - last + 1); // append '\0'
	return text;
}

char * stb_include_file(const char * filename, const char *inject, char error[256])
{
	size_t len = strlen(filename);
	char * path = alloca(len+1);
	char * text = stb_include_load_file(filename, &len);
	char * result;

	/* use same dir as filename */
	strcpy(path, filename);
	for (result = strchr(path, 0)-1; result > path && *result != '/' && *result != '\\'; result --);
	if (result > path) *result = 0;

	if (text == NULL)
	{
		strcpy(error, "Error: couldn't load '");
		strcat(error, filename);
		strcat(error, "'");
		return 0;
	}
	result = stb_include_string(text, inject, path, filename, error);
	free(text);
	return result;
}

#endif // STB_INCLUDE_IMPLEMENTATION
