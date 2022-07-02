#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include "hashtable.h"

#define HASHTABLE_MIN_BUCKET_SIZE (sizeof(Bucket) +sizeof(Keyval))

typedef struct Bucket Bucket;

struct Bucket {
	Keyval* entry;  // Entries
	size_t size;    // Max entries
	size_t n;       // Number of currently stored entries
	Bucket* next;   // Next Bucket
};

struct Hashtable {
	Bucket** table; // Hash table index
	size_t size;    // Index size
	size_t bsize;   // Bucket size
	size_t n;       // Number of currently stored entries

	// For traversal
	Bucket* cur_bucket;
	int i;          // Hashtable index
	int bi;         // Bucket index
};

static int hashtable_hash(Hashtable* ht, const char* key);

static Bucket* bucket_init(size_t size);
static void    bucket_free(Bucket* b, void (*free_val)(void*));
static int     bucket_insert(Bucket* b, const char* key, void* val);
static void*   bucket_find(Bucket* b, const char* key);
static Keyval* bucket_next(Bucket** b, int* i);

char* hashtable_error(Hashtable_errcode errcode)
{
	struct Hashtable_err {
		Hashtable_errcode errcode;
		char* errmsg;
	} err[] = {
		{ HASHTABLE_SUCCESS, "Success" },
		{ HASHTABLE_NOMEM,   "Out of memory" },
		{ HASHTABLE_DUPKEY,  "The specified key already exists in the hash table" }
	};

	return err[errcode].errmsg;
}

int hashtable_min_bucket_size(void)
{
	return (int)HASHTABLE_MIN_BUCKET_SIZE;
}

static int hashtable_hash(Hashtable* ht, const char* key)
{
	size_t len = strlen(key);
	int num = 0;
	int i;

	for (i = 0; i < len; ++i)
		num += tolower(key[i]);

	return num % ht->size;
}

static Bucket* bucket_init(size_t size)
{
	Bucket* b;

	b = malloc(sizeof(*b));
	if (b) {
		b->n     = 0;
		b->size  = size;
		b->next  = NULL;
		b->entry = malloc(size*sizeof(*b->entry));
		if (!b->entry) {
			free(b);
			return NULL;
		}
	}

	return b;
}

static void bucket_free(Bucket* b, void (*free_val)(void*))
{
	Bucket* btemp;

	if (b) {
		do {
			for (int i = 0; i < b->size; ++i) {
				free(b->entry[i].key);
				if (free_val)
					free_val(b->entry[i].val);
			}

			free(b->entry);
			btemp = b->next;
			free(b);
		} while ((b = btemp));
	}
}

/* Insert a key/value pair in the specified bucket.
 *
 * Return value:
 *
 * On success the function returns 0. If the specified key already exists in bucket chain,
 * the functions aborts the operation and HASHTABLE_DUPKEY is returned.
 */
static int bucket_insert(Bucket* b, const char* key, void* val)
{
	Bucket* bp;

	do {
		bp = b;

		for (int i = 0; i < b->n; ++i)
			if (!strcasecmp(b->entry[i].key, key))
				return HASHTABLE_DUPKEY;

	} while ((b = b->next));

	b = bp;

	if (b->n == b->size) {
		bp = bucket_init(b->size);
		if (!bp)
			return HASHTABLE_NOMEM;

		b->next = bp;
		b = b->next;
	}

	b->entry[b->n].key = malloc(strlen(key) +1);
	if (!b->entry[b->n].key)
		return HASHTABLE_NOMEM;

	strcpy(b->entry[b->n].key, key);
	b->entry[b->n].val = val;
	b->n++;

	return HASHTABLE_SUCCESS;
}

static void* bucket_find(Bucket* b, const char* key)
{
	bool found = false;
	int i;

	do {
		for (i = 0; i < b->n; ++i)
			if (!strcasecmp(b->entry[i].key, key)) {
				found = true;
				goto done;
			}
	} while ((b = b->next));

done:
	if (found)
		return b->entry[i].val;
	else
		return NULL;
}

static Keyval* bucket_next(Bucket** b, int* i)
{
	Keyval* kv;

	while (*b) {
		if (*i < (*b)->n) {
			kv = &(*b)->entry[*i];
			(*i)++;

			return kv;
		}
		else {
			*b = (*b)->next;
			*i = 0;
		}
	}

	return NULL;
}

Hashtable* hashtable_init(size_t size, size_t bsize)
{
	Hashtable* ht = malloc(sizeof(*ht));

	if (ht) {
		ht->table = calloc(size, sizeof(*ht->table));
		if (!ht->table) {
			free(ht);
			return NULL;
		}

		ht->i  = 0;
		ht->bi = 0;
		ht->cur_bucket = NULL;
		ht->n     = 0;
		ht->size  = size;
		ht->bsize = (bsize - HASHTABLE_MIN_BUCKET_SIZE)/sizeof(Keyval) +1;
		//printf("Will have %ld entries per bucket\n", ht->bsize);
	}

	return ht;
}

void hashtable_free(Hashtable* ht, void (*free_val)(void*))
{
	for (int i = 0; i < ht->size; i++)
		bucket_free(ht->table[i], free_val);

	free(ht->table);
	free(ht);
}

size_t hashtable_nentries(Hashtable* ht)
{
	return ht->n;
}

int hashtable_insert(Hashtable* ht, const char* key, void* val)
{
	int hash;
	int ret;

	hash = hashtable_hash(ht, key);
	if (ht->table[hash] == 0) {
		ht->table[hash] = bucket_init(ht->bsize);
		if (!ht->table[hash])
			return HASHTABLE_NOMEM;
	}

	ret = bucket_insert(ht->table[hash], key, val);
	if (ret == HASHTABLE_SUCCESS)
		ht->n++;

	return ret;
}

void* hashtable_find(Hashtable* ht, const char* key)
{
	int hash;

	hash = hashtable_hash(ht, key);
	if (ht->table[hash] == 0)
		return NULL;

	return bucket_find(ht->table[hash], key);
}

Keyval* hashtable_next(Hashtable* ht)
{
	int* i = &ht->i;
	Keyval* kv;

	while (*i < ht->size) {
		if (!ht->cur_bucket)
			ht->cur_bucket = ht->table[*i];

		kv = bucket_next(&ht->cur_bucket, &ht->bi);
		if (kv)
			return kv;

		(*i)++;
	}

	// After traversing the whole hash table reset counters
	ht->i  = 0;
	ht->bi = 0;
	ht->cur_bucket = NULL;

	return NULL;
}
