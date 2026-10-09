#define _GNU_SOURCE
#include "http_response_parser.h"
#include "bytebuffer.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>

#define HTTP_MAX_BODY ((size_t)64 * 1024 * 1024)
#define HTTP_INITIAL_BODY_CAP ((size_t)16 * 1024)

/* 엄격한 Direct Decimal Content-Length 파서 (SP/HTAB OWS만 허용) */
static bool parse_content_length(const char* s, size_t max_limit, size_t* out) {
    if (!s || !out) return false;

    /* leading OWS: SP / HTAB only */
    while (*s == ' ' || *s == '\t') s++;

    if (*s == '\0') return false;

    size_t value = 0;
    bool has_digit = false;

    while (*s >= '0' && *s <= '9') {
        size_t digit = (size_t)(*s - '0');
        has_digit = true;

        if (value > max_limit / 10 ||
            (value == max_limit / 10 &&
             digit > max_limit % 10)) {
            return false;
        }

        value = value * 10 + digit;
        s++;
    }

    if (!has_digit) return false;

    /* trailing OWS: SP / HTAB only */
    while (*s == ' ' || *s == '\t') s++;

    if (*s != '\0') return false;

    *out = value;
    return true;
}

HttpClientResponse* HttpResponseParser_parse_with_status(HttpTransport* transport, const char* initial_status_line) {
    HttpClientResponse* res = new_HttpClientResponse();
    if (!res) return NULL;

    char line[4096];

    if (initial_status_line && strlen(initial_status_line) > 0) {
        strncpy(line, initial_status_line, sizeof(line)-1);
        line[sizeof(line)-1] = '\0';
    } else {
        int ret = HttpTransport_recv_line(transport, line, sizeof(line));
        if (ret <= 0) {
            printf("[DEBUG] 치명적 오류: 상태 줄을 읽을 수 없습니다. (ret: %d)\n", ret);
            RELEASE((Object*)res);
            return NULL;
        }
    }

    while(1) {
        sscanf(line, "HTTP/1.%*d %d", &res->status_code);
        if (res->status_code >= 100 && res->status_code < 200) {
            while (HttpTransport_recv_line(transport, line, sizeof(line)) > 0 && strlen(line) > 0);
            if (HttpTransport_recv_line(transport, line, sizeof(line)) <= 0) {
                RELEASE((Object*)res);
                return NULL;
            }
            continue;
        }
        break;
    }

    size_t content_length = 0;
    bool has_content_length = false;
    int is_chunked = 0;

    while (HttpTransport_recv_line(transport, line, sizeof(line)) > 0) {
        if (strlen(line) == 0) break;
        char* colon = strchr(line, ':');
        if (colon) {
            *colon = '\0';
            char* key = line;
            char* raw_val = colon + 1; /* ✅ raw 원문 보존 포인트 */
            char* val = raw_val;
            while (isspace((unsigned char)*val)) val++;

            hashmap_put_str(res->headers, key, val);

            if (strcasecmp(key, "Set-Cookie") == 0) {
                String* cstr = new_String(val);
                if (cstr) {
                    res->cookies->add(res->cookies, (Object*)cstr);
                    RELEASE((Object*)cstr);
                }
            }
            if (strcasecmp(key, "Content-Length") == 0) {
                /* ✅ raw_val을 직접 투입하여 pre-normalization 우회 차단 */
                if (!parse_content_length(raw_val, HTTP_MAX_BODY, &content_length)) {
                    RELEASE((Object*)res);
                    return NULL;
                }
                has_content_length = true;
            }
            if (strcasecmp(key, "Transfer-Encoding") == 0 && strcasestr(val, "chunked")) is_chunked = 1;
        }
    }

    /* 스마트 초기 할당: min(content_length, 16KB) 또는 0일 때 최소 보장 */
    size_t initial_cap = HTTP_INITIAL_BODY_CAP;
    if (has_content_length) {
        if (content_length > 0) {
            initial_cap = (content_length < HTTP_INITIAL_BODY_CAP) ? content_length : HTTP_INITIAL_BODY_CAP;
        } else {
            initial_cap = 1;
        }
    }

    ByteBuffer* body_buf = new_ByteBuffer(initial_cap);
    if (!body_buf) { RELEASE((Object*)res); return NULL; }

    if (is_chunked) {
        while (1) {
            if (HttpTransport_recv_line(transport, line, sizeof(line)) <= 0) break;
            if (strlen(line) == 0) continue;

            char* ext = strchr(line, ';');
            if (ext) *ext = '\0';

            long chunk_size = strtol(line, NULL, 16);
            if (chunk_size < 0) {
                RELEASE((Object*)body_buf);
                RELEASE((Object*)res);
                return NULL;
            }

            if (chunk_size == 0) {
                while (HttpTransport_recv_line(transport, line, sizeof(line)) > 0) {
                    if (strlen(line) == 0) break;
                }
                break;
            }

            long read_total = 0;
            while (read_total < chunk_size) {
                int c = HttpTransport_getc(transport);
                if (c < 0) break;
                if (body_buf->writeByte(body_buf, (uint8_t)c) < 0) break;
                read_total++;
            }
            HttpTransport_recv_line(transport, line, sizeof(line));
        }
    } else if (has_content_length) {
        size_t read_total = 0;
        while (read_total < content_length) {
            int c = HttpTransport_getc(transport);
            if (c < 0) break;
            if (body_buf->writeByte(body_buf, (uint8_t)c) < 0) break;
            read_total++;
        }
    } else {
        int c;
        while ((c = HttpTransport_getc(transport)) >= 0) {
            if (body_buf->writeByte(body_buf, (uint8_t)c) < 0) break;
        }
    }

    res->body_len = body_buf->write_pos;

    res->body = (char*)malloc(res->body_len + 1);
    if (!res->body) {
        RELEASE((Object*)body_buf);
        RELEASE((Object*)res);
        return NULL;
    }

    if (res->body_len > 0) memcpy(res->body, body_buf->data, res->body_len);
    res->body[res->body_len] = '\0';

    RELEASE((Object*)body_buf);
    return res;
}

HttpClientResponse* HttpResponseParser_parse(HttpTransport* transport) {
    return HttpResponseParser_parse_with_status(transport, NULL);
}