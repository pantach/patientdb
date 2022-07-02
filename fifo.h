#ifndef FIFO_H
#define FIFO_H

void  write_fifo(int fd, const char* msg, size_t bufsize);
ssize_t read_fifo(int fd, char** msg, size_t bufsize);
void  write_fifo_raw(int fd, const void* mem, size_t memsize, size_t bufsize);
ssize_t read_fifo_raw(int fd, void* mem, size_t bufsize);

#endif
