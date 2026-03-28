#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include "libcore.h"

// 🚀 [ARC 규격에 맞춘 테스트용 IntObject 래퍼 클래스]
typedef struct {
    Object base;
    int val;
} IntObject;

static void Int_Finalize(Object* obj) { 
	(void)obj;
}
static void Int_ToString(Object* obj, char* buffer, size_t len) {
    snprintf(buffer, len, "Int(%d)", ((IntObject*)obj)->val);
}
const Class intClass = {
    .name = "Int", .size = sizeof(IntObject),
    .toString = Int_ToString, .finalize = Int_Finalize
};

// 생성자
IntObject* new_Int(int val) {
    IntObject* obj = calloc(1, sizeof(IntObject));
    Object_Init((Object*)obj, &intClass);
    obj->val = val;
    return obj;
}

#define NUM_THREADS 4
#define PUSH_COUNT_PER_THREAD 1000

void* thread_worker(void* arg) {
    List* list = (List*)arg;
    for (int i = 1; i <= PUSH_COUNT_PER_THREAD; i++) {
        IntObject* num = new_Int(i); // 객체 생성 (ref=1)
        if (i % 2 == 0) list->pushBack(list, (Object*)num); // List가 RETAIN (ref=2)
        else list->pushFront(list, (Object*)num);
        RELEASE((Object*)num); // 내 소유권 포기 (ref=1) -> List만 소유함
    }
    return NULL;
}

int main(void) {
    printf("==================================================\n");
    printf("🏰 투스(Toos) IT 홀딩스 - List ARC 완전 규격 테스트 🏰\n");
    printf("==================================================\n\n");

    List* list = new_List();
    printf("[1] 리스트 생성 완료 (초기 Size: %d)\n", list->getSize(list));

    // 🚀 [수정] 단일 스레드 테스트에서도 반드시 ARC 객체를 밀어 넣어야 합니다!
    printf("\n[2] 단일 스레드 기능 검증 시작...\n");
    IntObject* n1 = new_Int(10);
    IntObject* n2 = new_Int(20);
    IntObject* n3 = new_Int(5);

    list->pushBack(list, (Object*)n1);
    list->pushBack(list, (Object*)n2);
    list->pushFront(list, (Object*)n3);

    // List에 넣었으니 로컬 포인터의 소유권은 해제 (List가 소멸될 때 완전히 파괴됨)
    RELEASE((Object*)n1);
    RELEASE((Object*)n2);
    RELEASE((Object*)n3);

    printf("  - push 완료 후 Size: %d\n", list->getSize(list));
    printf("  - 인덱스 0 (예상 5) : %d\n", ((IntObject*)list->get(list, 0))->val);
    printf("  - 인덱스 1 (예상 10): %d\n", ((IntObject*)list->get(list, 1))->val);
    printf("  - 인덱스 2 (예상 20): %d\n", ((IntObject*)list->get(list, 2))->val);

    // insertAt 검증
    IntObject* n4 = new_Int(7);
    list->insertAt(list, 1, (Object*)n4);
    RELEASE((Object*)n4);

    printf("\n[3] insertAt(1) 후 1번 요소: %d (Size: %d)\n",
            ((IntObject*)list->get(list, 1))->val, list->getSize(list));

    // 🚀 removeAt으로 빼낸 객체는 소유권이 나에게 돌아옴! (반드시 직접 RELEASE 해야 함)
    Object* removed = list->removeAt(list, 2);
    printf("  - removeAt(2)로 제거된 요소: %d (Size: %d)\n",
            ((IntObject*)removed)->val, list->getSize(list));
    RELEASE(removed);

    list->clear(list);
    printf("\n[4] clear() 호출 후 Size: %d\n", list->getSize(list));

    printf("\n[5] 멀티스레드 스트레스 테스트 진입!\n");
    pthread_t threads[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_create(&threads[i], NULL, thread_worker, list);
    }
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    printf("  - 모든 스레드 종료! 최종 Size: %d\n", list->getSize(list));

    printf("\n[6] ARC 소각로 가동. RELEASE() 호출!\n");
    RELEASE((Object*)list);

    printf("\n==================================================\n");
    printf("✅ 모든 테스트 완료! (Make sure to run with Valgrind)\n");
    printf("==================================================\n");

    return 0;
}
