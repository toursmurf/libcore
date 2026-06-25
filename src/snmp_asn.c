#include "snmp_asn.h"
#include <stdio.h>
#include <string.h>

static void SnmpVarBind_finalize(Object* obj) {
    (void)obj; // 🚨 -Wunused-parameter 경고 해결!
}

static Class SnmpVarBind_Class = {
    .name     = "SnmpVarBind",
    .size     = sizeof(SnmpVarBind),
    .finalize = SnmpVarBind_finalize
};

* 🚀 1. 태그 번호를 사람이 읽기 쉬운 문자열로 변환해주는 메서드 */
static const char* varbind_get_type_name_impl(SnmpVarBind* self) {
    if (!self) {
        return "Unknown";
    }

    switch (self->tag) {
        case 0x02: return "INTEGER";
        case 0x04: return "OCTET STRING";
        case 0x05: return "NULL";
        case 0x06: return "OBJECT IDENTIFIER";
        case 0x40: return "IpAddress";
        case 0x41: return "Counter32";
        case 0x42: return "Gauge32";
        case 0x43: return "TimeTicks";
        case 0x44: return "Opaque";
        case 0x46: return "Counter64";
        case 0x80: return "NoSuchObject";
        case 0x81: return "NoSuchInstance";
        case 0x82: return "EndOfMibView";
        default:   return "Unknown";
    }
}

/* 🚀 2. 문자열로 저장된 값을 안전하게 int 로 캐스팅하는 메서드 */
static int varbind_as_int_impl(SnmpVarBind* self) {
    if (!self) {
        return 0;
    }

    // 정수형이나 카운터 타입일 때만 변환 (아니면 0 반환)
    if (self->tag == 0x02 || self->tag == 0x41 || self->tag == 0x42 || self->tag == 0x43) {
        return atoi(self->value_str);
    }

    return 0;
}

/* 🚀 3. Counter64 같은 큰 숫자를 안전하게 long long 으로 캐스팅하는 메서드 */
static long long varbind_as_long_impl(SnmpVarBind* self) {
    if (!self) {
        return 0;
    }

    if (self->tag == 0x02 || self->tag == 0x41 || self->tag == 0x42 || self->tag == 0x43 || self->tag == 0x46) {
        return atoll(self->value_str);
    }

    return 0;
}

// 🚨 [추가] OCTET STRING이 사람이 읽을 수 있는 문자인지 판별하는 함수
static bool is_printable_string(const uint8_t* data, size_t len) {
    if (!data || len == 0) {
        return false;
    }

    for (size_t i = 0; i < len; i++) {
        if (data[i] < 32 || data[i] > 126) {
            if (data[i] != '\r' && data[i] != '\n' && data[i] != '\t') {
                return false;
            }
        }
    }

    return true;
}

// 🚨 [핵심] ASN.1 태그 기반 밸류 포맷팅 함수
void snmp_asn_format_value(uint8_t tag, const uint8_t* val_data, size_t val_len, char* out_buf, size_t out_sz) {
    if (!out_buf || out_sz == 0) {
        return;
    }

    memset(out_buf, 0, out_sz);

    switch (tag) {
        /* [1] 정수형 및 카운터 처리 */
        case 0x02: // ASN_INTEGER
        case 0x41: // Counter32
        case 0x42: // Gauge32
        case 0x43: // TimeTicks
        {
            uint32_t val = 0;
            for (size_t i = 0; i < val_len; i++) {
                val = (val << 8) | val_data[i];
            }

            if (tag == 0x43) { // TimeTicks 특화 포맷팅 (1/100초 단위)
                uint32_t sec = val / 100;
                uint32_t days = sec / 86400;
                uint32_t hours = (sec % 86400) / 3600;
                uint32_t mins = (sec % 3600) / 60;
                uint32_t secs = sec % 60;
                snprintf(out_buf, out_sz, "%u days %02u:%02u:%02u", days, hours, mins, secs);
            } else {
                snprintf(out_buf, out_sz, "%u", val);
            }
            break;
        }

        /* [2] IP Address 처리 (0x40) */
        case 0x40:
        {
            if (val_len == 4) {
                snprintf(out_buf, out_sz, "%u.%u.%u.%u",
                         val_data[0], val_data[1], val_data[2], val_data[3]);
            }
            break;
        }

        /* [3] OCTET STRING 처리 (0x04) */
        case 0x04:
        {
            if (is_printable_string(val_data, val_len)) {
                // 일반 문자열 (예: "eth0")
                size_t copy_len = (val_len < out_sz - 1) ? val_len : out_sz - 1;
                memcpy(out_buf, val_data, copy_len);
            } else {
                // MAC 주소 등 Hex 형태 (예: 00:1A:2B:3C:4D:5E)
                size_t pos = 0;
                for (size_t i = 0; i < val_len && pos < out_sz - 3; i++) {
                    pos += snprintf(out_buf + pos, out_sz - pos, "%02X%s",
                                    val_data[i], (i == val_len - 1) ? "" : ":");
                }
            }
            break;
        }

        /* [4] OID 처리 (0x06) - 기존 OID 디코딩 로직 활용 */
        case 0x06:
        {
            // snmp_asn_decode_oid(val_data, val_len, out_buf, out_sz);
            // (기존에 작성하신 OID 파싱 함수 호출)
            snprintf(out_buf, out_sz, "OID Data (Parsed)");
            break;
        }

        /* 예외 처리 */
        default:
            snprintf(out_buf, out_sz, "[Type: %02X, Len: %zu]", tag, val_len);
            break;
    }
}

