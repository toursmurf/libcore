#include "asn1.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <errno.h>

/* =====================================================================
 * [DECODER API]
 * ===================================================================== */

const uint8_t* asn1_decode_length(const uint8_t* ptr, const uint8_t* end, size_t* out_len) {
    if (ptr >= end) return NULL;
    uint8_t b = *ptr++;

    if (b < 128) {
        *out_len = b;
        return ptr;
    }

    if (b == 0x80) return NULL;

    uint8_t num = b & 0x7F;
    if (num == 0 || num > sizeof(size_t) || ptr + num > end) return NULL;

    /* 🚨 ③ 의도 명확화: 다중 바이트 길이에서 비최소 표현(Leading Zero) 방어 */
    if (num > 1 && *ptr == 0x00) return NULL;

    *out_len = 0;
    while (num--) {
        if (*out_len > (SIZE_MAX >> 8)) return NULL;
        *out_len = (*out_len << 8) | *ptr++;
    }

    return (*out_len < 128) ? NULL : ptr;
}

const uint8_t* asn1_decode_integer(const uint8_t* ptr, const uint8_t* end, int32_t* out_val) {
    if (ptr >= end || *ptr != ASN1_INTEGER) return NULL;
    ptr++;

    size_t len;
    ptr = asn1_decode_length(ptr, end, &len);
    if (!ptr || len == 0 || len > 4 || ptr + len > end) return NULL;

    if (len > 1) {
        if ((ptr[0] == 0x00 && (ptr[1] & 0x80) == 0) ||
            (ptr[0] == 0xFF && (ptr[1] & 0x80) != 0)) {
            return NULL;
        }
    }

    *out_val = (ptr[0] & 0x80) ? -1 : 0;
    while (len--) *out_val = (*out_val << 8) | *ptr++;

    return ptr;
}

const uint8_t* asn1_decode_unsigned(const uint8_t* ptr, const uint8_t* end, uint32_t* out_val) {
    if (ptr >= end || (*ptr != ASN1_COUNTER32 && *ptr != ASN1_GAUGE32 && *ptr != ASN1_TIMETICKS)) return NULL;
    ptr++;

    size_t len;
    ptr = asn1_decode_length(ptr, end, &len);
    if (!ptr || len == 0 || len > 5 || ptr + len > end) return NULL;

    if (len > 1 && ptr[0] == 0x00) {
        if ((ptr[1] & 0x80) == 0) return NULL;
        ptr++;
        len--;
    } else if (len == 5) {
        return NULL;
    }

    *out_val = 0;
    while (len--) *out_val = (*out_val << 8) | *ptr++;

    return ptr;
}

const uint8_t* asn1_decode_unsigned64(const uint8_t* ptr, const uint8_t* end, uint64_t* out_val) {
    if (ptr >= end || *ptr != ASN1_COUNTER64) return NULL;
    ptr++;

    size_t len;
    ptr = asn1_decode_length(ptr, end, &len);
    if (!ptr || len == 0 || len > 9 || ptr + len > end) return NULL;

    if (len > 1 && ptr[0] == 0x00) {
        if ((ptr[1] & 0x80) == 0) return NULL;
        ptr++;
        len--;
    } else if (len == 9) {
        return NULL;
    }

    *out_val = 0;
    while (len--) *out_val = (*out_val << 8) | *ptr++;

    return ptr;
}

const uint8_t* asn1_decode_ip(const uint8_t* ptr, const uint8_t* end, char* out_ip, size_t max_len) {
    if (ptr >= end || !out_ip || max_len == 0) return NULL;
    if (*ptr != ASN1_IPADDRESS) return NULL;
    ptr++;

    size_t len;
    ptr = asn1_decode_length(ptr, end, &len);
    if (!ptr || len != 4 || ptr + len > end) return NULL;

    snprintf(out_ip, max_len, "%u.%u.%u.%u", ptr[0], ptr[1], ptr[2], ptr[3]);
    return ptr + len;
}

const uint8_t* asn1_decode_string(const uint8_t* ptr, const uint8_t* end, char* out_str, size_t max_len) {
    if (ptr >= end || !out_str || max_len == 0) return NULL;
    if (*ptr != ASN1_OCTET_STRING) return NULL;
    ptr++;

    size_t len;
    ptr = asn1_decode_length(ptr, end, &len);
    if (!ptr || ptr + len > end) return NULL;

    size_t copy_len = (len < max_len - 1) ? len : max_len - 1;
    memcpy(out_str, ptr, copy_len);
    out_str[copy_len] = '\0';

    return ptr + len;
}

const uint8_t* asn1_decode_oid(const uint8_t* ptr, const uint8_t* end, uint32_t* oids, size_t* count) {
    if (ptr >= end || *ptr != ASN1_OBJECT_ID) return NULL;
    ptr++;

    size_t len;
    ptr = asn1_decode_length(ptr, end, &len);
    if (!ptr || len == 0 || ptr + len > end) return NULL;

    const uint8_t* obj_end = ptr + len;
    *count = 0;

    /* 🚨 ① 2개 동시 저장 대비 안전장치 (126 이하일 때만 허용) */
    if (ptr < obj_end && *count <= 126) {
        uint8_t first = *ptr++;
        uint32_t x = first / 40;
        if (x > 2) x = 2;
        uint32_t y = first - (x * 40);
        oids[(*count)++] = x;
        oids[(*count)++] = y;
    }

    uint32_t val = 0;
    int oid_bytes = 0;

    while (ptr < obj_end && *count < 128) {
        oid_bytes++;
        if (oid_bytes > 5) return NULL;
        if (val > (UINT32_MAX >> 7)) return NULL;

        val = (val << 7) | (*ptr & 0x7F);

        if ((*ptr & 0x80) == 0) {
            oids[(*count)++] = val;
            val = 0;
            oid_bytes = 0;
        }
        ptr++;
    }

    if (oid_bytes != 0) return NULL;

    return obj_end;
}

