#define _GNU_SOURCE
#include "http_response_parser.h"
#include "bytebuffer.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HTTP_MAX_BODY ((size_t)BB_MAX_CAPACITY)
#define HTTP_INITIAL_BODY_CAP ((size_t)16 * 1024)

static bool parse_content_length(const char* s, size_t max_limit, size_t* out) {
    if (!s || !out) return false;

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

    while (*s == ' ' || *s == '\t') s++;
    if (*s != '\0') return false;

    *out = value;
    return true;
}

static bool parse_chunk_size(const char* s, size_t* out) {
    if (!s || !*s || !out) return false;

    if (*s == ' ' || *s == '\t' || *s == '+' || *s == '-') return false;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) return false;

    size_t value = 0;
    bool has_digit = false;

    while (*s != '\0' && *s != ';' && *s != ' ' && *s != '\t' && *s != '\r' && *s != '\n') {
        char c = *s;
        size_t digit = 0;
        if (c >= '0' && c <= '9') digit = (size_t)(c - '0');
        else if (c >= 'a' && c <= 'f') digit = (size_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') digit = (size_t)(c - 'A' + 10);
        else return false;

        has_digit = true;

        if (value > SIZE_MAX / 16 ||
            (value == SIZE_MAX / 16 &&
             digit > SIZE_MAX % 16)) {
            return false;
        }
        value = value * 16 + digit;
        s++;
    }

    if (!has_digit) return false;

    while (*s == ' ' || *s == '\t') s++;
    if (*s != '\0' && *s != ';') return false;

    *out = value;
    return true;
}

