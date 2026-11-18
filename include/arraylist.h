#ifndef ARRAYLIST_H  
#define ARRAYLIST_H  
  
#include <pthread.h>  
#include <stdbool.h>  
#include <stdlib.h>  
#include "object.h"

/*
 * [콜백 및 수명 계약]
 * 1. ArrayListActionFunc (forEach용):
 *    - 스냅샷 시점에 내부적으로 RETAIN된 원소(Object*)가 전달되며,
 *      콜백 반환 직후 리스트가 RELEASE합니다. (락 외부에서 실행됨)
 *    - 주의: 콜백 내에서 전달받은 item의 소유권을 임의로 해제(RELEASE)하지 마십시오.
 *
 * 2. ArrayListCompareFunc (find / sort 공용 시그니처):
 *    - find(self, target, compare) 호출 시:
 *        itemA = 리스트 원소 포인터 (const Object*)
 *        itemB = 검색 대상 포인터 (const void* target)
 *    - sort(self, compare) 호출 시 (C 표준 qsort 규약):
 *        itemA = 배열 슬롯 주소 (const Object* const*)
 *        itemB = 배열 슬롯 주소 (const Object* const*)
 *    - 주의: find 및 sort는 뮤텍스 락을 보유한 상태에서 compare를 호출하므로,
 *            compare 내부에서 동일 ArrayList 인스턴스의 메서드를 재진입 호출하지 마십시오.
 *
 * 3. Finalize 재진입 금지 계약:
 *    - 객체의 finalize 콜백(소멸 중인 상태)은 이미 파괴 대기 중인 부모 컨테이너에
 *      재진입하여 수정(add/remove/clear)을 시도해서는 안 됩니다.
 *
 * 4. Borrowed Reference 동시성 계약:
 *    - get/find/iterator->next는 원소의 생명주기를 보장하지 않는 Borrowed Reference를 반환합니다.
 *    - 멀티스레드 환경에서 다른 스레드가 동시에 remove/clear를 수행할 수 있다면,
 *      반환된 포인터를 장기 보관하기 전에 호출자 측의 별도 외부 동기화로 보호해야 합니다.
 *      반환 직후 단순 RETAIN() 호출만으로는 경쟁 조건(Race Condition)을 해결할 수 없습니다.
 */
typedef void (*ArrayListActionFunc)(Object* item);
typedef int (*ArrayListCompareFunc)(const void* itemA, const void* itemB);

extern const Class arrayListClass;
extern const Class arrayListIteratorClass;

typedef struct ArrayListIterator ArrayListIterator;
typedef struct ArrayList ArrayList;

struct ArrayListIterator {
    Object base;
    ArrayList *list;
    int currentIndex;

    bool (*hasNext)(ArrayListIterator *self);
    /* [BORROWED] 소유권을 이전하지 않으며, 동시 변경 시 외부 동기화가 필수적입니다. */
    Object* (*next)(ArrayListIterator *self);
};

struct ArrayList {
    Object base;
    Object** items;
    int size;
    int capacity;
    pthread_mutex_t lock;

    /* [RETAIN] 추가 시 리스트가 원소의 참조 카운트를 1 증가시킵니다. */
    void (*add)(ArrayList* self, Object* item);

    /* [BORROWED] 내부 원소 포인터를 반환합니다 (RETAIN 없음). 동시 제거 가능 시 외부 동기화 필수. */
    Object* (*get)(ArrayList* self, int index);

    /* [RELEASE] 리스트에서 제거 후 락 외부에서 참조 카운트를 1 감소시킵니다. */
    void (*remove)(ArrayList* self, int index);
    void (*removeResult)(ArrayList* self, int index);

    int (*getSize)(ArrayList* self);

    /* [Capacity Reset 정책] clear 수행 시 메모리를 반환하여 capacity를 0으로 리셋합니다. */
    void (*clear)(ArrayList* self);
    bool (*isEmpty)(ArrayList* self);
    void (*trimToSize)(ArrayList* self);
    void (*ensureCapacity)(ArrayList* self, int min_capacity);
    void (*forEach)(ArrayList* self, ArrayListActionFunc action);
    void* (*find)(ArrayList* self, void* target, ArrayListCompareFunc compare);
    void (*sort)(ArrayList* self, ArrayListCompareFunc compare);

    /* [OWNED] 사용 후 RELEASE((Object*)iter)로 해제해야 합니다. */
    ArrayListIterator* (*iterator)(ArrayList* self);
    void (*destroy)(ArrayList* self);

    /* [OWNED] 리스트에서 원소를 분리하여 RELEASE 없이 소유권을 호출자에게 이전합니다. */
    Object* (*detach)(ArrayList *self, int index);
    bool (*is_arraylist)(Object *obj);
};

ArrayList* new_ArrayList(int initial_capacity);

#endif // ARRAYLIST_H