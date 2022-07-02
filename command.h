#ifndef COMMAND_H
#define COMMAND_H

typedef enum {
	DISEASE_FREQUENCY = 0,
	TOPK_AGE_RANGES,
	SEARCH_PATIENT_RECORD,
	NUM_PATIENT_ADMISSIONS,
	NUM_PATIENT_DISCHARGES,
	LAST
} Command_val;

typedef struct  {
	const Command_val val;
	const char* name;
	const int mandargs;
	const int cntrarg_pos;
} Command;

Command* get_command(const char* command_str);

#endif
