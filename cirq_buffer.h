#ifndef CIRQ_BUFFER_H
#define CIRQ_BUFFER_H

typedef struct {
	void** buffer;
	size_t size;
	size_t n;
	size_t cur;
	size_t cur_pop;
} Cirq_buffer; 

Cirq_buffer* cirq_buffer_init(size_t cb_size);
void  cirq_buffer_free(Cirq_buffer* cb);
int   cirq_buffer_push(Cirq_buffer* cb, void* entry);
void* cirq_buffer_pop(Cirq_buffer* cb);

#endif
