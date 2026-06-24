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

SnmpVarBind* new_SnmpVarBind(uint8_t tag, const char* oid, const char* value) {
    SnmpVarBind* self = calloc(1, sizeof(SnmpVarBind));
    if (!self) return NULL;
    Object_Init((Object*)self, &SnmpVarBind_Class);

    self->tag = tag;
    strncpy(self->oid, oid, sizeof(self->oid) - 1);
    self->oid[sizeof(self->oid) - 1] = '\0';
    strncpy(self->value_str, value, sizeof(self->value_str) - 1);
    self->value_str[sizeof(self->value_str) - 1] = '\0';
    return self;
}

// 🚨 [수정 완료] GetResponse 껍데기를 정확히 벗겨내는 고성능 파서!
bool snmp_asn_decode_response(const uint8_t* buf, size_t len, ArrayList* out_varbinds) {
    if (!buf || !out_varbinds || len < 10) return false;

    // 1. GetResponse PDU (0xA2) 태그를 찾을 때까지 스캔합니다.
    const uint8_t* p = buf;
    const uint8_t* end = buf + len;
    while (p < end && *p != 0xA2) p++;
    if (p >= end) return false; // 응답 PDU가 없으면 종료

    p++; // 0xA2 태그 스킵
    size_t pdu_len;
    p = asn1_decode_length(p, &pdu_len);
    if (!p) return false;

    // 2. RequestID, ErrorStatus, ErrorIndex (각각 0x02 태그) 껍데기 스킵
    for (int i = 0; i < 3; i++) {
        if (p >= end || *p != 0x02) return false;
        p++;
        size_t l;
        p = asn1_decode_length(p, &l);
        if (!p) return false;
        p += l;
    }

    // 3. 드디어 알맹이인 VarBindList (0x30) 진입!
    if (p >= end || *p != 0x30) return false;
    p++;
    size_t vbl_len;
    p = asn1_decode_length(p, &vbl_len);
    if (!p) return false;
    const uint8_t* vbl_end = p + vbl_len;

    // 4. 개별 VarBind (0x30) 추출 루프
    while (p < vbl_end && p < end) {
        if (*p != 0x30) break;
        p++;
        size_t vb_len;
        p = asn1_decode_length(p, &vb_len);
        if (!p) break;
        const uint8_t* vb_end = p + vb_len;

        // OID 태그 (0x06) 확인
        if (p < vb_end && *p == 0x06) {
            uint32_t oids[128];
            size_t cnt = 0;
            p = asn1_decode_oid(p, oids, &cnt);

            char oid_str[256] = {0};
            int off = 0;
            for (size_t k = 0; k < cnt; k++) {
                off += snprintf(oid_str + off, sizeof(oid_str) - off, "%s%u", (k == 0 ? "" : "."), oids[k]);
            }

            char val[512] = {0};
            uint8_t tag = *p;

            // Value 타입에 맞게 파싱
            if (tag == 0x04) { // OCTET STRING
                p = asn1_decode_string(p, val, sizeof(val));
            } else if (tag == 0x02) { // INTEGER
                int32_t v = 0;
                p = asn1_decode_integer(p, &v);
                snprintf(val, sizeof(val), "%d", v);
            } else if (tag == 0x40) { // IP ADDRESS
                p = asn1_decode_ip(p, val, sizeof(val));
            } else if (tag == 0x41 || tag == 0x42 || tag == 0x43) { // UNSIGNED (Counter/Gauge/TimeTicks)
                uint32_t uv = 0;
                p = asn1_decode_unsigned(p, &uv);
                snprintf(val, sizeof(val), "%u", uv);
            } else if (tag == 0x05) { // NULL
                snprintf(val, sizeof(val), "NULL");
            } else if (tag == 0x80 || tag == 0x81 || tag == 0x82) { // SNMPv2 예외 (EndOfMibView 등)
                snprintf(val, sizeof(val), "Exception/End(%02X)", tag);
            } else {
                snprintf(val, sizeof(val), "UnknownTag(%02X)", tag);
            }

            SnmpVarBind* vb = new_SnmpVarBind(tag, oid_str, val);
            if (vb) {
                out_varbinds->add(out_varbinds, (Object*)vb);
                RELEASE_NULL(vb);
            }
        }
        p = vb_end; // 무조건 다음 Varbind 시작점으로 안전하게 점프!
    }
    return true;
}

size_t snmp_asn_encode_pdu(uint8_t* buf, size_t buf_sz, uint8_t pdu_type,
                           int version_val, const char* sec_name,
                           const char* oid, int non_repeaters, int max_repetitions,
                           const char* set_value) {
    if (!buf || !sec_name || !oid || buf_sz < 512) return 0;
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