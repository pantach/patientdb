#ifndef HASHTABLE_H
#define HASHTABLE_H

typedef struct Hashtable Hashtable;

typedef enum {
	HASHTABLE_SUCCESS = 0,
	HASHTABLE_NOMEM,
	HASHTABLE_DUPKEY
} Hashtable_errcode;

typedef struct {
	char* key;
	void* val;
} Keyval;

int   hashtable_min_bucket_size(void);
char* hashtable_error(Hashtable_errcode);

Hashtable* hashtable_init(size_t size, size_t bsize);
void  hashtable_free(Hashtable* ht, void (*free_val)(void*));
int   hashtable_insert(Hashtable* ht, const char* key, void* val);
void* hashtable_find(Hashtable* ht, const char* key);
Keyval* hashtable_next(Hashtable* ht);
size_t hashtable_nentries(Hashtable* ht);

#endif
