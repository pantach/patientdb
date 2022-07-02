#ifndef MSG_H
#define MSG_H

void   write_msg(int fd, const char* msg);
ssize_t read_msg(int fd, char** msg);

#endif
