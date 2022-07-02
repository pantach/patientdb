#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <libgen.h>
#include <signal.h>
#include "patient.h"
#include "tools.h"
#include "vector.h"
#include "list.h"
#include "hashtable.h"
#include "fifo.h"
#include "msg.h"
#include "command.h"

#define BACKLOG 128
#define WORKER_FIFO_TEMPLATE "wfifo_%d"
#define FIFO_TEMPLATE_LEN (sizeof(WORKER_FIFO_TEMPLATE) +16)

struct CLA {
	int workers_num;
	int buffer_size;
	char* input_dir;
	char* srv_ip;
	int   srv_port;
};

typedef struct {
	char* name;
	bool parsed;
} Record_file;

static void print_usage(char* progname);
static void parse_cla(int argc, char** argv);

static void  worker(const char* fifo);
static char* worker_generate_stats(List* patients);

static void  update_recordfiles(Vector* rec_files, const char* country);
static char* parse_recordfiles(Vector* rec_files, PatientDB* db);
static void  free_recordfiles(Vector* rec_files);
static void  recordfile_free(Record_file* r);
static inline void recordfile_free_generic(void* r);
static int   recordfile_date_comp(const void* v1, const void* v2);
static int   recordfile_name_comp(const void* v1, const void* v2);

static void parent_signal_handler(int signum);
static void worker_signal_handler(int signum);
static void parent_sigact(void);
static void worker_sigact(void);

// Globals

struct CLA g_cla;
int g_termed_pid;
volatile sig_atomic_t parent_sigint;
volatile sig_atomic_t parent_sigquit;
volatile sig_atomic_t parent_sigchld;

volatile sig_atomic_t worker_sigint;
volatile sig_atomic_t worker_sigquit;
volatile sig_atomic_t worker_sigusr1;

static void print_usage(char* progname)
{
	fprintf(stderr, "%s –w numWorkers -b bufferSize –s serverIP –p serverPort -i "
	        "input_dir\n", progname);
	exit(EXIT_FAILURE);
}

static void parse_cla(int argc, char** argv)
{
	int i;

	if (argc < 7)
		print_usage(argv[0]);

	for (i = 1; i < argc; ++i) {
		if (!strcmp(argv[i], "-w"))
			g_cla.workers_num = getint(argv[++i], 0);

		else if (!strcmp(argv[i], "-b"))
			g_cla.buffer_size = getint(argv[++i], 0);

		else if (!strcmp(argv[i], "-i"))
			g_cla.input_dir = argv[++i];

		else if (!strcmp(argv[i], "-s"))
			g_cla.srv_ip = argv[++i];

		else if (!strcmp(argv[i], "-p"))
			g_cla.srv_port = getint(argv[++i], 0);

		else {
			fprintf(stderr, "Unknown argument %s:\n", argv[i]);
			print_usage(argv[0]);
		}
	}

	if (g_cla.workers_num <= 0)
		err_exit("Invalid number of workers");

	if (g_cla.buffer_size <= 0)
		err_exit("Invalid buffer size");
}

/* Also used for sigquit */
static void parent_signal_handler(int signum)
{
	if (signum == SIGINT)
		parent_sigint  = 1;
	else if (signum == SIGQUIT)
		parent_sigquit = 1;
	else if (signum == SIGCHLD) {
		g_termed_pid = waitpid(-1, NULL, 0);
		parent_sigchld = 1;
	}
}

static void parent_sigact(void)
{
	sigset_t maskset;
	struct sigaction sigact;

	sigemptyset(&maskset);
	sigaddset(&maskset, SIGINT);
	sigaddset(&maskset, SIGQUIT);
	sigaddset(&maskset, SIGCHLD);

	sigact.sa_handler = parent_signal_handler;
	sigact.sa_mask  = maskset;
	sigact.sa_flags = 0;

	sigaction(SIGINT,  &sigact, NULL);
	sigaction(SIGQUIT, &sigact, NULL);
	sigaction(SIGCHLD, &sigact, NULL);
}

