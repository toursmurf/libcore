// ============================================================================
// 🚨 TARGET OS: 64-bit Linux Only (32-bit not supported) 🚨
// ============================================================================
#include "ws_protocol.h"
#include "crypto.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

char* ws_compute_accept_key(const char* client_key) {
    if (!client_key) return NULL;
    // WebSocket client key는 Base64(16바이트) = 24자, 여유 포함 128자 초과 시 거부
    if (strlen(client_key) > 128) return NULL;
    const char* magic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    char combined[256];
    snprintf(combined, sizeof(combined), "%s%s", client_key, magic);

    uint8_t hash[20];
    Crypto_SHA1((const uint8_t*)combined, strlen(combined), hash);

    return Crypto_Base64Encode(hash, 20);
}

size_t ws_build_text_frame(const char* msg, uint8_t* out_buf, size_t max_len) {
    size_t msg_len = strlen(msg);
    size_t header_len = 2;

    // 길이에 따른 헤더 사이즈 결정
    if (msg_len <= 125) {
        header_len = 2;
    } else if (msg_len <= 65535) {
        header_len = 4; // 126 (16비트 확장)
    } else {
        header_len = 10; // 127 (64비트 확장)
    }

    if (header_len + msg_len > max_len) return 0; // 버퍼 초과 방지

    out_buf[0] = 0x81; // FIN(1) + Opcode(0x1 Text)

    // 길이에 따른 Payload Length 및 확장 길이 세팅 (Big Endian)
    if (msg_len <= 125) {
        out_buf[1] = (uint8_t)msg_len;
    } else if (msg_len <= 65535) {
        out_buf[1] = 126;
        out_buf[2] = (msg_len >> 8) & 0xFF;
        out_buf[3] = msg_len & 0xFF;
    } else {
        // 🚨 64비트 시프트 연산 (32비트 OS에서는 경고/오류 발생 가능)
        out_buf[1] = 127;
        out_buf[2] = (msg_len >> 56) & 0xFF;
        out_buf[3] = (msg_len >> 48) & 0xFF;
        out_buf[4] = (msg_len >> 40) & 0xFF;
        out_buf[5] = (msg_len >> 32) & 0xFF;
        out_buf[6] = (msg_len >> 24) & 0xFF;
        out_buf[7] = (msg_len >> 16) & 0xFF;
        out_buf[8] = (msg_len >> 8) & 0xFF;
        out_buf[9] = msg_len & 0xFF;
    }

    memcpy(out_buf + header_len, msg, msg_len);
    return header_len + msg_len;
}

ssize_t ws_decode_frame(const uint8_t* in_buf, size_t in_len, char* out_msg, size_t max_out) {
    if (in_len < 6) return -1;

    uint8_t opcode = in_buf[0] & 0x0F;
    if (opcode == 0x8) return -1; // Close 프레임 수신 시 -1 반환

    uint8_t masked = (in_buf[1] >> 7) & 1;
    if (!masked) return -1; // 클라이언트는 반드시 마스킹해야 함

    size_t payload_len = in_buf[1] & 0x7F;
    size_t header_len = 2;

    // 길이에 따른 확장 헤더 파싱 (Big Endian -> Host 변환)
    if (payload_len == 126) {
        if (in_len < 8) return -1; // Header(2) + ExtLen(2) + Mask(4)
        payload_len = (in_buf[2] << 8) | in_buf[3];
        header_len = 4;
    } else if (payload_len == 127) {
        if (in_len < 14) return -1; // Header(2) + ExtLen(8) + Mask(4)
        payload_len = ((size_t)in_buf[2] << 56) |
                      ((size_t)in_buf[3] << 48) |
                      ((size_t)in_buf[4] << 40) |
                      ((size_t)in_buf[5] << 32) |
                      ((size_t)in_buf[6] << 24) |
                      ((size_t)in_buf[7] << 16) |
                      ((size_t)in_buf[8] << 8) |
                      ((size_t)in_buf[9]);
        header_len = 10;
    }

    if (payload_len > max_out) return -1; // 버퍼 초과 방지
    if (in_len < header_len + 4 + payload_len) return -1; // 전체 데이터 대기

    uint8_t mask[4];
    memcpy(mask, in_buf + header_len, 4); // 마스킹 키 4바이트 추출

    size_t data_offset = header_len + 4;

    // XOR 연산으로 암호 해독!!
    for (size_t i = 0; i < payload_len; i++) {
        out_msg[i] = in_buf[data_offset + i] ^ mask[i % 4];
    }

    out_msg[payload_len] = '\0';
    return (ssize_t)payload_len;
}

ssize_t ws_decode_frame2(const uint8_t* in_buf, size_t in_len,
                         char* out_msg, size_t max_out,
                         size_t* consumed, int* is_ping) {
    if (consumed) *consumed = 0;
    if (is_ping) *is_ping = 0;
    if (!in_buf || !out_msg || in_len < 2) return 0;   /* 최소 헤더 대기 */

    uint8_t opcode = in_buf[0] & 0x0F;
    uint8_t masked = (in_buf[1] >> 7) & 1;
    if (!masked) return -2;                             /* 클라이언트 미마스킹 = RFC 위반 */

    size_t payload_len = in_buf[1] & 0x7F;
    size_t header_len = 2;

    if (payload_len == 126) {
        if (in_len < 4) return 0;
        payload_len = ((size_t)in_buf[2] << 8) | in_buf[3];
        header_len = 4;
    } else if (payload_len == 127) {
        if (in_len < 10) return 0;
        payload_len = ((size_t)in_buf[2] << 56) | ((size_t)in_buf[3] << 48) |
                      ((size_t)in_buf[4] << 40) | ((size_t)in_buf[5] << 32) |
                      ((size_t)in_buf[6] << 24) | ((size_t)in_buf[7] << 16) |
                      ((size_t)in_buf[8] << 8)  | ((size_t)in_buf[9]);
        header_len = 10;
    }

    if (payload_len + 1 > max_out) return -2;           /* 수용 불가 크기 */

    size_t frame_total = header_len + 4 + payload_len;
    if (in_len < frame_total) return 0;                 /* 프레임 미완성 */

    uint8_t mask[4];
    memcpy(mask, in_buf + header_len, 4);

    size_t data_offset = header_len + 4;
    for (size_t i = 0; i < payload_len; i++) {
        out_msg[i] = (char)(in_buf[data_offset + i] ^ mask[i % 4]);
    }
    out_msg[payload_len] = '\0';

    if (consumed) *consumed = frame_total;

    if (opcode == 0x8) return -1;                       /* Close */
    if (opcode == 0x9) { if (is_ping) *is_ping = 1; }   /* Ping -> Pong 회신 필요 */

    return (ssize_t)payload_len;
}