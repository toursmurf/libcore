#ifndef VECTOR_H
#define VECTOR_H

#include "object.h"
#include <pthread.h>

extern const Class vectorClass;

typedef struct Vector Vector;
typedef struct VectorIterator VectorIterator; // 이름 충돌 방지

struct VectorIterator {
    Object **ptr; // ARC 전용 포인터
};

struct Vector {
    Object base; // [1] ARC 상속 필수

    Object **items;
    int size;
    int capacity;
    
    pthread_mutex_t mutex; // Thread 동기화 락

    // 메서드 인터페이스
    void (*push_back)(Vector *self, Object *item);
    Object* (*at)(Vector *self, int index);
    Object* (*pop_back)(Vector *self); // 🚀 반환형 변경 (소유권 이전)
    int (*get_size)(Vector *self);
    
    // Thread 수동 잠금 제어 (의장님 명품 철학 보존)
    void (*lock)(Vector *self);
    void (*unlock)(Vector *self);

    // 반복자
    VectorIterator (*begin)(Vector *self);
    VectorIterator (*end)(Vector *self);
};

/* [Constructor] */
Vector *new_Vector(int initial_capacity);

// Iterator 헬퍼
void it_next(VectorIterator *it);
Object* it_get(VectorIterator *it);
int it_neq(VectorIterator a, VectorIterator b);

// 🚀 [ARC 호환 매크로] Object 캐스팅 내장
#define VECTOR_FOREACH(vec, type, var) \
    for (VectorIterator _it_##var = (vec)->begin(vec); \
         it_neq(_it_##var, (vec)->end(vec)); \
         it_next(&_it_##var)) \
        for (type *var = (type*)it_get(&_it_##var); var != NULL; var = NULL)

#endif