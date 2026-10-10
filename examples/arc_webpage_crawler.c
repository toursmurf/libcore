/**
 * @file arc_webpage_crawler.c
 * @brief libcore HttpClient 기반 범용 웹페이지 스니퍼 (Swiss Army Knife)
 *
 * @note
 *   - HTTP/HTTPS/포트번호를 자동으로 인식합니다. (라이브러리 파서 의존)
 *   - URL 하드닝(URL-1) 작전을 위한 불량 포트 타격 전용 도구로 활용 가능합니다.
 *   - 텍스트(HTML/JSON 등) 응답은 본문을 출력하고, 바이너리는 건너뜁니다.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

#include "libcore.h"
#include "http_client.h"

#define TIMEOUT_MS 30000

/* -------------------------------------------------------------------------- */
/* Helper Functions                                                           */
/* -------------------------------------------------------------------------- */

/* 대소문자를 무시하고 헤더 값을 찾는 Helper */
static const char* get_header_case_insensitive(HashMap* headers, const char* target_key) {
    if (!headers || !target_key) return NULL;

    ArrayList* keys = headers->keys(headers);
    if (!keys) return NULL;

    const char* result = NULL;
    for (int i = 0; i < keys->getSize(keys); i++) {
        String* k = (String*)keys->get(keys, i);
        if (k && k->c_str && strcasecmp(k->c_str(k), target_key) == 0) {
            result = hashmap_get_str(headers, k->c_str(k));
            break;
        }
    }

    RELEASE((Object*)keys);
    return result;
}

/* "text/html; charset=utf-8" 에서 "text/html"만 추출 */
static void extract_media_type(const char* content_type, char* out_buf, size_t out_cap) {
    if (!content_type || !out_buf || out_cap == 0) return;

    out_buf[0] = '\0';

    /* 선행 공백 무시 */
    while (isspace((unsigned char)*content_type)) content_type++;

    size_t i = 0;
    while (content_type[i] != '\0' && content_type[i] != ';' && isspace((unsigned char)content_type[i]) == 0) {
        if (i < out_cap - 1) {
            out_buf[i] = content_type[i];
        }
        i++;
    }

    if (i < out_cap) {
        out_buf[i] = '\0';
    } else {
        out_buf[out_cap - 1] = '\0';
    }
}

/* -------------------------------------------------------------------------- */
/* Main                                                                       */
/* -------------------------------------------------------------------------- */

int main(int argc, char* argv[])
{
    HttpClient* client = NULL;
    HttpClientResponse* res = NULL;
    int exit_code = EXIT_FAILURE;

    /* [계약 1] 인자가 없으면 사용법 출력 후 exit 1 */
    if (argc != 2) {
        fprintf(stderr, "========================================================\n");
        fprintf(stderr, "  libcore - WebPage Crawler (HTTP/HTTPS Auto-detect)\n");
        fprintf(stderr, "========================================================\n");
        fprintf(stderr, "Usage: %s <URL>\n", argv[0]);
        fprintf(stderr, "  ex) %s https://n.news.naver.com/mnews/hotissue/...\n", argv[0]);
        fprintf(stderr, "  ex) %s http://example.com:4444/test\n\n", argv[0]);
        fprintf(stderr, "Note: HTML/JSON/XML 등 텍스트만 본문을 출력하며,\n");
        fprintf(stderr, "      이미지 및 Content-Type 누락 응답은 바이너리로 간주해 생략합니다.\n");
        return EXIT_FAILURE;
    }

    const char* url = argv[1];
    printf("[*] Target URL: %s\n", url);

    /*
     * 예제는 URL을 검증하지 않고 라이브러리의 파싱 능력(URL-1 버그)에 온전히 위임한다.
     */
    client = new_HttpClient(NULL);
    if (client == NULL) {
        fprintf(stderr, "[!] Failed to create HttpClient\n");
        goto cleanup;
    }

    /* 압축 응답을 요청하지 않고 원본 응답을 확인한다. */
    client->options.enable_compression = false;
    client->options.timeout_ms = TIMEOUT_MS;

    /* [주의] User-Agent는 현재 http_client.c(FULL LOCK) 내부에서 강제 삽입하므로
     * 중복 헤더 방지를 위해 예제에서는 별도로 세팅하지 않습니다.
     * (추후 libcore 코어 개편 시 Chrome UA 주입 구조로 변경 필요) */

    /* [계약 2] 퍼블릭 API 사용: GET 요청 실행 */
    res = client->GET(client, url, NULL);

    /* [계약 4] 실패(NULL 응답) 시 exit 1 (쉘 스크립트 판정용) */
    if (res == NULL) {
        fprintf(stderr, "\n[!] HTTP Request Failed or Returned NULL.\n");
        goto cleanup;
    }

    printf("\n--- [ Response Info ] ---\n");
    printf("Status: %d\n", res->status_code);

    const char* content_type = get_header_case_insensitive(res->headers, "Content-Type");
    const char* content_length = get_header_case_insensitive(res->headers, "Content-Length");
    const char* transfer_encoding = get_header_case_insensitive(res->headers, "Transfer-Encoding");

    printf("Content-Type: %s\n", content_type ? content_type : "(none)");
    if (content_length) {
        printf("Content-Length: %s\n", content_length);
    }
    if (transfer_encoding) {
        printf("Transfer-Encoding: %s\n", transfer_encoding);
    }

    printf("\n--- [ Body ] ---\n");

    /* [계약 3] 보수적인 Content-Type 판정 (누락 시 바이너리 취급) */
    bool should_print_body = false;
    char media_type[128] = {0};

    if (content_type != NULL) {
        extract_media_type(content_type, media_type, sizeof(media_type));

        if (strncasecmp(media_type, "text/", 5) == 0 ||
            strcasecmp(media_type, "application/json") == 0 ||
            strcasecmp(media_type, "application/xml") == 0 ||
            strcasecmp(media_type, "application/xhtml+xml") == 0 ||
            strcasecmp(media_type, "application/javascript") == 0) {
            should_print_body = true;
        }
    }

    if (should_print_body) {
        if (res->body != NULL && res->body_len > 0) {
            /* NUL character 안전성 및 정확한 바이너리 출력을 위해 fwrite 사용 */
            fwrite(res->body, 1, res->body_len, stdout);
            printf("\n");
        } else {
            printf("(Empty Body)\n");
        }
    } else {
        printf("[!] binary skipped (Content-Type: %s, %zu bytes received)\n",
               content_type ? content_type : "unknown",
               res->body_len);
    }
    printf("-------------------------\n");

    exit_code = EXIT_SUCCESS;

cleanup:
    /* [계약 5] 완벽한 해제 (Valgrind lost 0 유지) */
    if (res != NULL) {
        RELEASE((Object*)res);
    }
    if (client != NULL) {
        RELEASE((Object*)client);
    }

    return exit_code;
}