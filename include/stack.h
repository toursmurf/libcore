#ifndef STACK_H
#define STACK_H

#include "object.h"
#include "arraylist.h"
#include <pthread.h>

extern const Class stackClass;

typedef struct Stack Stack;

struct Stack {
    Object base;
    ArrayList* container;
    pthread_mutex_t lock;

    void (*push)(Stack* self, void* data);
    void* (*pop)(Stack* self);
    void* (*peek)(Stack* self);

    bool (*isEmpty)(Stack* self);
    bool (*isFull)(Stack* self); // 🚀 [의장님 하명] 추가 완료
    int (*size)(Stack* self);
};

Stack* new_Stack(int initial_capacity);

#endif