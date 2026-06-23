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

/* SNMP Application Tags */
#define ASN1_IPADDRESS    0x40
#define ASN1_COUNTER32    0x41
#define ASN1_GAUGE32      0x42
#define ASN1_TIMETICKS    0x43

// 디코더 API
const uint8_t* asn1_decode_length(const uint8_t* buf, size_t* out_length);
const uint8_t* asn1_decode_integer(const uint8_t* buf, int32_t* out_value);
const uint8_t* asn1_decode_unsigned(const uint8_t* buf, uint32_t* out_value);
const uint8_t* asn1_decode_ip(const uint8_t* buf, char* out_ip, size_t max_len);
const uint8_t* asn1_decode_string(const uint8_t* buf, char* out_str, size_t max_len);
const uint8_t* asn1_decode_oid(const uint8_t* buf, uint32_t* out_oids, size_t* out_count);

// 인코더 API
uint8_t* asn1_encode_length(uint8_t* buf, size_t length);
uint8_t* asn1_encode_integer(uint8_t* buf, int32_t value);
uint8_t* asn1_encode_unsigned(uint8_t* buf, uint32_t value, uint8_t tag);
uint8_t* asn1_encode_ip(uint8_t* buf, const uint8_t ip[4]);
uint8_t* asn1_encode_string(uint8_t* buf, const char* str, size_t len);
uint8_t* asn1_encode_oid(uint8_t* buf, const char* oid_str);

#endifㄴ