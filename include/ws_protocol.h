#ifndef WS_PROTOCOL_H
#define WS_PROTOCOL_H

#define MAX_WS_PAYLOAD_SIZE (1024 * 1024)

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>

#define WS_OPCODE_CONTINUATION 0x0
#define WS_OPCODE_TEXT         0x1
#define WS_OPCODE_BINARY       0x2
#define WS_OPCODE_CLOSE        0x8
#define WS_OPCODE_PING         0x9
#define WS_OPCODE_PONG         0xA

#define WS_DECODE_OK           1
#define WS_DECODE_NEED_MORE    0
#define WS_DECODE_ERROR       -1

char* ws_compute_accept_key(const char* client_key);

size_t ws_build_text_frame(const char* msg, uint8_t* out_buf, size_t max_len);
ssize_t ws_decode_frame(const uint8_t* in_buf, size_t in_len, char* out_msg, size_t max_out);
ssize_t ws_decode_frame2(const uint8_t* in_buf, size_t in_len, char* out_msg, size_t max_out, size_t* consumed, int* is_ping);

size_t ws_build_frame(uint8_t opcode, const uint8_t* msg, size_t msg_len, uint8_t* out_buf, size_t max_len);
int ws_decode_frame3(uint8_t* in_buf, size_t in_len, uint8_t** out_payload, size_t* out_payload_len, size_t* consumed, int* out_opcode);

#endif // WS_PROTOCOL_H