#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "tools.h"
#include "fifo.h"

#define HEADER_SIZE sizeof(size_t)

static void   _write_fifo(int fd, const void* mem, size_t memsize, size_t bufsize);
static ssize_t _read_fifo(int fd, void* mem, size_t memsize, size_t bufsize);

/*
#include <stdio.h>
int main(void)
{
	char* fifo = "testfifo";
	int bufsize = 6;
	int rfd;
	int wfd;
	int pid;
	int i;

	if ((mkfifo(fifo, 0775) == -1))
		syserr_exit("mkfifo() failure");

	switch ((pid = fork())) {
	case -1:
		syserr_exit("fork() failure");

	case 0:
		if ((rfd = open(fifo, O_RDONLY)) == -1)
			syserr_exit("open() read failure");

		char* msg;
		size_t bread;
		long long l1;

		while (bread = read_fifo(rfd, &msg, bufsize)) {
			printf("Read %ld: %s\n", bread, msg);
			free(msg);
		}

		bread = read_fifo_raw(rfd, &l1, bufsize);
		printf("Read %ld: %lld\n", bread, l1);

		if (close(rfd) == -1)
			syserr_exit("close() failure");

		_exit(0);

	default:
		if ((wfd = open(fifo, O_WRONLY)) == -1)
			syserr_exit("open() write failure");

		char* str[] = {
			"This long string should be passed over a fifo for reading",
			"Another string",
			"One more string containing numbers: 123456789",
			"a",
			""};
		long long l2 = 1234567890;

		for (i = 0; i < 5; ++i) {
			write_fifo(wfd, str[i], bufsize);
			printf("Wrote %ld\n", strlen(str[i]));
		}

		write_fifo_raw(wfd, &l2, sizeof(l2), bufsize);
		printf("Wrote %ld\n", (sizeof(l2)));

		if (close(wfd) == -1)
			syserr_exit("close() failure");
	}

	if (unlink(fifo) == -1)
		syserr_exit("unlink() failure");

}
*/

void write_fifo(int fd, const char* msg, size_t bufsize)
{
	size_t bodylen = strlen(msg) +1;

	// Write header (size)
	_write_fifo(fd, &bodylen, HEADER_SIZE, bufsize);
	// Write body (msg)
	_write_fifo(fd, msg, bodylen, bufsize);
}

void write_fifo_raw(int fd, const void* mem, size_t memsize, size_t bufsize)
{
	// Write header (size)
	_write_fifo(fd, &memsize, HEADER_SIZE, bufsize);
	// Write body (msg)
	_write_fifo(fd, mem, memsize, bufsize);
}

static void _write_fifo(int fd, const void* mem, size_t memsize, size_t bufsize)
{
	char buf[bufsize];
	size_t segmsize;
	ssize_t bwritten;

	while ((segmsize = segmem(mem, memsize, buf, bufsize))) {
		bwritten = write(fd, buf, segmsize);
		if (bwritten == -1)
			syserr_exit("write() failure");
		if (bwritten < segmsize)
			err_exit("Partial write");
	}
}

ssize_t read_fifo(int fd, char** msg, size_t bufsize)
{
	size_t bodylen;
	ssize_t bread;

	// Read header (size)
	bread = _read_fifo(fd, &bodylen, HEADER_SIZE, bufsize);
	if (bread == -1)
		return -1;

	*msg = xmalloc(bodylen);

	bread = _read_fifo(fd, *msg, bodylen, bufsize);
	if (bread == -1) {
		free(*msg);
		return -1;
	}

	bread -= 1;

	// If the empty string was read, free it and return zero
	if (bread == 0)
		free(*msg);

	return bread;
}

ssize_t read_fifo_raw(int fd, void* mem, size_t bufsize)
{
	size_t bodylen;
	size_t bread;

	// Read header (size)
	_read_fifo(fd, &bodylen, HEADER_SIZE, bufsize);

	// Read body (msg)
	bread = _read_fifo(fd, mem, bodylen, bufsize);

	return bread;
}

static ssize_t _read_fifo(int fd, void* mem, size_t memsize, size_t bufsize)
{
	char buf[bufsize];
	ssize_t bread;
	ssize_t bread_sum = 0;

	bufsize = (memsize <= bufsize) ? memsize : bufsize;

	while ((bread = read(fd, buf, bufsize))) {
		if (bread == -1) {
			if (errno == EINTR)
				return -1;
			else
				syserr_exit("read() failure");
		}

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
