#include <stdio.h>
#include <stdlib.h>
#include "btree.h"

/* =========================================
 * [내부 구현체] 노드 관리 및 ARC 소각로
 * ========================================= */
static BTreeNode* create_node(int t, bool is_leaf) {
    BTreeNode *node = (BTreeNode*)calloc(1, sizeof(BTreeNode));
    if (!node) return NULL;

    node->is_leaf = is_leaf;
    node->num_keys = 0;
    // 2t - 1 개의 키와 값, 2t 개의 자식 포인터 할당
    node->keys = (Object**)calloc(2 * t - 1, sizeof(Object*));
    node->values = (Object**)calloc(2 * t - 1, sizeof(Object*));
    node->children = (BTreeNode**)calloc(2 * t, sizeof(BTreeNode*));

    return node;
}

// 🚀 내부 노드 파괴 시 완벽한 RELEASE 연쇄 폭발!
static void BTree_free_node(BTree *self, BTreeNode *node) {
    if (!node) return;

    // 현재 노드가 소유한 key와 value 모두 해제!
    for (int i = 0; i < node->num_keys; i++) {
        RELEASE(node->keys[i]);
        RELEASE(node->values[i]);
    }

    // 자식 노드가 있다면 재귀적으로 소각
    if (!node->is_leaf) {
        for (int i = 0; i <= node->num_keys; i++) {
            BTree_free_node(self, node->children[i]);
        }
    }

    free(node->keys);
    free(node->values);
    free(node->children);
    free(node);
}

/* =========================================
 * [내부 구현체] BTree 알고리즘 핵심 (Lock-Free 내부 함수)
 * ========================================= */
static void BTree_split_child(BTree *self, BTreeNode *parent, int i, BTreeNode *y) {
    int t = self->t;
    BTreeNode *z = create_node(t, y->is_leaf);
    if (!z) return;
    z->num_keys = t - 1;

    // y의 우측 절반을 z로 이동 (소유권 이전이므로 별도 RETAIN 불필요)
    for (int j = 0; j < t - 1; j++) {
        z->keys[j] = y->keys[j + t];
        z->values[j] = y->values[j + t];
    }
    if (!y->is_leaf) {
        for (int j = 0; j < t; j++) z->children[j] = y->children[j + t];
    }
    y->num_keys = t - 1;

    // parent 자식 배열 이동 및 중간 키 승격
    for (int j = parent->num_keys; j >= i + 1; j--) {
        parent->children[j + 1] = parent->children[j];
    }
    parent->children[i + 1] = z;

    for (int j = parent->num_keys - 1; j >= i; j--) {
        parent->keys[j + 1] = parent->keys[j];
        parent->values[j + 1] = parent->values[j];
    }

    parent->keys[i] = y->keys[t - 1];     // 중간 키 이동 (소유권 유지)
    parent->values[i] = y->values[t - 1]; // 중간 값 이동
    parent->num_keys++;
}

static void BTree_insert_non_full(BTree *self, BTreeNode *node, Object *key, Object *value) {
    int i = node->num_keys - 1;
    // (본 예제에서는 Object 비교를 위해 임의의 해시/주소 비교 로직을 단순화합니다)
    // 실제 구현 시 key->clazz->compare(key1, key2) 사용 권장!

    if (node->is_leaf) {
        // 자리 찾기 (여기서는 단순 포인터 값으로 정렬한다고 가정)
        while (i >= 0 && (void*)node->keys[i] > (void*)key) {
            node->keys[i + 1] = node->keys[i];
            node->values[i + 1] = node->values[i];
            i--;
        }
        // 🚀 [버그 2 수정 완료] 소유권(RETAIN) 완벽 획득!
        node->keys[i + 1] = RETAIN(key);
        node->values[i + 1] = RETAIN(value);
        node->num_keys++;
    } else {
        while (i >= 0 && (void*)node->keys[i] > (void*)key) i--;
        i++;
        if (node->children[i]->num_keys == 2 * self->t - 1) {
            BTree_split_child(self, node, i, node->children[i]);
            if ((void*)node->keys[i] < (void*)key) i++;
        }
        BTree_insert_non_full(self, node->children[i], key, value);
    }
}

/* =========================================
 * [공개 API] Mutex 래퍼
 * ========================================= */
static void impl_insert(BTree *self, Object *key, Object *value) {
    pthread_mutex_lock(&self->lock);
    BTreeNode *r = self->root;

    if (r->num_keys == 2 * self->t - 1) {
        BTreeNode *s = create_node(self->t, false);
        self->root = s;
        s->children[0] = r;
        BTree_split_child(self, s, 0, r);
        BTree_insert_non_full(self, s, key, value);
    } else {
        BTree_insert_non_full(self, r, key, value);
    }
    self->size++;
    pthread_mutex_unlock(&self->lock);
}

// 탐색 로직 (간략화)
static Object* impl_search(BTree *self, Object *key) {
    // ... 탐색 로직 구현 ...
    (void)self;
    (void) key;
    return NULL;
}

// 🚀 [버그 3 수정 완료] clear 후 root NULL 체크 방어막 전개!
static void impl_clear(BTree *self) {
    pthread_mutex_lock(&self->lock);
    if (self->root) {
        BTree_free_node(self, self->root);
    }

    self->root = create_node(self->t, true);
    if (!self->root) {
        // 투스 IT 홀딩스 규격: 심각한 에러 로깅!
        fprintf(stderr, "[LOG_E] BTree clear: 루트 노드 재생성 실패! (메모리 고갈)\n");
    }
    self->size = 0;
    pthread_mutex_unlock(&self->lock);
}

static int impl_getSize(BTree *self) {
    pthread_mutex_lock(&self->lock);
    int s = self->size;
    pthread_mutex_unlock(&self->lock);
    return s;
}

/* =========================================
 * [ARC v2.0] Object 연동
 * ========================================= */
static void BTree_Finalize(Object* obj) {
    BTree* self = (BTree*)obj;
    if (self->root) {
        BTree_free_node(self, self->root);
    }
    pthread_mutex_destroy(&self->lock);
    // free(self) 없음! 부모가 처리!
}

static void BTree_ToString(Object* obj, char* buffer, size_t len) {
    BTree* self = (BTree*)obj;
    snprintf(buffer, len, "BTree(Size=%d, t=%d)", impl_getSize(self), self->t);
}

const Class btreeClass = {
    .name = "BTree",
    .size = sizeof(BTree),
    .toString = BTree_ToString,
    .finalize = BTree_Finalize
};

/* =========================================
 * [생성자]
 * ========================================= */
BTree* new_BTree(int t) {
    BTree *tree = (BTree*)calloc(1, sizeof(BTree));
    if (!tree) return NULL;

    Object_Init((Object*)tree, &btreeClass);
    tree->t = t < 2 ? 2 : t; // 최소 차수 2 보장
    tree->root = create_node(tree->t, true);
    tree->size = 0;

    pthread_mutex_init(&tree->lock, NULL);

    tree->insert = impl_insert;
    tree->search = impl_search;
    tree->clear = impl_clear;
    tree->getSize = impl_getSize;

    return tree;
}
