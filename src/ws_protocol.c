#include "ws_protocol.h"
#include "crypto.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

char* ws_compute_accept_key(const char* client_key) {
    if (!client_key) return NULL;
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
    if (msg_len <= 125) header_len = 2;
    else if (msg_len <= 65535) header_len = 4;
    else header_len = 10;
    if (header_len + msg_len > max_len) return 0;
    out_buf[0] = 0x81;
    if (msg_len <= 125) out_buf[1] = (uint8_t)msg_len;
    else if (msg_len <= 65535) {
        out_buf[1] = 126; out_buf[2] = (msg_len >> 8) & 0xFF; out_buf[3] = msg_len & 0xFF;
    } else {
        out_buf[1] = 127;
        out_buf[2] = (msg_len >> 56) & 0xFF; out_buf[3] = (msg_len >> 48) & 0xFF;
        out_buf[4] = (msg_len >> 40) & 0xFF; out_buf[5] = (msg_len >> 32) & 0xFF;
        out_buf[6] = (msg_len >> 24) & 0xFF; out_buf[7] = (msg_len >> 16) & 0xFF;
        out_buf[8] = (msg_len >> 8) & 0xFF;  out_buf[9] = msg_len & 0xFF;
    }
    memcpy(out_buf + header_len, msg, msg_len);
    return header_len + msg_len;
}

ssize_t ws_decode_frame(const uint8_t* in_buf, size_t in_len, char* out_msg, size_t max_out) {
    if (in_len < 6) return -1;
    uint8_t opcode = in_buf[0] & 0x0F;
    if (opcode == 0x8) return -1;
    uint8_t masked = (in_buf[1] >> 7) & 1;
    if (!masked) return -1;
    size_t payload_len = in_buf[1] & 0x7F;
    size_t header_len = 2;
    if (payload_len == 126) {
        if (in_len < 8) return -1;
        payload_len = (in_buf[2] << 8) | in_buf[3];
        header_len = 4;
    } else if (payload_len == 127) {
        if (in_len < 14) return -1;
        payload_len = ((size_t)in_buf[2] << 56) | ((size_t)in_buf[3] << 48) |
                      ((size_t)in_buf[4] << 40) | ((size_t)in_buf[5] << 32) |
                      ((size_t)in_buf[6] << 24) | ((size_t)in_buf[7] << 16) |
                      ((size_t)in_buf[8] << 8) | ((size_t)in_buf[9]);
        header_len = 10;
    }
    if (payload_len > max_out) return -1;
    if (in_len < header_len + 4 + payload_len) return -1;
    uint8_t mask[4];
    memcpy(mask, in_buf + header_len, 4);
    size_t data_offset = header_len + 4;
    for (size_t i = 0; i < payload_len; i++) out_msg[i] = in_buf[data_offset + i] ^ mask[i % 4];
    out_msg[payload_len] = '\0';
    return (ssize_t)payload_len;
}

ssize_t ws_decode_frame2(const uint8_t* in_buf, size_t in_len, char* out_msg, size_t max_out, size_t* consumed, int* is_ping) {
    if (consumed) *consumed = 0;
    if (is_ping) *is_ping = 0;
    if (!in_buf || !out_msg || in_len < 2) return 0;
    uint8_t opcode = in_buf[0] & 0x0F;
    uint8_t masked = (in_buf[1] >> 7) & 1;
    if (!masked) return -2;
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
    if (payload_len > MAX_WS_PAYLOAD_SIZE) return -2;
    if (payload_len + 1 > max_out) return -2;
    size_t frame_total = header_len + 4 + payload_len;
    if (in_len < frame_total) return 0;
    uint8_t mask[4];
    memcpy(mask, in_buf + header_len, 4);
    size_t data_offset = header_len + 4;
    for (size_t i = 0; i < payload_len; i++) out_msg[i] = (char)(in_buf[data_offset + i] ^ mask[i % 4]);
    out_msg[payload_len] = '\0';
    if (consumed) *consumed = frame_total;
    if (opcode == 0x8) return -1;
    if (opcode == 0x9) { if (is_ping) *is_ping = 1; }
    return (ssize_t)payload_len;
}

