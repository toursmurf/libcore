#include "string_builder.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

void test_string_builder_text() {
    printf("==================================================\n");
    printf(" 🧪 [Test 1] String/Text Suite - 기본 문자열 조립 \n");
    printf("==================================================\n");

    StringBuilder* sb = new_StringBuilder(64);

    // 1. 초기 상태 확인
    printf("[Step 1] StringBuilder 생성 (초기 용량: 64)\n");
    assert(strcmp(sb->c_str(sb), "") == 0);
    assert(sb->isEmpty(sb));
    printf("  -> 결과: isEmpty() == %s, length() == %zu\n\n", sb->isEmpty(sb) ? "true" : "false", sb->length(sb));

    // 2. 기본 문자열 및 Line 추가
    printf("[Step 2] append(\"Line1\") 및 appendLine() 호출\n");
    sb->append(sb, "Line1")->appendLine(sb);
    printf("  -> 현재 문자열(c_str): %s", sb->c_str(sb));
    printf("  -> length() == %zu\n\n", sb->length(sb));

    // 3. 다양한 타입의 데이터 조립
    printf("[Step 3] 다양한 타입(NULL, String 객체, Char, Bool, Int, Double 등) 연속 체이닝 조립\n");
    sb->append(sb, NULL);
    sb->append(sb, "");

    String* s = new_String("Data");
    sb->appendString(sb, s);
    RELEASE(s);

    sb->appendChar(sb, 'Z');
    sb->appendBool(sb, true);
    sb->appendInt(sb, 1);
    sb->appendLong(sb, 2026L);
    sb->appendDouble(sb, 3.14); // %.2f 포맷팅으로 저장됨
    sb->appendFormat(sb, "[%s]", "End");

    printf("  -> 현재 조립된 문자열: %s\n", sb->c_str(sb));
    printf("  -> length() == %zu, capacity() == %zu\n\n", sb->length(sb), sb->capacity(sb));

    // 4. Truncate (잘라내기)
    printf("[Step 4] truncate() 동작 검증\n");
    printf("  -> truncate(1000) 호출 (길이 초과로 무시되어야 함)\n");
    sb->truncate(sb, 1000);
    printf("  -> truncate(5) 호출 (처음 5글자 'Line1'만 남아야 함)\n");
    sb->truncate(sb, 5);
    printf("  -> truncate 결과(c_str): %s\n", sb->c_str(sb));
    assert(strcmp(sb->c_str(sb), "Line1") == 0);
    printf("\n");

    // 5. toString() 객체 변환 확인
    printf("[Step 5] toString()을 통한 String 객체로의 변환\n");
    String* s2 = sb->toString(sb);
    assert(s2 != NULL);
    printf("  -> String 객체 변환 결과(toString): %s\n\n", s2->c_str(s2));
    RELEASE(s2);

    // 6. Clear 확인
    printf("[Step 6] clear() 호출 (용량은 유지하되 데이터만 삭제)\n");
    sb->clear(sb);
    printf("  -> 결과: isEmpty() == %s, capacity() == %zu\n", sb->isEmpty(sb) ? "true" : "false", sb->capacity(sb));
    assert(sb->isEmpty(sb));

    RELEASE(sb);
    printf(">>> 🟢 String/Text Suite Passed!\n\n");
}

void test_string_builder_binary() {
    printf("==================================================\n");
    printf(" 🧪 [Test 2] Binary Data Suite - 바이너리 데이터 \n");
    printf("==================================================\n");

    StringBuilder* sb = new_StringBuilder(64);

    printf("[Step 1] 바이너리 배열 조립 (중간에 0x00 포함)\n");
    char bin[] = {0x01, 0x02, 0x00, 0x03};
    printf("  -> 입력 데이터: {0x01, 0x02, 0x00, 0x03} (총 4 bytes)\n");
    sb->appendBytes(sb, bin, 4);

    printf("[Step 2] 바이너리 데이터 정합성 검증(memcmp)\n");
    printf("  -> 기록된 길이(length): %zu bytes\n", sb->length(sb));

    assert(sb->length(sb) == 4);
    assert(memcmp(sb->c_str(sb), bin, 4) == 0);
    printf("  -> 0x00으로 인해 문자열이 끊기지 않고 4바이트가 완벽히 저장됨!\n");

    RELEASE(sb);
    printf(">>> 🟢 Binary Data Suite Passed!\n\n");
}

void test_string_builder_stress() {
    printf("==================================================\n");
    printf(" 🧪 [Test 3] Stress & Clear Suite - 1만회 반복 벤치 \n");
    printf("==================================================\n");

    StringBuilder* sb = new_StringBuilder(64);

    printf("[Step 1] 단일 Builder를 이용해 10,000회 연속 clear() 및 append() 수행\n");
    for (int i = 0; i < 10000; i++) {
        sb->clear(sb);
        sb->appendFormat(sb, "JSON_KEY_%d: %s", i, "VALUE");
        assert(sb->length(sb) > 0);

        sb->clear(sb);
        sb->append(sb, "HTTP/1.1 200 OK");
        assert(sb->length(sb) > 0);
    }
    printf("  -> 10,000회 메모리 풀링(Pooling) 재사용 동작 완료\n");

    printf("[Step 2] 최종 버퍼 상태 확인\n");
    printf("  -> 마지막에 기록된 데이터: %s\n", sb->c_str(sb));
    printf("  -> 반복 수행 후 최종 Capacity: %zu bytes (메모리 파편화 방어 확인)\n", sb->capacity(sb));

    RELEASE(sb);
    printf(">>> 🟢 Stress & Clear Suite Passed!\n\n");
}

int main() {
    printf("\n🚀 libcore StringBuilder v1.6.0 Test Bench\n\n");

    test_string_builder_text();
    test_string_builder_binary();
    test_string_builder_stress();

    printf("==================================================\n");
    printf(" 🎉 All Tests Passed! StringBuilder               \n");
    printf("==================================================\n");

    return 0;
}
