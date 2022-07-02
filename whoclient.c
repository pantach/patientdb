#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "tools.h"
#include "msg.h"

struct CLA {
	char* query_filepath;
	int nthreads;
	char* srv_ip;
	int srv_port;
};

struct handler_data {
	struct sockaddr_in srv_addr;
	char* query;
};

void print_usage(char* progname);
void parse_cla(int argc, char** argv);
void* query_handler(void* query);

// Globals
struct CLA g_cla;
pthread_mutex_t mutex_print = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  cond  = PTHREAD_COND_INITIALIZER;
bool g_start = false;

void print_usage(char* progname)
{
	fprintf(stderr, "%s –q queryFile -w numThreads –sip servIP –sp servPort\n", progname);
	exit(EXIT_FAILURE);
}

void parse_cla(int argc, char** argv)
{
	if (argc != 9)
		print_usage(argv[0]);

	for (int i = 1; i < argc; ++i) {
		if (!strcmp(argv[i], "-q"))
			g_cla.query_filepath = argv[++i];

		else if (!strcmp(argv[i], "-w"))
			g_cla.nthreads = getint(argv[++i], 0);

		else if (!strcmp(argv[i], "-sip"))
			g_cla.srv_ip   = argv[++i];

		else if (!strcmp(argv[i], "-sp"))
			g_cla.srv_port = getint(argv[++i], 0);

		else {
			fprintf(stderr, "Unknown argument %s:\n", argv[i]);
			print_usage(argv[0]);
		}
	}

	if (g_cla.nthreads <= 0)
		err_exit("Invalid number of threads");
}

int main(int argc, char** argv)
{
	struct sockaddr_in srv_addr;
	struct in_addr srv_ip;
	FILE*  query_file;
	char*  query = NULL;
	size_t query_size = 0;
	int i, j;

	parse_cla(argc, argv);

	// Open query file
	query_file = fopen(g_cla.query_filepath, "r");
	if (!query_file)
		syserr_exit("Cannot open \"%s\"", g_cla.query_filepath);

	// Convert IP
	if (!inet_pton(AF_INET, g_cla.srv_ip, &srv_ip))
		err_exit("Invalid IP address");

	// Initialize server address
	srv_addr.sin_family = AF_INET;
	srv_addr.sin_port   = htons(g_cla.srv_port);
	srv_addr.sin_addr   = srv_ip;

	// Read query file and create threads
	struct handler_data data[g_cla.nthreads];
	pthread_t         thread[g_cla.nthreads];

	for (i = 0; getline(&query, &query_size, query_file) != -1; ++i) {

		data[i].srv_addr = srv_addr;
		data[i].query    = xstrdup(query);

		if (pthread_create(&thread[i], NULL, query_handler, &data[i]))
			syserr_exit("pthread_create()");

		// Commence execution of threads
		if (i == g_cla.nthreads -1) {
			pthread_mutex_lock(&mutex);
			g_start = true;
			pthread_mutex_unlock(&mutex);

			pthread_cond_broadcast(&cond);

			for (j = 0; j <= i; ++j) {
				pthread_join(thread[j], NULL);
				free(data[j].query);
			}

			i = -1;
			g_start = false;
		}
	}

	pthread_mutex_lock(&mutex);
	g_start = true;
	pthread_mutex_unlock(&mutex);

	pthread_cond_broadcast(&cond);

	for (j = 0; j < i; ++j) {
		pthread_join(thread[j], NULL);
		free(data[j].query);
	}

	free(query);
	fclose(query_file);

	return 0;
}

void* query_handler(void* data)
{
	struct handler_data* d = data;
	int cln_fd;
	char* reply;

	pthread_mutex_lock(&mutex);

	while (g_start == false)
		pthread_cond_wait(&cond, &mutex);

	pthread_mutex_unlock(&mutex);

	if ((cln_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
		syserr_exit("socket()");

	if (connect(cln_fd, (struct sockaddr *)&d->srv_addr, (socklen_t)sizeof(d->srv_addr)))
		syserr_exit("connect()");

	write_msg(cln_fd, d->query);

	pthread_mutex_lock(&mutex_print);
	printf("%s", d->query);
	while (read_msg(cln_fd, &reply)) {
		printf("%s", reply);
		free(reply);
	}
	printf("\n");
	pthread_mutex_unlock(&mutex_print);

	close(cln_fd);

	return NULL;
}
