#include <stdio.h>
#include <stdlib.h>
#include "libcore.h"

int main() {
    printf("=== [투스 IT 홀딩스] Queue 1.0 ARC & Iterator 통합 테스트 ===\n\n");

    // 1. 큐 생성 (초기 용량 5)
    Queue* queue = new_Queue(5);
    printf("[1] Queue 생성 완료: %p\n", queue);

    // 2. 데이터 준비 및 Enqueue
    // (new_String은 참조 카운트 1로 생성됨)
    String* s1 = new_String("First_Job");
    String* s2 = new_String("Second_Job");
    String* s3 = new_String("Third_Job");

    printf("[2] 데이터 Enqueue 시작...\n");
    queue->enqueue(queue, (Object*)s1); // Queue가 RETAIN 함 (Count=2)
    queue->enqueue(queue, (Object*)s2);
    queue->enqueue(queue, (Object*)s3);

    // 생성자가 가졌던 소유권은 여기서 반납 (이제 Queue만 소유)
    RELEASE_NULL(s1);
    RELEASE_NULL(s2);
    RELEASE_NULL(s3);

    printf(" - 현재 Queue 사이즈: %d (예상: 3)\n", queue->size(queue));

    // 3. Iterator 테스트 (의장님께서 복구하신 핵심 기능!)
    printf("\n[3] Iterator를 통한 전체 순회 테스트\n");
    ArrayListIterator* it = queue->iterator(queue);
    while (it->hasNext(it)) {
        String* item = (String*)it->next(it); // Borrowed reference
        char buf[128];
        toString((Object*)item, buf, sizeof(buf));
        printf("  > 순회 중 아이템: %s\n", buf);
    }
    RELEASE_NULL(it); // 이터레이터 반납 (내부 리스트 RETAIN 해제됨)

    // 4. Dequeue 및 소유권 확인
    printf("\n[4] Dequeue 테스트 (소유권 이전 확인)\n");
    while (!queue->isEmpty(queue)) {
        // dequeue는 내부적으로 detach를 사용하므로
        // 반환된 객체의 소유권은 '나(main)'에게 있음 (Count=1)
        String* popped = (String*)queue->dequeue(queue);

        char buf[128];
        toString((Object*)popped, buf, sizeof(buf));
        printf("  - 꺼낸 아이템: %s (메모리 주소: %p)\n", buf, popped);

        // 내가 소유권을 가졌으므로 반드시 직접 RELEASE 해야 함!
        RELEASE_NULL(popped);
    }

    printf(" - Dequeue 완료 후 사이즈: %d\n", queue->size(queue));

    // 5. 최종 정리
    printf("\n[5] 최종 큐 객체 해제 (ARC Cascade)\n");
    RELEASE_NULL(queue);

    printf("\n=== 테스트 종료: Valgrind 결과를 확인하십시오 ===\n");

    return 0;
}
