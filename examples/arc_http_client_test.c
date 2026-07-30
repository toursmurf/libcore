/**
 * @file test_http_client.c
 * @brief libcore HttpClient 종합 검증 테스트 수트
 *
 * 검증 항목:
 *   TEST 1 — HTTPS GET + 302 리다이렉트 + Cookie Jar 수집
 *   TEST 2 — POST_RAW JSON 페이로드 + Content-Length 검증
 *   TEST 3 — update_cookie_jar 중복 방지 (동일 키 갱신, size==1)
 *   TEST 4 — 다음 요청에 Cookie 헤더 자동 송신 검증
 *
 * 컴파일:
 *   gcc -o test_http test_http_client.c -lcore -luring -lssl -lcrypto -lpthread
 *
 * @note TEST 1/3/4 는 c1, TEST 2 는 c2 — 상태 간섭 차단
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libcore.h"

/* ── 단언 매크로 ─────────────────────────────────────────────────
 * 조건 불충족 시 메시지 출력 후 지정 라벨로 점프.
 * 자원 해제는 호출 측 cleanup 라벨에서 일괄 처리.
 * ───────────────────────────────────────────────────────────── */
#define ASSERT(cond, msg, label) \
    do { \
        if (!(cond)) { \
            printf("  [FAIL] %s\n\n", msg); \
            goto label; \
        } \
    } while (0)

static int total = 0;
static int passed = 0;

/* 테스트 결과 집계 */
#define TEST_PASS(name) \
    do { total++; passed++; printf("  => [%s] PASS\n\n", name); } while(0)
#define TEST_FAIL(name) \
    do { total++;          printf("  => [%s] FAIL\n\n", name); } while(0)

/* =========================================================
 * TEST 1 — HTTPS GET + 302 추적 + Cookie Jar 수집
 * postman-echo.com/cookies/set 은 302 → /cookies 로 리다이렉트.
 * 리다이렉트 후 최종 응답 200, cookie_jar 에 쿠키 1개 이상.
 * ========================================================= */
static int test1_get_redirect_cookie(void) {
    printf("▶ [TEST 1] HTTPS GET + Redirect + Cookie Jar 수집\n");

    HttpClient* c = new_HttpClient(NULL);
    if (!c) {
        printf("  [FAIL] HttpClient 생성 실패\n\n");
        TEST_FAIL("TEST 1");
        return -1;
    }
    c->options.timeout_ms = 10000;
    c->setHeader(c, "User-Agent", "ToosIT-TestBot/1.6.0");
    c->setHeader(c, "Accept",     "application/json, */*");

    // 🚀 [타겟 변경]: httpbin.org -> postman-echo.com
    HttpClientResponse* res =
        c->GET(c, "https://postman-echo.com/cookies/set?session_token=baaaaaaam_12345", NULL);

    /* 단언 1: 응답 존재 */
    ASSERT(res != NULL, "응답 NULL — 타임아웃 또는 연결 실패", t1_fail);

    /* 단언 2: 최종 상태 200 (리다이렉트 추적 완료) */
    printf("  - 응답 코드: %d\n", res->status_code);
    ASSERT(res->status_code == 200,
           "status_code != 200 — 리다이렉트 추적 실패", t1_fail);

    /* 단언 3: cookie_jar 에 쿠키 1개 이상 */
    {
        int sz = c->cookie_jar->getSize(c->cookie_jar);
        printf("  - Cookie Jar 크기: %d\n", sz);
        ASSERT(sz > 0, "쿠키 0개 — Cookie Jar 수집 실패", t1_fail);

        for (int i = 0; i < sz; i++) {
            HttpCookie* ck = (HttpCookie*)c->cookie_jar->get(c->cookie_jar, i);
            if (ck && ck->name)
                printf("    * [%s] = %s\n", ck->name, ck->value ? ck->value : "(null)");
        }
    }

    RELEASE((Object*)res);
    RELEASE((Object*)c);
    TEST_PASS("TEST 1");
    return 0;

t1_fail:
    if (res) RELEASE((Object*)res);
    RELEASE((Object*)c);
    TEST_FAIL("TEST 1");
    return -1;
}

