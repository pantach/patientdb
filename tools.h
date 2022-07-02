#ifndef TOOLS_H
#define TOOLS_H

#include <stdarg.h>
#include "vector.h"

void* xmalloc(size_t size);
void* xcalloc(size_t num, size_t size);
void* memdup(const void* src, size_t n);
char* xstrdup(const char* str);
void  xstrcat(char** str1, const char* str2);

void err_exit(const char* format, ...);
void syserr_exit(const char* format, ...);

int xsprintf(char** str, const char* format, ...);

#define GETINT_NOFLAGS 0
#define GETINT_NOEXIT  1

typedef enum {
	GETINT_ESUCCESS =  0,
	GETINT_ENULLSTR = -1,
	GETINT_EEMPTSTR = -2,
	GETINT_ESTRTOL  = -3,
	GETINT_ENONNUM  = -4
} Getint_err;

int getint(const char* numstr, int flags, ...);

#define GETDIR_DEFAULT  0
#define GETDIR_FULLPATH 1

Vector* getdir(const char* path, int flags);

Vector* tokenize(const char* str, const char* delim);
size_t  segmem(const void* mem, size_t memsize, void* segm, size_t segmsize_max);
char*   string_arr_flatten(char** arr, size_t* len, size_t arrsize);


#endif
