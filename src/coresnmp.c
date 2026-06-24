#include "coresnmp.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static void SnmpTrap_finalize(Object* obj) {
    (void)obj;
}

static Class SnmpTrap_Class = {
    .name     = "SnmpTrap",
    .size     = sizeof(SnmpTrap),
    .finalize = SnmpTrap_finalize
};

SnmpTrap* new_SnmpTrap(void) {
    SnmpTrap* self = (SnmpTrap*)calloc(1, sizeof(SnmpTrap));
    if (!self) return NULL;
    Object_Init((Object*)self, &SnmpTrap_Class);
    return self;
}

static void CoreSnmp_finalize(Object* obj) {
    CoreSnmp* self = (CoreSnmp*)obj;
    RELEASE_NULL(self->trap_receiver);
    RELEASE_NULL(self->snmp_sender);
    RELEASE_NULL(self->oid_map);
}

const Class CoreSnmp_Class_Instance = {
    .name     = "CoreSnmp",
    .size     = sizeof(CoreSnmp),
    .finalize = CoreSnmp_finalize
};

static ErrorCode startListen_impl(CoreSnmp* self, int port) {
    if (!self) return ERR_INVALID;
    if (self->trap_receiver) {
        RELEASE_NULL(self->trap_receiver);
    }
    char url[64];
    snprintf(url, sizeof(url), "%s0.0.0.0:%d",
             (self->transport == SNMP_TRANS_UDP ? "udp://" : "tcp://"), port);
    self->trap_receiver = createServer(url, NULL);
    if (!self->trap_receiver) return ERR_NET_CONNECT;
    self->trap_port = port;
    return OK;
}

static void stopListen_impl(CoreSnmp* self) {
    if (self && self->trap_receiver) {
        self->trap_receiver->close(self->trap_receiver);
        RELEASE_NULL(self->trap_receiver);
        self->trap_port = 0;
    }
}

static void setTrapPort_impl(CoreSnmp* self, int port) {
    if (self) self->trap_port = port;
}

static void setAgentPort_impl(CoreSnmp* self, int port) {
    if (self) self->agent_port = port;
}

static inline int get_version_val(SnmpVersion v) {
    if (v == SNMP_V3) return 3;
    if (v == SNMP_V2C) return 1;
    return 0;
}

static inline const char* get_sec_name(CoreSnmp* self) {
    return (self->version == SNMP_V3) ? self->username : self->community;
}

static ErrorCode sendGet_impl(CoreSnmp* self, const char* ip, const char* oid, void* out, size_t sz, size_t* out_len) {
    if (!self || !self->snmp_sender) return ERR_INVALID;
    uint8_t pdu[1024];
    size_t pdu_len = snmp_asn_encode_pdu(pdu, sizeof(pdu), 0xA0, get_version_val(self->version), get_sec_name(self), oid, 0, 0, NULL);
    ssize_t sent = self->snmp_sender->send(self->snmp_sender, pdu, pdu_len, ip, self->agent_port);
    if (sent < 0) return ERR_NET_CONNECT;

    char recv_ip[64]; // 🚨 [수정 완료] 수신용 IP 안전 버퍼 추가
    int port;
    ssize_t recvd = self->snmp_sender->recv(self->snmp_sender, out, sz, recv_ip, &port);
    if (recvd > 0) {
        if (out_len) *out_len = (size_t)recvd;
        return OK;
    }
    return ERR_NET_TIMEOUT;
}

static ErrorCode sendGetNext_impl(CoreSnmp* self, const char* ip, const char* oid, void* out, size_t sz, size_t* out_len) {
    if (!self || !self->snmp_sender) return ERR_INVALID;
    uint8_t pdu[1024];
    size_t pdu_len = snmp_asn_encode_pdu(pdu, sizeof(pdu), 0xA1, get_version_val(self->version), get_sec_name(self), oid, 0, 0, NULL);
    ssize_t sent = self->snmp_sender->send(self->snmp_sender, pdu, pdu_len, ip, self->agent_port);
    if (sent < 0) return ERR_NET_CONNECT;

    char recv_ip[64]; // 🚨 [수정 완료] 수신용 IP 안전 버퍼 추가
    int port;
    ssize_t recvd = self->snmp_sender->recv(self->snmp_sender, out, sz, recv_ip, &port);
    if (recvd > 0) {
        if (out_len) *out_len = (size_t)recvd;
        return OK;
    }
    return ERR_NET_TIMEOUT;
}

