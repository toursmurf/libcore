#include <stdio.h>
#include <stdlib.h>
#include "libcore.h"

#define GET_CLASS(obj) ( *( (const Class**) (obj) ) )

// 가상의 IntObject (테스트용)
typedef struct { Object base; int val; } IntObject;
static void Int_ToString(Object* obj, char* buf, size_t len) {
    snprintf(buf, len, "%d", ((IntObject*)obj)->val);
}
static void Int_Finalize(Object* obj) { 
	(void)obj;
} // 추가 해제 불필요
const Class intClass = { .name = "Int", .size = sizeof(IntObject), .toString = Int_ToString, .finalize = Int_Finalize };

IntObject* new_Int(int val) {
    IntObject* obj = calloc(1, sizeof(IntObject));
    Object_Init((Object*)obj, &intClass);
    obj->val = val;
    return obj;
}

// CompareFunc 콜백
int int_compare(Object* a, Object* b) {
    int valA = ((IntObject*)a)->val;
    int valB = ((IntObject*)b)->val;
    if (valA > valB) return 1;
    if (valA < valB) return -1;
    return 0;
}

int main(void) {
    printf("==================================================\n");
    printf("🏰 투스(Toos) IT 홀딩스 - ARC Tree 통합 테스트 🏰\n");
    printf("==================================================\n\n");

    Tree* tree = new_Tree(int_compare);

    // 1. 데이터 삽입
    IntObject* vals[] = { new_Int(50), new_Int(30), new_Int(70), new_Int(20), new_Int(40), new_Int(60), new_Int(80) };
    for (int i = 0; i < 7; i++) {
        tree->insert(tree, (Object*)vals[i]);
        RELEASE((Object*)vals[i]); // 트리가 RETAIN 했으므로 로컬 소유권 포기!
    }

    printf("[1] 트리 구축 완료 (높이: %d)\n", tree->getHeight(tree->root));
    tree->traverseBFS(tree);

    // 2. Iterator(반복자) 테스트
    printf("\n[2] Iterator 중위 순회 (정렬 출력): ");
    TreeIterator* it = tree->createIterator(tree);
    char buf[64];
    while (it->hasNext(it)) {
        Object* obj = it->next(it);
        GET_CLASS(obj)->toString(obj, buf, sizeof(buf));
        printf("%s ", buf);
    }
    printf("\n");

    // 🔥 Iterator 사용 완료 후 반드시 소각! (내부 Stack 메모리 반환)
    RELEASE((Object*)it);

    // 3. 노드 삭제 테스트 (자식이 둘인 노드 50 루트 삭제)
    printf("\n[3] 루트 노드(50) 삭제 후 BFS...\n");
    IntObject* key = new_Int(50);
    tree->remove(tree, (Object*)key);
    RELEASE((Object*)key);
    tree->traverseBFS(tree);

    // 4. ARC 소각로 가동
    printf("\n[4] ARC 소각로 가동! RELEASE(tree) 호출!\n");
    RELEASE((Object*)tree); // 연쇄 폭발 발생!

    printf("\n==================================================\n");
    printf("✅ 모든 테스트 완료! (Make sure to run with Valgrind)\n");
    printf("==================================================\n");

    return 0;
}
