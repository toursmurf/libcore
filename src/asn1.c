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
    if (!ASN_FITS(ptr, 1, end)) return NULL;
    uint8_t b = *ptr++;

    if (b < 128) {
        *out_len = b;
        return ptr;
    }

    if (b == 0x80) return NULL;

    uint8_t num = b & 0x7F;
    if (num == 0 || num > sizeof(size_t) || !ASN_FITS(ptr, num, end)) return NULL;
    if (num > 1 && *ptr == 0x00) return NULL;

    *out_len = 0;
    while (num--) {
        if (*out_len > (SIZE_MAX >> 8)) return NULL;
        *out_len = (*out_len << 8) | *ptr++;
    }

    return (*out_len < 128) ? NULL : ptr;
}

const uint8_t* asn1_decode_integer(const uint8_t* ptr, const uint8_t* end, int32_t* out_val) {
    if (!ASN_FITS(ptr, 1, end) || *ptr != ASN1_INTEGER) return NULL;
    ptr++;

    size_t len;
    ptr = asn1_decode_length(ptr, end, &len);
    if (!ptr || len == 0 || len > 4 || !ASN_FITS(ptr, len, end)) return NULL;

    if (len > 1) {
        if ((ptr[0] == 0x00 && (ptr[1] & 0x80) == 0) ||
            (ptr[0] == 0xFF && (ptr[1] & 0x80) != 0)) {
            return NULL;
        }
    }

    /* 🛡️ C UB 방어: Negative Left-Shift 금지 (int64_t 곱셈 기반 파싱 적용) */
    int64_t v = (ptr[0] & 0x80) ? -1 : 0;
    for (size_t i = 0; i < len; i++) {
        v = v * 256 + ptr[i];
    }

    if (v < INT32_MIN || v > INT32_MAX) return NULL;

    *out_val = (int32_t)v;
    return ptr + len;
}

const uint8_t* asn1_decode_unsigned(const uint8_t* ptr, const uint8_t* end, uint32_t* out_val) {
    if (!ASN_FITS(ptr, 1, end) || (*ptr != ASN1_COUNTER32 && *ptr != ASN1_GAUGE32 && *ptr != ASN1_TIMETICKS)) return NULL;
    ptr++;

    size_t len;
    ptr = asn1_decode_length(ptr, end, &len);
    if (!ptr || len == 0 || len > 5 || !ASN_FITS(ptr, len, end)) return NULL;

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
    if (!ASN_FITS(ptr, 1, end) || *ptr != ASN1_COUNTER64) return NULL;
    ptr++;

    size_t len;
    ptr = asn1_decode_length(ptr, end, &len);
    if (!ptr || len == 0 || len > 9 || !ASN_FITS(ptr, len, end)) return NULL;

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
    if (!ASN_FITS(ptr, 1, end) || !out_ip || max_len == 0) return NULL;
    if (*ptr != ASN1_IPADDRESS) return NULL;
    ptr++;

    size_t len;
    ptr = asn1_decode_length(ptr, end, &len);
    if (!ptr || len != 4 || !ASN_FITS(ptr, len, end)) return NULL;

    snprintf(out_ip, max_len, "%u.%u.%u.%u", ptr[0], ptr[1], ptr[2], ptr[3]);
    return ptr + len;
}

const uint8_t* asn1_decode_string(const uint8_t* ptr, const uint8_t* end, char* out_str, size_t max_len) {
    if (!ASN_FITS(ptr, 1, end) || !out_str || max_len == 0) return NULL;
    if (*ptr != ASN1_OCTET_STRING) return NULL;
    ptr++;

    size_t len;
    ptr = asn1_decode_length(ptr, end, &len);
    if (!ptr || !ASN_FITS(ptr, len, end)) return NULL;

    size_t copy_len = (len < max_len - 1) ? len : max_len - 1;
    memcpy(out_str, ptr, copy_len);
    out_str[copy_len] = '\0';

    return ptr + len;
}

