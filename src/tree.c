#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tree.h"

/* =================================================================
 * [핵심] 트리 재귀 깊이 제한
 * ================================================================= */
#define MAX_TREE_DEPTH 1000
#define GET_CLASS(obj) ( *( (const Class**) (obj) ) )

// ==================================================
// [내부 유틸리티] 동적 스택 & 큐 (복구 완료!!!!)
// ==================================================
typedef struct TreeStackNode {
    TreeNode* treeNode;
    struct TreeStackNode* next;
} TreeStackNode;

typedef struct TreeQueueNode {
    TreeNode* treeNode;
    struct TreeQueueNode* next;
} TreeQueueNode;

typedef struct {
    TreeQueueNode* front;
    TreeQueueNode* rear;
} DynamicQueue;

static void push(TreeStackNode** top, TreeNode* node) {
    TreeStackNode* sn;
    sn = (TreeStackNode*)malloc(sizeof(TreeStackNode));

    if (!sn) {
        return;
    }

    sn->treeNode = node;
    sn->next = *top;
    *top = sn;
}

static TreeNode* pop(TreeStackNode** top) {
    if (!*top) {
        return NULL;
    }

    TreeStackNode* temp;
    temp = *top;

    TreeNode* res;
    res = temp->treeNode;

    *top = temp->next;

    free(temp);
    return res;
}

static void enqueue(DynamicQueue* q, TreeNode* node) {
    TreeQueueNode* qn;
    qn = (TreeQueueNode*)malloc(sizeof(TreeQueueNode));

    if (!qn) {
        return;
    }

    qn->treeNode = node;
    qn->next = NULL;

    if (q->rear == NULL) {
        q->front = qn;
        q->rear = qn;
        return;
    }

    q->rear->next = qn;
    q->rear = qn;
}

static TreeNode* dequeue(DynamicQueue* q) {
    if (q->front == NULL) {
        return NULL;
    }

    TreeQueueNode* temp;
    temp = q->front;

    TreeNode* node;
    node = temp->treeNode;

    q->front = q->front->next;

    if (q->front == NULL) {
        q->rear = NULL;
    }

    free(temp);
    return node;
}

// ==================================================
// [TreeNode] Raw Struct 생성 및 소각
// ==================================================
static TreeNode* createNode(Object* data) {
    TreeNode* n;
    n = (TreeNode*)malloc(sizeof(TreeNode));

    if (!n) {
        return NULL;
    }

    n->data = RETAIN(data);
    n->left = NULL;
    n->right = NULL;

    return n;
}

// 🚀 [보안 패치] 가장 확실한 재귀적 노드 소각!!!!
static void _freeNodesRecursive(TreeNode* node) {
    if (!node) {
        return;
    }

    if (node->left) {
        _freeNodesRecursive(node->left);
    }

    if (node->right) {
        _freeNodesRecursive(node->right);
    }

    if (node->data) {
        RELEASE(node->data);
    }

    free(node);
}

// ==================================================
// [TreeIterator] ARC 객체 구현
// ==================================================
static bool iter_hasNext(TreeIterator* self) {
    if (self->current != NULL) {
        return true;
    }

    return self->stack != NULL;
}

static Object* iter_next(TreeIterator* self) {
    while (self->current != NULL) {
        push((TreeStackNode**)&(self->stack), self->current);
        self->current = self->current->left;
    }

    if (self->stack == NULL) {
        return NULL;
    }

    TreeNode* popped;
    popped = pop((TreeStackNode**)&(self->stack));

    Object* data;
    data = popped->data;

    self->current = popped->right;
    return data;
}

static void TreeIterator_Finalize(Object* obj) {
    TreeIterator* self;
    self = (TreeIterator*)obj;

    TreeStackNode* stack;
    stack = (TreeStackNode*)self->stack;

    while (stack != NULL) {
        pop(&stack);
    }
}

const Class treeIteratorClass = {
    .name = "TreeIterator",
    .size = sizeof(TreeIterator),
    .finalize = TreeIterator_Finalize
};

TreeIterator* new_TreeIterator(Tree* tree) {
    TreeIterator* it;
    it = (TreeIterator*)calloc(1, sizeof(TreeIterator));

    if (!it) {
        return NULL;
    }

    Object_Init((Object*)it, &treeIteratorClass);
    it->current = tree->root;
    it->hasNext = iter_hasNext;
    it->next = iter_next;

    return it;
}

// ==================================================
// [Tree] 내부 재귀 로직 (VTable 복구 완료!!!!)
// ==================================================
static TreeNode* _insertRec(TreeNode* node, Object* data, CompareFunc cmp, int depth) {
    if (depth > MAX_TREE_DEPTH) {
        return node;
    }

    if (!node) {
        return createNode(data);
    }

    int res;
    res = cmp(data, node->data);

    if (res < 0) {
        node->left = _insertRec(node->left, data, cmp, depth + 1);
    }
    else if (res > 0) {
        node->right = _insertRec(node->right, data, cmp, depth + 1);
    }

    return node;
}

