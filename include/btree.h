#ifndef BTREE_H
#define BTREE_H

#include "object.h"
#include <pthread.h>
#include <stdbool.h>

extern const Class btreeClass;

// 🚀 언더바(_) 소각 완료
typedef struct BTreeNode BTreeNode;
struct BTreeNode {
    Object **keys;
    Object **values;
    BTreeNode **children; // 🚀 타입 변경 적용
    int num_keys;
    bool is_leaf;
};

// 🚀 언더바(_) 소각 완료
typedef struct BTree BTree;
struct BTree {
    Object base;

    BTreeNode *root; // 🚀 타입 변경 적용
    int t;
    int size;
    pthread_mutex_t lock;

    void    (*insert)(BTree *self, Object *key, Object *value);
    Object* (*search)(BTree *self, Object *key);
    void    (*clear) (BTree *self);
    int     (*getSize)(BTree *self);
};

BTree* new_BTree(int t);

#endif // BTREE_H