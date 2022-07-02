#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <libgen.h>
#include "tools.h"
#include "tree.h"
#include "patient.h"

#define ERROPT SUCCINCT
#define DATESTR_UNDEF ""
#define DATE_UNDEF_INIT (struct tm){.tm_mon = -1}
#define DATE_ISUNDEF(date)  ((date).tm_mon == -1)
#define DATE_ISDEF(date)    ((date).tm_mon != -1)

typedef enum {
	VERBOSE = 0,
	SUCCINCT
} Patient_err_opt;

typedef enum {
	PATIENT_ELINE = 0,
	PATIENT_EEXIT,
	PATIENT_EDUPID,
	PATIENT_EINVID,
	PATIENT_ERECDAT
} Patient_err;

static Patient* patient_init(const char* id, const char* fname, const char* lname,
                             const char* virus, const char* country, const char* age,
                             const char* entry_date, const char* exit_date);
static inline Patient* dummy_patient_init(const char* entry_date, const char* exit_date);
static void patient_free(Patient* p);
static void patient_free_generic(void* p);
static int  patient_set_exit(Patient* p, char* exit_date);
static void patient_printerr(Patient_err_opt opt, Patient_err err, ...);

static void patientDB_hashhash_insert(Hashtable* ht, Patient* p, const char* country);
static void patientDB_hashtree_insert(Hashtable* ht, Patient* p, const char* key);

static int  patient_date_comp_generic(const void* p1, const void* p2);

static int dss_freq_cb(List* patients, void* cb_data);
static int vir_freq_cb(List* patients, void* cb_data);
static int adm_cb     (List* patients, void* cb_data);
static int dis_cb     (List* patients, void* cb_data);

static struct vir_freq* patientDB_vir_freq(PatientDB* db, const char* country,
                                           const char* virus, const char* start_date,
                                           const char* end_date);

static int vfv_comp_desc(const void* v1, const void* v2);


/*
int main(int argc, char** argv)
{
	char time[2][1];
	printf("%ld\n", sizeof(time[0]));

	return 0;
}
*/

int date_init(const char* datestr, struct tm* date)
{
	Vector* dtok;
	int err = 0;

	if (!strcmp(datestr, DATESTR_UNDEF))
		*date = DATE_UNDEF_INIT;
	else {
		*date = (struct tm){0};

		dtok = tokenize(datestr, "-");
		if ((err = (dtok->size < 3)))
			goto end;

		date->tm_mday = getint(dtok->entry[0], GETINT_NOEXIT, &err);
		if (err) goto end;

		date->tm_mon  = getint(dtok->entry[1], GETINT_NOEXIT, &err) -1;
		if (err) goto end;

		date->tm_year = getint(dtok->entry[2], GETINT_NOEXIT, &err) -1900;
		if (err) goto end;

		if ((err = !(date->tm_mday >= 1 && date->tm_mday <= 31)))
			goto end;

		if ((err = !(date->tm_mon >= 0 && date->tm_mon <= 11)))
			goto end;

end:
		vector_free(dtok, free);
	}

	return err;
}

char* date_tostring(struct tm* date, char* buf)
{
	if (DATE_ISUNDEF(*date))
		strcpy(buf, "--");
	else
		strftime(buf, DATE_BUFSIZE, "%d-%m-%Y", date);

	return buf;
}

int date_comp(const struct tm* date1, const struct tm* date2)
{
	struct tm d1 = *date1;
	struct tm d2 = *date2;
	double diff;

	if (DATE_ISDEF(d1) && DATE_ISDEF(d2)) {
		diff = difftime(mktime(&d1), mktime(&d2));
		// Prevent integer overflow due to conversion to a smaller type
		if (diff > 0) return  1;
		if (diff < 0) return -1;
		return 0;
	}
	if (DATE_ISDEF(d1)) return  1;
	if (DATE_ISDEF(d2)) return -1;

	return 0;
}

