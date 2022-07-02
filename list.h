#ifndef LIST_H
#define LIST_H

typedef enum {
	LIST_ERR_SUCCESS = 0,
	LIST_ERR_NOMEM
} List_errcode;

typedef struct List_node List_node;

struct List_node {
	void* data;
	List_node* next;
};

typedef struct {
	List_node* head;
	List_node* tail;
	size_t size;
} List;

List* list_init(void);
void  list_free(List* list, void (*free_data)(void*));
int   list_append(List* list, void* data);
char* list_error(List_errcode errcode);

#endif