SnmpVarBind* new_SnmpVarBind(uint8_t tag, const char* oid, const char* value) {
    SnmpVarBind* self = calloc(1, sizeof(SnmpVarBind));
    if (!self) return NULL;
    Object_Init((Object*)self, &SnmpVarBind_Class);

    self->tag = tag;
    strncpy(self->oid, oid, sizeof(self->oid) - 1);
    self->oid[sizeof(self->oid) - 1] = '\0';
    strncpy(self->value_str, value, sizeof(self->value_str) - 1);
    self->value_str[sizeof(self->value_str) - 1] = '\0';
    self->getTypeName = varbind_get_type_name_impl;
    self->asInt       = varbind_as_int_impl;
    self->asLong      = varbind_as_long_impl;
    return self;
}

bool snmp_asn_decode_response(const uint8_t* buf, size_t len, ArrayList* out_varbinds) {
    if (!buf || !out_varbinds || len < 10) {
        return false;
    }

    const uint8_t* p = buf;
    const uint8_t* end = buf + len;

    while (p < end && *p != 0xA2) {
        p++;
    }

    if (p >= end) {
        return false;
    }

    p++;

    size_t pdu_len;
    p = asn1_decode_length(p, &pdu_len);

    if (!p) {
        return false;
    }

    for (int i = 0; i < 3; i++) {
        if (p >= end || *p != 0x02) {
            return false;
        }

        p++;

        size_t l;
        p = asn1_decode_length(p, &l);

        if (!p) {
            return false;
        }

        p += l;
    }

    if (p >= end || *p != 0x30) {
        return false;
    }

    p++;

    size_t vbl_len;
    p = asn1_decode_length(p, &vbl_len);

    if (!p) {
        return false;
    }

    const uint8_t* vbl_end = p + vbl_len;

    while (p < vbl_end && p < end) {
        if (*p != 0x30) {
            break;
        }

        p++;

        size_t vb_len;
        p = asn1_decode_length(p, &vb_len);

        if (!p) {
            break;
        }

        const uint8_t* vb_end = p + vb_len;

        if (p < vb_end && *p == 0x06) {
            uint32_t oids[128];
            size_t cnt = 0;
            p = asn1_decode_oid(p, oids, &cnt);

            char oid_str[256];
            memset(oid_str, 0, sizeof(oid_str));

            int off = 0;

            for (size_t k = 0; k < cnt; k++) {
                off += snprintf(oid_str + off, sizeof(oid_str) - off, "%s%u", (k == 0 ? "" : "."), oids[k]);
            }

            char val[512];
            memset(val, 0, sizeof(val));

            uint8_t tag = *p;
            const uint8_t* val_ptr = p + 1;
            size_t val_len = 0;

            val_ptr = asn1_decode_length(val_ptr, &val_len);

            if (tag == 0x06) {
                uint32_t v_oids[128];
                size_t v_cnt = 0;
                p = asn1_decode_oid(p, v_oids, &v_cnt);

                int voff = 0;

                for (size_t k = 0; k < v_cnt; k++) {
                    voff += snprintf(val + voff, sizeof(val) - voff, "%s%u", (k == 0 ? "" : "."), v_oids[k]);
                }
            } else if (tag == 0x46) {
                uint64_t uv64 = 0;
                p = asn1_decode_unsigned64(p, &uv64);
                snprintf(val, sizeof(val), "%llu", (unsigned long long)uv64);
            } else if (tag == 0x05) {
                snprintf(val, sizeof(val), "NULL");
                p = val_ptr + val_len;
            } else if (tag >= 0x80) {
                snprintf(val, sizeof(val), "Exception/End(%02X)", tag);
                p = val_ptr + val_len;
            } else {
                // 🚨 여기서 임시 버퍼(val)에 스마트 포맷팅을 수행합니다!
                snmp_asn_format_value(tag, val_ptr, val_len, val, sizeof(val));
                p = val_ptr + val_len;
            }

            // 🚨 가공이 끝난 예쁜 문자열(val)을 생성자에 던져 캡슐화를 완벽히 유지합니다!
            SnmpVarBind* vb = new_SnmpVarBind(tag, oid_str, val);

            if (vb) {
                out_varbinds->add(out_varbinds, (Object*)vb);
                RELEASE_NULL(vb);
            }
        }

        p = vb_end;
    }

    return true;
}

