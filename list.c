#include <stdlib.h>
#include "list.h"

static List_node*  list_node_init(void* data);
static inline void list_node_free(List_node* node, void (*free_data)(void*));

char* list_error(List_errcode errcode)
{
	struct List_err {
		List_errcode errcode;
		char* errmsg;
	} err[] = {
		{ LIST_ERR_SUCCESS, "Success" },
		{ LIST_ERR_NOMEM,   "Out of memory" }
	};

	return err[errcode].errmsg;
}

List* list_init(void)
{
	List* list = calloc(1, sizeof(*list));
	return list;
}

void list_free(List* list, void (*free_data)(void*))
{
	List_node* node = list->head;
	List_node* tmp;

	while (node) {
		tmp = node->next;
		list_node_free(node, free_data);
		node = tmp;
	}

	free(list);
}

int list_append(List* list, void* data)
{
	List_node* node = list_node_init(data);
	if (!node)
		return LIST_ERR_NOMEM;

	if (!list->tail)
		list->head = list->tail = node;
	else {
		list->tail->next = node;
		list->tail = node;
	}
	list->size++;

	return 0;
}

static List_node* list_node_init(void* data)
{
	List_node* node = malloc(sizeof(*node));
	if (node) {
		node->data = data;
		node->next = NULL;
	}

	return node;
}

static inline void list_node_free(List_node* node, void (*free_data)(void*))
{
	if (free_data)
		free_data(node->data);
	free(node);
}
