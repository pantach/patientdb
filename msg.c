#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "tools.h"
#include "msg.h"

#define HEADER_SIZE sizeof(size_t)
#define BUFSIZE 1024

static void   _write_msg(int fd, const void* mem, size_t memsize);
static ssize_t _read_msg(int fd, void* mem, size_t memsize);

void write_msg(int fd, const char* msg)
{
	size_t bodylen = strlen(msg) +1;

	// Write header (size)
	_write_msg(fd, &bodylen, HEADER_SIZE);
	// Write body (msg)
	_write_msg(fd, msg, bodylen);
}

static void _write_msg(int fd, const void* mem, size_t memsize)
{
	ssize_t bwritten;

	bwritten = write(fd, mem, memsize);
	if (bwritten == -1)
		syserr_exit("write() failure");
	if (bwritten < memsize)
		err_exit("Partial write");
}

ssize_t read_msg(int fd, char** msg)
{
	size_t bodylen;
	ssize_t bread;

	// Read header (size)
	_read_msg(fd, &bodylen, HEADER_SIZE);

	*msg = xmalloc(bodylen);

	bread = _read_msg(fd, *msg, bodylen);
	bread -= 1;

	// If the empty string was read, free it and return zero
	if (bread == 0) {
		free(*msg);
		*msg = NULL;
	}

	return bread;
}

static ssize_t _read_msg(int fd, void* mem, size_t memsize)
{
	char buf[BUFSIZE];
	ssize_t bread;
	ssize_t bread_sum = 0;
	size_t bufsize;

	bufsize = (memsize <= BUFSIZE) ? memsize : BUFSIZE;

	while ((bread = read(fd, buf, bufsize))) {
		if (bread == -1)
			syserr_exit("read() failure");

		memcpy(mem +bread_sum, buf, bread);
		bread_sum += bread;

		if (bread_sum == memsize)
			break;

		bufsize = (bufsize <= memsize -bread_sum) ? bufsize : memsize -bread_sum;
	}

	if (bread_sum < memsize)
		err_exit("Partial read");

	return bread_sum;
}

/*
static ssize_t _read_msg(int fd, void* mem, size_t memsize)
{
	ssize_t bread;
	ssize_t bread_sum = 0;

	while (bread = read(fd, mem, memsize) != -1) {
	}
	if (bread == -1)
		syserr_exit("read() failure");
	if (bread < memsize)
		err_exit("Partial read");

	return bread;
}
*/
