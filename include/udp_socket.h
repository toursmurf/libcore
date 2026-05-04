#ifndef UDP_SOCKET_H
#define UDP_SOCKET_H

#include "socket_base.h"

// UDP는 별도의 하위 메서드 없이 Socket 베이스의 통합 능력을 100% 활용
typedef struct UdpSocket {
    Socket base;
} UdpSocket;

UdpSocket* new_UdpServer(const char* host, int port);
UdpSocket* new_UdpClient(void);
UdpSocket* new_UdpSocket_from_fd(int fd);

const Class* udp_socket_class_ptr(void);

#endif
