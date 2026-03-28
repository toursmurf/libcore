#include <stdio.h>
#include <stdlib.h>
#include "tree.h"

#define GET_CLASS(obj) ( *( (const Class**) (obj) ) )

// ==================================================
// [내부 유틸리티] 스택 & 노드 생성
// ==================================================
typedef struct TreeStackNode {
    TreeNode* treeNode;
    struct TreeStackNode* next;
} TreeStackNode;

static void push(TreeStackNode** top, TreeNode* node) {
    TreeStackNode* sn = (TreeStackNode*)malloc(sizeof(TreeStackNode));
    sn->treeNode = node;
    sn->next = *top;
    *top = sn;
}

static TreeNode* pop(TreeStackNode** top) {
    if (!*top) return NULL;
    TreeStackNode* temp = *top;
    TreeNode* res = temp->treeNode;
    *top = temp->next;
    free(temp);
    return res;
}

static int isStackEmpty(TreeStackNode* top) { return top == NULL; }

static TreeNode* createNode(Object* data) {
    TreeNode* n = (TreeNode*)malloc(sizeof(TreeNode));
    n->data = RETAIN(data);
    n->left = n->right = NULL;
    return n;
}

// ==================================================
// [Iterator] ARC 객체 구현
// ==================================================
static bool iter_hasNext(TreeIterator* self) {
    return (self->current != NULL || !isStackEmpty(self->stack));
}

static Object* iter_next(TreeIterator* self) {
    while (self->current != NULL) {
        push(&(self->stack), self->current);
        self->current = self->current->left;
    }
    if (isStackEmpty(self->stack)) return NULL;

    TreeNode* popped = pop(&(self->stack));
    Object* data = popped->data;
    self->current = popped->right;
    return data;
}

static void TreeIterator_Finalize(Object* obj) {
    TreeIterator* self = (TreeIterator*)obj;
    while (!isStackEmpty(self->stack)) {
        pop(&(self->stack));
    }
}

const Class treeIteratorClass = {
    .name = "TreeIterator",
    .size = sizeof(TreeIterator),
    .finalize = TreeIterator_Finalize
};

TreeIterator* new_TreeIterator(Tree* tree) {
    TreeIterator* it = (TreeIterator*)calloc(1, sizeof(TreeIterator));
    Object_Init((Object*)it, &treeIteratorClass);
    it->stack = NULL;
    it->current = tree->root;
    it->hasNext = iter_hasNext;
    it->next = iter_next;
    return it;
}

// ==================================================
// [Tree] 내부 로직 (재귀 함수들)
// ==================================================
static TreeNode* _insertRec(TreeNode* node, Object* data, CompareFunc cmp) {
    if (!node) return createNode(data);
    int res = cmp(data, node->data);
    if (res < 0) node->left = _insertRec(node->left, data, cmp);
    else if (res > 0) node->right = _insertRec(node->right, data, cmp);
    return node;
}

static TreeNode* _findMin(TreeNode* node) {
    while (node && node->left) node = node->left;
    return node;
}

static TreeNode* _deleteRec(TreeNode* root, Object* key, CompareFunc cmp) {
    if (!root) return root;

    int res = cmp(key, root->data);
    if (res < 0) root->left = _deleteRec(root->left, key, cmp);
    else if (res > 0) root->right = _deleteRec(root->right, key, cmp);
    else {
        if (!root->left) {
            TreeNode* temp = root->right;
            RELEASE(root->data);
            free(root);
            return temp;
        } else if (!root->right) {
            TreeNode* temp = root->left;
            RELEASE(root->data);
            free(root);
            return temp;
        }

        TreeNode* temp = _findMin(root->right);

        Object* tempPtr = root->data;
        root->data = temp->data;
        temp->data = tempPtr;

        root->right = _deleteRec(root->right, tempPtr, cmp);
    }
    return root;
}

static void _foreachRec(TreeNode* node, void (*func)(Object*)) {
    if (!node) return;
    _foreachRec(node->left, func);
    func(node->data);
    _foreachRec(node->right, func);
}

static void _freeAll(TreeNode* node) {
    if (!node) return;
    _freeAll(node->left);
    _freeAll(node->right);
    RELEASE(node->data);
    free(node);
}

static int _getHeightRec(TreeNode* node) {
    if (!node) return 0;
    int l = _getHeightRec(node->left);
    int r = _getHeightRec(node->right);
    return (l > r ? l : r) + 1;
}

// ==================================================
// [Tree] 공개 API 래퍼
// ==================================================
static void impl_insert(Tree* self, Object* data) {
    self->root = _insertRec(self->root, data, self->compare);
}

static void impl_insertAt(TreeNode* parent, Object* data, int isLeft) {
    if (!parent) return;
    TreeNode* newNode = createNode(data);
    if (isLeft) {
        newNode->left = parent->left;
        parent->left = newNode;
    } else {
        newNode->right = parent->right;
        parent->right = newNode;
    }
}

static Object* impl_search(Tree* self, Object* key) {
    TreeNode* cur = self->root;
    while (cur) {
        int res = self->compare(key, cur->data);
        if (res == 0) return cur->data;
        if (res < 0) cur = cur->left;
        else cur = cur->right;
    }
    return NULL;
}

static void impl_remove(Tree* self, Object* key) {
    self->root = _deleteRec(self->root, key, self->compare);
}

static void impl_foreach(Tree* self, void (*func)(Object*)) {
    _foreachRec(self->root, func);
}

static TreeIterator* impl_createIterator(Tree* self) {
    return new_TreeIterator(self);
}

static void impl_traverseBFS(Tree* self) {
    if (!self->root) return;
    TreeNode* queue[100];
    int front = 0, rear = 0;
    queue[rear++] = self->root;

    char buf[128];
    printf("BFS Level Order: ");
    while (front < rear) {
        TreeNode* cur = queue[front++];
        if (cur->data) {
            GET_CLASS(cur->data)->toString(cur->data, buf, sizeof(buf));
            printf("[%s] ", buf);
        }
        if (cur->left) queue[rear++] = cur->left;
        if (cur->right) queue[rear++] = cur->right;
    }
    printf("\n");
}

static int impl_getHeight(TreeNode* node) { return _getHeightRec(node); }

static void impl_clear(Tree* self) {
    _freeAll(self->root);
    self->root = NULL;
}

// ==================================================
// [ARC v2.0] Tree 소각로
// ==================================================
static void Tree_Finalize(Object* obj) {
    Tree* self = (Tree*)obj;
    impl_clear(self);
}

const Class treeClass = {
    .name = "Tree",
    .size = sizeof(Tree),
    .finalize = Tree_Finalize
};

Tree* new_Tree(CompareFunc cmp) {
    Tree* t = (Tree*)calloc(1, sizeof(Tree));
    Object_Init((Object*)t, &treeClass);

    t->root = NULL;
    t->compare = cmp;

    t->insert = impl_insert;
    t->insertAt = impl_insertAt;
    t->search = impl_search;
    t->remove = impl_remove;
    t->foreach = impl_foreach;
    t->createIterator = impl_createIterator;
    t->traverseBFS = impl_traverseBFS;
    t->getHeight = impl_getHeight;
    t->clear = impl_clear;

    return t;
}