static ErrorCode sendGetBulk_impl(CoreSnmp* self, const char* ip, const char* oid, int non_repeaters, int max_repetitions, ArrayList* out_varbinds) {
    if (!self || !self->snmp_sender) return ERR_INVALID;
    uint8_t pdu[1024];
    size_t pdu_len = snmp_asn_encode_pdu(pdu, sizeof(pdu), 0xA5, get_version_val(self->version), get_sec_name(self), oid, non_repeaters, max_repetitions, NULL);
    ssize_t sent = self->snmp_sender->send(self->snmp_sender, pdu, pdu_len, ip, self->agent_port);
    if (sent < 0) return ERR_NET_CONNECT;

    char recv_ip[64]; // 🚨 [수정 완료] 수신용 IP 안전 버퍼 추가
    uint8_t raw_out[4096];
    int port;
    ssize_t recvd = self->snmp_sender->recv(self->snmp_sender, raw_out, sizeof(raw_out), recv_ip, &port);
    if (recvd > 0) {
        if (out_varbinds) snmp_asn_decode_response(raw_out, (size_t)recvd, out_varbinds);
        return OK;
    }
    return ERR_NET_TIMEOUT;
}

static ErrorCode sendSet_impl(CoreSnmp* self, const char* ip, const char* oid, const char* value) {
    if (!self || !self->snmp_sender) return ERR_INVALID;
    uint8_t pdu[1024];
    size_t pdu_len = snmp_asn_encode_pdu(pdu, sizeof(pdu), 0xA3, get_version_val(self->version), get_sec_name(self), oid, 0, 0, value);
    ssize_t sent = self->snmp_sender->send(self->snmp_sender, pdu, pdu_len, ip, self->agent_port);
    if (sent < 0) return ERR_NET_CONNECT;

    char recv_ip[64]; // 🚨 [수정 완료] 수신용 IP 안전 버퍼 추가
    uint8_t dummy[512];
    int port;
    ssize_t recvd = self->snmp_sender->recv(self->snmp_sender, dummy, sizeof(dummy), recv_ip, &port);
    return (recvd > 0) ? OK : ERR_NET_TIMEOUT;
}

static ErrorCode sendTrap_impl(CoreSnmp* self, const char* ip, const char* oid) {
    if (!self || !self->snmp_sender) return ERR_INVALID;
    uint8_t pdu[1024];
    size_t pdu_len = snmp_asn_encode_pdu(pdu, sizeof(pdu), 0xA4, get_version_val(self->version), get_sec_name(self), oid, 0, 0, NULL);
    ssize_t sent = self->snmp_sender->send(self->snmp_sender, pdu, pdu_len, ip, self->trap_port);
    return (sent > 0) ? OK : ERR_NET_CONNECT;
}

static ErrorCode sendInform_impl(CoreSnmp* self, const char* ip, const char* oid) {
    if (!self || !self->snmp_sender) return ERR_INVALID;
    uint8_t pdu[1024];
    size_t pdu_len = snmp_asn_encode_pdu(pdu, sizeof(pdu), 0xA6, get_version_val(self->version), get_sec_name(self), oid, 0, 0, NULL);
    ssize_t sent = self->snmp_sender->send(self->snmp_sender, pdu, pdu_len, ip, self->trap_port);
    if (sent < 0) return ERR_NET_CONNECT;

    char recv_ip[64]; // 🚨 [수정 완료] 수신용 IP 안전 버퍼 추가
    uint8_t ack[512];
    int port;
    ssize_t recvd = self->snmp_sender->recv(self->snmp_sender, ack, sizeof(ack), recv_ip, &port);
    return (recvd > 0) ? OK : ERR_NET_TIMEOUT;
}