/* =========================================================
 * TEST 2 — POST_RAW JSON 페이로드 + Content-Length 검증
 * postman-echo.com/post 는 수신한 body 를 json (또는 data) 필드에 Echo.
 * 응답 200, body 에 전송 페이로드 문자열 포함 여부 검증.
 * ========================================================= */
static int test2_post_raw_json(void) {
    printf("▶ [TEST 2] POST_RAW JSON 페이로드 + Content-Length 검증\n");

    HttpClient* c = new_HttpClient(NULL);
    if (!c) {
        printf("  [FAIL] HttpClient 생성 실패\n\n");
        TEST_FAIL("TEST 2");
        return -1;
    }
    c->options.timeout_ms = 10000;
    c->setHeader(c, "User-Agent", "ToosIT-TestBot/1.6.0");
    c->setHeader(c, "Accept",     "application/json");

    const char* payload =
        "{\"mission\":\"v1.6.0_complete\",\"status\":\"BAAAAAAAM!!!\"}";

    // 🚀 [타겟 변경]: httpbin.org -> postman-echo.com
    HttpClientResponse* res = c->POST_RAW(
        c, "https://postman-echo.com/post",
        payload, strlen(payload), "application/json");

    ASSERT(res != NULL, "응답 NULL — 타임아웃 또는 연결 실패", t2_fail);

    printf("  - 응답 코드: %d\n", res->status_code);
    ASSERT(res->status_code == 200, "status_code != 200", t2_fail);

    /* 단언: 서버 Echo 에 페이로드 문자열 포함 */
    ASSERT(res->body != NULL, "body NULL — 응답 본문 없음", t2_fail);
    ASSERT(strstr(res->body, "v1.6.0_complete") != NULL,
           "Echo 없음 — Content-Length 오류 또는 body 미전송", t2_fail);

    printf("  - Echo 확인: 페이로드 정상 수신\n");

    RELEASE((Object*)res);
    RELEASE((Object*)c);
    TEST_PASS("TEST 2");
    return 0;

t2_fail:
    if (res) RELEASE((Object*)res);
    RELEASE((Object*)c);
    TEST_FAIL("TEST 2");
    return -1;
}

/* =========================================================
 * TEST 3 — update_cookie_jar 중복 방지 검증
 * session_token 을 두 번 set → cookie_jar size == 1 이어야 함.
 * 2 이상이면 중복 누적 버그.
 * ========================================================= */
static int test3_cookie_dedup(void) {
    printf("▶ [TEST 3] update_cookie_jar 중복 방지 검증\n");

    HttpClient* c = new_HttpClient(NULL);
    if (!c) {
        printf("  [FAIL] HttpClient 생성 실패\n\n");
        TEST_FAIL("TEST 3");
        return -1;
    }
    c->options.timeout_ms = 10000;
    c->setHeader(c, "User-Agent", "ToosIT-TestBot/1.6.0");

    /* 1차 세팅 */
    // 🚀 [타겟 변경]: httpbin.org -> postman-echo.com
    HttpClientResponse* r1 =
        c->GET(c, "https://postman-echo.com/cookies/set?session_token=first_value", NULL);
    ASSERT(r1 != NULL, "1차 응답 NULL", t3_fail);
    printf("  - 1차 응답 코드: %d\n", r1->status_code);
    RELEASE((Object*)r1); r1 = NULL;

    int sz1 = c->cookie_jar->getSize(c->cookie_jar);
    printf("  - 1차 Jar 크기: %d (기대값: 1)\n", sz1);
    ASSERT(sz1 == 1, "1차 Jar 크기 != 1", t3_fail);

    /* 2차 세팅 — 동일 키, 값 덮어쓰기 */
    // 🚀 [타겟 변경]: httpbin.org -> postman-echo.com
    HttpClientResponse* r2 =
        c->GET(c, "https://postman-echo.com/cookies/set?session_token=second_value", NULL);
    ASSERT(r2 != NULL, "2차 응답 NULL", t3_fail);
    printf("  - 2차 응답 코드: %d\n", r2->status_code);
    RELEASE((Object*)r2); r2 = NULL;

    int sz2 = c->cookie_jar->getSize(c->cookie_jar);
    printf("  - 2차 Jar 크기: %d (기대값: 1, 2 이상이면 중복 누적 버그)\n", sz2);
    ASSERT(sz2 == 1, "Jar 크기 == 2 이상 — 중복 누적 버그!", t3_fail);

    /* 값 갱신 확인 */
    HttpCookie* ck = (HttpCookie*)c->cookie_jar->get(c->cookie_jar, 0);
    ASSERT(ck != NULL && ck->value != NULL, "쿠키 객체 NULL", t3_fail);
    printf("  - 갱신된 값: %s (기대값: second_value)\n", ck->value);
    ASSERT(strcmp(ck->value, "second_value") == 0,
           "값 갱신 실패 — update_cookie_jar 오동작", t3_fail);

    RELEASE((Object*)c);
    TEST_PASS("TEST 3");
    return 0;

t3_fail:
    RELEASE((Object*)c);
    TEST_FAIL("TEST 3");
    return -1;
}

