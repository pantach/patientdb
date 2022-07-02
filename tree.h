/* AVL Tree */

#ifndef TREE_H
#define TREE_H

#include "list.h"

typedef enum {
	TREE_ERR_SUCCESS = 0,
	TREE_ERR_NOMEM
} Tree_errcode;

typedef enum {
	TREE_PREORDER = 0,
	TREE_INORDER,
	TREE_POSTORDER
} Tree_travord;

typedef struct Tree_node Tree_node;
typedef int (*Tree_comp)(const void*, const void*);
typedef int (*Tree_act)(List*, void*);

struct Tree_node {
	List* list;
	int height;
	Tree_node* left;
	Tree_node* right;
};

typedef struct {
	Tree_node* root;
	size_t size;
	Tree_comp comp;
} Tree;

char* tree_error(Tree_errcode errcode);
Tree* tree_init(Tree_comp comp);
void  tree_free(Tree* tree, void (*free_data)(void*));
int   tree_insert(Tree* tree, void* data);
List* tree_locate(Tree* tree, void* data);
int   tree_traverse(Tree* tree, Tree_travord ord, void* cb_data, Tree_act cb);
int   tree_traverse_range(Tree* tree, Tree_travord order, void* cb_data, Tree_act cb,
                          void* min, void* max);

#endif