int main(int argc, char** argv)
{
	parse_cla(argc, argv);

	// Retrieve input directory's contents
	Vector* countries = getdir(g_cla.input_dir, GETDIR_DEFAULT);

	// Create worker FIFOs
	const int BUFFER_SIZE = g_cla.buffer_size;
	const int WORKERS_NUM = (countries->size < g_cla.workers_num) ?
							 countries->size : g_cla.workers_num;
	pid_t pid[WORKERS_NUM];
	char worker_fifo[WORKERS_NUM][FIFO_TEMPLATE_LEN];
	int  worker_fd[WORKERS_NUM];
	struct sockaddr_in srv_addr;
	struct in_addr srv_ip;
	int i;

	for (i = 0; i < WORKERS_NUM; ++i) {
		snprintf(worker_fifo[i], FIFO_TEMPLATE_LEN, WORKER_FIFO_TEMPLATE, i);

		if ((mkfifo(worker_fifo[i], 0775) == -1 && errno != EEXIST))
			syserr_exit("mkfifo()");
	}

	// Convert who Server IP/port
	if (!inet_pton(AF_INET, g_cla.srv_ip, &srv_ip))
		err_exit("Invalid IP address");

	// Initialize server address
	srv_addr.sin_family = AF_INET;
	srv_addr.sin_port   = htons(g_cla.srv_port);
	srv_addr.sin_addr   = srv_ip;

	// Spawn workers
	for (i = 0; i < WORKERS_NUM; ++i) {
		switch ((pid[i] = fork())) {
		case -1:
			syserr_exit("fork()");

		case 0:
			worker(worker_fifo[i]);

		default:
			continue;
		}
	}

	// Establish signal handlers
	parent_sigact();

	// Open worker FIFOs
	for (i = 0; i < WORKERS_NUM; ++i)
		if ((worker_fd[i] = open(worker_fifo[i], O_WRONLY)) == -1)
			syserr_exit("open()");

	// Distribute the countries' subdirectories to the workers
	for (i = 0; i < countries->size; ++i)
		write_fifo(worker_fd[i % WORKERS_NUM], countries->entry[i], BUFFER_SIZE);

	// Send an empty message to signify the end of the countries' message sequence.
	// Send the address of whoServer
	for (i = 0; i < WORKERS_NUM; ++i) {
		write_fifo(worker_fd[i], "", BUFFER_SIZE);
		write_fifo_raw(worker_fd[i], &srv_addr, sizeof(srv_addr), BUFFER_SIZE);
	}

	// Pause waiting for signals
	while (pause())
	{
		// Termination signal received
		if (parent_sigint || parent_sigquit) {
			for (i = 0; i < WORKERS_NUM; ++i)
				if (kill(pid[i], SIGKILL) == -1)
					syserr_exit("kill()");
			break;
		}

		// A worker exited. Spawn a new one
		if (parent_sigchld) {
			int pos = 0;

			for (i = 0; i < WORKERS_NUM; ++i)
				if (pid[i] == g_termed_pid)
					break;

			if (i != WORKERS_NUM) {
				pos = i;

				printf("Worker %d (pid: %d) exited. Spawning a new one\n", pos,
				       g_termed_pid);

				switch ((pid[pos] = fork())) {
				case -1:
					syserr_exit("fork()");

				case 0:
					worker(worker_fifo[pos]);

				default:
					if (close(worker_fd[pos]) == -1)
						syserr_exit("close()");

					if ((worker_fd[pos] = open(worker_fifo[pos], O_WRONLY)) == -1)
						syserr_exit("open()");
				}

				for (i = 0; i < countries->size; ++i)
					if (pos == (i % WORKERS_NUM))
						write_fifo(worker_fd[pos], countries->entry[i], BUFFER_SIZE);
				write_fifo(worker_fd[pos], "", BUFFER_SIZE);
			}

			g_termed_pid   = 0;
			parent_sigchld = 0;

			continue;
		}
	}
	vector_free(countries, free);

	// Close and unlink FIFOs
	for (i = 0; i < WORKERS_NUM; ++i) {
		if (close(worker_fd[i]) == -1)
			syserr_exit("close()");

		if (unlink(worker_fifo[i]) == -1)
			syserr_exit("unlink()");
	}

	for (i = 0; i < WORKERS_NUM; ++i)
		waitpid(pid[i], NULL, 0);

	if (parent_sigquit)
		abort();

	return 0;
}

static void worker_signal_handler(int signum)
{
	if (signum == SIGINT)
		worker_sigint  = 1;
	else if (signum == SIGQUIT)
		worker_sigquit = 1;
	else if (signum == SIGUSR1)
		worker_sigusr1 = 1;
}

static void worker_sigact(void)
{
	struct sigaction sigact;
	sigset_t maskset;

	// Establish signal handlers
	sigemptyset(&maskset);
	sigaddset(&maskset, SIGINT);
	sigaddset(&maskset, SIGQUIT);
	sigaddset(&maskset, SIGUSR1);

	sigact.sa_handler = worker_signal_handler;
	sigact.sa_mask  = maskset;
	sigact.sa_flags = 0;

	sigaction(SIGINT,  &sigact, NULL);
	sigaction(SIGQUIT, &sigact, NULL);
	sigaction(SIGUSR1, &sigact, NULL);
}

