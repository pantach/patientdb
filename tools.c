#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <dirent.h>
#include <sys/types.h>
#include "tools.h"

void* xmalloc(size_t size)
{
	void* ptr = malloc(size);
	if (!ptr)
		abort();

	return ptr;
};

void* xcalloc(size_t num, size_t size)
{
	void* ptr;

	ptr = calloc(num, size);
	if (!ptr)
		abort();

	return ptr;
}

void* memdup(const void* src, size_t n)
{
	char* dest = xmalloc(n);

	return memcpy(dest, src, n);
}

char* xstrdup(const char* str)
{
	char*  str2 = NULL;
	size_t len;

	if (str) {
		len  = strlen(str);
		str2 = xmalloc(len +1);
		memcpy(str2, str, len +1);
	}

	return str2;
}

void xstrcat(char** str1, const char* str2)
{
	size_t len1;
	size_t len2;
	char*  cat;

	len1 = (*str1 == NULL) ? 0 : strlen(*str1);
	len2 = strlen(str2);

	cat = xmalloc(len1 +len2 +1);

	if (*str1)
		memcpy(cat, *str1, len1);
	memcpy(cat +len1, str2, len2);
	cat[len1 +len2] = '\0';

	free(*str1);
	*str1 = cat;
}

void err_exit(const char* format, ...)
{
	va_list args;
	char nformat[1024];

	strncpy(nformat, format, 1023);
	strcat (nformat, "\n");

	va_start(args, format);
	vfprintf(stderr, nformat, args);
	va_end(args);

	fflush(stdout);
	exit(EXIT_FAILURE);
}

void syserr_exit(const char* format, ...)
{
	va_list args;
	char usermsg[1024];
	int errno_copy = errno;

	va_start(args, format);
	vsnprintf(usermsg, 1024, format, args);
	fprintf(stderr, "%s: %s\n", usermsg, strerror(errno_copy));
	va_end(args);

	fflush(stdout);
	exit(EXIT_FAILURE);
}

int xsprintf(char** str, const char* format, ...)
{
	va_list args;
	int len;
	int wlen;

	va_start(args, format);
	len = vsnprintf(NULL, 0, format, args);
	va_end(args);

	*str = xmalloc(len +1);

	va_start(args, format);
	wlen = vsnprintf(*str, len +1, format, args);
	va_end(args);

	return wlen;
}

int getint(const char* numstr, int flags, ...)
{
	va_list args;
	long num = 0;
	char* endptr;
	Getint_err  err  = 0;
	Getint_err* perr = NULL;

	if (flags & GETINT_NOEXIT) {
		va_start(args, flags);
		perr = va_arg(args, Getint_err*);
		va_end(args);
	}

	if (numstr) {
		errno = 0;
		num = strtol(numstr, &endptr, 10);
		if (errno)
			err = GETINT_ESTRTOL;

		else if (endptr == numstr)
			err = GETINT_EEMPTSTR;

		else if (*endptr != '\0')
			err = GETINT_ENONNUM;
	}
	else
		err = GETINT_ENULLSTR;

	if (flags ^ GETINT_NOEXIT) {
		switch (err) {
		case GETINT_ENULLSTR:
			err_exit("getint: Null input string");
		case GETINT_EEMPTSTR:
			err_exit("getint: Empty input string");
		case GETINT_ESTRTOL:
			syserr_exit("getint: strtol failed");
		case GETINT_ENONNUM:
			err_exit("getint: Non-numeric characters: %s", numstr);
		default:
			break;
		}
	}

	if (perr)
		*perr = err;

	return num;
}

Vector* tokenize(const char* str, const char* delim)
{
	Vector* v;
	char*  nstr;
	char*  pch;

	v = vector_init();
	if (!v)
		abort();

	nstr = xstrdup(str);

	pch = strtok(nstr, delim);
	while (pch) {
		vector_append(v, xstrdup(pch));
		pch = strtok(NULL, delim);
	}

	free(nstr);

	return v;
}

size_t segmem(const void* mem, size_t memsize, void* segm, size_t segmsize_max)
{
	static size_t offset  = 0;
	static void*  prevmem = NULL;
	char* pmem;
	size_t segmsize;

	if (mem != prevmem) {
		prevmem = (void*)mem;
		offset = 0;
	}

	pmem = &((char*)mem)[offset];

	if (offset +segmsize_max <= memsize)
		segmsize = segmsize_max;
	else
		segmsize = memsize -offset;

	if (segmsize) {
		memcpy(segm, pmem, segmsize);
		offset += segmsize;
	}

	return segmsize;
}

Vector* getdir(const char* path, int flags)
{
	const int pathlen = strlen(path);
	DIR* dir;
	struct dirent* dir_entry;
	Vector* entries;

	if ((entries = vector_init()) == NULL)
		abort();

	dir = opendir(path);
	if (!dir)
		syserr_exit("Cannot open \"%s\"", path);

	errno = 0;
	while ((dir_entry = readdir(dir))) {

		if (!strcmp(dir_entry->d_name, ".") || !strcmp(dir_entry->d_name, ".."))
			continue;

		if (flags & GETDIR_FULLPATH) {
			char* d_name = xmalloc(pathlen +strlen(dir_entry->d_name) +2);
			strcpy(d_name, path);
			strcat(d_name, "/");
			strcat(d_name, dir_entry->d_name);
			vector_append(entries, d_name);
		}
		else
			vector_append(entries, xstrdup(dir_entry->d_name));
	}
	if (errno)
		syserr_exit("Cannot read \"%s\"", path);

	closedir(dir);

	return entries;
}

char* string_arr_flatten(char** arr, size_t* len, size_t arrsize)
{
	size_t strlen_sum = 0;
	size_t endoffs = 0;
	size_t* plen;
	char* res;
	int i;

	plen = len;
	if (!plen) {
		plen = xmalloc(arrsize * sizeof(*len));

		for (i = 0; i < arrsize; ++i)
			plen[i] = strlen(arr[i]);
	}

	for (i = 0; i < arrsize; ++i)
		strlen_sum += plen[i];

	res = xmalloc(strlen_sum +1);

	for (i = 0; i < arrsize; ++i) {
		memcpy(&res[endoffs], arr[i], plen[i]);
		endoffs += plen[i];
	}
	res[endoffs] = '\0';

	if (plen != len)
		free(plen);

	return res;
}
