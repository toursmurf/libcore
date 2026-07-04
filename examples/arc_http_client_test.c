#include "event_loop.h"
#include "http_client.h"
#include "string_obj.h"
#include "hashmap.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    printf("============================================\n");
    printf("  🚀 libcore v1.5 HttpClient 실전 테스트\n");
    printf("============================================\n\n");

    /* 1. 제국 표준 이벤트 루프 생성 */
    EventLoop* loop = new_EventLoop(1024);

    /* 🚨 [수정 완] 인자로 loop를 반드시 넘겨야 함! */
    HttpClient* client = new_HttpClient(loop);
    if (!client) {
        printf("❌ HttpClient 생성 실패!\n");
        return -1;
    }

    /* 2. 전역 설정 (구조체 직접 접근) */
    client->options.timeout_ms = 300000;
    client->options.follow_redirects = true;

    /* 🚨 [수정 완] add_header -> setHeader 사용! */
    client->setHeader(client, "User-Agent", "WebCore-Client/1.5");
    client->setHeader(client, "Accept", "application/json");

    /* =========================================================
     * [TEST 1] GET 요청
     * ========================================================= */
    printf("📡 [TEST 1] GET 요청 전송 중...\n");

    /* 🚨 [수정 완] GET 메서드 호출 (쿼리 파라미터는 NULL) */
    HttpClientResponse* res_get = client->GET(
        client,
        "https://jsonplaceholder.typicode.com:443/todos/1",
        NULL
    );

    if (res_get) {
        /* 🚨 [수정 완] status -> status_code, body는 순수 char* ! */
        printf("✅ GET 성공! (HTTP Status: %d)\n", res_get->status_code);
        if (res_get->body) {
            printf("--- [수신된 Body] ---\n%s\n---------------------\n", res_get->body);
        }
        RELEASE((Object*)res_get); /* ARC 회수 */
    } else {
        printf("❌ GET 실패! (서버 연결 불가 또는 타임아웃)\n");
    }

    printf("\n");

    /* =========================================================
     * [TEST 2] POST 요청 (JSON 원시 데이터 전송)
     * ========================================================= */
    printf("📡 [TEST 2] POST 요청 전송 중...\n");

    const char* payload = "{\"title\":\"WebCore\",\"body\":\"v1.5 is Awesome!\",\"userId\":1}";

    /* 🚨 [수정 완] POST_RAW를 사용하여 원시 JSON 문자열 전송! */
    HttpClientResponse* res_post = client->POST_RAW(
        client,
        "https://jsonplaceholder.typicode.com:443/posts",
        payload,
        strlen(payload),
        "application/json; charset=utf-8"
    );

    if (res_post) {
        printf("✅ POST 성공! (HTTP Status: %d)\n", res_post->status_code);
        if (res_post->body) {
            printf("--- [수신된 Body] ---\n%s\n---------------------\n", res_post->body);
        }
        RELEASE((Object*)res_post); /* ARC 회수 */
    } else {
        printf("❌ POST 실패!\n");
    }

    /* 3. 자원 연쇄 해제 (Valgrind 0 bytes를 향해) */
    RELEASE((Object*)client);
    RELEASE((Object*)loop);

    printf("\n============================================\n");
    printf("  테스트 종료!! (Valgrind 누수 0을 기대합니다)\n");
    printf("============================================\n");

    return 0;
}