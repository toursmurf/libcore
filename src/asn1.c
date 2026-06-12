/* ASN.1 파서 구현) */
#include "asn1.h"
#include <string.h>

uint8_t* asn1_encode_length(uint8_t* buf, size_t length) {
    if (length < 128) {
        *buf++ = (uint8_t)length;
    } else {
        uint8_t bytes = 0;
        size_t temp = length;
        while (temp > 0) {
            bytes++;
            temp >>= 8;
        }
        *buf++ = 0x80 | bytes;
        for (int i = bytes - 1; i >= 0; i--) {
            *buf++ = (uint8_t)(length >> (i * 8));
        }
    }
    return buf;
}

uint8_t* asn1_encode_integer(uint8_t* buf, int32_t value) {
    *buf++ = ASN1_INTEGER;
    *buf++ = 1;
    *buf++ = (uint8_t)(value & 0xFF);
    return buf;
}

uint8_t* asn1_encode_string(uint8_t* buf, const char* str, size_t len) {
    *buf++ = ASN1_OCTET_STRING;
    buf = asn1_encode_length(buf, len);
    memcpy(buf, str, len);
    return buf + len;
}

const uint8_t* asn1_decode_length(const uint8_t* buf, size_t* out_length) {
    if (*buf < 128) {
        *out_length = *buf++;
    } else {
        uint8_t bytes = *buf++ & 0x7F;
        *out_length = 0;
        for (int i = 0; i < bytes; i++) {
            *out_length = (*out_length << 8) | *buf++;
        }
    }
    return buf;
}

const uint8_t* asn1_decode_integer(const uint8_t* buf, int32_t* out_value) {
    if (*buf++ != ASN1_INTEGER) return NULL;
    size_t len;
    buf = asn1_decode_length(buf, &len);
    *out_value = 0;
    for (size_t i = 0; i < len; i++) {
        *out_value = (*out_value << 8) | *buf++;
    }
    return buf;
}