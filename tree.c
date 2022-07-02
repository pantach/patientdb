// #define _XOPEN_SOURCE
#include <stdlib.h>
#include "tree.h"

static Tree_node* init(void* data);
static void       free_recurs(Tree_node* node, void (*free_data)(void*));
static Tree_node* insert_recurs(Tree_node* node, void* data,
                                          int (*comp)(const void*, const void*));
static List*      locate_recurs(Tree_node* node, void* data, Tree_comp comp);
static int traverse(Tree_node* node, Tree_travord order, void* cb_data,
                    Tree_act cb);
static int traverse_range(Tree_node* node, Tree_travord order, void* cb_data,
                          Tree_act cb, Tree_comp comp, void* min, void* max);

static Tree_node* rotate_right(Tree_node* node);
static Tree_node* rotate_left(Tree_node* node);
static int max(int i, int j);

/*
#include <stdio.h>
#include <time.h>

int int_comp(const void* i1, const void* i2)
{
	return *(int*)i1 -*(int*)i2;
}

int int_cb(List* list, const void* cb_data)
{
	int list_data = *(int*)list->head->data;

	if (list->size == 1)
		printf("%d\n", list_data);
	else
		printf("%d: %ld\n", list_data, list->size);

	return 0;
}

int date_comp(const void* i1, const void* i2)
{
	struct tm date1 = {0};
	struct tm date2 = {0};

	strptime(i1, "%d-%m-%Y", &date1);
	strptime(i2, "%d-%m-%Y", &date2);

	return difftime(mktime(&date1), mktime(&date2));
}

int date_cb(List* list, const void* cb_data)
{
	char* list_data = (char*)list->head->data;

	if (!date_comp("14-3-2001", list_data))
		return -2;

	return 0;
}

int main(void)
{
	const int SIZE = 10;
	char arr[10][SIZE];
	Tree* tree = tree_init(date_comp);
	char* min = "2-2-2010";
	char* max = "19-6-2015";

	srand(1);
	for (int i = 0; i < SIZE; ++i) {
		sprintf(arr[i], "%d-%d-%d", rand() % 31 +1, rand() % 12 +1, rand() % 20 +2000);
		tree_insert(tree, &arr[i]);
		printf("Inserted %s\n", arr[i]);
	}

	int ret;

	ret = tree_traverse(tree, TREE_INORDER, NULL, date_cb);
	printf("ret: %d\n\n", ret);

	ret = tree_traverse_range(tree, TREE_INORDER, NULL, date_cb, min, max);
	printf("ret: %d\n\n", ret);

	tree_free(tree, NULL);

	return 0;
}
*/

char* tree_error(Tree_errcode errcode)
{
	struct Tree_err {
		Tree_errcode errcode;
		char* errmsg;
	} err[] = {
		{ TREE_ERR_SUCCESS, "Success" },
		{ TREE_ERR_NOMEM,   "Out of memory" }
	};

	return err[errcode].errmsg;
}


Tree* tree_init(int (*comp)(const void*, const void*))
{
	Tree* tree = malloc(sizeof(*tree));

	if (tree) {
		tree->root = NULL;
		tree->size = 0;
		tree->comp = comp;
	}

	return tree;
}

void tree_free(Tree* tree, void (*free_data)(void*))
{
	free_recurs(tree->root, free_data);
	free(tree);
}

int tree_insert(Tree* tree, void* data)
{
	tree->root = insert_recurs(tree->root, data, tree->comp);
	if (!tree->root)
		return TREE_ERR_NOMEM;

	tree->size++;

	return 0;
}

List* tree_locate(Tree* tree, void* data)
{
	return locate_recurs(tree->root, data, tree->comp);
}

int tree_traverse(Tree* tree, Tree_travord order, void* cb_data, Tree_act cb)
{
	return traverse(tree->root, order, cb_data, cb);
}

int tree_traverse_range(Tree* tree, Tree_travord order, void* cb_data, Tree_act cb,
                        void* min, void* max)
{
	return traverse_range(tree->root, order, cb_data, cb, tree->comp, min, max);
}

static Tree_node* init(void* data)
{
	Tree_node* node = calloc(1, sizeof(*node));
	if (node) {
		node->list = list_init();
		if (!node->list) {
			free(node);
			return NULL;
		}
		list_append(node->list, data);
	}

	return node;
}

static void free_recurs(Tree_node* node, void (*free_data)(void*))
{
	if (node == NULL)
		return;

	free_recurs(node->right, free_data);
	free_recurs(node->left, free_data);

	list_free(node->list, free_data);
	free(node);
}

int max(int i, int j)
{
	return i > j ? i : j;
}

int height(Tree_node* node)
{
	if (!node)
		return -1;

	return node->height;
}

