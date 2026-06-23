#include "asn1.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

const uint8_t* asn1_decode_length(const uint8_t* buf, size_t* out_length) {
    if (!buf || !out_length) return NULL;
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
    if (!buf || *buf != ASN1_INTEGER) return NULL;
    buf++;
    size_t len;
    buf = asn1_decode_length(buf, &len);
    if (!buf) return NULL;
    *out_value = 0;
    for (size_t i = 0; i < len; i++) {
        *out_value = (*out_value << 8) | *buf++;
    }
    return buf;
}

const uint8_t* asn1_decode_unsigned(const uint8_t* buf, uint32_t* out_value) {
    if (!buf || !out_value) return NULL;
    uint8_t tag = *buf++;
    if (tag != ASN1_COUNTER32 && tag != ASN1_GAUGE32 && tag != ASN1_TIMETICKS) return NULL;
    size_t len;
    buf = asn1_decode_length(buf, &len);
    if (!buf) return NULL;
    *out_value = 0;
    for (size_t i = 0; i < len; i++) {
        *out_value = (*out_value << 8) | *buf++;
    }
    return buf;
}

const uint8_t* asn1_decode_ip(const uint8_t* buf, char* out_ip, size_t max_len) {
    if (!buf || *buf != ASN1_IPADDRESS || !out_ip) return NULL;
    buf++;
    size_t len;
    buf = asn1_decode_length(buf, &len);
    if (!buf || len != 4) return NULL;
    if (max_len < 16) return NULL;
    snprintf(out_ip, max_len, "%u.%u.%u.%u", buf[0], buf[1], buf[2], buf[3]);
    return buf + 4;
}

const uint8_t* asn1_decode_string(const uint8_t* buf, char* out_str, size_t max_len) {
    if (!buf || *buf != ASN1_OCTET_STRING || !out_str) return NULL;
    buf++;
    size_t len;
    buf = asn1_decode_length(buf, &len);
    if (!buf) return NULL;
    size_t copy_len = (len < max_len - 1) ? len : max_len - 1;
    memcpy(out_str, buf, copy_len);
    out_str[copy_len] = '\0';
    return buf + len;
}

const uint8_t* asn1_decode_oid(const uint8_t* buf, uint32_t* out_oids, size_t* out_count) {
    if (!buf || *buf != ASN1_OBJECT_ID) return NULL;
    buf++;
    size_t len;
    buf = asn1_decode_length(buf, &len);
    if (!buf) return NULL;
    const uint8_t* end = buf + len;
    out_oids[0] = *buf / 40;
    out_oids[1] = *buf % 40;
    *out_count = 2;
    buf++;
    while (buf < end) {
        uint32_t subid = 0;
        uint8_t b;
        do {
            b = *buf++;
            subid = (subid << 7) | (b & 0x7F);
        } while (b & 0x80);
        out_oids[(*out_count)++] = subid;
    }
    return end;
}

uint8_t* asn1_encode_length(uint8_t* buf, size_t length) {
    if (length < 128) {
        *buf++ = (uint8_t)length;
    } else {
        uint8_t bytes = 0;
        size_t temp = length;
        while (temp > 0) { bytes++; temp >>= 8; }
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

uint8_t* asn1_encode_unsigned(uint8_t* buf, uint32_t value, uint8_t tag) {
    *buf++ = tag;
    uint8_t temp[5];
    int len = 0;
    if (value == 0) {
        temp[len++] = 0;
    } else {
        for (int i = 24; i >= 0; i -= 8) {
            uint8_t b = (value >> i) & 0xFF;
            if (len > 0 || b > 0 || i == 0) temp[len++] = b;
        }
    }
    buf = asn1_encode_length(buf, len);
    memcpy(buf, temp, len);
    return buf + len;
}

uint8_t* asn1_encode_ip(uint8_t* buf, const uint8_t ip[4]) {
    *buf++ = ASN1_IPADDRESS;
    *buf++ = 4;
    memcpy(buf, ip, 4);
    return buf + 4;
}

uint8_t* asn1_encode_string(uint8_t* buf, const char* str, size_t len) {
    *buf++ = ASN1_OCTET_STRING;
    buf = asn1_encode_length(buf, len);
    if (len > 0 && str) {
        memcpy(buf, str, len);
        buf += len;
    }
    return buf;
}

uint8_t* asn1_encode_oid(uint8_t* buf, const char* oid_str) {
    *buf++ = ASN1_OBJECT_ID;
    if (!oid_str) {
        *buf++ = 0x00;
        return buf;
    }
    uint32_t oids[128];
    int count = 0;
    const char* p = oid_str;
    while (*p && count < 128) {
        char* endptr;
        uint32_t val = (uint32_t)strtoul(p, &endptr, 10);
        if (p == endptr) break;
        oids[count++] = val;
        p = endptr;
        if (*p == '.') p++;
    }
    if (count < 2) {
        *buf++ = 0x00;
        return buf;
    }
    uint8_t temp[128];
    int t_len = 0;
    temp[t_len++] = (uint8_t)((oids[0] * 40) + oids[1]);
    for (int i = 2; i < count; i++) {
        uint32_t val = oids[i];
        uint8_t bytes[5];
        int b_idx = 0;
        bytes[b_idx++] = val & 0x7F;
        val >>= 7;
        while (val > 0) {
            bytes[b_idx++] = (val & 0x7F) | 0x80;
            val >>= 7;
        }
        for (int k = b_idx - 1; k >= 0; k--) {
            temp[t_len++] = bytes[k];
        }
    }
    buf = asn1_encode_length(buf, (size_t)t_len);
    memcpy(buf, temp, (size_t)t_len);
    return buf + t_len;
}