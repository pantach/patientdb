#ifndef PATIENT_H
#define PATIENT_H

#include <time.h>
#include "vector.h"
#include "hashtable.h"
#include "list.h"

#define DATE_BUFSIZE 11

typedef struct {
	char* id;
	char* fname;
	char* lname;
	char* virus;
	char* country;
	int   age;
	struct tm entry_date;
	struct tm exit_date;
} Patient;

typedef struct {
	Hashtable* cntrid;
	Hashtable* cntree;
	Hashtable* virtree;
} PatientDB;

int date_init(const char* datestr, struct tm* date);
int date_comp(const struct tm* date1, const struct tm* date2);
char* date_tostring(struct tm* date, char* buf);

List* patient_parse_file(const char* file, PatientDB* db);
void  patient_print(Patient* p, char** pstr);

PatientDB* patientDB_init(void);
void patientDB_free(PatientDB* db);
void patientDB_insert(PatientDB* db, Patient* p);
Patient* patientDB_get(PatientDB* db, const char* country, const char* id);
Hashtable* patientDB_getbycountry(PatientDB* db, const char* country);
List* patientDB_getbydate(PatientDB* db, const char* country, const char* date);

int patientDB_diseaseFreq(PatientDB* db, const char* virus, const char* start_date,
                          const char* end_date, const char* country);

char* patientDB_topkAgeRanges(PatientDB* db, int k, const char* country,
                              const char* virus, const char* start_date,
                              const char* end_date);

char* patientDB_admissions(PatientDB* db, const char* country, const char* virus,
                           const char* start_date, const char* end_date);

char* patientDB_discharges(PatientDB* db, const char* country, const char* virus,
                           const char* start_date, const char* end_date);
#endif