/* =========================================================
 * TEST 4 — Cookie 헤더 자동 송신 검증
 * TEST 3 와 별도 c 사용. 쿠키 세팅 후 /cookies 호출.
 * postman-echo.com 은 수신한 쿠키를 body 에 그대로 반환.
 * body 에 session_token 없으면 Cookie 헤더 미전송 버그.
 * ========================================================= */
static int test4_cookie_send(void) {
    printf("▶ [TEST 4] Cookie 헤더 자동 송신 검증\n");

    HttpClient* c = new_HttpClient(NULL);
    if (!c) {
        printf("  [FAIL] HttpClient 생성 실패\n\n");
        TEST_FAIL("TEST 4");
        return -1;
    }
    c->options.timeout_ms = 10000;
    c->setHeader(c, "User-Agent", "ToosIT-TestBot/1.6.0");
    c->setHeader(c, "Accept",     "application/json");

    /* 쿠키 세팅 (302 → /cookies 리다이렉트) */
    // 🚀 [타겟 변경]: httpbin.org -> postman-echo.com
    HttpClientResponse* r_set =
        c->GET(c, "https://postman-echo.com/cookies/set?session_token=send_test", NULL);
    ASSERT(r_set != NULL, "쿠키 세팅 응답 NULL", t4_fail);
    RELEASE((Object*)r_set); r_set = NULL;

    /* Cookie Jar 확인 */
    int sz = c->cookie_jar->getSize(c->cookie_jar);
    printf("  - Jar 크기: %d\n", sz);
    ASSERT(sz > 0, "쿠키 미수집 — 송신 전제 실패", t4_fail);

    /* /cookies 호출 → 서버가 수신한 쿠키 반환 */
    // 🚀 [타겟 변경]: httpbin.org -> postman-echo.com
    HttpClientResponse* r_chk = c->GET(c, "https://postman-echo.com/cookies", NULL);
    ASSERT(r_chk != NULL, "검증 응답 NULL", t4_fail);

    printf("  - 응답 코드: %d\n", r_chk->status_code);
    ASSERT(r_chk->status_code == 200, "status_code != 200", t4_fail);
    ASSERT(r_chk->body != NULL, "body NULL", t4_fail);

    /* 단언: 서버가 session_token 을 수신했는지 */
    ASSERT(strstr(r_chk->body, "session_token") != NULL,
           "session_token 없음 — Cookie 헤더 미전송 버그!", t4_fail);

    printf("  - 서버 수신 확인: session_token 존재\n");

    RELEASE((Object*)r_chk);
    RELEASE((Object*)c);
    TEST_PASS("TEST 4");
    return 0;

t4_fail:
    RELEASE((Object*)c);
    TEST_FAIL("TEST 4");
    return -1;
}

/* =========================================================
 * main
 * ========================================================= */
int main(void) {
    printf("\n");
    printf("========================================================\n");
    printf("  [TEST SUITE] libcore v1.6.0 HttpClient 종합 검증\n");
    printf("  Target  : postman-echo.com (HTTPS)\n");
    printf("  항목    : Redirect / Cookie Jar / POST / Dedup / Send\n");
    printf("========================================================\n\n");

    test1_get_redirect_cookie();
    test2_post_raw_json();
    test3_cookie_dedup();
    test4_cookie_send();

    printf("========================================================\n");
    printf("  결과: %d / %d PASS%s\n",
           passed, total,
           (passed == total) ? " — 전항목 통과" : " — 실패 항목 있음");
    printf("========================================================\n\n");

    return (passed == total) ? 0 : -1;
}