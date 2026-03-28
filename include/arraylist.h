#ifndef ARRAYLIST_H
#define ARRAYLIST_H

#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include "object.h"

// 전방 선언
typedef struct ArrayList ArrayList;
typedef struct ArrayListIterator ArrayListIterator;

// 함수 포인터 타입 정의
typedef void (*ArrayListActionFunc)(Object* item);
typedef int (*ArrayListCompareFunc)(const void* itemA, const void* itemB);

extern const Class arrayListClass;
extern const Class arrayListIteratorClass;

/* [ArrayListIterator] ARC 대응 이터레이터 */
struct ArrayListIterator {
    Object base;
    ArrayList *list;
    int currentIndex;

    bool (*hasNext)(ArrayListIterator *self);
    Object* (*next)(ArrayListIterator *self);
};

/* [ArrayList] 의장님 스타일의 가상 함수 인터페이스 + ARC 강화 */
struct ArrayList {
    // 1. 상속 (Object)
    Object base;

    // 2. 멤버 변수
    Object** items;        // [ARC] 모든 데이터는 Object 상속 객체로 관리
    int size;
    int capacity;
    pthread_mutex_t lock;

    // 3. 메서드 (함수 포인터 바인딩)
    void (*add)(ArrayList* self, Object* item);
    Object* (*get)(ArrayList* self, int index);
    void (*remove)(ArrayList* self, int index);      // 삭제 시 RELEASE
    void (*removeResult)(ArrayList* self, int index); // 삭제만 수행

    int (*getSize)(ArrayList* self);
    void (*clear)(ArrayList* self);
    bool (*isEmpty)(ArrayList* self);
    void (*trimToSize)(ArrayList* self);
    void (*ensureCapacity)(ArrayList* self, int min_capacity);

    void (*forEach)(ArrayList* self, ArrayListActionFunc action);
    void* (*find)(ArrayList* self, void* target, ArrayListCompareFunc compare);
    void (*sort)(ArrayList* self, ArrayListCompareFunc compare);

    // Iterator 생성
    ArrayListIterator* (*iterator)(ArrayList* self);

    // 소멸 및 소유권 이전
    void (*destroy)(ArrayList* self);
    Object* (*detach)(ArrayList *self, int index); // 소유권 이전 (RELEASE 안 함)
};

/* 생성자: PascalCase 규칙 적용 */
ArrayList* new_ArrayList(int initial_capacity);

#endif // ARRAYLIST_H
