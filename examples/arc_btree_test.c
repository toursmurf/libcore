#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "libcore.h"

// (테스트용 가상 IntObject 구현체)
typedef struct { Object base; int val; } IntObject;
static void Int_Finalize(Object* obj) { 
	(void)obj;
}
const Class intClass = { .name = "Int", .size = sizeof(IntObject), .finalize = Int_Finalize };
IntObject* new_Int(int val) {
    IntObject* obj = calloc(1, sizeof(IntObject));
    Object_Init((Object*)obj, &intClass);
    obj->val = val;
    return obj;
}

int main(void) {
    printf("==================================================\n");
    printf("🏰 투스(Toos) IT 홀딩스 - BTree ARC 규격 테스트 🏰\n");
    printf("==================================================\n\n");

    BTree* tree = new_BTree(3); // 차수 3 생성!

    printf("[1] 단일 스레드 삽입 (RETAIN 테스트)...\n");
    IntObject *k1 = new_Int(10); IntObject *v1 = new_Int(100);
    IntObject *k2 = new_Int(20); IntObject *v2 = new_Int(200);

    tree->insert(tree, (Object*)k1, (Object*)v1);
    tree->insert(tree, (Object*)k2, (Object*)v2);

    // 트리 내부에 RETAIN 되었으므로, 로컬 소유권은 해제!
    RELEASE((Object*)k1); RELEASE((Object*)v1);
    RELEASE((Object*)k2); RELEASE((Object*)v2);

    printf("  - 삽입 후 Size: %d\n", tree->getSize(tree));

    printf("\n[2] clear 및 루트 예외 방어 테스트...\n");
    tree->clear(tree);
    printf("  - clear 후 Size: %d\n", tree->getSize(tree));

    // ARC 소각로 가동!
    printf("\n[3] ARC 소각로 가동. RELEASE(tree) 호출!\n");
    RELEASE((Object*)tree); // 🚀 정품 RELEASE 매크로가 트리를 완벽 소각!

    printf("\n==================================================\n");
    printf("✅ 모든 테스트 완료! (Make sure to run with Valgrind)\n");
    printf("==================================================\n");

    return 0;
}