static void worker(const char* fifo)
{
	const int BUFFER_SIZE = g_cla.buffer_size;
	struct sockaddr_in srv_addr;
	struct sockaddr_in wrk_addr;
	socklen_t wrk_addrlen;
	PatientDB* db;
	Vector* countries;
	char* msg;
	char* stats;
	char port_msg[11];
	int master_fd;
	int worker_fd;
	int server_fd;
	int accept_fd;
	int i;

	worker_sigact();

	// Open worker FIFO
	if ((master_fd = open(fifo, O_RDONLY)) == -1)
		syserr_exit("open()");


	// Read assigned countries
	countries = vector_init();
	while (read_fifo(master_fd, &msg, BUFFER_SIZE))
		vector_append(countries, msg);


	// Fetch record files
	Vector* recfiles[countries->size];

	for (i = 0; i < countries->size; ++i) {
		recfiles[i] = vector_init();
		update_recordfiles(recfiles[i], countries->entry[i]);
	}


	// Read whoServer IP/port
	read_fifo_raw(master_fd, &srv_addr, BUFFER_SIZE);


	// Establish client connection with whoServer
	if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1 ||
	    (worker_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
		syserr_exit("socket()");

	if (connect(server_fd, (struct sockaddr *)&srv_addr, (socklen_t)sizeof(srv_addr)))
		syserr_exit("connect()");


	// Establish server connection
	wrk_addr.sin_family = AF_INET;
	wrk_addr.sin_port = htons(0);
	wrk_addr.sin_addr.s_addr = htonl(INADDR_ANY);

	if (bind(worker_fd, (struct sockaddr *)&wrk_addr, sizeof(wrk_addr)))
		syserr_exit("bind()");

	if (listen(worker_fd, BACKLOG))
		syserr_exit("listen()");


	// Send whoServer the listening port
	wrk_addrlen = sizeof(wrk_addr);

	if (getsockname(worker_fd, (struct sockaddr *)&wrk_addr, &wrk_addrlen))
		syserr_exit("getsockname()");

	snprintf(port_msg, 11, "PORT:%d", ntohs(wrk_addr.sin_port));
	write_msg(server_fd, port_msg);


	// Sort record files by date, parse them, generate statistics and send them
	// to the whoServer
	db = patientDB_init();

	for (i = 0; i < countries->size; ++i) {
		vector_sort(recfiles[i], recordfile_date_comp);
		stats = parse_recordfiles(recfiles[i], db);
		if (stats) {
			write_msg(server_fd, stats);
			free(stats);
		}
	}
	// Send an empty message to signify the end of the message sequence
	write_msg(server_fd, "");

	// Read commands
	Command* command;
	char*    cmdline;
	char*    cmdname;
	Vector*  cmdarg;

	while ((accept_fd = accept(worker_fd, NULL, NULL)) || errno == EINTR)
	{
		// Termination signal received
		if (worker_sigint || worker_sigquit)
			break;

		// Update record files and build stats
		if (worker_sigusr1) {
			int writes = 0;

			if (connect(server_fd, (struct sockaddr *)&srv_addr,
			            (socklen_t)sizeof(srv_addr)))
				syserr_exit("connect()");

			for (i = 0; i < countries->size; ++i)
				update_recordfiles(recfiles[i], countries->entry[i]);

			for (i = 0; i < countries->size; ++i) {
				vector_sort(recfiles[i], recordfile_date_comp);
				stats = parse_recordfiles(recfiles[i], db);
				if (stats) {
					write_msg(server_fd, stats);
					free(stats);
					writes++;
				}
			}
			if (writes)
				write_msg(server_fd, "");

			worker_sigusr1 = 0;
			continue;
		}

		read_msg(accept_fd, &cmdline);

		cmdarg  = tokenize(cmdline, " \n");
		cmdname = vector_get(cmdarg, 0);

		// Empty command
		if (!cmdname) {
			vector_free(cmdarg, free);
			free(cmdline);
			close(accept_fd);
			continue;
		}

		command = get_command(cmdname);

		if (!command)
			write_msg(accept_fd, "Unknown command\n");

		else if (cmdarg->size < command->mandargs)
			write_msg(accept_fd, "Please provide all the necessary arguments\n");

		else if (command->val == DISEASE_FREQUENCY)
		{
			char* const virus      = vector_get(cmdarg, 1);
			char* const start_date = vector_get(cmdarg, 2);
			char* const end_date   = vector_get(cmdarg, 3);
			char* const country    = vector_get(cmdarg, 4);
			char freq_sum_str[16];
			int freq_sum = 0;
			int freq;

			if (country)
				freq_sum = patientDB_diseaseFreq(db, virus, start_date, end_date,
												 country);
			else {
				for (i = 0; i < countries->size; ++i) {
					freq = patientDB_diseaseFreq(db, virus, start_date, end_date,
												 countries->entry[i]);
					freq_sum += freq;

					if (freq_sum == -1) break;
				}
			}

			snprintf(freq_sum_str, 16, "%d", freq_sum);
			write_msg(accept_fd, freq_sum_str);
		}

		else if (command->val == TOPK_AGE_RANGES)
		{
			char* const k          = vector_get(cmdarg, 1);
			char* const country    = vector_get(cmdarg, 2);
			char* const virus      = vector_get(cmdarg, 3);
			char* const start_date = vector_get(cmdarg, 4);
			char* const end_date   = vector_get(cmdarg, 5);
			char* stats = NULL;
			bool  success = false;
			int kval;
			int err;

			kval = getint(k, GETINT_NOEXIT, &err);
			if (!err) {
				stats = patientDB_topkAgeRanges(db, kval, country, virus, start_date,
												end_date);
				if (stats) {
					write_msg(accept_fd, stats);
					free(stats);
					success = true;
				}
			}

			if (success == false)
				write_msg(accept_fd, "");
		}

		else if (command->val == SEARCH_PATIENT_RECORD)
		{
			char* const id = vector_get(cmdarg, 1);
			Vector*  patients;
			Patient* patient;
			char*    patients_str = NULL;
			char*    patient_str;

			patients = vector_init();

			for (i = 0; i < countries->size; ++i)
				if ((patient = patientDB_get(db, countries->entry[i], id)))
					vector_append(patients, patient);

			xstrcat(&patients_str, "");
			for (i = 0; i < patients->size; ++i) {
				patient_print(patients->entry[i], &patient_str);
				xstrcat(&patients_str, patient_str);
				free(patient_str);
			}
			write_msg(accept_fd, patients_str);

			vector_free(patients, NULL);
			free(patients_str);
		}

		else if (command->val == NUM_PATIENT_ADMISSIONS)
		{
			char* const virus      = vector_get(cmdarg, 1);
			char* const start_date = vector_get(cmdarg, 2);
			char* const end_date   = vector_get(cmdarg, 3);
			char* country          = vector_get(cmdarg, 4);
			char* msg_total = NULL;
			char* msg;

			xstrcat(&msg_total, "");
			if (country) {
				msg = patientDB_admissions(db, country, virus, start_date, end_date);
				if (msg) {
					xstrcat(&msg_total, msg);
					free(msg);
				}
			}
			else {
				for (i = 0; i < countries->size; ++i) {
					country = countries->entry[i];
					msg = patientDB_admissions(db, country, virus, start_date, end_date);
					if (msg) {
						xstrcat(&msg_total, msg);
						free(msg);
					}
				}
			}
			write_msg(accept_fd, msg_total);
			free(msg_total);
		}

		else if (command->val == NUM_PATIENT_DISCHARGES)
		{
			char* const virus      = vector_get(cmdarg, 1);
			char* const start_date = vector_get(cmdarg, 2);
			char* const end_date   = vector_get(cmdarg, 3);
			char* country          = vector_get(cmdarg, 4);
			char* msg_total = NULL;
			char* msg;

			xstrcat(&msg_total, "");
			if (country) {
				msg = patientDB_discharges(db, country, virus, start_date, end_date);
				if (msg) {
					xstrcat(&msg_total, msg);
					free(msg);
				}
			}
			else {
				for (i = 0; i < countries->size; ++i) {
					country = countries->entry[i];
					msg = patientDB_discharges(db, country, virus, start_date, end_date);
					if (msg) {
						xstrcat(&msg_total, msg);
						free(msg);
					}
				}
			}
			write_msg(accept_fd, msg_total);
			free(msg_total);
		}

		vector_free(cmdarg, free);
		free(cmdline);
		close(accept_fd);
	}

	for (i = 0; i < countries->size; ++i)
		free_recordfiles(recfiles[i]);

	vector_free(countries, free);
	patientDB_free(db);

	if (close(master_fd) == -1 || close(server_fd) == -1 || close(worker_fd) == -1)
		syserr_exit("close()");

	if (worker_sigquit) abort();

	_exit(EXIT_SUCCESS);
}

static char* parse_recordfiles(Vector* recfiles, PatientDB* db)
{
	Record_file* record_file;
	List* patients_added;
	char* stats_total = NULL;
	char* stats;
	int i;

	for (i = 0; i < recfiles->size; i++) {
		record_file = recfiles->entry[i];

		if (record_file->parsed == false) {
			patients_added = patient_parse_file(record_file->name, db);
			if (patients_added) {
				stats = worker_generate_stats(patients_added);
				xstrcat(&stats_total, stats);
				free(stats);
			}
			record_file->parsed = true;
		}
	}

	return stats_total;
}

static void update_recordfiles(Vector* rec_files, const char* country)
{
	Vector* rec_filenames;
	char* country_path;
	int pos, i;

	xsprintf(&country_path, "%s/%s", g_cla.input_dir, country);
	rec_filenames = getdir(country_path, GETDIR_FULLPATH);
	free(country_path);

	Record_file* rec_file;
	Record_file  rec_file_new;

	for (i = 0; i < rec_filenames->size; ++i) {
		rec_file_new.name = rec_filenames->entry[i];

		pos = vector_find(rec_files, &rec_file_new, recordfile_name_comp);
		if (pos == -1) {
			rec_file         = xmalloc(sizeof(*rec_file));
			rec_file->name   = xstrdup(rec_filenames->entry[i]);
			rec_file->parsed = false;
			vector_append(rec_files, rec_file);
		}
	}

	vector_free(rec_filenames, free);
}

static void free_recordfiles(Vector* rec_files)
{
	vector_free(rec_files, recordfile_free_generic);
}

static inline void recordfile_free_generic(void* r)
{
	recordfile_free(r);
}

static void recordfile_free(Record_file* r)
{
	free(r->name);
	free(r);
}

static int recordfile_name_comp(const void* v1, const void* v2)
{
	Record_file* r1 = (Record_file*)v1;
	Record_file* r2 = (Record_file*)v2;

	return strcmp(r1->name, r2->name);
}
static int recordfile_date_comp(const void* v1, const void* v2)
{
	Record_file* r1;
	Record_file* r2;
	struct tm date1;
	struct tm date2;
	char* date_str1;
	char* date_str2;
	char* rec_name1;
	char* rec_name2;

	r1 = *((Record_file**)v1);
	r2 = *((Record_file**)v2);

	rec_name1 = xstrdup(r1->name);
	rec_name2 = xstrdup(r2->name);

	date_str1 = basename(rec_name1);
	date_str2 = basename(rec_name2);

	date_init(date_str1, &date1);
	date_init(date_str2, &date2);

	free(rec_name1);
	free(rec_name2);

	return date_comp(&date1, &date2);
}

/* Generate stats for a specific country/date and place the resulting string in stats
 * Return value:
 * The number of bytes written to the returned sting
 * */
static char* worker_generate_stats(List* patients)
{
	struct Virus_frequency {
		int upto20;
		int upto40;
		int upto60;
		int plus60;
	} *vir_freq;

	List_node* node;
	Hashtable* vir_freq_table;
	Patient* patient;
	Keyval* keyval;

	vir_freq_table = hashtable_init(20, hashtable_min_bucket_size());

	for (node = patients->head; node; node = node->next) {
		patient = node->data;
		vir_freq = hashtable_find(vir_freq_table, patient->virus);
		if (!vir_freq) {
			vir_freq = xcalloc(1, sizeof(*vir_freq));
			hashtable_insert(vir_freq_table, patient->virus, vir_freq);
		}

		if (patient->age <= 20)
			vir_freq->upto20++;
		else if (patient->age <= 40)
			vir_freq->upto40++;
		else if (patient->age <= 60)
			vir_freq->upto60++;
		else
			vir_freq->plus60++;
	}

	const int BUFS_NUM = hashtable_nentries(vir_freq_table) +1;
	char*  buf [BUFS_NUM];
	size_t blen[BUFS_NUM];
	char   date[DATE_BUFSIZE];
	int i;

	date_tostring(&patient->entry_date, date);
	blen[0] = xsprintf(&buf[0], "%s\n%s\n", date, patient->country);

	for (i = 1; (keyval = hashtable_next(vir_freq_table)); ++i) {
		vir_freq = keyval->val;

		blen[i] = xsprintf(&buf[i],
		          "%s\n"
		          "Age range 0-20 years: %d cases\n"
		          "Age range 21-40 years: %d cases\n"
		          "Age range 41-60 years: %d cases\n"
		          "Age range 60+ years: %d cases\n\n",
		          keyval->key,
		          vir_freq->upto20, vir_freq->upto40, vir_freq->upto60,
		          vir_freq->plus60);
	}

	hashtable_free(vir_freq_table, free);

	char* stats = string_arr_flatten(buf, blen, BUFS_NUM);

	for (i = 0; i < BUFS_NUM; ++i)
		free(buf[i]);

	return stats;
}
