#ifndef HTTP_TRANSPORT_H
#define HTTP_TRANSPORT_H

#include "socket_base.h"
#include <stdbool.h>
#include <sys/types.h>

#ifdef HAS_LIBURING
#include <liburing.h>
#endif

typedef struct HttpTransport HttpTransport;
struct HttpTransport {
    char scheme[16];
    char host[256];
    char path[2048];
    int port;
    Socket* sock;
    bool use_uring;

#ifdef HAS_LIBURING
    struct io_uring own_ring;   /* [OWNED] Transport 전용 링 */
    bool            has_own_ring;
#endif

    char read_buf[4096];
    int  read_pos;
    int  read_end;
};

HttpTransport* HttpTransport_connect(const char* url, void* ignored_ring, int timeout_ms);
ssize_t HttpTransport_send(HttpTransport* self, const void* buf, size_t len);
ssize_t HttpTransport_recv(HttpTransport* self, void* buf, size_t len);
void HttpTransport_close(HttpTransport* self);

int HttpTransport_getc(HttpTransport* self);
int HttpTransport_recv_line(HttpTransport* self, char* buffer, size_t max_len);
ssize_t HttpTransport_read(HttpTransport* self, void* buf, size_t len);

#endif /* HTTP_TRANSPORT_H */