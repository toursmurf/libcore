#ifndef SNMP_ASN_H
#define SNMP_ASN_H

#include "asn1.h"
#include "libcore.h"

/* 🛡️ Strict Parser 용량 계약 명문화 */
#define SNMP_OID_STR_CAP    256
#define SNMP_VALUE_STR_CAP  512

typedef struct SnmpVarBind SnmpVarBind;
struct SnmpVarBind {
    Object base;
    size_t  value_len;
    uint8_t tag;
    char oid[SNMP_OID_STR_CAP];
    char value_str[SNMP_VALUE_STR_CAP];
    const char* (*getTypeName) (SnmpVarBind* self);
    int         (*asInt)       (SnmpVarBind* self);
    long long   (*asLong)      (SnmpVarBind* self);
};

SnmpVarBind* new_SnmpVarBind(uint8_t tag, const char* oid, const char* value);
bool snmp_asn_decode_response(const uint8_t* buf, size_t len, ArrayList* out_varbinds);
size_t snmp_asn_encode_pdu(uint8_t* buf, size_t buf_sz, uint8_t pdu_type,
                           int version_val, const char* sec_name,
                           const char* oid, int non_repeaters, int max_repetitions,
                           const char* set_value);

#endif // SNMP_ASN_H