#ifndef SSL_SERVER_H
#define SSL_SERVER_H

#include "ssl_socket.h"

/**
 * [SslServer]
 * 용도: HTTPS 서버
 * - bind() + listen()
 * - cert/key PEM 파일 필요
 * - ctx 소유권: new_SslServer() 가 생성/보유
 *
 * accept 후 반드시 SslSocket_accept() 호출!!
 * → SSL_CTX_up_ref() + SSL_new() + SSL_accept() 수행
 */
SslSocket* new_SslServer(const char* host, int port,
                         const char* cert, const char* key);

/**
 * [SslSocket_accept]
 * 서버 accept 이후 클라이언트 SslSocket 생성
 *
 * SSL_CTX Ownership:
 * SSL_CTX_up_ref(server->ctx) 호출
 * → 클라이언트 finalize 시 SSL_CTX_free() 안전!!
 * → 서버 ctx 먼저 해제되는 문제 방지!!
 */
SslSocket* SslSocket_accept(SslSocket* server);

#endif /* SSL_SERVER_H */