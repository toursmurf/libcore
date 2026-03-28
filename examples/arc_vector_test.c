#include <stdio.h>
#include <stdlib.h>
#include "libcore.h"

int main() {
    printf("=== [투스 IT 홀딩스] Vector 1.0 (STL-Style) ARC 통합 테스트 ===\n\n");

    // 1. Vector 생성 (초기 용량 3)
    Vector* vec = new_Vector(3);
    printf("[1] Vector 생성 완료 (초기 Capacity: 3): %p\n", vec);

    // 2. 데이터 준비 및 push_back
    String* s1 = new_String("Data_A");
    String* s2 = new_String("Data_B");
    String* s3 = new_String("Data_C");
    String* s4 = new_String("Data_D"); // 용량 초과 확장(Resize) 테스트용

    printf("\n[2] 데이터 push_back 시작...\n");

    // push_back 내부에서 RETAIN 하므로, main의 소유권은 즉시 RELEASE!
    vec->push_back(vec, (Object*)s1); RELEASE_NULL(s1);
    vec->push_back(vec, (Object*)s2); RELEASE_NULL(s2);
    vec->push_back(vec, (Object*)s3); RELEASE_NULL(s3);

    printf("  ! 3개 push_back 완료. 현재 사이즈: %d\n", vec->get_size(vec));

    vec->push_back(vec, (Object*)s4); RELEASE_NULL(s4);
    printf("  > 총 push_back 완료. 최종 사이즈: %d\n", vec->get_size(vec));

    // 3. VECTOR_FOREACH 매크로 테스트 (의장님의 걸작!)
    printf("\n[3] VECTOR_FOREACH 매크로 순회 테스트\n");
    int index = 0;
    // 매크로가 내부적으로 begin, end, it_next를 광속으로 처리함!
    VECTOR_FOREACH(vec, String, item) {
        char buf[128];
        toString((Object*)item, buf, sizeof(buf));
        printf("  > [%d] 순회 중 아이템: %s\n", index++, buf);
    }

    // 4. pop_back 테스트 (소유권 이전)
    printf("\n[4] pop_back 테스트 (마지막 요소 소유권 이전 확인)\n");
    String* popped = (String*)vec->pop_back(vec);
    if (popped) {
        char buf[128];
        toString((Object*)popped, buf, sizeof(buf));
        printf("  - 꺼낸 아이템(pop_back): %s (메모리 주소: %p)\n", buf, popped);

        // main이 다시 소유권을 넘겨받았으므로(Count=1) 반드시 직접 RELEASE!
        RELEASE_NULL(popped);
    }
    printf("  - pop_back 완료 후 Vector 사이즈: %d (예상: 3)\n", vec->get_size(vec));

    // 5. 최종 정리
    printf("\n[5] 최종 Vector 객체 해제 (ARC Cascade)\n");
    // Vector가 소멸되면서 남은 A, B, C도 함께 완벽히 소멸!
    RELEASE_NULL(vec);

    printf("\n=== 테스트 종료: Valgrind 결과를 확인하십시오 ===\n");

    return 0;
}
