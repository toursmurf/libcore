#ifndef ASN1_H
#define ASN1_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ASN.1 기본 태그 정의 */
#define ASN1_INTEGER      0x02
#define ASN1_OCTET_STRING 0x04
#define ASN1_NULL         0x05
#define ASN1_OBJECT_ID    0x06
#define ASN1_SEQUENCE     0x30
#define ASN1_IPADDRESS    0x40
#define ASN1_COUNTER32    0x41
#define ASN1_GAUGE32      0x42
#define ASN1_TIMETICKS    0x43

/* ASN.1 인코딩/디코딩 헬퍼 인터페이스 */
uint8_t* asn1_encode_length(uint8_t* buf, size_t length);
uint8_t* asn1_encode_integer(uint8_t* buf, int32_t value);
uint8_t* asn1_encode_string(uint8_t* buf, const char* str, size_t len);
uint8_t* asn1_encode_oid(uint8_t* buf, const uint32_t* oid, size_t oid_len);

const uint8_t* asn1_decode_length(const uint8_t* buf, size_t* out_length);
const uint8_t* asn1_decode_integer(const uint8_t* buf, int32_t* out_value);
const uint8_t* asn1_decode_string(const uint8_t* buf, char* out_str, size_t max_len);

#endif // ASN1_H