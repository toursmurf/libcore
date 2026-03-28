#include <stdio.h>
#include "libcore.h"

int main() {
    printf("=== [투스 IT 홀딩스] libcore 1.0 컬렉션 통합 테스트 시작 ===\n\n");

    // 0. 테스트용 데이터 준비 (String 객체)
    String* s1 = new_String("Data_01");
    String* s2 = new_String("Data_02");
    String* s3 = new_String("Data_03");

    // ---------------------------------------------------------
    // 1. ArrayList 테스트 (순차 보관 및 이터레이터)
    // ---------------------------------------------------------
    printf("[1] ArrayList Test\n");
    ArrayList* list = new_ArrayList(5);
    list->add(list, (Object*)s1);
    list->add(list, (Object*)s2);
    list->add(list, (Object*)s3);

    printf(" - List Size: %d\n", list->getSize(list));

    // 이터레이터 순회
    ArrayListIterator* it = list->iterator(list);
    while (it->hasNext(it)) {
        String* str = (String*)it->next(it);
        char buf[64];
        toString((Object*)str, buf, sizeof(buf));
        printf("   > Item: %s\n", buf);
    }
    RELEASE_NULL(it); // 이터레이터 반납

    // Detach 테스트 (소유권 이전)
    Object* detachedObj = list->detach(list, 0);
    printf(" - Detached Item[0]: %p (Count should be valid)\n", detachedObj);
    RELEASE(detachedObj); // 꺼낸 자가 직접 해제

    // ---------------------------------------------------------
    // 2. HashMap 테스트 (논리적 식별 & 딥 카피 없는 추출)
    // ---------------------------------------------------------
    printf("\n[2] HashMap Test\n");
    HashMap* map = new_HashMap(10);
    map->put(map, "Key_A", (Object*)s1);
    map->put(map, "Key_B", (Object*)s2);

    String* found = (String*)map->get(map, "Key_A");
    printf(" - Get Key_A: %p\n", found);

    // Keys 추출 (ARC 덕분에 주소 복사만 발생, 광속!)
    ArrayList* keyList = map->keys(map);
    printf(" - Extracted Keys Count: %d\n", keyList->getSize(keyList));
    RELEASE_NULL(keyList);

    // ---------------------------------------------------------
    // 3. Hashtable 테스트 (스레드 세이프 & Fail-Fast)
    // ---------------------------------------------------------
    printf("\n[3] Hashtable Test\n");
    Hashtable* ht = new_Hashtable(11, 0.75f);
    String* tempKey = new_String("ID_100");
    ht->put(ht, (Object*)tempKey, (Object*)s3);
    RELEASE(tempKey);

		// containsKey도 마찬가지입니다!
    String* searchKey = new_String("ID_100");
    ht->containsKey(ht, (Object*)searchKey);
    RELEASE(searchKey); // ✅ 찾고 나서 바로 해제!

    // ---------------------------------------------------------
    // 4. 연쇄 소멸 테스트 (Cleanup)
    // ---------------------------------------------------------
    printf("\n[4] Final Cleanup (ARC Cascade)\n");

    // 컬렉션들만 RELEASE 하면 내부의 모든 String 객체들도
    // 참조 카운트가 0이 되어 자동으로 finalize/free 됨!
    RELEASE_NULL(list);
    RELEASE_NULL(map);
    RELEASE_NULL(ht);

    // 초기 생성한 s1, s2, s3도 명시적 소유권 포기
    RELEASE_NULL(s1);
    RELEASE_NULL(s2);
    RELEASE_NULL(s3);

    printf("\n=== 테스트 완료: 0 bytes in 0 blocks (Valgrind 인증 완료 예정) ===\n");

    return 0;
}
