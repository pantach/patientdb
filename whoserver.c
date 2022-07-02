#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "cirq_buffer.h"
#include "tools.h"
#include "msg.h"
#include "vector.h"
#include "command.h"

#define BACKLOG 128

struct CLA {
	int query_port;
	int stats_port;
	int nthreads;
	int buffer_size;
};

typedef enum {
	QUERY = 0,
	STATS,
} Conn_type;

typedef struct {
	Conn_type type;
	int fd;
	struct sockaddr_in addr;
	socklen_t addrlen;
} Conn;

struct conn_handler_data {
	Cirq_buffer* conns;
	Vector* worker_addrs;
};

void print_usage(char* progname);
void parse_cla(int argc, char** argv);

Conn* conn_init(Conn_type type);
void  conn_free(Conn* conn);
void* conn_handler(void* data);
void  conn_stats_handler(Conn* conn, Vector* worker_addrs);
void  conn_query_handler(Conn* conn, Vector* worker_addrs);

int create_socket(int port);
struct sockaddr_in create_addr(int port);

void signal_handler(int signum);
void sigact(void);

// Globals
struct CLA g_cla;
pthread_mutex_t mutex_print = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_buf   = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  cond_avail  = PTHREAD_COND_INITIALIZER;
int g_avail;
int g_sigint;

void print_usage(char* progname)
{
	fprintf(stderr, "%s –q queryPort -s statisticsPort –w numThreads –b bufferSize\n",
	        progname);
	exit(EXIT_FAILURE);
}

void parse_cla(int argc, char** argv)
{
	if (argc != 9)
		print_usage(argv[0]);

	for (int i = 1; i < argc; ++i) {
		if (!strcmp(argv[i], "-q"))
			g_cla.query_port  = getint(argv[++i], 0);

		else if (!strcmp(argv[i], "-s"))
			g_cla.stats_port  = getint(argv[++i], 0);

		else if (!strcmp(argv[i], "-w"))
			g_cla.nthreads    = getint(argv[++i], 0);

		else if (!strcmp(argv[i], "-b"))
			g_cla.buffer_size = getint(argv[++i], 0);

		else {
			fprintf(stderr, "Unknown argument %s:\n", argv[i]);
			print_usage(argv[0]);
		}
	}

	if (g_cla.nthreads <= 0)
		err_exit("Invalid number of threads");

	if (g_cla.buffer_size <= 0)
		err_exit("Invalid buffer size");
}

void signal_handler(int signum)
{
	if (signum == SIGINT)
		g_sigint = 1;
}

void sigact(void)
{
	sigset_t maskset;
	struct sigaction sigact;

	sigemptyset(&maskset);
	sigaddset(&maskset, SIGINT);

	sigact.sa_handler = signal_handler;
	sigact.sa_mask  = maskset;
	sigact.sa_flags = 0;

	sigaction(SIGINT, &sigact, NULL);
}

