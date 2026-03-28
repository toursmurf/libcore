#include <stdio.h>
#include <stdlib.h>
#include "libcore.h"

int main() {
    printf("=== [투스 IT 홀딩스] Stack 1.0 ARC & LIFO 통합 테스트 ===\n\n");

    // 1. 스택 생성 (물리적 한계 테스트를 위해 초기 용량 3으로 설정)
    Stack* stack = new_Stack(3);
    printf("[1] Stack 생성 완료 (Capacity: 3): %p\n", stack);

    // 2. 데이터 Push 및 isFull 테스트
    printf("\n[2] 데이터 Push 시작...\n");

    char* jobs[] = {"Task_01", "Task_02", "Task_03", "Task_04"};
    for(int i = 0; i < 4; i++) {
        String* s = new_String(jobs[i]);

        if (stack->isFull(stack)) {
            printf("  ! 경보: Stack이 가득 찼습니다. (Size: %d) 곧 확장이 일어납니다.\n", stack->size(stack));
        }

        stack->push(stack, (Object*)s);
        printf("  > Push 성공: %s (현재 사이즈: %d)\n", jobs[i], stack->size(stack));

        // 생성자가 가졌던 소유권은 즉시 반납 (이제 Stack이 소유)
        RELEASE_NULL(s);
    }

    // 3. Peek 테스트
    printf("\n[3] Peek 테스트 (최상단 데이터 확인)\n");
    String* top = (String*)stack->peek(stack);
    if (top) {
        char buf[128];
        toString((Object*)top, buf, sizeof(buf));
        printf("  - Top Item: %s (사이즈 변화 없음: %d)\n", buf, stack->size(stack));
    }

    // 4. Pop 및 소유권 이전 테스트 (LIFO 확인)
    printf("\n[4] Pop 테스트 (LIFO 순서 및 ARC 소각 확인)\n");
    while (!stack->isEmpty(stack)) {
        // pop은 내부적으로 detach를 사용하므로 소유권이 main으로 넘어옴 (Count=1)
        String* popped = (String*)stack->pop(stack);

        char buf[128];
        toString((Object*)popped, buf, sizeof(buf));
        printf("  - Pop 성공: %s (메모리 주소: %p)\n", buf, popped);

        // main이 소유권을 가졌으므로 반드시 직접 RELEASE 해야 함!
        RELEASE_NULL(popped);
    }

    printf(" - Pop 완료 후isEmpty: %s\n", stack->isEmpty(stack) ? "True" : "False");

    // 5. 최종 정리
    printf("\n[5] 최종 스택 객체 해제 (ARC Cascade)\n");
    RELEASE_NULL(stack);

    printf("\n=== 테스트 종료: Valgrind 결과를 확인하십시오 ===\n");

    return 0;
}