static int patient_date_comp_generic(const void* p1, const void* p2)
{
	const Patient* pa = p1;
	const Patient* pb = p2;

	return date_comp(&pa->entry_date, &pb->entry_date);
}

static Patient* patient_init(const char* id, const char* fname, const char* lname,
                             const char* virus, const char* country, const char* age,
                             const char* entry_date, const char* exit_date)
{
	Patient* p;
	struct tm entry_tm;
	struct tm exit_tm;
	int ageval;
	int error;

	if (date_init(entry_date, &entry_tm))
		return NULL;

	if (date_init(exit_date, &exit_tm))
		return NULL;

	if (date_comp(&entry_tm, &exit_tm) < 0)
		return NULL;

	ageval = getint(age, GETINT_NOEXIT, &error);
	if (error)
		return NULL;

	if (ageval <= 0 || ageval > 120)
		return NULL;

	p = xmalloc(sizeof(*p));

	p->id      = xstrdup(id);
	p->fname   = xstrdup(fname);
	p->lname   = xstrdup(lname);
	p->virus   = xstrdup(virus);
	p->country = xstrdup(country);
	p->age     = ageval;
	p->entry_date = entry_tm;
	p->exit_date  = exit_tm;

	return p;
}

static inline Patient* dummy_patient_init(const char* entry_date, const char* exit_date)
{
	Patient* dummy = patient_init("a", "a", "a", "a", "a", "1", entry_date, exit_date);
	return dummy;
}

static void patient_free(Patient* p)
{
	if (p) {
		free(p->id);
		free(p->fname);
		free(p->lname);
		free(p->virus);
		free(p->country);
		free(p);
	}
}

static void patient_free_generic(void* p)
{
	patient_free(p);
}

static int patient_set_exit(Patient* p, char* exit_date)
{
	struct tm exit_tm;

	if (date_init(exit_date, &exit_tm))
		return 0;

	if (date_comp(&exit_tm, &p->entry_date) >= 0) {
		p->exit_date = exit_tm;
		return 1;
	}

	return 0;
}

void patient_print(Patient* p, char** pstr)
{
	char date[2][DATE_BUFSIZE];

	date_tostring(&p->entry_date, date[0]);
	date_tostring(&p->exit_date,  date[1]);

	xsprintf(pstr, "%s %s %s %s %d %s %s\n",
	         p->id, p->fname, p->lname, p->virus, p->age, date[0], date[1]);
}

static void patient_printerr(Patient_err_opt opt, Patient_err err, ...)
{

	va_list args;
	char* errdat;

	switch (opt) {

	case VERBOSE:
		va_start(args, err);
		errdat = va_arg(args, char*);
		va_end(args);

		switch (err) {

		case PATIENT_ELINE:
			fprintf(stderr, "Erroneous line: %s\n", errdat);
			break;

		case PATIENT_EEXIT:
			fprintf(stderr, "Exit date comes before entry date (id: %s)\n", errdat);
			break;

		case PATIENT_EDUPID:
			fprintf(stderr, "Duplicate record id: %s\n", errdat);
			break;

		case PATIENT_EINVID:
			fprintf(stderr, "Invalid record id: %s\n", errdat);
			break;

		case PATIENT_ERECDAT:
			fprintf(stderr, "Erroneous record data: %s\n", errdat);
			break;
		}
		break;

	case SUCCINCT:
		fprintf(stderr, "ERROR\n");
	}
}

