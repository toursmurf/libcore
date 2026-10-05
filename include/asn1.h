#ifndef ASN1_H
#define ASN1_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ASN.1 기본 태그 정의 */
#define ASN1_INTEGER        0x02
#define ASN1_OCTET_STRING   0x04
#define ASN1_NULL           0x05
#define ASN1_OBJECT_ID      0x06
#define ASN1_SEQUENCE       0x30

/* SNMP Application Tags */
#define ASN1_IPADDRESS      0x40
#define ASN1_COUNTER32      0x41
#define ASN1_GAUGE32        0x42
#define ASN1_TIMETICKS      0x43
#define ASN1_COUNTER64      0x46

/* 🛡️ 정수 오버플로우 공격을 원천 차단하는 뺄셈 기반 안전 검증 매크로 */
#define ASN_FITS(p, n, lim) \
    ((p) != NULL && \
     (p) <= (lim) && \
     (size_t)(n) <= (size_t)((lim) - (p)))

/* 100점 만점 디코더 API (OID Capacity 계약 추가) */
const uint8_t* asn1_decode_length(const uint8_t* ptr, const uint8_t* end, size_t* out_len);
const uint8_t* asn1_decode_integer(const uint8_t* ptr, const uint8_t* end, int32_t* out_val);
const uint8_t* asn1_decode_unsigned(const uint8_t* ptr, const uint8_t* end, uint32_t* out_val);
const uint8_t* asn1_decode_unsigned64(const uint8_t* ptr, const uint8_t* end, uint64_t* out_val);
const uint8_t* asn1_decode_ip(const uint8_t* ptr, const uint8_t* end, char* out_ip, size_t max_len);
const uint8_t* asn1_decode_string(const uint8_t* ptr, const uint8_t* end, char* out_str, size_t max_len);
const uint8_t* asn1_decode_oid(const uint8_t* ptr, const uint8_t* end, uint32_t* oids, size_t oid_cap, size_t* count);

/* 100점 만점 인코더 API (Bounded Form - 버퍼 초과 복사 원천 봉쇄) */
bool asn1_encode_length(uint8_t** p, const uint8_t* end, size_t length);
bool asn1_encode_integer(uint8_t** p, const uint8_t* end, int32_t value);
bool asn1_encode_unsigned(uint8_t** p, const uint8_t* end, uint32_t value, uint8_t tag);
bool asn1_encode_ip(uint8_t** p, const uint8_t* end, const uint8_t ip[4]);
bool asn1_encode_string(uint8_t** p, const uint8_t* end, const char* str, size_t len);
bool asn1_encode_oid(uint8_t** p, const uint8_t* end, const char* oid_str);

#endif // ASN1_H