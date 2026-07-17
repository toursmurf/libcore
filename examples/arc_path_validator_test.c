#include "path_validator.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

/* ✨ 테스트 데이터 구조체: 입력, 기대 상태, 기대 정규화 경로 */
typedef struct {
    const char* type;           /* "[정상]" 또는 "[공격]" */
    const char* raw_path;       /* 입력 데이터 */
    bool expected_valid;        /* 기대 통과 여부 */
    const char* expected_canon; /* 통과 시 기대되는 정규화 결괏값 */
} TestCase;

/* ✨ 그룹별 테스트 실행 런너 */
static void run_test_group(PathValidator* pv, const char* group_name, TestCase cases[], size_t count) {
    printf("\n▶️ %s\n", group_name);
    printf("--------------------------------------------------------------------------------------\n");
    printf(" %-8s | %-25s | %-25s | %s\n", "타입", "입력 데이터(Raw)", "정규화/차단 결과", "판정");
    printf("--------------------------------------------------------------------------------------\n");

    for (size_t i = 0; i < count; i++) {
        char out[MAX_PATH_LEN + 1];
        bool res = pv->validate(pv, cases[i].raw_path, out, sizeof(out));

        /* 의장님 지시: strncmp로 시큐어 코딩 적용 */
        assert(res == cases[i].expected_valid);
        if (res) {
            assert(strncmp(out, cases[i].expected_canon, sizeof(out)) == 0);
        }

        /* 시인성을 위한 출력 포맷팅 */
        printf(" %-6s | %-25s | %-25s | ✅ PASS\n",
               cases[i].type,
               cases[i].raw_path,
               res ? out : "⛔ (차단됨)");
    }
}

int main(void) {
    printf("======================================================================================\n");
    printf("[Test] PathValidator Data-Driven Security Suite Start...\n");
    printf("======================================================================================\n");

    PathValidator* pv = new_PathValidator();
    assert(pv != NULL);

    /* 1. 경로 탐색(Traversal) 및 상대 경로 방어 테스트 */
    TestCase group1[] = {
        {"[정상]", "/api/./users", true, "/api/users"},
        {"[정상]", "/static/img/logo.png", true, "/static/img/logo.png"},
        {"[공격]", "/api/../users", false, NULL},
        {"[공격]", "api/users", false, NULL} /* 상대 경로 */
    };
    run_test_group(pv, "[그룹 1] Directory Traversal 및 상대 경로 검증", group1, 4);

    /* 2. 다중 슬래시 및 Trailing Slash 정규화 테스트 */
    TestCase group2[] = {
        {"[정상]", "//api///users", true, "/api/users"},
        {"[정상]", "//", true, "/"},
        {"[정상]", "/api/users/", true, "/api/users"},
        {"[공격]", "/%2F%2Fapi", false, NULL} /* 디코딩 슬래시 밀수 시도 */
    };
    run_test_group(pv, "[그룹 2] Slash 정규화 및 밀수(Smuggling) 방어", group2, 4);

    /* 3. 특수 문자 및 인코딩 우회 공격 방어 테스트 */
    TestCase group3[] = {
        {"[정상]", "/user/%EA%B0%80", true, "/user/가"}, /* 정상 UTF-8 인코딩 */
        {"[정상]", "/file/name_with-dash~.txt", true, "/file/name_with-dash~.txt"},
        {"[공격]", "/%2e%2e/admin", false, NULL}, /* 인코딩된 Traversal */
        {"[공격]", "/api?query=1", false, NULL} /* 경로 내 예약 문자(?) 침범 */
    };
    run_test_group(pv, "[그룹 3] 인코딩 우회 및 예약 문자 침범 방어", group3, 4);

    /* 4. 치명적 시스템 공격 (NUL, 백슬래시, 서로게이트) 테스트 */
    TestCase group4[] = {
        {"[정상]", "/health-check", true, "/health-check"},
        {"[정상]", "/v1/data.json", true, "/v1/data.json"},
        {"[공격]", "/api%00/secret", false, NULL}, /* 문자열 종료 공격 (NUL) */
        {"[공격]", "/windows\\system32", false, NULL}, /* 백슬래시 우회 */
        {"[공격]", "/%ED%A0%80", false, NULL} /* 깨진 UTF-8 (Surrogate) */
    };
    run_test_group(pv, "[그룹 4] C언어 취약점(NUL) 및 깨진 인코딩 타겟 공격 방어", group4, 5);

    RELEASE((Object*)pv);

    printf("--------------------------------------------------------------------------------------\n");
    printf("🎯 All Test Data Matched Expected Behaviors! (Secure Coded & Data-Driven)\n");
    printf("======================================================================================\n");
    return 0;
}