HttpClientResponse* HttpResponseParser_parse_with_options(HttpTransport* transport, const char* initial_status_line, const char* request_method) {
    HttpClientResponse* res = new_HttpClientResponse();
    ByteBuffer* body_buf = NULL;
    char line[4096];

    if (!res) return NULL;

    if (initial_status_line && strlen(initial_status_line) > 0) {
        strncpy(line, initial_status_line, sizeof(line)-1);
        line[sizeof(line)-1] = '\0';
    } else {
        int ret = HttpTransport_recv_line(transport, line, sizeof(line));
        if (ret <= 0) goto reject;
    }

    while(1) {
        sscanf(line, "HTTP/1.%*d %d", &res->status_code);
        if (res->status_code == 101) goto reject; /* [2B-1] 101 Switching Protocols 즉시 거부 */
        if (res->status_code >= 100 && res->status_code < 200) {
            while (HttpTransport_recv_line(transport, line, sizeof(line)) > 0 && strlen(line) > 0);
            if (HttpTransport_recv_line(transport, line, sizeof(line)) <= 0) goto reject;
            continue;
        }
        break;
    }

    size_t content_length = 0;
    bool has_content_length = false;
    bool content_length_exceeds_body_cap = false;
    bool content_length_conflict = false; /* [2B-1] 중복 CL 불일치 유예 플래그 */
    int is_chunked = 0;
    bool has_transfer_encoding = false;

    while (HttpTransport_recv_line(transport, line, sizeof(line)) > 0) {
        if (strlen(line) == 0) break;
        char* colon = strchr(line, ':');
        if (colon) {
            *colon = '\0';
            char* key = line;
            char* raw_val = colon + 1;
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
                size_t parsed_content_length = 0;
                if (!parse_content_length(raw_val, SIZE_MAX, &parsed_content_length)) {
                    goto reject;
                }
                if (parsed_content_length > HTTP_MAX_BODY) {
                    content_length_exceeds_body_cap = true;
                }
                if (!has_content_length) {
                    content_length = parsed_content_length;
                    has_content_length = true;
                } else {
                    if (parsed_content_length != content_length) {
                        content_length_conflict = true; /* [2B-1] 즉시 거부 대신 플래그 설정 후 유예 */
                    }
                }
            }
            if (strcasecmp(key, "Transfer-Encoding") == 0) {
                has_transfer_encoding = true;
                if (strcasestr(val, "chunked")) {
                    is_chunked = 1;
                }
            }
        }
    }

    bool has_body = true;
    if (request_method && strcmp(request_method, "HEAD") == 0) {
        has_body = false;
    }
    if (res->status_code == 204 || res->status_code == 304) {
        has_body = false;
    }

    if (has_body) {
        /* [2B-1] 충돌 및 상한 검사는 has_body 판정 이후로 일원화 */
        if (has_content_length && has_transfer_encoding) {
            goto reject;
        }
        if (content_length_conflict) {
            goto reject;
        }
        if (has_content_length && content_length_exceeds_body_cap) {
            goto reject;
        }

        size_t initial_cap = HTTP_INITIAL_BODY_CAP;
        if (has_content_length) {
            if (content_length > 0) {
                initial_cap = (content_length < HTTP_INITIAL_BODY_CAP) ? content_length : HTTP_INITIAL_BODY_CAP;
            } else {
                initial_cap = 1;
            }
        }

        body_buf = new_ByteBuffer(initial_cap);
        if (!body_buf) goto reject;

        if (is_chunked) {
            size_t total_chunk_bytes = 0;
            while (1) {
                int ret = HttpTransport_recv_line(transport, line, sizeof(line));
                if (ret <= 0) goto reject;

                if (ret - (int)strlen(line) != 2) goto reject;

                size_t chunk_size = 0;
                if (!parse_chunk_size(line, &chunk_size)) goto reject;

                if (chunk_size == 0) {
                    for (;;) {
                        int tr_ret = HttpTransport_recv_line(transport, line, sizeof(line));
                        if (tr_ret <= 0) goto reject;

                        size_t line_len = strlen(line);
                        if ((size_t)tr_ret < line_len || (size_t)tr_ret - line_len != 2) {
                            goto reject;
                        }

                        if (line_len == 0) {
                            break;
                        }
                    }
                    break;
                }

                if (chunk_size > HTTP_MAX_BODY - total_chunk_bytes) goto reject;
                total_chunk_bytes += chunk_size;

                size_t read_total = 0;
                while (read_total < chunk_size) {
                    int c = HttpTransport_getc(transport);
                    if (c < 0) {
                        goto reject; /* [2B-1] Chunk Short-Read Atomic Reject */
                    }
                    if (body_buf->writeByte(body_buf, (uint8_t)c) < 0) {
                        goto reject;
                    }
                    read_total++;
                }

                int r1 = HttpTransport_getc(transport);
                if (r1 != '\r') goto reject;

                int r2 = HttpTransport_getc(transport);
                if (r2 != '\n') goto reject;
            }
        } else if (has_content_length) {
            size_t read_total = 0;
            while (read_total < content_length) {
                int c = HttpTransport_getc(transport);
                if (c < 0) {
                    goto reject; /* [2B-1] Content-Length Short-Read Atomic Reject */
                }
                if (body_buf->writeByte(body_buf, (uint8_t)c) < 0) {
                    goto reject;
                }
                read_total++;
            }
        } else {
            size_t total_eof_bytes = 0;
            int c;
            while ((c = HttpTransport_getc(transport)) >= 0) {
                if (total_eof_bytes >= HTTP_MAX_BODY) goto reject;
                if (body_buf->writeByte(body_buf, (uint8_t)c) < 0) goto reject;
                total_eof_bytes++;
            }
        }

        res->body_len = body_buf->write_pos;
        res->body = (char*)malloc(res->body_len + 1);
        if (!res->body) goto reject;

        if (res->body_len > 0) memcpy(res->body, body_buf->data, res->body_len);
        res->body[res->body_len] = '\0';

        RELEASE((Object*)body_buf);
    } else {
        res->body_len = 0;
        res->body = (char*)malloc(1);
        if (!res->body) goto reject;
        res->body[0] = '\0';
    }

    return res;

reject:
    if (body_buf) RELEASE((Object*)body_buf);
    if (res) RELEASE((Object*)res);
    return NULL;
}

HttpClientResponse* HttpResponseParser_parse_with_status(HttpTransport* transport, const char* initial_status_line) {
    return HttpResponseParser_parse_with_options(transport, initial_status_line, NULL);
}

HttpClientResponse* HttpResponseParser_parse(HttpTransport* transport) {
    return HttpResponseParser_parse_with_options(transport, NULL, NULL);
}