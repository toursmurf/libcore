// ============================================================================
// 🚨 TARGET OS: 64-bit Linux Only (32-bit not supported) 🚨
// ============================================================================
#ifndef WS_PROTOCOL_H
#define WS_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

// P2 핸드셰이크용 키 생성기
char* ws_compute_accept_key(const char* client_key);

// P3 서버 -> 브라우저 (무제한 Payload 지원, 마스킹 없음)
size_t ws_build_text_frame(const char* msg, uint8_t* out_buf, size_t max_len);

// P3 브라우저 -> 서버 (무제한 Payload 해독, 마스킹 해제)
ssize_t ws_decode_frame(const uint8_t* in_buf, size_t in_len, char* out_msg, size_t max_out);

#endif // WS_PROTOCOL_H