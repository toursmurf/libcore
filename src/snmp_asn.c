#include "snmp_asn.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static void SnmpVarBind_finalize(Object* obj) {
    (void)obj;
}

static Class SnmpVarBind_Class = {
    .name     = "SnmpVarBind",
    .size     = sizeof(SnmpVarBind),
    .finalize = SnmpVarBind_finalize
};

static const char* varbind_get_type_name_impl(SnmpVarBind* self) {
    if (!self) return "Unknown";
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

static int varbind_as_int_impl(SnmpVarBind* self) {
    if (!self) return 0;
    if (self->tag == 0x02 || self->tag == 0x41 || self->tag == 0x42 || self->tag == 0x43) {
        return atoi(self->value_str);
    }
    return 0;
}

static long long varbind_as_long_impl(SnmpVarBind* self) {
    if (!self) return 0;
    if (self->tag == 0x02 || self->tag == 0x41 || self->tag == 0x42 || self->tag == 0x43 || self->tag == 0x46) {
        return atoll(self->value_str);
    }
    return 0;
}

static bool is_printable_string(const uint8_t* data, size_t len) {
    if (!data || len == 0) return false;
    for (size_t i = 0; i < len; i++) {
        if (data[i] < 32 || data[i] > 126) {
            if (data[i] != '\r' && data[i] != '\n' && data[i] != '\t') {
                return false;
            }
        }
    }
    return true;
}

SnmpVarBind* new_SnmpVarBind(uint8_t tag, const char* oid, const char* value) {
    if (!oid || !value) return NULL;

    /* 🛡️ Strict Parser: Silent Truncation 원천 차단 (Atomic Reject) */
    size_t oid_len = strlen(oid);
    size_t val_len = strlen(value);
    if (oid_len >= SNMP_OID_STR_CAP || val_len >= SNMP_VALUE_STR_CAP) {
        return NULL; /* 할당 전에 찌꺼기 없이 Reject */
    }

    SnmpVarBind* self = calloc(1, sizeof(SnmpVarBind));
    if (!self) return NULL;

    Object_Init((Object*)self, &SnmpVarBind_Class);
    self->tag = tag;
    self->value_len = val_len;

    /* 🛡️ 계약된 길이만큼만 안전하게 복사 */
    memcpy(self->oid, oid, oid_len + 1);
    memcpy(self->value_str, value, val_len + 1);

    self->getTypeName = varbind_get_type_name_impl;
    self->asInt       = varbind_as_int_impl;
    self->asLong      = varbind_as_long_impl;

    return self;
}

bool snmp_asn_decode_response(const uint8_t* buf, size_t len, ArrayList* out_varbinds) {
    if (!buf || !out_varbinds || len < 10) return false;

    const uint8_t* p = buf;
    const uint8_t* end = buf + len;

    ArrayList* temp_vbs = new_ArrayList(32);
    if (!temp_vbs) return false;

    if (p >= end || *p != ASN1_SEQUENCE) goto decode_fail;
    p++;

    size_t msg_len;
    p = asn1_decode_length(p, end, &msg_len);
    if (!p || !ASN_FITS(p, msg_len, end)) goto decode_fail;

    const uint8_t* msg_end = p + msg_len;
    if (msg_end != end) goto decode_fail;

    int32_t version;
    p = asn1_decode_integer(p, msg_end, &version);
    if (!p) goto decode_fail;

    if (p >= msg_end || *p != ASN1_OCTET_STRING) goto decode_fail;
    p++;
    size_t comm_len;
    p = asn1_decode_length(p, msg_end, &comm_len);
    if (!p || !ASN_FITS(p, comm_len, msg_end)) goto decode_fail;
    p += comm_len;

    if (p >= msg_end || *p != 0xA2) goto decode_fail;
    p++;

    size_t pdu_len;
    p = asn1_decode_length(p, msg_end, &pdu_len);
    if (!p || !ASN_FITS(p, pdu_len, msg_end)) goto decode_fail;

    const uint8_t* pdu_end = p + pdu_len;
    if (pdu_end != msg_end) goto decode_fail;

    int32_t request_id, err_status, err_index;
    p = asn1_decode_integer(p, pdu_end, &request_id);
    if (!p) goto decode_fail;

    p = asn1_decode_integer(p, pdu_end, &err_status);
    if (!p) goto decode_fail;

    p = asn1_decode_integer(p, pdu_end, &err_index);
    if (!p) goto decode_fail;

    if (err_status != 0) goto decode_fail;

    if (p >= pdu_end || *p != ASN1_SEQUENCE) goto decode_fail;
    p++;

    size_t vbl_len;
    p = asn1_decode_length(p, pdu_end, &vbl_len);
    if (!p || !ASN_FITS(p, vbl_len, pdu_end)) goto decode_fail;

    const uint8_t* vbl_end = p + vbl_len;
    if (vbl_end != pdu_end) goto decode_fail;

    while (p < vbl_end) {
        if (*p != ASN1_SEQUENCE) goto decode_fail;
        p++;

        size_t vb_len;
        p = asn1_decode_length(p, vbl_end, &vb_len);
        if (!p || !ASN_FITS(p, vb_len, vbl_end)) goto decode_fail;

        const uint8_t* vb_end = p + vb_len;

        if (p < vb_end && *p == ASN1_OBJECT_ID) {
            uint32_t oids[128];
            size_t cnt = 0;
            p = asn1_decode_oid(p, vb_end, oids, 128, &cnt);
            if (!p) goto decode_fail;

            /* 🛡️ 계약된 OID 버퍼 크기 적용 및 초과 방어 */
            char oid_str[SNMP_OID_STR_CAP];
            memset(oid_str, 0, sizeof(oid_str));
            int off = 0;

            for (size_t k = 0; k < cnt; k++) {
                int n = snprintf(oid_str + off, sizeof(oid_str) - off, "%s%u", (k == 0 ? "" : "."), oids[k]);
                if (n < 0 || (size_t)n >= sizeof(oid_str) - off) goto decode_fail;
                off += n;
            }

            /* 🛡️ 계약된 Value 버퍼 크기 적용 */
            char val[SNMP_VALUE_STR_CAP];
            memset(val, 0, sizeof(val));

            if (p >= vb_end) goto decode_fail;
            uint8_t tag = *p;

            if (tag == ASN1_INTEGER) {
                int32_t v;
                const uint8_t *tp = asn1_decode_integer(p, vb_end, &v);
                if (!tp || tp != vb_end) goto decode_fail;
                snprintf(val, sizeof(val), "%d", v);

            } else if (tag == ASN1_COUNTER32 || tag == ASN1_GAUGE32 || tag == ASN1_TIMETICKS) {
                uint32_t v;
                const uint8_t *tp = asn1_decode_unsigned(p, vb_end, &v);
                if (!tp || tp != vb_end) goto decode_fail;
                if (tag == ASN1_TIMETICKS) {
                    uint32_t sec = v / 100;
                    snprintf(val, sizeof(val), "%u days %02u:%02u:%02u",
                             sec / 86400, (sec % 86400) / 3600, (sec % 3600) / 60, sec % 60);
                } else {
                    snprintf(val, sizeof(val), "%u", v);
                }

            } else if (tag == ASN1_COUNTER64) {
                uint64_t uv64 = 0;
                const uint8_t* tp = asn1_decode_unsigned64(p, vb_end, &uv64);
                if (!tp || tp != vb_end) goto decode_fail;
                snprintf(val, sizeof(val), "%llu", (unsigned long long)uv64);

            } else if (tag == ASN1_IPADDRESS) {
                const uint8_t *tp = asn1_decode_ip(p, vb_end, val, sizeof(val));
                if (!tp || tp != vb_end) goto decode_fail;

            } else if (tag == ASN1_OCTET_STRING || tag == 0x44) {
                const uint8_t* val_ptr = p + 1;
                size_t parsed_len = 0;
                val_ptr = asn1_decode_length(val_ptr, vb_end, &parsed_len);
                if (!val_ptr || !ASN_FITS(val_ptr, parsed_len, vb_end) || val_ptr + parsed_len != vb_end) goto decode_fail;

                if (is_printable_string(val_ptr, parsed_len)) {
                    /* 🛡️ Value Silent Truncation 엄격한 방어 */
                    if (parsed_len >= sizeof(val)) goto decode_fail;
                    memcpy(val, val_ptr, parsed_len);
                } else {
                    size_t pos = 0;
                    for (size_t i = 0; i < parsed_len && pos < sizeof(val); i++) {
                        int n = snprintf(val + pos, sizeof(val) - pos, "%02X%s", val_ptr[i], (i == parsed_len - 1) ? "" : ":");
                        /* 🛡️ Hex 변환 시 버퍼 초과 즉시 Reject */
                        if (n < 0 || (size_t)n >= sizeof(val) - pos) goto decode_fail;
                        pos += n;
                    }
                }

            } else if (tag == ASN1_OBJECT_ID) {
                uint32_t v_oids[128];
                size_t v_cnt = 0;
                const uint8_t* tp = asn1_decode_oid(p, vb_end, v_oids, 128, &v_cnt);
                if (!tp || tp != vb_end) goto decode_fail;
                int voff = 0;
                for (size_t k = 0; k < v_cnt; k++) {
                    int n = snprintf(val + voff, sizeof(val) - voff, "%s%u", (k == 0 ? "" : "."), v_oids[k]);
                    if (n < 0 || (size_t)n >= sizeof(val) - voff) goto decode_fail;
                    voff += n;
                }

            } else if (tag == ASN1_NULL || tag == 0x80 || tag == 0x81 || tag == 0x82) {
                const uint8_t* val_ptr = p + 1;
                size_t val_len = 0;
                val_ptr = asn1_decode_length(val_ptr, vb_end, &val_len);
                if (!val_ptr || val_len != 0 || val_ptr != vb_end) goto decode_fail;

                if (tag == ASN1_NULL) snprintf(val, sizeof(val), "NULL");
                else snprintf(val, sizeof(val), "Exception/End(%02X)", tag);

            } else {
                goto decode_fail;
            }

            SnmpVarBind* vb = new_SnmpVarBind(tag, oid_str, val);
            if (!vb) goto decode_fail; /* 할당 또는 계약 실패 시 철저하게 튕겨냄 */
            temp_vbs->add(temp_vbs, (Object*)vb);
            RELEASE_NULL(vb);
        } else {
            goto decode_fail;
        }
        p = vb_end;
    }

    for (int i = 0; i < temp_vbs->getSize(temp_vbs); i++) {
        Object* obj = temp_vbs->get(temp_vbs, i);
        out_varbinds->add(out_varbinds, obj);
    }
    RELEASE_NULL(temp_vbs);
    return true;

decode_fail:
    RELEASE_NULL(temp_vbs);
    return false;
}

size_t snmp_asn_encode_pdu(uint8_t* buf, size_t buf_sz, uint8_t pdu_type,
                           int version_val, const char* sec_name,
                           const char* oid, int non_repeaters, int max_repetitions,
                           const char* set_value) {
    if (!buf || !sec_name || !oid || buf_sz < 512) {
        return 0;
    }

    uint8_t oid_val[1024];
    uint8_t* ov_ptr = oid_val;
    uint8_t* ov_end = oid_val + sizeof(oid_val);

    if (!asn1_encode_oid(&ov_ptr, ov_end, oid)) return 0;

    if (set_value) {
        if (!asn1_encode_string(&ov_ptr, ov_end, set_value, strlen(set_value))) return 0;
    } else {
        if (!ASN_FITS(ov_ptr, 2, ov_end)) return 0;
        *ov_ptr++ = ASN1_NULL;
        *ov_ptr++ = 0x00;
    }

    size_t ov_len = (size_t)(ov_ptr - oid_val);

    uint8_t varbind[1024];
    uint8_t* vb_ptr = varbind;
    uint8_t* vb_end = varbind + sizeof(varbind);

    if (!ASN_FITS(vb_ptr, 1, vb_end)) return 0;
    *vb_ptr++ = ASN1_SEQUENCE;
    if (!asn1_encode_length(&vb_ptr, vb_end, ov_len)) return 0;
    if (!ASN_FITS(vb_ptr, ov_len, vb_end)) return 0;
    memcpy(vb_ptr, oid_val, ov_len);
    vb_ptr += ov_len;

    size_t varbind_len = (size_t)(vb_ptr - varbind);

    uint8_t varbind_list[1536];
    uint8_t* vl_ptr = varbind_list;
    uint8_t* vl_end = varbind_list + sizeof(varbind_list);

    if (!ASN_FITS(vl_ptr, 1, vl_end)) return 0;
    *vl_ptr++ = ASN1_SEQUENCE;
    if (!asn1_encode_length(&vl_ptr, vl_end, varbind_len)) return 0;
    if (!ASN_FITS(vl_ptr, varbind_len, vl_end)) return 0;
    memcpy(vl_ptr, varbind, varbind_len);
    vl_ptr += varbind_len;

    size_t varbind_list_len = (size_t)(vl_ptr - varbind_list);

    uint8_t pdu_payload[2048];
    uint8_t* pp_ptr = pdu_payload;
    uint8_t* pp_end = pdu_payload + sizeof(pdu_payload);

    if (!asn1_encode_integer(&pp_ptr, pp_end, 1001)) return 0;
    if (!asn1_encode_integer(&pp_ptr, pp_end, non_repeaters)) return 0;
    if (!asn1_encode_integer(&pp_ptr, pp_end, max_repetitions)) return 0;

    if (!ASN_FITS(pp_ptr, varbind_list_len, pp_end)) return 0;
    memcpy(pp_ptr, varbind_list, varbind_list_len);
    pp_ptr += varbind_list_len;

    size_t pdu_payload_len = (size_t)(pp_ptr - pdu_payload);

    uint8_t msg_payload[4096];
    uint8_t* m_ptr = msg_payload;
    uint8_t* m_end = msg_payload + sizeof(msg_payload);

    if (!asn1_encode_integer(&m_ptr, m_end, version_val)) return 0;
    if (!asn1_encode_string(&m_ptr, m_end, sec_name, strlen(sec_name))) return 0;

    if (!ASN_FITS(m_ptr, 1, m_end)) return 0;
    *m_ptr++ = pdu_type;

    if (!asn1_encode_length(&m_ptr, m_end, pdu_payload_len)) return 0;
    if (!ASN_FITS(m_ptr, pdu_payload_len, m_end)) return 0;
    memcpy(m_ptr, pdu_payload, pdu_payload_len);
    m_ptr += pdu_payload_len;

    size_t msg_payload_len = (size_t)(m_ptr - msg_payload);

    uint8_t* final_ptr = buf;
    uint8_t* final_end = buf + buf_sz;

    if (!ASN_FITS(final_ptr, 1, final_end)) return 0;
    *final_ptr++ = ASN1_SEQUENCE;
    if (!asn1_encode_length(&final_ptr, final_end, msg_payload_len)) return 0;
    if (!ASN_FITS(final_ptr, msg_payload_len, final_end)) return 0;
    memcpy(final_ptr, msg_payload, msg_payload_len);
    final_ptr += msg_payload_len;

    return (size_t)(final_ptr - buf);
}