static Tree_node* insert_recurs(Tree_node* node, void* data, Tree_comp comp)
{
	void* node_data;
	int balance;
	int rel;

	if (node == NULL)
		return init(data);

	node_data = node->list->head->data;

	rel = comp(node_data, data);
	if (rel > 0) {
		node->left  = insert_recurs(node->left, data, comp);
	}
	else if (rel < 0) {
		node->right = insert_recurs(node->right, data, comp);
	}
	else
		list_append(node->list, data);

	node->height = 1 +max(height(node->left), height(node->right));

	balance = height(node->left) -height(node->right);

	if (balance > 1) {
		node_data = node->left->list->head->data;
		rel = comp(node_data, data);
		if (rel > 0)
			node = rotate_right(node);
		else if (rel < 0) {
			node->left = rotate_left(node->left);
			node = rotate_right(node);
		}
	}
	else if (balance < -1) {
		node_data = node->right->list->head->data;
		rel = comp(node_data, data);
		if (rel < 0)
			node = rotate_left(node);
		else if (rel > 0) {
			node->right = rotate_right(node->right);
			node = rotate_left(node);
		}
	}

	return node;
}

Tree_node* rotate_right(Tree_node* node)
{
	Tree_node* lnode  = node->left;
	Tree_node* lrnode = lnode->right;

	lnode->right = node;
	node->left = lrnode;

	node->height  = 1 +max(height(node->left),  height(node->right));
	lnode->height = 1 +max(height(lnode->left), height(lnode->right));

	return lnode;
}

Tree_node* rotate_left(Tree_node* node)
{
	Tree_node* rnode  = node->right;
	Tree_node* rlnode = rnode->left;

	rnode->left = node;
	node->right = rlnode;

	node->height  = 1 +max(height(node->left),  height(node->right));
	rnode->height = 1 +max(height(rnode->left), height(rnode->right));

	return rnode;
}

static List* locate_recurs(Tree_node* node, void* data, Tree_comp comp)
{
	void* node_data;
	List* list;
	int rel;

	if (node == NULL)
		return NULL;

	node_data = node->list->head->data;

	rel = comp(node_data, data);
	if (rel > 0)
		list = locate_recurs(node->left, data, comp);

	else if (rel < 0)
		list = locate_recurs(node->right, data, comp);

	else
		list = node->list;

	return list;
}

/* Traverse the whole tree calling the callback function cb for every node, until the
 * callback returns a non-zero value, in which case the traversal stops and the callback's
 * return value is propagated to the top
 * */
int traverse(Tree_node* node, Tree_travord order, void* cb_data, Tree_act cb)
{
	int ret;

	if (node == NULL)
		return 0;

	switch (order) {
	case TREE_PREORDER:
		if ((ret = cb(node->list, cb_data))) break;
		if ((ret = traverse(node->left,  order, cb_data, cb))) break;
		if ((ret = traverse(node->right, order, cb_data, cb))) break;
		break;
	case TREE_INORDER:
		if ((ret = traverse(node->left,  order, cb_data, cb))) break;
		if ((ret = cb(node->list, cb_data))) break;
		if ((ret = traverse(node->right, order, cb_data, cb))) break;
		break;
	case TREE_POSTORDER:
		if ((ret = traverse(node->left,  order, cb_data, cb))) break;
		if ((ret = traverse(node->right, order, cb_data, cb))) break;
		if ((ret = cb(node->list, cb_data))) break;
		break;
	}

	return ret;
}

int traverse_range(Tree_node* node, Tree_travord order, void* cb_data, Tree_act cb,
                   Tree_comp comp, void* min, void* max)
{
	if (node == NULL)
		return 0;

	void* node_data = node->list->head->data;
	int min_comp = comp(node_data, min);
	int max_comp = comp(node_data, max);
	int ret = 0;

	switch (order) {
	case TREE_PREORDER:
		if (min_comp >= 0 && max_comp <= 0)
			if ((ret = cb(node->list, cb_data))) break;

		if (min_comp > 0)
			if ((ret = traverse_range(node->left,  order, cb_data, cb, comp, min, max)))
				break;

		if (max_comp < 0)
			if ((ret = traverse_range(node->right, order, cb_data, cb, comp, min, max)))
				break;
		break;

	case TREE_INORDER:
		if (min_comp > 0)
			if ((ret = traverse_range(node->left,  order, cb_data, cb, comp, min, max)))
				break;

		if (min_comp >= 0 && max_comp <= 0)
			if ((ret = cb(node->list, cb_data))) break;

		if (max_comp < 0)
			if ((ret = traverse_range(node->right, order, cb_data, cb, comp, min, max)))
				break;
		break;

	case TREE_POSTORDER:
		if (min_comp > 0)
			if ((ret = traverse_range(node->left,  order, cb_data, cb, comp, min, max)))
				break;

		if (max_comp < 0)
			if ((ret = traverse_range(node->right, order, cb_data, cb, comp, min, max)))
				break;

		if (min_comp >= 0 && max_comp <= 0)
			if ((ret = cb(node->list, cb_data))) break;

		break;
	}

	return ret;
}
