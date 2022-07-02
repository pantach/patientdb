#include <string.h>
#include "command.h"

Command* get_command(const char* command_name)
{
	static Command command[] = {
		{ DISEASE_FREQUENCY,      "/diseaseFrequency",     4, 4 },
		{ TOPK_AGE_RANGES,        "/topk-AgeRanges",       6, 2 },
		{ SEARCH_PATIENT_RECORD,  "/searchPatientRecord",  2, 0 },
		{ NUM_PATIENT_ADMISSIONS, "/numPatientAdmissions", 4, 4 },
		{ NUM_PATIENT_DISCHARGES, "/numPatientDischarges", 4, 4 }};

	for (int i = 0; i < LAST; ++i)
		if (!strcmp(command_name, command[i].name))
			return &command[i];

	return NULL;
}

