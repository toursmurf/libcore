#ifndef TCP_SOCKET_H
#define TCP_SOCKET_H

#include "socket_base.h"

typedef struct TcpSocket {
    Socket base;
    struct TcpSocket* (*accept)(struct TcpSocket* self, char* ip, int* port);
} TcpSocket;

TcpSocket* new_TcpServer(const char* host, int port);
TcpSocket* new_TcpClient(const char* host, int port);
TcpSocket* new_TcpSocket_from_fd(int fd);
const Class* tcp_socket_class_ptr(void);
#endif