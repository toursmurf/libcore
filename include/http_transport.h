#ifndef HTTP_TRANSPORT_H
#define HTTP_TRANSPORT_H

#include "socket_base.h"
#include <sys/types.h>

typedef struct {
    Socket* sock;
    char* host;
    char* path;
    int port;
    char scheme[16];

    char read_buf[8192];
    int read_pos;
    int read_end;
} HttpTransport;

HttpTransport* HttpTransport_connect(const char* url);
ssize_t HttpTransport_send(HttpTransport* self, const void* buf, size_t len);
ssize_t HttpTransport_recv(HttpTransport* self, void* buf, size_t len);
int HttpTransport_getc(HttpTransport* self);

/**
 * @brief Reads one complete line from the HTTP transport.
 *
 * Success:
 *   - LF must actually be observed within the max_len - 1 byte
 *     input window.
 *   - CRLF and bare LF termination are accepted.
 *   - CR/LF terminators are not stored in line_buf.
 *   - line_buf is NUL-terminated.
 *   - ret is the number of bytes actually consumed from the
 *     transport, including the line terminator.
 *   - Successful return range is:
 *
 *         1 <= ret && ret <= max_len - 1
 *
 * Failure:
 *   - Returns -1.
 *   - self == NULL, line_buf == NULL, or max_len < 2 is failure.
 *   - EOF before LF is failure, including EOF with zero bytes consumed.
 *   - Embedded NUL is failure.
 *   - A CR not immediately followed by LF is failure.
 *   - If LF is not observed within max_len - 1 consumed bytes,
 *     the line is oversized/incomplete and fails.
 *   - No byte beyond the max_len - 1 input window is read in an
 *     attempt to complete the line.
 *
 * Buffer state on failure:
 *   - If line_buf != NULL and max_len >= 1, line_buf[0] is guaranteed
 *     to be '\0'.
 *
 * Stream state on failure:
 *   - The stream position is unspecified.
 *   - The transport must not be reused for continued parsing.
 *
 * Protocol layering:
 *   - Callers requiring strict CRLF framing must additionally verify:
 *
 *         ret - strlen(line_buf) == 2
 */
int HttpTransport_recv_line(HttpTransport* self, char* line_buf, int max_len);

void HttpTransport_close(HttpTransport* self);

#endif /* HTTP_TRANSPORT_H */