#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libcore.h"

// 🚀 [의장님 명품 로직 1] String 객체 비교용 딜리게이트 함수
int compare_strings(Object* a, Object* b) {
    String* sa = (String*)a;
    String* sb = (String*)b;
    if (sa && sb && sa->value && sb->value) {
        return strcmp(sa->value, sb->value); // 같으면 0 반환
    }
    return -1;
}

// 🚀 [의장님 명품 로직 2] String 객체 출력용 딜리게이트 함수
void display_string(Object* obj) {
    if (obj) {
        char buf[128];
        toString(obj, buf, sizeof(buf));
        printf("[%s] -> ", buf);
    }
}

int main() {
    printf("=== [투스 IT 홀딩스] LinkedList 1.0 (Perfect 10) ARC 통합 테스트 ===\n\n");

    // 1. 리스트 생성
    LinkedList* list = new_LinkedList();
    printf("[1] LinkedList 생성 완료: %p\n", list);

    // 2. 데이터 준비 및 추가 (ARC 소유권 이전)
    String* s1 = new_String("Node_Alpha");
    String* s2 = new_String("Node_Bravo");
    String* s3 = new_String("Node_Charlie");

    printf("\n[2] 데이터 add_node 시작...\n");
    list->add_node(list, (void*)s1); RELEASE_NULL(s1);
    list->add_node(list, (void*)s2); RELEASE_NULL(s2);
    list->add_node(list, (void*)s3); RELEASE_NULL(s3);

    printf("  > 총 %d개 노드 추가 완료.\n", list->get_size(list));

    // 3. 전체 리스트 출력 (display 딜리게이트 사용)
    printf("\n[3] print_list 테스트 (display 함수 포인터 연동)\n  ");
    list->print_list(list, display_string);

    // 4. 특정 노드 삭제 테스트 (compare 딜리게이트 사용)
    printf("\n[4] delete_node 테스트 ('Node_Bravo' 삭제 및 ARC 소각 확인)\n");
    // 삭제할 대상을 찾기 위한 임시 비교용 객체 생성
    String* target = new_String("Node_Bravo");

    // 의장님의 명품 delete_node 호출! (내부에서 찾아서 제거하고 RELEASE 처리됨)
    list->delete_node(list, (void*)target, compare_strings);

    // 임시 비교용 객체는 임무를 다했으니 해제
    RELEASE_NULL(target);

    printf("  > 삭제 완료. 현재 리스트 사이즈: %d\n  ", list->get_size(list));
    list->print_list(list, display_string);

    // 5. 최종 리스트 객체 해제 (ARC Cascade - 남은 Alpha, Charlie 자동 소각)
    printf("\n[5] 최종 LinkedList 객체 해제 (ARC Cascade)\n");
    RELEASE_NULL(list);

    printf("\n=== 테스트 종료: Valgrind 결과를 확인하십시오 ===\n");

    return 0;
}
