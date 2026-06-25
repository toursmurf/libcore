#ifndef SNMP_ASN_H
#define SNMP_ASN_H

#include "asn1.h"
#include "libcore.h"

typedef  struct SnmpVarBind  SnmpVarBind;
struct SnmpVarBind {
    Object base;
    size_t  value_len;
    uint8_t tag;        // 🚀 타입 태그 필드 추가
    char oid[256];
    char value_str[512];
    const char* (*getTypeName) (SnmpVarBind* self);
    int         (*asInt)       (SnmpVarBind* self);
    long long   (*asLong)      (SnmpVarBind* self);
};

// 🚀 tag 인자 추가된 생성자
SnmpVarBind* new_SnmpVarBind(uint8_t tag, const char* oid, const char* value);
bool snmp_asn_decode_response(const uint8_t* buf, size_t len, ArrayList* out_varbinds);
size_t snmp_asn_encode_pdu(uint8_t* buf, size_t buf_sz, uint8_t pdu_type,
                           int version_val, const char* sec_name,
                           const char* oid, int non_repeaters, int max_repetitions,
                           const char* set_value);

#endif