#define _GNU_SOURCE
#include "http_response_parser.h"
#include "bytebuffer.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

HttpClientResponse* HttpResponseParser_parse_with_status(HttpTransport* transport, const char* initial_status_line) {
    HttpClientResponse* res = new_HttpClientResponse();
    if (!res) return NULL;

    char line[4096];

    if (initial_status_line && strlen(initial_status_line) > 0) {
        strncpy(line, initial_status_line, sizeof(line)-1);
        line[sizeof(line)-1] = '\0';
    } else {
        if (HttpTransport_recv_line(transport, line, sizeof(line)) <= 0) {
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

    long content_length = -1;
    int is_chunked = 0;

    while (HttpTransport_recv_line(transport, line, sizeof(line)) > 0) {
        if (strlen(line) == 0) break;
        char* colon = strchr(line, ':');
        if (colon) {
            *colon = '\0';
            char* key = line; char* val = colon + 1;
            while (isspace((unsigned char)*val)) val++;

            hashmap_put_str(res->headers, key, val);

            /* V1.5 단순 세션 추출기 */
            if (strcasecmp(key, "Set-Cookie") == 0) {
                String* cstr = new_String(val);
                if (cstr) {
                    res->cookies->add(res->cookies, (Object*)cstr);
                }
            }
            if (strcasecmp(key, "Content-Length") == 0) content_length = atol(val);
            if (strcasecmp(key, "Transfer-Encoding") == 0 && strcasestr(val, "chunked")) is_chunked = 1;
        }
    }

    ByteBuffer* body_buf = new_ByteBuffer(content_length > 0 ? content_length + 1 : 16384);
    if (!body_buf) { RELEASE((Object*)res); return NULL; }

    if (is_chunked) {
        while (1) {
            if (HttpTransport_recv_line(transport, line, sizeof(line)) <= 0) break;
            if (strlen(line) == 0) continue;

            char* ext = strchr(line, ';');
            if (ext) *ext = '\0';

            long chunk_size = strtol(line, NULL, 16);
            if (chunk_size == 0) { HttpTransport_recv_line(transport, line, sizeof(line)); break; }

            long read_total = 0;
            while (read_total < chunk_size) {
                int c = HttpTransport_getc(transport);
                if (c < 0) break;
                body_buf->writeByte(body_buf, (uint8_t)c);
                read_total++;
            }
            HttpTransport_recv_line(transport, line, sizeof(line));
        }
    } else if (content_length > 0) {
        long read_total = 0;
        while (read_total < content_length) {
            int c = HttpTransport_getc(transport);
            if (c < 0) break;
            body_buf->writeByte(body_buf, (uint8_t)c);
            read_total++;
        }
    } else {
        int c;
        while ((c = HttpTransport_getc(transport)) >= 0) {
            body_buf->writeByte(body_buf, (uint8_t)c);
        }
    }

    res->body_len = body_buf->write_pos;

    /* 🚨 NULL 방어막 완비: 메모리 부족 시 즉각 파기 */
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