size_t snmp_asn_encode_pdu(uint8_t* buf, size_t buf_sz, uint8_t pdu_type,
                           int version_val, const char* sec_name,
                           const char* oid, int non_repeaters, int max_repetitions,
                           const char* set_value) {
    if (!buf || !sec_name || !oid || buf_sz < 512) {
        return 0;
    }

    uint8_t oid_val[256];
    uint8_t* ov_ptr = oid_val;

    ov_ptr = asn1_encode_oid(ov_ptr, oid);

    if (set_value) {
        ov_ptr = asn1_encode_string(ov_ptr, set_value, strlen(set_value));
    } else {
        *ov_ptr++ = ASN1_NULL;
        *ov_ptr++ = 0x00;
    }

    size_t ov_len = (size_t)(ov_ptr - oid_val);

    uint8_t varbind[512];
    uint8_t* vb_ptr = varbind;

    *vb_ptr++ = ASN1_SEQUENCE;
    vb_ptr = asn1_encode_length(vb_ptr, ov_len);

    memcpy(vb_ptr, oid_val, ov_len);
    vb_ptr += ov_len;

    size_t varbind_len = (size_t)(vb_ptr - varbind);

    uint8_t varbind_list[1024];
    uint8_t* vl_ptr = varbind_list;

    *vl_ptr++ = ASN1_SEQUENCE;
    vl_ptr = asn1_encode_length(vl_ptr, varbind_len);

    memcpy(vl_ptr, varbind, varbind_len);
    vl_ptr += varbind_len;

    size_t varbind_list_len = (size_t)(vl_ptr - varbind_list);

    uint8_t pdu_payload[2048];
    uint8_t* pp_ptr = pdu_payload;

    pp_ptr = asn1_encode_integer(pp_ptr, 1001);
    pp_ptr = asn1_encode_integer(pp_ptr, non_repeaters);
    pp_ptr = asn1_encode_integer(pp_ptr, max_repetitions);

    memcpy(pp_ptr, varbind_list, varbind_list_len);
    pp_ptr += varbind_list_len;

    size_t pdu_payload_len = (size_t)(pp_ptr - pdu_payload);

    uint8_t msg_payload[4096];
    uint8_t* m_ptr = msg_payload;

    m_ptr = asn1_encode_integer(m_ptr, version_val);
    m_ptr = asn1_encode_string(m_ptr, sec_name, strlen(sec_name));

    *m_ptr++ = pdu_type;

    m_ptr = asn1_encode_length(m_ptr, pdu_payload_len);

    memcpy(m_ptr, pdu_payload, pdu_payload_len);
    m_ptr += pdu_payload_len;

    size_t msg_payload_len = (size_t)(m_ptr - msg_payload);

    uint8_t* final_ptr = buf;

    *final_ptr++ = ASN1_SEQUENCE;
    final_ptr = asn1_encode_length(final_ptr, msg_payload_len);

    memcpy(final_ptr, msg_payload, msg_payload_len);
    final_ptr += msg_payload_len;

    return (size_t)(final_ptr - buf);
}