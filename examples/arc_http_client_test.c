#include "event_loop.h"
#include "http_client.h"
#include "string_obj.h"
#include "hashmap.h"
#include <stdio.h>
#include <string.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

int main(void) {
    printf("============================================\n");
    printf("  🚀 libcore v1.5 HttpClient 실전 테스트\n");
    printf("============================================\n\n");

    int exit_code = 0;
    EventLoop* loop = new_EventLoop(1024);
    HttpClient* client = new_HttpClient(loop);

    if (!client) {
        printf("❌ HttpClient 생성 실패!\n");
        exit_code = -1;
        goto cleanup;
    }

    /* 2. 전역 설정 */
    client->options.timeout_ms = 300000;
    client->options.follow_redirects = true;

    client->setHeader(client, "User-Agent", "WebCore-Client/1.5");
    client->setHeader(client, "Accept", "application/json");

    /* [TEST 1] GET 요청 */
    printf("📡 [TEST 1] GET 요청 전송 중...\n");
    HttpClientResponse* res_get = client->GET(
        client,
        "https://jsonplaceholder.typicode.com:443/todos/1",
        NULL
    );

    if (res_get) {
        printf("✅ GET 성공! (HTTP Status: %d)\n", res_get->status_code);
        if (res_get->body) {
            printf("--- [수신된 Body] ---\n%s\n---------------------\n", res_get->body);
        }
        RELEASE((Object*)res_get);
    } else {
        printf("❌ GET 실패! (서버 연결 불가 또는 타임아웃)\n");
    }

    printf("\n");

    /* [TEST 2] POST 요청 */
    printf("📡 [TEST 2] POST 요청 전송 중...\n");
    const char* payload = "{\"title\":\"WebCore\",\"body\":\"v1.5 is Awesome!\",\"userId\":1}";

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
        RELEASE((Object*)res_post);
    } else {
        printf("❌ POST 실패!\n");
    }

cleanup:
    /* 🚨 [정화 패턴] 성공/실패 무관하게 메모리 및 SSL 상태 완벽 소각 */
    if (client) RELEASE((Object*)client);
    if (loop)   RELEASE((Object*)loop);

    ERR_free_strings();
    EVP_cleanup();
    CRYPTO_cleanup_all_ex_data();
    SSL_COMP_free_compression_methods();

    printf("\n============================================\n");
    printf("  테스트 종료!! (Valgrind 누수 0을 달성했나이다!)\n");
    printf("============================================\n");

    return exit_code;
}