static bool setOid_impl(CoreSnmp* self, const char* oid, const char* desc) {
    if (!self || !oid || !desc) return false;
    String* desc_str = new_String(desc);
    if (!desc_str) return false;
    self->oid_map->put(self->oid_map, oid, (Object*)desc_str);
    RELEASE((Object*)desc_str);
    return true;
}

static String* getOidDesc_impl(CoreSnmp* self, const char* oid) {
    if (!self || !oid) return NULL;
    String* desc = (String*)self->oid_map->get(self->oid_map, oid);
    if (desc) RETAIN((Object*)desc);
    return desc;
}

static bool walkOid_impl(CoreSnmp* self, const char* ip, const char* root_oid) {
    if (!self || !self->snmp_sender) return false;
    size_t dummy_len; uint8_t buf[2048];
    return (self->sendGetNext(self, ip, root_oid, buf, 2048, &dummy_len) == OK);
}

static size_t getTrapCount_impl(CoreSnmp* self) {
    if (!self) return 0;
    return atomic_load(&self->trap_count);
}

static void resetStats_impl(CoreSnmp* self) {
    if (self) atomic_store(&self->trap_count, 0);
}

static bool CoreSnmp_init_common(CoreSnmp* self, SnmpTransport transport) {
    Object_Init((Object*)self, &CoreSnmp_Class_Instance);
    self->startListen  = startListen_impl;
    self->stopListen   = stopListen_impl;
    self->setTrapPort  = setTrapPort_impl;
    self->setAgentPort = setAgentPort_impl;
    self->sendGet      = sendGet_impl;
    self->sendGetNext  = sendGetNext_impl;
    self->sendGetBulk  = sendGetBulk_impl;
    self->sendSet      = sendSet_impl;
    self->sendTrap     = sendTrap_impl;
    self->sendInform   = sendInform_impl;
    self->setOid       = setOid_impl;
    self->getOidDesc   = getOidDesc_impl;
    self->walkOid      = walkOid_impl;
    self->getTrapCount = getTrapCount_impl;
    self->resetStats   = resetStats_impl;

    self->transport = transport;
    self->trap_receiver = NULL;
    self->agent_port = 161;
    self->snmp_sender = createSyncClient((transport == SNMP_TRANS_UDP ? "udp://" : "tcp://"), NULL);

    self->oid_map  = new_HashMap(16);
    return (self->snmp_sender && self->oid_map);
}

CoreSnmp* new_Snmp(SnmpTransport transport, const char* version_str, const char* community) {
    if (!version_str || !community) return NULL;

    SnmpVersion pv;
    if (strcmp(version_str, "2c") == 0) pv = SNMP_V2C;
    else if (strcmp(version_str, "1") == 0) pv = SNMP_V1;
    else if (strcmp(version_str, "3") == 0) pv = SNMP_V3;
    else return NULL;

    CoreSnmp* self = (CoreSnmp*)calloc(1, sizeof(CoreSnmp));
    if (!self || !CoreSnmp_init_common(self, transport)) {
        if (self) RELEASE((Object*)self);
        return NULL;
    }
    self->version = pv;
    strncpy(self->community, community, 63);
    self->community[63] = '\0';
    return self;
}

CoreSnmp* new_SnmpV3(SnmpTransport transport, const char* uname, SnmpSecLevel sl,
                     SnmpAuthProto ap, const uint8_t* ak, size_t akl,
                     SnmpPrivProto pp, const uint8_t* pk, size_t pkl) {
    if (!uname) return NULL;
    if (ak && akl > 32) return NULL;
    if (pk && pkl > 32) return NULL;

    CoreSnmp* self = (CoreSnmp*)calloc(1, sizeof(CoreSnmp));
    if (!self || !CoreSnmp_init_common(self, transport)) {
        if (self) RELEASE((Object*)self);
        return NULL;
    }
    self->version = SNMP_V3;
    self->sec_level = sl;
    self->auth_proto = ap;
    self->priv_proto = pp;
    strncpy(self->username, uname, 63);
    self->username[63] = '\0';
    if (ak && akl > 0) memcpy(self->auth_key, ak, akl);
    if (pk && pkl > 0) memcpy(self->priv_key, pk, pkl);
    return self;
}