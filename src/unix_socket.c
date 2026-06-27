#define _GNU_SOURCE 
#include "unix_socket.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/un.h>
#include <errno.h>

// [1] 시그니처 수정: Object* 로 고정 (경고 해결)
static void UnixSocket_finalize(Object* obj) {
    Socket_finalize(obj);
}

static const Class _unixSocketClass = {
		.name =  "UnixSocket",
		.size = sizeof(UnixSocket),
    .finalize = UnixSocket_finalize
};

const Class* unix_socket_class_ptr(void) {
    return &_unixSocketClass;
}

// ----------------------------------------------------------------------------
// [VTable 구현부]
// ----------------------------------------------------------------------------

static int UnixSocket_bind_impl(Socket* s, const char* path, int port) {
    (void)port;
    if (!s || !path) return -1;
    UnixSocket* self = (UnixSocket*)s;

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    unlink(path);
    int ret = bind(s->fd, (struct sockaddr*)&addr, sizeof(addr));

    if (ret == 0) {
        // 성공 시 나중에 지우기 위해 경로 저장
        strncpy(self->bound_path, path, sizeof(self->bound_path) - 1);
    }
    return ret;
}

static int UnixSocket_listen_impl(Socket* s, int backlog) {
    if (!s) return -1;
    return listen(s->fd, (backlog > 0) ? backlog : TCP_BACKLOG);
}

static int UnixSocket_connect_impl(Socket* s, const char* path, int port) {
    (void)port;
    if (!s || !path) return -1;

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    int res = connect(s->fd, (struct sockaddr*)&addr, sizeof(addr));
    if (res < 0 && errno == EINPROGRESS) return SOCKET_WOULD_BLOCK;
    return res;
}

// [수정] 인자 2개 명시 (UnixSocket*, char*)
static UnixSocket* UnixSocket_accept_impl(UnixSocket* self, char* path) {
    if (!self) return NULL;
    
    // 🚨 [수정 1]: Valgrind 경고 방어를 위해 메모리 공간 0으로 초기화!
    struct sockaddr_un addr = {0};
    socklen_t addr_len = sizeof(addr);

    int c_fd = accept4(self->base.fd, (struct sockaddr*)&addr, &addr_len, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (c_fd < 0) return NULL;

    if (path) {
        // 🚨 [수정 2]: 클라이언트가 bind를 안 해서 이름이 없는 경우(unnamed) 방어
        if (addr_len > sizeof(sa_family_t)) {
            size_t len = strlen(addr.sun_path);

            if (len >= sizeof(path))
              len = sizeof(path) - 1;

            memcpy(path, addr.sun_path, len);
            path[len] = '\0';
        } else {
            path[0] = '\0'; // 이름 없는 클라이언트
        }
        path[sizeof(addr.sun_path) - 1] = '\0'; // 안전망
    }

    return new_UnixSocket_from_fd(c_fd);
}
// ----------------------------------------------------------------------------
// [팩토리 메서드]
// ----------------------------------------------------------------------------

UnixSocket* new_UnixSocket_from_fd(int fd) {
    if (fd < 0) return NULL;
    UnixSocket* self = (UnixSocket*)calloc(1, sizeof(UnixSocket));
    if (!self) return NULL;

    // [수정] 인자 3개: SOCKET_UNIX 명시
    Socket_init_base(&self->base, fd, SOCKET_UNIX);

    self->base.base.type = &_unixSocketClass;

    self->base.bind    = UnixSocket_bind_impl;
    self->base.listen  = UnixSocket_listen_impl;
    self->base.connect = UnixSocket_connect_impl;
    self->accept       = UnixSocket_accept_impl;

    return self;
}

UnixSocket* new_UnixServer(const char* path) {
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) return NULL;

    UnixSocket* self = new_UnixSocket_from_fd(fd);
    if (!self) { close(fd); return NULL; }

    if (self->base.bind(&self->base, path, 0) != 0 ||
        self->base.listen(&self->base, TCP_BACKLOG) != 0) {
        RELEASE(self);
        return NULL;
    }
    return self;
}

UnixSocket* new_UnixClient(const char* path) {
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) return NULL;

    UnixSocket* self = new_UnixSocket_from_fd(fd);
    if (!self) { close(fd); return NULL; }

    if (path) {
        int ret = self->base.connect(&self->base, path, 0);
        if (ret < 0 && ret != SOCKET_WOULD_BLOCK) {
            RELEASE(self);
            return NULL;
        }
    }
    return self;
}