/* =====================================================================
 * [ENCODER API]
 * ===================================================================== */

uint8_t* asn1_encode_length(uint8_t* buf, size_t length) {
    if (!buf) return NULL;

    if (length < 128) {
        *buf++ = (uint8_t)length;
    } else {
        uint8_t len_bytes[sizeof(size_t)];
        int num_bytes = 0;
        size_t temp = length;

        while (temp > 0) {
            len_bytes[num_bytes++] = temp & 0xFF;
            temp >>= 8;
        }

        *buf++ = 0x80 | (uint8_t)num_bytes;
        for (int i = num_bytes - 1; i >= 0; i--) {
            *buf++ = len_bytes[i];
        }
    }
    return buf;
}

uint8_t* asn1_encode_integer(uint8_t* buf, int32_t value) {
    if (!buf) return NULL;
    *buf++ = ASN1_INTEGER;

    uint8_t bytes[4];
    bytes[0] = (value >> 24) & 0xFF;
    bytes[1] = (value >> 16) & 0xFF;
    bytes[2] = (value >> 8) & 0xFF;
    bytes[3] = value & 0xFF;

    int start = 0;
    while (start < 3) {
        if (bytes[start] == 0x00 && (bytes[start + 1] & 0x80) == 0) {
            start++;
        } else if (bytes[start] == 0xFF && (bytes[start + 1] & 0x80) != 0) {
            start++;
        } else {
            break;
        }
    }

    int size = 4 - start;
    *buf++ = size;

    for (int i = start; i < 4; i++) {
        *buf++ = bytes[i];
    }

    return buf;
}

uint8_t* asn1_encode_unsigned(uint8_t* buf, uint32_t value, uint8_t tag) {
    if (!buf) return NULL;
    *buf++ = tag;

    uint8_t bytes[5];
    bytes[0] = 0x00;
    bytes[1] = (value >> 24) & 0xFF;
    bytes[2] = (value >> 16) & 0xFF;
    bytes[3] = (value >> 8) & 0xFF;
    bytes[4] = value & 0xFF;

    int start = 0;
    while (start < 4) {
        if (bytes[start] == 0x00 && (bytes[start + 1] & 0x80) == 0) {
            start++;
        } else {
            break;
        }
    }

    int size = 5 - start;
    *buf++ = size;

    for (int i = start; i < 5; i++) {
        *buf++ = bytes[i];
    }

    return buf;
}

uint8_t* asn1_encode_ip(uint8_t* buf, const uint8_t ip[4]) {
    if (!buf) return NULL;
    *buf++ = ASN1_IPADDRESS;
    *buf++ = 4;
    memcpy(buf, ip, 4);
    return buf + 4;
}

uint8_t* asn1_encode_string(uint8_t* buf, const char* str, size_t len) {
    if (!buf) return NULL;
    *buf++ = ASN1_OCTET_STRING;
    buf = asn1_encode_length(buf, len);
    if (str && len > 0) {
        memcpy(buf, str, len);
        buf += len;
    }
    return buf;
}

uint8_t* asn1_encode_oid(uint8_t* buf, const char* oid_str) {
    if (!buf || !oid_str) return NULL;

    uint32_t oids[128];
    size_t count = 0;
    const char* p = oid_str;

    while (*p && count < 128) {
        char* endptr;
        errno = 0;
        oids[count++] = strtoul(p, &endptr, 10);

        if (p == endptr || errno == ERANGE) return NULL;
        p = endptr;

        if (*p == '.') p++;
        else if (*p != '\0') return NULL;
    }

    if (count < 2) return NULL;
    if (oids[0] > 2) return NULL;
    if (oids[0] < 2 && oids[1] >= 40) return NULL;

    *buf++ = ASN1_OBJECT_ID;

    uint8_t temp[512];
    uint8_t* t_ptr = temp;
    /* 🚨 ② t_end 포인터 미리 계산하여 가독성 및 속도 확보 */
    uint8_t* t_end = temp + sizeof(temp);

    *t_ptr++ = (oids[0] * 40) + oids[1];

    for (size_t i = 2; i < count; i++) {
        if (t_end - t_ptr < 5) return NULL;

        uint32_t val = oids[i];
        if (val < 128) {
            *t_ptr++ = val;
        } else {
            uint8_t v_bytes[5];
            int v_idx = 0;
            v_bytes[v_idx++] = val & 0x7F;
            val >>= 7;
            while (val > 0) {
                v_bytes[v_idx++] = (val & 0x7F) | 0x80;
                val >>= 7;
            }
            for (int j = v_idx - 1; j >= 0; j--) {
                *t_ptr++ = v_bytes[j];
            }
        }
    }

    size_t enc_len = t_ptr - temp;
    buf = asn1_encode_length(buf, enc_len);
    memcpy(buf, temp, enc_len);
    return buf + enc_len;
}