const uint8_t* asn1_decode_oid(const uint8_t* ptr, const uint8_t* end, uint32_t* oids, size_t oid_cap, size_t* count) {
    if (!ptr || !end || !oids || !count || oid_cap < 2) return NULL;
    if (!ASN_FITS(ptr, 1, end) || *ptr != ASN1_OBJECT_ID) return NULL;
    ptr++;

    size_t len;
    ptr = asn1_decode_length(ptr, end, &len);
    if (!ptr || len == 0 || !ASN_FITS(ptr, len, end)) return NULL;

    const uint8_t* obj_end = ptr + len;
    *count = 0;

    uint64_t first_val = 0;
    while (true) {
        if (ptr >= obj_end) return NULL;
        if (first_val > (UINT64_MAX >> 7)) return NULL;

        uint8_t b = *ptr++;
        first_val = (first_val << 7) | (b & 0x7F);

        if (!(b & 0x80)) break;
    }

    if (*count <= oid_cap - 2) {
        uint32_t x, y;
        if (first_val < 40) {
            x = 0;
            y = (uint32_t)first_val;
        } else if (first_val < 80) {
            x = 1;
            y = (uint32_t)(first_val - 40);
        } else {
            uint64_t yy = first_val - 80;
            if (yy > UINT32_MAX) return NULL;
            x = 2;
            y = (uint32_t)yy;
        }
        oids[(*count)++] = x;
        oids[(*count)++] = y;
    }

    uint32_t val = 0;
    int oid_bytes = 0;

    while (ptr < obj_end && *count < oid_cap) {
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

    if (ptr != obj_end) return NULL;
    if (oid_bytes != 0) return NULL;

    return obj_end;
}

/* =====================================================================
 * [ENCODER API]
 * ===================================================================== */

bool asn1_encode_length(uint8_t** p, const uint8_t* end, size_t length) {
    if (!p || !*p || !end) return false;
    uint8_t* buf = *p;

    if (length < 128) {
        if (!ASN_FITS(buf, 1, end)) return false;
        *buf++ = (uint8_t)length;
    } else {
        uint8_t len_bytes[sizeof(size_t)];
        int num_bytes = 0;
        size_t temp = length;

        while (temp > 0) {
            len_bytes[num_bytes++] = temp & 0xFF;
            temp >>= 8;
        }

        if (!ASN_FITS(buf, 1 + num_bytes, end)) return false;
        *buf++ = 0x80 | (uint8_t)num_bytes;
        for (int i = num_bytes - 1; i >= 0; i--) *buf++ = len_bytes[i];
    }
    *p = buf;
    return true;
}

bool asn1_encode_integer(uint8_t** p, const uint8_t* end, int32_t value) {
    if (!p || !*p || !end) return false;
    if (!ASN_FITS(*p, 1, end)) return false;
    *(*p)++ = ASN1_INTEGER;

    uint8_t bytes[4];
    bytes[0] = (value >> 24) & 0xFF;
    bytes[1] = (value >> 16) & 0xFF;
    bytes[2] = (value >> 8) & 0xFF;
    bytes[3] = value & 0xFF;

    int start = 0;
    while (start < 3) {
        if (bytes[start] == 0x00 && (bytes[start + 1] & 0x80) == 0) start++;
        else if (bytes[start] == 0xFF && (bytes[start + 1] & 0x80) != 0) start++;
        else break;
    }

    int size = 4 - start;
    if (!ASN_FITS(*p, 1 + size, end)) return false;
    *(*p)++ = size;

    for (int i = start; i < 4; i++) *(*p)++ = bytes[i];
    return true;
}

bool asn1_encode_unsigned(uint8_t** p, const uint8_t* end, uint32_t value, uint8_t tag) {
    if (!p || !*p || !end) return false;
    if (!ASN_FITS(*p, 1, end)) return false;
    *(*p)++ = tag;

    uint8_t bytes[5];
    bytes[0] = 0x00;
    bytes[1] = (value >> 24) & 0xFF;
    bytes[2] = (value >> 16) & 0xFF;
    bytes[3] = (value >> 8) & 0xFF;
    bytes[4] = value & 0xFF;

    int start = 0;
    while (start < 4) {
        if (bytes[start] == 0x00 && (bytes[start + 1] & 0x80) == 0) start++;
        else break;
    }

    int size = 5 - start;
    if (!ASN_FITS(*p, 1 + size, end)) return false;
    *(*p)++ = size;

    for (int i = start; i < 5; i++) *(*p)++ = bytes[i];
    return true;
}

bool asn1_encode_ip(uint8_t** p, const uint8_t* end, const uint8_t ip[4]) {
    if (!p || !*p || !end) return false;
    if (!ASN_FITS(*p, 6, end)) return false;
    *(*p)++ = ASN1_IPADDRESS;
    *(*p)++ = 4;
    memcpy(*p, ip, 4);
    *p += 4;
    return true;
}

bool asn1_encode_string(uint8_t** p, const uint8_t* end, const char* str, size_t len) {
    if (!p || !*p || !end) return false;
    if (!ASN_FITS(*p, 1, end)) return false;
    *(*p)++ = ASN1_OCTET_STRING;

    if (!asn1_encode_length(p, end, len)) return false;
    if (str && len > 0) {
        if (!ASN_FITS(*p, len, end)) return false;
        memcpy(*p, str, len);
        *p += len;
    }
    return true;
}

bool asn1_encode_oid(uint8_t** p, const uint8_t* end, const char* oid_str) {
    if (!p || !*p || !end || !oid_str) return false;

    uint32_t oids[128];
    size_t count = 0;
    const char* s = oid_str;

    while (*s && count < 128) {
        if (*s < '0' || *s > '9') return false;

        char* endptr;
        errno = 0;
        unsigned long v = strtoul(s, &endptr, 10);

        if (s == endptr || errno == ERANGE || v > UINT32_MAX) return false;

        oids[count++] = (uint32_t)v;
        s = endptr;

        /* 🛡️ Trailing-dot("1.3.") 방어 적용 */
        if (*s == '.') {
            s++;
            if (*s == '\0') return false;
        } else if (*s != '\0') {
            return false;
        }
    }

    if (*s != '\0') return false;
    if (count < 2) return false;
    if (oids[0] > 2) return false;
    if (oids[0] < 2 && oids[1] >= 40) return false;

    if (!ASN_FITS(*p, 1, end)) return false;
    *(*p)++ = ASN1_OBJECT_ID;

    uint8_t temp[512];
    uint8_t* t_ptr = temp;
    uint8_t* t_end = temp + sizeof(temp);

    uint64_t first_val = (uint64_t)oids[0] * 40u + (uint64_t)oids[1];

    if (first_val < 128) {
        *t_ptr++ = (uint8_t)first_val;
    } else {
        uint8_t v_bytes[10];
        int v_idx = 0;
        v_bytes[v_idx++] = first_val & 0x7F;
        first_val >>= 7;
        while (first_val > 0) {
            v_bytes[v_idx++] = (first_val & 0x7F) | 0x80;
            first_val >>= 7;
        }
        for (int j = v_idx - 1; j >= 0; j--) {
            *t_ptr++ = v_bytes[j];
        }
    }

    for (size_t i = 2; i < count; i++) {
        if (t_end - t_ptr < 5) return false;

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
    if (!asn1_encode_length(p, end, enc_len)) return false;
    if (!ASN_FITS(*p, enc_len, end)) return false;

    memcpy(*p, temp, enc_len);
    *p += enc_len;

    return true;
}