int create_socket(int port)
{
	struct sockaddr_in addr;
	int fd;

	if ((fd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
		syserr_exit("socket()");

	addr = create_addr(port);

	if (bind(fd, (struct sockaddr *)&addr, (socklen_t)sizeof(addr)))
		syserr_exit("bind()");

	if (listen(fd, BACKLOG))
		syserr_exit("listen()");

	return fd;
}

struct sockaddr_in create_addr(int port)
{
	struct sockaddr_in addr;

	memset(&addr, 0, sizeof(addr));

	addr.sin_family = AF_INET;
	addr.sin_port   = htons(port);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	return addr;
}

int main(int argc, char** argv)
{
	struct conn_handler_data data;
	Cirq_buffer* cb;
	Vector* v;
	Conn* conn;
	fd_set srv_fds;
	int srv_fd[2];
	int i;

	sigact();
	parse_cla(argc, argv);

	srv_fd[QUERY] = create_socket(g_cla.query_port);
	srv_fd[STATS] = create_socket(g_cla.stats_port);

	cb = cirq_buffer_init(g_cla.buffer_size);
	v  = vector_init();

	data.conns = cb;
	data.worker_addrs = v;

	pthread_t thread[g_cla.nthreads];

	for (i = 0; i < g_cla.nthreads; ++i)
		if (pthread_create(&thread[i], NULL, conn_handler, &data))
			syserr_exit("pthread_create()");

	for (;;) {
		FD_ZERO(&srv_fds);
		FD_SET(srv_fd[QUERY], &srv_fds);
		FD_SET(srv_fd[STATS], &srv_fds);

		if (select(srv_fd[STATS] +1, &srv_fds, NULL, NULL, NULL) == -1) {
			if (g_sigint) break;

			syserr_exit("select()");
		}

		for (i = 0; i < 2; ++i) {
			if (FD_ISSET(srv_fd[i], &srv_fds)) {

				conn = conn_init(i);

				conn->fd = accept(srv_fd[i], (struct sockaddr*)&conn->addr,
				                  &conn->addrlen);
				if (conn->fd == -1)
					syserr_exit("accept()");

				pthread_mutex_lock(&mutex_buf);

				if (!cirq_buffer_push(cb, conn)) {
					char* errmsg = "Circular buffer full. Closing connection...\n";

					pthread_mutex_lock(&mutex_print);
					fprintf(stderr, "%s\n", errmsg);
					pthread_mutex_unlock(&mutex_print);

					write_msg(conn->fd, errmsg);
					write_msg(conn->fd, "");

					conn_free(conn);
					pthread_mutex_unlock(&mutex_buf);
					continue;
				}
				g_avail++;

				pthread_mutex_unlock(&mutex_buf);

				pthread_cond_signal(&cond_avail);
			}
		}
	}

	pthread_cond_broadcast(&cond_avail);

	for (i = 0; i < g_cla.nthreads; ++i)
		if (pthread_join(thread[i], NULL))
			syserr_exit("pthread_join()");

	cirq_buffer_free(cb);
	vector_free(v, free);
	close(srv_fd[QUERY]);
	close(srv_fd[STATS]);

	return 0;
}

Conn* conn_init(Conn_type type)
{
	Conn* conn = xmalloc(sizeof(*conn));

	conn->type = type;
	conn->addrlen = (socklen_t)sizeof(conn->addr);

	return conn;
}

void conn_free(Conn* conn)
{
	close(conn->fd);
	free(conn);
}

void* conn_handler(void* data)
{
	struct conn_handler_data* chdata = data;
	Vector* worker_addrs = chdata->worker_addrs;
	Cirq_buffer* conns   = chdata->conns;
	Conn* conn;

	for (;;) {
		pthread_mutex_lock(&mutex_buf);
		while (g_avail == 0) {
			if (g_sigint == 1) {
				pthread_mutex_unlock(&mutex_buf);
				return NULL;
			}

			pthread_cond_wait(&cond_avail, &mutex_buf);
		}

		conn = cirq_buffer_pop(conns);
		if (!conn)
			err_exit("Expected non-empty buffer");

		g_avail--;
		pthread_mutex_unlock(&mutex_buf);


		if (conn->type == QUERY)
			conn_query_handler(conn, worker_addrs);

		else if (conn->type == STATS)
			conn_stats_handler(conn, worker_addrs);

		else
			assert(0);

		conn_free(conn);
	}
}

void conn_query_handler(Conn* conn, Vector* worker_addrs)
{
	struct sockaddr_in* waddr;
	size_t waddrlen;
	int wfd;
	Command* command;
	Vector* cmdarg;
	char* cmdname;
	char* query;
	char* reply = NULL;
	char dss_freq_str[32];
	int dss_freq_sum = 0;
	int dss_freq;
	int i;

	read_msg(conn->fd, &query);

	cmdarg  = tokenize(query, " \n");
	cmdname = vector_get(cmdarg, 0);

	// Non-empty command
	if (cmdname) {
		pthread_mutex_lock(&mutex_print);
		printf("%s", query);

		command = get_command(cmdname);

		if (!command)
			reply = "Unknown command\n";

		else if (cmdarg->size < command->mandargs)
			reply = "Please provide all the necessary arguments\n";

		else {
			for (i = 0; i < worker_addrs->size; ++i) {
				waddr    = worker_addrs->entry[i];
				waddrlen = sizeof(*waddr);

				if ((wfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
					syserr_exit("socket()");

				if (connect(wfd, (struct sockaddr*)waddr, (socklen_t)waddrlen)) {
					// Worker exited?
					if (errno == ECONNREFUSED) {
						close(wfd);
						continue;
					}
					else
						syserr_exit("connect()");
				}

				write_msg(wfd, query);
				read_msg(wfd, &reply);

				if (command->val == DISEASE_FREQUENCY) {
					dss_freq = getint(reply, 0);
					if (dss_freq != -1)
						dss_freq_sum += dss_freq;
				}
				else if (reply) {
					printf("%s", reply);
					write_msg(conn->fd, reply);
				}
				free(reply);
				reply = NULL;

				close(wfd);
			}

			if (command->val == DISEASE_FREQUENCY) {
				snprintf(dss_freq_str, sizeof(dss_freq_str) -2, "%d\n", dss_freq_sum);
				reply = dss_freq_str;
			}
		}

		if (reply) {
			write_msg(conn->fd, reply);
			printf("%s", reply);
		}

		printf("\n");
		pthread_mutex_unlock(&mutex_print);
	}
	write_msg(conn->fd, "");
	vector_free(cmdarg, free);
	free(query);
}

void conn_stats_handler(Conn* conn, Vector* worker_addrs)
{
	struct sockaddr_in* worker_addr;
	char* msg;
	char port_str[7];
	int  port;

	pthread_mutex_lock(&mutex_print);
	while (read_msg(conn->fd, &msg)) {

		if (!strncmp(msg, "PORT:", 5)) {
			strcpy(port_str, &msg[5]);
			port = getint(port_str, 0);

			worker_addr  = xmalloc(sizeof(*worker_addr));
			*worker_addr = conn->addr;
			worker_addr->sin_port = htons(port);
			vector_append(worker_addrs, worker_addr);
		}
		else {
			// printf("%s", msg);
		}

		free(msg);
	}
	pthread_mutex_unlock(&mutex_print);
}
