#ifndef TREE_H
#define TREE_H

#include "object.h"
#include <stdbool.h>

// --------------------------------------------------
// 1. 타입 및 구조체 정의
// --------------------------------------------------
extern const Class treeClass;
extern const Class treeIteratorClass;

// 🚀 언더바(_) 소각 완료!
typedef struct TreeNode TreeNode;
struct TreeNode {
    Object* data;
    TreeNode *left, *right;
};

// 비교 콜백 (Object 기반)
typedef int (*CompareFunc)(Object* a, Object* b);

// 🚀 충돌 방지: 범용 Stack과 겹치지 않게 Tree 전용 명시!
struct TreeStackNode;

// 🚀 언더바(_) 소각 완료!
typedef struct TreeIterator TreeIterator;
struct TreeIterator {
    Object base; // [상속] 메모리 누수 방지용

    struct TreeStackNode* stack; // 🚀 이름 변경 반영
    TreeNode* current;

    bool    (*hasNext)(TreeIterator* self);
    Object* (*next)   (TreeIterator* self);
};

// 트리 객체 구조체
typedef struct Tree Tree;
struct Tree {
    Object base; // [상속] ARC 심장

    TreeNode* root;
    CompareFunc compare;

    // [메서드] 다형성 인터페이스
    void (*insert)  (Tree* self, Object* data);
    void (*insertAt)(TreeNode* parent, Object* data, int isLeft);

    Object* (*search) (Tree* self, Object* key);
    void    (*remove) (Tree* self, Object* key);

    // [메서드] 순회
    void (*foreach)    (Tree* self, void (*func)(Object*));
    TreeIterator* (*createIterator)(Tree* self);
    void (*traverseBFS)(Tree* self); // Object의 toString 활용

    int  (*getHeight)(TreeNode* node);
    void (*clear)    (Tree* self);
};

// --------------------------------------------------
// 2. 생성자 프로토타입 (Toos 코딩 표준 8호)
// --------------------------------------------------
Tree* new_Tree(CompareFunc cmp);
TreeIterator* new_TreeIterator(Tree* tree);

#endif // TREE_H