List* patient_parse_file(const char* file, PatientDB* db)
{
	FILE* fp;
	Patient* patient;
	char*  line = NULL;
	size_t line_size = 0;
	char* country;
	char* date;
	char* file_copy;

	fp = fopen(file, "rb");
	if (!fp)
		err_exit("fopen() failure");

	file_copy = xstrdup(file);

	date    = basename(file_copy);
	country = basename(dirname(file_copy));

	for (int i = 0; getline(&line, &line_size, fp) != -1; ++i) {
		Vector* field = tokenize(line, " \t\n");

		if (field->size < 6) {
			patient_printerr(ERROPT, PATIENT_ELINE, line);
			vector_free(field, free);
			continue;
		}

		char* const id    = vector_get(field, 0);
		char* const act   = vector_get(field, 1);
		char* const fname = vector_get(field, 2);
		char* const lname = vector_get(field, 3);
		char* const virus = vector_get(field, 4);
		char* const age   = vector_get(field, 5);

		if ((patient = patientDB_get(db, country, id))) {
			if (!strcmp(act, "EXIT")) {
				if (!patient_set_exit(patient, date))
					patient_printerr(ERROPT, PATIENT_EEXIT, id);
			}
			else
				patient_printerr(ERROPT, PATIENT_EDUPID, id);
		}
		else {
			if (!strcmp(act, "EXIT"))
				patient_printerr(ERROPT, PATIENT_EINVID, id);
			else {
				patient = patient_init(id, fname, lname, virus, country, age, date,
				                       DATESTR_UNDEF);
				if (patient)
					patientDB_insert(db, patient);
				else
					patient_printerr(ERROPT, PATIENT_ERECDAT, line);
			}
		}

		vector_free(field, free);
	}

	List* patients_added = patientDB_getbydate(db, country, date);

	free(line);
	free(file_copy);
	fclose(fp);

	return patients_added;
}

PatientDB* patientDB_init(void)
{
	PatientDB* db = xmalloc(sizeof(*db));

	db->cntrid  = hashtable_init(100, hashtable_min_bucket_size());
	db->cntree  = hashtable_init(100, hashtable_min_bucket_size());
	db->virtree = hashtable_init(100, hashtable_min_bucket_size());

	return db;
}

void patientDB_free(PatientDB* db)
{
	Keyval* keyval;

	while ((keyval = hashtable_next(db->cntrid)))
		hashtable_free(keyval->val, patient_free_generic);

	while ((keyval = hashtable_next(db->cntree)))
		tree_free(keyval->val, NULL);

	while ((keyval = hashtable_next(db->virtree)))
		tree_free(keyval->val, NULL);

	hashtable_free(db->cntrid,  NULL);
	hashtable_free(db->cntree,  NULL);
	hashtable_free(db->virtree, NULL);

	free(db);
}

static void patientDB_hashhash_insert(Hashtable* ht, Patient* p, const char* country)
{
	Hashtable* cntr_patients;

	cntr_patients = hashtable_find(ht, country);
	if (cntr_patients)
		hashtable_insert(cntr_patients, p->id, p);
	else {
		cntr_patients = hashtable_init(100, hashtable_min_bucket_size());
		hashtable_insert(cntr_patients, p->id, p);
		hashtable_insert(ht, country, cntr_patients);
	}
}

static void patientDB_hashtree_insert(Hashtable* ht, Patient* p, const char* key)
{
	Tree* tree;

	tree = hashtable_find(ht, key);
	if (tree)
		tree_insert(tree, p);
	else {
		tree = tree_init(patient_date_comp_generic);
		tree_insert(tree, p);
		hashtable_insert(ht, key, tree);
	}
}

void patientDB_insert(PatientDB* db, Patient* p)
{
	patientDB_hashhash_insert(db->cntrid,  p, p->country);
	patientDB_hashtree_insert(db->cntree,  p, p->country);
	patientDB_hashtree_insert(db->virtree, p, p->virus);
}

Patient* patientDB_get(PatientDB* db, const char* country, const char* id)
{
	Hashtable* cntr_patients;

	cntr_patients = hashtable_find(db->cntrid, country);
	if (cntr_patients)
		return hashtable_find(cntr_patients, id);

	return NULL;
}

Hashtable* patientDB_getbycountry(PatientDB* db, const char* country)
{
	return hashtable_find(db->cntrid, country);
}

