#include <stdlib.h>
#include "cirq_buffer.h"

Cirq_buffer* cirq_buffer_init(size_t cb_size)
{
	Cirq_buffer* cb = calloc(1, sizeof(*cb));

	if (cb) {
		cb->buffer = calloc(cb_size, sizeof(*cb->buffer));
		if (!cb->buffer) {
			free(cb);
			return NULL;
		}
		cb->size = cb_size;
	}

	return cb;
}

void cirq_buffer_free(Cirq_buffer* cb)
{
	free(cb->buffer);
	free(cb);
}

int cirq_buffer_push(Cirq_buffer* cb, void* entry)
{
	if (cb->n == cb->size)
		return 0;

	cb->buffer[cb->cur] = entry;
	cb->n++;
	cb->cur = (cb->cur +1) % cb->size;

	return 1;
}

void* cirq_buffer_pop(Cirq_buffer* cb)
{
	void* entry = cb->buffer[cb->cur_pop];

	if (entry) {
		cb->buffer[cb->cur_pop] = NULL;
		cb->n--;
		cb->cur_pop = (cb->cur_pop +1) % cb->size;
	}

	return entry;
}
