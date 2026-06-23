#include "snmp_asn.h"
#include <stdio.h>
#include <string.h>

static void SnmpVarBind_finalize(Object* obj) { }

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

bool snmp_asn_decode_response(const uint8_t* buf, size_t len, ArrayList* out_varbinds) {
    if (!buf || !out_varbinds) return false;
    const uint8_t* p = buf;
    if (*p++ != ASN1_SEQUENCE) return false;
    size_t seq_len;
    p = asn1_decode_length(p, &seq_len);
    if (!p) return false;

    while (p < buf + len) {
        uint32_t oids[32];
        size_t cnt = 0;
        char oid_str[256] = {0};
        char val[512] = {0};
        p = asn1_decode_oid(p, oids, &cnt);
        if (!p) break;

        int off = 0;
        for (size_t k = 0; k < cnt; k++) {
            off += snprintf(oid_str + off, sizeof(oid_str) - off, "%s%u", (k == 0 ? "" : "."), oids[k]);
        }

        uint8_t current_tag = *p;
        if (current_tag == ASN1_OCTET_STRING) {
            p = asn1_decode_string(p, val, sizeof(val));
        } else if (current_tag == ASN1_INTEGER) {
            int32_t v;
            p = asn1_decode_integer(p, &v);
            snprintf(val, sizeof(val), "%d", v);
        } else if (current_tag == ASN1_IPADDRESS) {
            p = asn1_decode_ip(p, val, sizeof(val));
        } else if (current_tag == ASN1_COUNTER32 || current_tag == ASN1_GAUGE32 || current_tag == ASN1_TIMETICKS) {
            uint32_t uv;
            p = asn1_decode_unsigned(p, &uv);
            snprintf(val, sizeof(val), "%u", uv);
        } else if (current_tag == ASN1_NULL) {
            snprintf(val, sizeof(val), "NULL");
            p += 2;
        } else { break; }

        SnmpVarBind* vb = new_SnmpVarBind(current_tag, oid_str, val);
        if (vb) {
            out_varbinds->add(out_varbinds, (Object*)vb);
            RELEASE_NULL((Object**)&vb);
        }
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