List* patientDB_getbydate(PatientDB* db, const char* country, const char* date)
{
	Patient* dummy = patient_init("1", "a", "a", "a", "a", "1", date, DATESTR_UNDEF);
	Tree* tree;
	List* all = NULL;

	tree = hashtable_find(db->cntree, country);
	if (tree)
		all = tree_locate(tree, dummy);

	patient_free(dummy);

	return all;
}

struct dss_freq_cb_data {
	const char* country;
	int freq;
};

static int dss_freq_cb(List* patients, void* cb_data)
{
	struct dss_freq_cb_data* data = cb_data;

	if (data->country) {
		for (List_node* node = patients->head; node; node = node->next) {
			Patient* p = node->data;
			if (!strcasecmp(p->country, data->country))
				data->freq++;
		}
	}
	else
		data->freq += patients->size;

	return 0;
}

int patientDB_diseaseFreq(PatientDB* db, const char* virus, const char* start_date,
                          const char* end_date, const char* country)
{
	struct dss_freq_cb_data cb_data = {country, -1};
	Patient* dummy1 = NULL;
	Patient* dummy2 = NULL;
	Tree* tree;

	if ((tree = hashtable_find(db->virtree, virus)) == NULL)
		goto end;

	if ((dummy1 = dummy_patient_init(start_date, DATESTR_UNDEF)) == NULL)
		goto end;

	if ((dummy2 = dummy_patient_init(end_date, DATESTR_UNDEF)) == NULL)
		goto end;

	cb_data.freq = 0;

	tree_traverse_range(tree, TREE_PREORDER, &cb_data, dss_freq_cb, dummy1, dummy2);

end:
	patient_free(dummy1);
	patient_free(dummy2);

	return cb_data.freq;
}

struct vir_freq {
	const char* virus;
	int upto20;
	int upto40;
	int upto60;
	int plus60;
};

static int vir_freq_cb(List* patients, void* cb_data)
{
	struct vir_freq* data = cb_data;

	for (List_node* node = patients->head; node; node = node->next) {
		Patient* p = node->data;
		if (!strcasecmp(p->virus, data->virus)) {
			if (p->age <= 20)
				data->upto20++;
			else if (p->age <= 40)
				data->upto40++;
			else if (p->age <= 60)
				data->upto60++;
			else
				data->plus60++;
		}
	}

	return 0;
}

static struct vir_freq* patientDB_vir_freq(PatientDB* db, const char* country,
                                           const char* virus, const char* start_date,
                                           const char* end_date)
{
	struct vir_freq* cb_data = NULL;
	Patient* dummy1 = NULL;
	Patient* dummy2 = NULL;
	Tree* tree;

	if ((tree = hashtable_find(db->cntree, country)) == NULL)
		goto end;

	if ((dummy1 = dummy_patient_init(start_date, DATESTR_UNDEF)) == NULL)
		goto end;

	if ((dummy2 = dummy_patient_init(end_date, DATESTR_UNDEF)) == NULL)
		goto end;

	cb_data  = xmalloc(sizeof(*cb_data));
	*cb_data = (struct vir_freq){ virus, 0, 0, 0, 0 };

	tree_traverse_range(tree, TREE_PREORDER, cb_data, vir_freq_cb, dummy1, dummy2);

end:
	patient_free(dummy1);
	patient_free(dummy2);

	return cb_data;
}

static int vfv_comp_desc(const void* v1, const void* v2)
{
	int i1 = **(int**)v1;
	int i2 = **(int**)v2;

	return i2 -i1;
}

