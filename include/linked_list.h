#ifndef LINKED_LIST_H
#define LINKED_LIST_H

#include <pthread.h>
#include "libcore.h"

extern const Class linkedListClass;

// 🚀 충돌 방지: Node -> LinkedListNode
typedef struct LinkedListNode LinkedListNode;
struct LinkedListNode {
    Object* data;       // RETAIN/RELEASE 대상!
    LinkedListNode* next;
};

// 리스트 관리 구조체
typedef struct LinkedList LinkedList;
struct LinkedList {
    Object base;

    LinkedListNode* head; // 🚀 타입 변경 적용
    int size;

    pthread_mutex_t lock;

    void (*add_node)(LinkedList* self, void* data);
    void (*delete_node)(LinkedList* self, void* data, int (*compare)(Object*, Object*));
    void (*print_list)(LinkedList* self, void (*display)(Object*));
    int (*get_size)(LinkedList* self);
    int (*getSize)(LinkedList* self);
};

LinkedList* new_LinkedList(void);

#endif
