#ifndef LIST_H
#define LIST_H

#include "object.h"
#include <pthread.h>
#include <stdbool.h>

extern const Class listClass;

// 🚀 언더바(_) 소각 완료
typedef struct ListNode ListNode;
struct ListNode {
    Object *data;
    ListNode *prev;
    ListNode *next;
};

// 🚀 언더바(_) 소각 완료
typedef struct List List;
struct List {
    Object base;

    ListNode *head;
    ListNode *tail;
    int size;
    pthread_mutex_t lock;

    // --- 메서드 ---
    void    (*pushBack) (List *self, Object *data);
    void    (*pushFront)(List *self, Object *data);
    Object* (*popBack)  (List *self);
    Object* (*popFront) (List *self);

    void    (*insertAt) (List *self, int index, Object *data);
    Object* (*removeAt) (List *self, int index);

    Object* (*get)      (List *self, int index);
    void    (*clear)    (List *self);
    int     (*getSize)  (List *self);
};

List* new_List(void);

#endif // LIST_H