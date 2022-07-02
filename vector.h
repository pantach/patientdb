#ifndef VECTOR_H
#define VECTOR_H

typedef struct {
	void** entry;
	size_t size;
	size_t capacity;
} Vector;

Vector* vector_init(void);
void  vector_free(Vector* v, void (*free_entry)(void*));
int   vector_append(Vector* v, void* entry);
int   vector_find(Vector* v, void* entry, int (*comp)(const void* p1, const void* p2));
void* vector_get(Vector* v, int pos);
void  vector_sort(Vector* v, int (*comp)(const void* p1, const void* p2));

int vector_strcmp(const void* v1, const void* v2);

#endif
