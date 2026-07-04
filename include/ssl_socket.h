#ifndef SSL_SOCKET_H
#define SSL_SOCKET_H

#include "socket_base.h"
#include <openssl/ssl.h>
#include <openssl/err.h>

/**
 * [SslSocket] — 단일 구조체
 * Socket 인터페이스 구현체.
 * fd + SSL* 만으로 동작. TcpSocket 객체 불필요!!
 *
 * 형제 관계:
 * Socket
 * ├── TcpSocket
 * ├── SslSocket   ← 여기
 * ├── UdpSocket
 * └── UnixSocket
 *
 * SSL_CTX Ownership Rule:
 * 1. new_SslServer()     → SSL_CTX_new()  → 소유권 보유
 * 2. SslSocket_accept()  → SSL_CTX_up_ref() → 참조 증가
 * 3. SslSocket_finalize()→ SSL_CTX_free() (OpenSSL refcount 기반)
 *
 * is_server 플래그 없음:
 * 생성 함수(new_SslClient/new_SslServer)가 역할 결정!!
 */
typedef struct SslSocket {
    Socket    base;       /* ARC 호환 (첫 멤버 필수!!)  */
    SSL_CTX* ctx;        /* ✅ 포인터!! OpenSSL 컨텍스트 */
    SSL* ssl;        /* ✅ 포인터!! OpenSSL 세션     */
    char      host[256];  /* SNI 및 인증서 검증용 호스트 */
} SslSocket;

void         SslSocket_init_base(SslSocket* self, int fd);
void         SslSocket_finalize(Object* obj);
const Class* ssl_socket_class_ptr(void);

#endif /* SSL_SOCKET_H */