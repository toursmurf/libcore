#ifndef UNIX_SOCKET_H
#define UNIX_SOCKET_H

#include "socket_base.h"
#include <sys/un.h>

typedef struct UnixSocket {
    Socket base;
    char bound_path[108]; // 👈 [수정] bind한 경로를 기억해야 나중에 unlink 합니다!
    struct UnixSocket* (*accept)(struct UnixSocket* self, char* path);
} UnixSocket;

UnixSocket* new_UnixServer(const char* path);
UnixSocket* new_UnixClient(const char* path);
UnixSocket* new_UnixSocket_from_fd(int fd);

const Class* unix_socket_class_ptr(void);

#endif