size_t ws_build_frame(uint8_t opcode, const uint8_t* msg, size_t msg_len, uint8_t* out_buf, size_t max_len) {
    if (!out_buf) return 0;
    if (msg_len > 0 && !msg) return 0;
    if (msg_len > MAX_WS_PAYLOAD_SIZE) return 0;
    if (opcode != WS_OPCODE_TEXT && opcode != WS_OPCODE_BINARY &&
        opcode != WS_OPCODE_CLOSE && opcode != WS_OPCODE_PING &&
        opcode != WS_OPCODE_PONG) { return 0; }
    if ((opcode & 0x08) && msg_len > 125) return 0;

    size_t header_len = 2;
    if (msg_len <= 125) header_len = 2;
    else if (msg_len <= 65535) header_len = 4;
    else header_len = 10;
    if (header_len > max_len || msg_len > max_len - header_len) return 0;

    out_buf[0] = 0x80 | (opcode & 0x0F);
    if (msg_len <= 125) out_buf[1] = (uint8_t)msg_len;
    else if (msg_len <= 65535) {
        out_buf[1] = 126; out_buf[2] = (msg_len >> 8) & 0xFF; out_buf[3] = msg_len & 0xFF;
    } else {
        out_buf[1] = 127;
        out_buf[2] = (msg_len >> 56) & 0xFF; out_buf[3] = (msg_len >> 48) & 0xFF;
        out_buf[4] = (msg_len >> 40) & 0xFF; out_buf[5] = (msg_len >> 32) & 0xFF;
        out_buf[6] = (msg_len >> 24) & 0xFF; out_buf[7] = (msg_len >> 16) & 0xFF;
        out_buf[8] = (msg_len >> 8) & 0xFF;  out_buf[9] = msg_len & 0xFF;
    }
    if (msg_len > 0) memcpy(out_buf + header_len, msg, msg_len);
    return header_len + msg_len;
}

int ws_decode_frame3(uint8_t* in_buf, size_t in_len, uint8_t** out_payload, size_t* out_payload_len, size_t* consumed, int* out_opcode) {
    if (consumed) *consumed = 0;
    if (out_payload) *out_payload = NULL;
    if (out_payload_len) *out_payload_len = 0;
    if (out_opcode) *out_opcode = 0;

    if (!in_buf) return WS_DECODE_ERROR;
    if (in_len < 2) return WS_DECODE_NEED_MORE;

    uint8_t fin = (in_buf[0] & 0x80) != 0;
    uint8_t rsv = in_buf[0] & 0x70;
    uint8_t opcode = in_buf[0] & 0x0F;
    uint8_t masked = (in_buf[1] & 0x80) != 0;
    size_t payload_len = in_buf[1] & 0x7F;

    if (rsv != 0) return WS_DECODE_ERROR;
    if (!masked) return WS_DECODE_ERROR;

    switch (opcode) {
        case WS_OPCODE_TEXT:
        case WS_OPCODE_BINARY:
            if (!fin) return WS_DECODE_ERROR;
            break;
        case WS_OPCODE_CLOSE:
        case WS_OPCODE_PING:
        case WS_OPCODE_PONG:
            if (!fin) return WS_DECODE_ERROR;
            if (payload_len > 125) return WS_DECODE_ERROR;
            if (opcode == WS_OPCODE_CLOSE && payload_len == 1) return WS_DECODE_ERROR;
            break;
        case WS_OPCODE_CONTINUATION:
        default:
            return WS_DECODE_ERROR;
    }

    size_t header_len = 2;
    if (payload_len == 126) {
        if (in_len < 4) return WS_DECODE_NEED_MORE;
        payload_len = ((size_t)in_buf[2] << 8) | in_buf[3];
        header_len = 4;
    } else if (payload_len == 127) {
        if (in_len < 10) return WS_DECODE_NEED_MORE;
        payload_len = ((size_t)in_buf[2] << 56) | ((size_t)in_buf[3] << 48) |
                      ((size_t)in_buf[4] << 40) | ((size_t)in_buf[5] << 32) |
                      ((size_t)in_buf[6] << 24) | ((size_t)in_buf[7] << 16) |
                      ((size_t)in_buf[8] << 8)  | ((size_t)in_buf[9]);
        header_len = 10;
    }

    if (payload_len > MAX_WS_PAYLOAD_SIZE) return WS_DECODE_ERROR;
    size_t frame_total = header_len + 4 + payload_len;
    if (in_len < frame_total) return WS_DECODE_NEED_MORE;

    uint8_t* mask = in_buf + header_len;
    uint8_t* payload = in_buf + header_len + 4;

    for (size_t i = 0; i < payload_len; i++) {
        payload[i] ^= mask[i % 4];
    }

    if (out_payload) *out_payload = payload;
    if (out_payload_len) *out_payload_len = payload_len;
    if (consumed) *consumed = frame_total;
    if (out_opcode) *out_opcode = opcode;
    return WS_DECODE_OK;
}