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

#endif /* SSL_CLIENT_H */