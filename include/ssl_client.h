#ifndef SSL_CLIENT_H
#define SSL_CLIENT_H

#include "ssl_socket.h"

/**
 * [SslClient]
 * 용도: https:// ssl:// wss://
 * - connect() + SSL_connect() + SNI
 * - 서버 인증서 CA 검증
 * - cert/key 불필요
 */
SslSocket* new_SslClient(const char* host, int port);

/**
 * [신규: 비동기 io_uring 결합용 하이브리드 생성자]
 * 용도: 외부(io_uring)에서 이미 TCP 연결을 끝낸 fd를 주입받음.
 * TCP 3-way Handshake의 병목을 건너뛰고 즉시 SSL Handshake만 수행.
 */
SslSocket* new_SslClient_from_fd(const char* host, int connected_fd);

#endif /* SSL_CLIENT_H */