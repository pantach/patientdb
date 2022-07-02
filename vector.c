#include <stdlib.h>
#include <string.h>
#include "vector.h"

#define VECTOR_GROWTH_FACTOR 2

static void* vector_resize(Vector* v, size_t num);

Vector* vector_init(void)
{
	Vector* v = calloc(1, sizeof(*v));

	return v;
}

void vector_free(Vector* v, void (*free_entry)(void*))
{
	unsigned int i;

	if (v) {
		if (free_entry)
			for (i = 0; i < v->size; ++i)
				free_entry(v->entry[i]);

		free(v->entry);
		free(v);
	}
}

static void* vector_resize(Vector* v, size_t num)
{
	void* tmp;

	tmp = realloc(v->entry, num*sizeof(*v->entry));
	if (!tmp)
		return NULL;

	v->entry = tmp;
	v->capacity = num;

	return v->entry;
}

int vector_append(Vector* v, void* entry)
{
	if (v->size == v->capacity)
		if (!vector_resize(v, (v->capacity +1)*VECTOR_GROWTH_FACTOR))
			return -1;

	v->entry[v->size] = entry;
	v->size++;

	return 0;
}

int vector_find(Vector* v, void* entry, int (*comp)(const void* p1, const void* p2))
{
	for (int i = 0; i < v->size; ++i)
		if (!comp(v->entry[i], entry))
			return i;

	return -1;
}

void* vector_get(Vector* v, int pos)
{
	if (pos >= v->size)
		return NULL;

	return v->entry[pos];
}

void vector_sort(Vector* v, int (*comp)(const void* p1, const void* p2))
{
	qsort(v->entry, v->size, sizeof(void*), comp);
}

int vector_strcmp(const void* v1, const void* v2)
{
	return strcasecmp((char*)v1, (char*)v2);
}