static TreeNode* _findMin(TreeNode* node) {
    while (node) {
        if (!node->left) {
            break;
        }
        node = node->left;
    }
    return node;
}

static TreeNode* _deleteRec(TreeNode* root, Object* key, CompareFunc cmp, int depth) {
    if (depth > MAX_TREE_DEPTH) {
        return root;
    }

    if (!root) {
        return root;
    }

    int res;
    res = cmp(key, root->data);

    if (res < 0) {
        root->left = _deleteRec(root->left, key, cmp, depth + 1);
    }
    else if (res > 0) {
        root->right = _deleteRec(root->right, key, cmp, depth + 1);
    }
    else {
        // 🚀 [패치] Raw Struct 해제 방식으로 변경 (RELEASE 후 free)
        if (!root->left) {
            TreeNode* temp;
            temp = root->right;
            RELEASE(root->data);
            free(root);
            return temp;
        }
        else if (!root->right) {
            TreeNode* temp;
            temp = root->left;
            RELEASE(root->data);
            free(root);
            return temp;
        }

        TreeNode* temp;
        temp = _findMin(root->right);

        Object* oldData;
        oldData = root->data;

        root->data = RETAIN(temp->data);
        RELEASE(oldData);

        root->right = _deleteRec(root->right, root->data, cmp, depth + 1);
    }

    return root;
}

static void _foreachRec(TreeNode* node, void (*func)(Object*), int depth) {
    if (!node) {
        return;
    }

    if (depth > MAX_TREE_DEPTH) {
        return;
    }

    _foreachRec(node->left, func, depth + 1);

    if (func) {
        func(node->data);
    }

    _foreachRec(node->right, func, depth + 1);
}

static int _getHeightRec(TreeNode* node, int depth) {
    if (!node || depth > MAX_TREE_DEPTH) {
        return 0;
    }

    int l;
    l = _getHeightRec(node->left, depth + 1);

    int r;
    r = _getHeightRec(node->right, depth + 1);

    if (l > r) {
        return l + 1;
    }

    return r + 1;
}

// ==================================================
// [Tree] 공개 API 래퍼
// ==================================================
static void impl_insert(Tree* self, Object* data) {
    if (!self) {
        return;
    }
    self->root = _insertRec(self->root, data, self->compare, 0);
}

static void impl_remove(Tree* self, Object* key) {
    if (!self) {
        return;
    }
    self->root = _deleteRec(self->root, key, self->compare, 0);
}

static void impl_foreach(Tree* self, void (*func)(Object*)) {
    if (!self) {
        return;
    }
    _foreachRec(self->root, func, 0);
}

static void impl_traverseBFS(Tree* self) {
    if (!self) {
        return;
    }

    if (!self->root) {
        return;
    }

    DynamicQueue q;
    q.front = NULL;
    q.rear = NULL;

    enqueue(&q, self->root);

    char buf[128];
    printf("BFS Level Order: ");

    while (q.front != NULL) {
        TreeNode* cur;
        cur = dequeue(&q);

        if (cur->data) {
            const Class* cls;
            cls = GET_CLASS(cur->data);
            cls->toString(cur->data, buf, sizeof(buf));
            printf("[%s] ", buf);
        }

        if (cur->left) {
            enqueue(&q, cur->left);
        }

        if (cur->right) {
            enqueue(&q, cur->right);
        }
    }

    printf("\n");
}

static void impl_clear(Tree* self) {
    if (!self) {
        return;
    }

    if (self->root) {
        _freeNodesRecursive(self->root);
        self->root = NULL;
    }
}

static int impl_getHeight(TreeNode* node) {
    return _getHeightRec(node, 0);
}

static void Tree_Finalize(Object* obj) {
    Tree* self;
    self = (Tree*)obj;
    impl_clear(self);
}

const Class treeClass = {
    .name = "Tree",
    .size = sizeof(Tree),
    .finalize = Tree_Finalize
};

Tree* new_Tree(CompareFunc cmp) {
    Tree* t;
    t = (Tree*)calloc(1, sizeof(Tree));

    if (!t) {
        return NULL;
    }

    Object_Init((Object*)t, &treeClass);

    t->root = NULL;
    t->compare = cmp;

    t->insert = impl_insert;
    t->remove = impl_remove;
    t->foreach = impl_foreach;
    t->traverseBFS = impl_traverseBFS;
    t->getHeight = impl_getHeight;
    t->createIterator = (TreeIterator* (*)(Tree*))new_TreeIterator;
    t->clear = impl_clear;

    return t;
}