#ifndef CORE_SNMP_H
#define CORE_SNMP_H

#include "libcore.h"
#include "socket_base.h"
#include "snmp_asn.h"

typedef enum {
    SNMP_TRANS_UDP = 0,
    SNMP_TRANS_TCP = 1,
} SnmpTransport;

typedef enum {
    SNMP_V1  = 0,
    SNMP_V2C = 1,
    SNMP_V3  = 3,
} SnmpVersion;

typedef enum {
    SNMP_SEC_NO_AUTH       = 0,
    SNMP_SEC_AUTH_NO_PRIV  = 1,
    SNMP_SEC_AUTH_PRIV     = 2,
} SnmpSecLevel;

typedef enum {
    SNMP_AUTH_NONE = 0,
    SNMP_AUTH_MD5  = 1,
    SNMP_AUTH_SHA  = 2,
} SnmpAuthProto;

typedef enum {
    SNMP_PRIV_NONE = 0,
    SNMP_PRIV_DES  = 1,
    SNMP_PRIV_AES  = 2,
} SnmpPrivProto;

typedef struct SnmpTrap {
    Object base;
    SnmpVersion version;
    char community[64];
    char username[64];
    char trap_oid[256];
    char from_ip[INET_ADDRSTRLEN];
    int from_port;
} SnmpTrap;

typedef struct CoreSnmp {
    Object          base;
    Socket* trap_receiver;
    Socket* snmp_sender;
    HashMap* oid_map;
    SnmpVersion     version;
    SnmpTransport   transport;
    int             trap_port;
    int             agent_port;
    atomic_size_t   trap_count;
    char            community[64];
    char            username[64];
    SnmpSecLevel    sec_level;
    SnmpAuthProto   auth_proto;
    SnmpPrivProto   priv_proto;
    uint8_t         auth_key[32];
    uint8_t         priv_key[32];

    ErrorCode (*startListen) (struct CoreSnmp* self, int port);
    void      (*stopListen)  (struct CoreSnmp* self);
    void      (*setTrapPort) (struct CoreSnmp* self, int port);
    void      (*setAgentPort)(struct CoreSnmp* self, int port);

    ErrorCode (*sendGet)     (struct CoreSnmp* self, const char* ip, const char* oid, void* out, size_t sz, size_t* out_len);
    ErrorCode (*sendGetNext) (struct CoreSnmp* self, const char* ip, const char* oid, void* out, size_t sz, size_t* out_len);
    ErrorCode (*sendGetBulk) (struct CoreSnmp* self, const char* ip, const char* oid, int non_repeaters, int max_repetitions, ArrayList* out_varbinds);
    ErrorCode (*snmpWalk)    (struct CoreSnmp* self, const char* ip, const char* root_oid, ArrayList* out_all);
    ErrorCode (*sendSet)     (struct CoreSnmp* self, const char* ip, const char* oid, const char* value);
    ErrorCode (*sendTrap)    (struct CoreSnmp* self, const char* ip, const char* oid);
    ErrorCode (*sendInform)  (struct CoreSnmp* self, const char* ip, const char* oid);

    bool      (*setOid)      (struct CoreSnmp* self, const char* oid, const char* desc);
    size_t    (*getTrapCount)(struct CoreSnmp* self);
    void      (*resetStats)  (struct CoreSnmp* self);
} CoreSnmp;

extern const Class CoreSnmp_Class_Instance;

SnmpTrap* new_SnmpTrap(void);
CoreSnmp* new_Snmp(SnmpTransport transport, const char* version_str, const char* community);
CoreSnmp* new_SnmpV3(SnmpTransport transport, const char* username, SnmpSecLevel sec_level,
                     SnmpAuthProto auth_proto, const uint8_t* auth_key, size_t auth_key_len,
                     SnmpPrivProto priv_proto, const uint8_t* priv_key, size_t priv_key_len);

#endif