#ifndef TRANSPORT_FACTORY_H
#define TRANSPORT_FACTORY_H

#include "object.h"
#include "string_obj.h"
#include "socket_base.h"
#include "exception.h"

/* URL 파싱 정보 객체 (ARC OWNED) */
typedef struct UrlInfo {
    Object base;
    String* scheme;
    String* host;
    int port;
    String* path;
    String* query;
} UrlInfo;

/* URL 파서 & 팩토리 인터페이스 */
UrlInfo* UrlInfo_parse(const char* url_str);
Socket* TransportFactory_createClient(UrlInfo* info, Exception** out_err);
Socket* TransportFactory_createServer(UrlInfo* info, const char* cert, const char* key, Exception** out_err);

#endif /* TRANSPORT_FACTORY_H */