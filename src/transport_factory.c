#include "transport_factory.h"
#include "tcp_socket.h"
#include "ssl_client.h"
#include "ssl_server.h"
#include <string.h>
#include <stdlib.h>
#include <errno.h>

/* ============================================================
* [1] UrlInfo ARC 메타데이터 및 소멸자
* ============================================================ */
static void UrlInfo_finalize(Object* obj) {
    UrlInfo* self = (UrlInfo*)obj;
    if (self->scheme) RELEASE(self->scheme);
    if (self->host)   RELEASE(self->host);
    if (self->path)   RELEASE(self->path);
    if (self->query)  RELEASE(self->query);
}

static const Class _UrlInfo_Class = {
    .name = "UrlInfo",
    .size = sizeof(UrlInfo),
    .finalize = UrlInfo_finalize
};

/* ============================================================
* [2] 궁극의 URL 파서 (🚀 나초안 방어막 + 클순 마님 잔여 권고 패치)
* ============================================================ */
UrlInfo* UrlInfo_parse(const char* url_str) {
    if (!url_str) return NULL;

    UrlInfo* info = (UrlInfo*)calloc(1, sizeof(UrlInfo));
    if (!info) return NULL;

    Object_Init((Object*)info, &_UrlInfo_Class);
    info->port = -1;

    const char* p = url_str;

    /* 1. Scheme 파싱 ("scheme://") */
    const char* scheme_end = strstr(p, "://");
    if (scheme_end) {
        info->scheme = new_StringN(p, (size_t)(scheme_end - p));
        if (!info->scheme) { RELEASE((Object*)info); return NULL; }
        p = scheme_end + 3;
    } else {
        info->scheme = new_String("http");
        if (!info->scheme) { RELEASE((Object*)info); return NULL; }
    }

    /* 2. Host와 Port 파싱 구간 분리 */
    const char* path_start = strchr(p, '/');
    const char* port_start = strchr(p, ':');
    const char* host_end = path_start ? path_start : (p + strlen(p));

    /* 🚨 [클순 마님 권고 패치 2] 깡통 호스트("https://") 입구컷! */
    if (p == host_end || (port_start && p == port_start)) {
        RELEASE((Object*)info);
        return NULL; /* Hostname is empty */
    }

    /* 🚨 [클순 마님 권고 패치 1] 포트 유효 범위 강제 검증! */
    if (port_start && port_start < host_end) {
        info->host = new_StringN(p, (size_t)(port_start - p));
        if (!info->host) { RELEASE((Object*)info); return NULL; }

        int parsed_port = atoi(port_start + 1);
        if (parsed_port > 0 && parsed_port <= 65535) {
            info->port = parsed_port;
        } else {
            RELEASE((Object*)info);
            return NULL; /* Invalid Port Exception */
        }
    } else {
        info->host = new_StringN(p, (size_t)(host_end - p));
        if (!info->host) { RELEASE((Object*)info); return NULL; }
    }

    /* 3. 기본 포트 세팅 (scheme 기반 자동 할당) */
    if (info->port == -1) {
        const char* scheme_cstr = info->scheme->c_str(info->scheme);
        if (strcmp(scheme_cstr, "https") == 0 ||
            strcmp(scheme_cstr, "wss") == 0 ||
            strcmp(scheme_cstr, "ssl") == 0) {
            info->port = 443;
        } else {
            info->port = 80;
        }
    }

    /* 4. Path와 Query 파싱 */
    if (path_start) {
        const char* query_start = strchr(path_start, '?');
        if (query_start) {
            info->path = new_StringN(path_start, (size_t)(query_start - path_start));
            if (!info->path) { RELEASE((Object*)info); return NULL; }

            info->query = new_String(query_start + 1);
            if (!info->query) { RELEASE((Object*)info); return NULL; }
        } else {
            info->path = new_String(path_start);
            if (!info->path) { RELEASE((Object*)info); return NULL; }
        }
    } else {
        info->path = new_String("/");
        if (!info->path) { RELEASE((Object*)info); return NULL; }
    }

    return info;
}

/* ============================================================
* [3] 클라이언트 전송망 팩토리
* ============================================================ */
Socket* TransportFactory_createClient(UrlInfo* info, Exception** out_err) {
    if (!info || !info->scheme || !info->host) {
        if (out_err) *out_err = throw_Exception(ERR_SOCK_URL, 0, "Invalid UrlInfo object");
        return NULL;
    }

    const char* scheme = info->scheme->c_str(info->scheme);
    const char* host = info->host->c_str(info->host);
    int port = info->port;

    Socket* sock = NULL;

    if (strcmp(scheme, "http") == 0 || strcmp(scheme, "tcp") == 0 || strcmp(scheme, "ws") == 0) {
        sock = (Socket*)new_TcpClient(host, port);
        if (!sock && out_err) {
            *out_err = throw_Exception(ERR_SOCK_CONNECT, errno, "Failed to connect TCP Client");
        }
    }
    else if (strcmp(scheme, "https") == 0 || strcmp(scheme, "ssl") == 0 || strcmp(scheme, "wss") == 0) {
        sock = (Socket*)new_SslClient(host, port);
        if (!sock && out_err) {
            *out_err = throw_Exception(ERR_SOCK_CONNECT, errno, "Failed to connect SSL Client");
        }
    }
    else {
        if (out_err) *out_err = throw_Exception(ERR_SOCK_SCHEME, 0, "Unsupported protocol scheme in Factory");
    }

    return sock;
}

/* ============================================================
* [4] 서버 전송망 팩토리
* ============================================================ */
Socket* TransportFactory_createServer(UrlInfo* info, const char* cert, const char* key, Exception** out_err) {
    if (!info || !info->scheme || !info->host) {
        if (out_err) *out_err = throw_Exception(ERR_SOCK_URL, 0, "Invalid UrlInfo object");
        return NULL;
    }

    const char* scheme = info->scheme->c_str(info->scheme);
    const char* host = info->host->c_str(info->host);
    int port = info->port;

    Socket* sock = NULL;

    if (strcmp(scheme, "http") == 0 || strcmp(scheme, "tcp") == 0 || strcmp(scheme, "ws") == 0) {
        sock = (Socket*)new_TcpServer(host, port);
        if (!sock && out_err) {
            *out_err = throw_Exception(ERR_SOCK_CREATE, errno, "Failed to create TCP Server");
        }
    }
    else if (strcmp(scheme, "https") == 0 || strcmp(scheme, "ssl") == 0 || strcmp(scheme, "wss") == 0) {
        if (!cert || !key) {
            if (out_err) *out_err = throw_Exception(ERR_SOCK_CREATE, 0, "SSL Server requires cert and key paths");
            return NULL;
        }
        sock = (Socket*)new_SslServer(host, port, cert, key);
        if (!sock && out_err) {
            *out_err = throw_Exception(ERR_SOCK_CREATE, errno, "Failed to create SSL Server");
        }
    }
    else {
        if (out_err) *out_err = throw_Exception(ERR_SOCK_SCHEME, 0, "Unsupported protocol scheme in Factory");
    }

    return sock;
}