char* patientDB_topkAgeRanges(PatientDB* db, int k, const char* country,
                              const char* virus, const char* start_date,
                              const char* end_date)
{
	struct vir_freq* freq;
	Vector* vfv;

	freq = patientDB_vir_freq(db, country, virus, start_date, end_date);
	if (!freq) return NULL;

	vfv = vector_init();
	vector_append(vfv, &freq->upto20);
	vector_append(vfv, &freq->upto40);
	vector_append(vfv, &freq->upto60);
	vector_append(vfv, &freq->plus60);
	vector_sort(vfv, vfv_comp_desc);

	const int age_categories = (k <= vfv->size) ? k : vfv->size;
	char* stats_total;
	char* stats[age_categories];
	char* format;
	int*  catval;
	int   freq_sum;

	freq_sum = freq->upto20 +freq->upto40 +freq->upto60 +freq->plus60;

	for (int i = 0; i < age_categories; ++i) {
		catval = vector_get(vfv, i);

		if (catval == &freq->upto20)
			format = "0-20: %.0f%%\n";
		else if (catval == &freq->upto40)
			format = "0-40: %.0f%%\n";
		else if (catval == &freq->upto60)
			format = "0-60: %.0f%%\n";
		else
			format =  "60+: %.0f%%\n";

		xsprintf(&stats[i], format, freq_sum ? (*catval/(float)freq_sum)*100 : 0);
	}

	stats_total = string_arr_flatten(stats, NULL, age_categories);

	for (int i = 0; i < age_categories; ++i)
		free(stats[i]);

	free(freq);
	vector_free(vfv, NULL);

	return stats_total;
}

struct adm_cb_data {
	const char* virus;
	int n;
};

static int adm_cb(List* patients, void* cb_data)
{
	struct adm_cb_data* data = cb_data;

	for (List_node* node = patients->head; node; node = node->next) {
		Patient* p = node->data;
		if (!strcasecmp(p->virus, data->virus))
			data->n++;
	}

	return 0;
}

char* patientDB_admissions(PatientDB* db, const char* country, const char* virus,
                           const char* start_date, const char* end_date)
{
	struct adm_cb_data cb_data = { virus, 0 };
	Patient* dummy1 = NULL;
	Patient* dummy2 = NULL;
	Tree* tree;
	char* res = NULL;

	if ((tree = hashtable_find(db->cntree, country)) == NULL)
		goto end;

	if ((dummy1 = dummy_patient_init(start_date, DATESTR_UNDEF)) == NULL)
		goto end;

	if ((dummy2 = dummy_patient_init(end_date, DATESTR_UNDEF)) == NULL)
		goto end;

	tree_traverse_range(tree, TREE_PREORDER, &cb_data, adm_cb, dummy1, dummy2);

	xsprintf(&res, "%s %d\n", country, cb_data.n);

end:
	patient_free(dummy1);
	patient_free(dummy2);

	return res;
}

struct dis_cb_data {
	const struct tm start_tm;
	const struct tm end_tm;
	const char* virus;
	int n;
};

static int dis_cb(List* patients, void* cb_data)
{
	struct dis_cb_data* data = cb_data;
	Patient* p;
	int after_start;
	int before_end;

	for (List_node* node = patients->head; node; node = node->next) {
		p = node->data;
		after_start = date_comp(&p->exit_date, &data->start_tm);
		before_end  = date_comp(&p->exit_date, &data->end_tm);
		if (!strcasecmp(p->virus, data->virus) && after_start >= 0 && before_end <= 0)
			data->n++;
	}

	return 0;
}

char* patientDB_discharges(PatientDB* db, const char* country, const char* virus,
                           const char* start_date, const char* end_date)
{
	struct tm start_tm;
	struct tm end_tm;
	Tree* tree;
	char* res;

	if ((tree = hashtable_find(db->cntree, country)) == NULL)
		return NULL;

	if (date_init(start_date, &start_tm))
		return NULL;

	if (date_init(end_date, &end_tm))
		return NULL;

	struct dis_cb_data cb_data = { start_tm, end_tm, virus, 0 };

	tree_traverse(tree, TREE_PREORDER, &cb_data, dis_cb);

	xsprintf(&res, "%s %d\n", country, cb_data.n);

	return res;
}
