#include "coresnmp.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static void SnmpTrap_finalize(Object* obj) { }

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
    RELEASE_NULL((Object**)&self->trap_receiver);
    RELEASE_NULL((Object**)&self->snmp_sender);
    RELEASE_NULL((Object**)&self->oid_map);
    RELEASE_NULL((Object**)&self->oid_lock);
}

const Class CoreSnmp_Class_Instance = {
    .name     = "CoreSnmp",
    .size     = sizeof(CoreSnmp),
    .finalize = CoreSnmp_finalize
};

static CoreResult startListen_impl(CoreSnmp* self, int port) {
    if (!self || !self->trap_receiver) return CORE_ERR_INVALID;
    self->trap_port = port;
    if (self->trap_receiver->bind) {
        return self->trap_receiver->bind(self->trap_receiver, "0.0.0.0", port) == 0 ? CORE_OK : CORE_ERR_NETWORK;
    }
    return CORE_ERR_INVALID;
}

static void stopListen_impl(CoreSnmp* self) {
    if (self && self->trap_receiver) {
        self->trap_receiver->close(self->trap_receiver);
    }
}

static void setTrapPort_impl(CoreSnmp* self, int port) {
    if (self) self->trap_port = port;
}

static inline int get_version_val(SnmpVersion v) {
    if (v == SNMP_V3) return 3;
    if (v == SNMP_V2C) return 1;
    return 0;
}

static inline const char* get_sec_name(CoreSnmp* self) {
    return (self->version == SNMP_V3) ? self->username : self->community;
}

static CoreResult sendGet_impl(CoreSnmp* self, const char* ip, const char* oid, void* out, size_t sz, size_t* out_len) {
    if (!self || !self->snmp_sender)
      return CORE_ERR_INVALID;
    uint8_t pdu[1024];
    size_t pdu_len = snmp_asn_encode_pdu(pdu, sizeof(pdu), 0xA0, get_version_val(self->version), get_sec_name(self), oid, 0, 0, NULL);
    ssize_t sent = self->snmp_sender->send(self->snmp_sender, pdu, pdu_len, ip, self->agent_port);
    if (sent < 0) return CORE_ERR_NETWORK;
    int port;
    ssize_t recvd = self->snmp_sender->recv(self->snmp_sender, out, sz, (char*)ip, &port);
    if (recvd > 0) {
        if (out_len) *out_len = (size_t)recvd;
        return CORE_OK;
    }
    return CORE_ERR_TIMEOUT;
}

static CoreResult sendGetNext_impl(CoreSnmp* self, const char* ip, const char* oid, void* out, size_t sz, size_t* out_len) {
    if (!self || !self->snmp_sender) return CORE_ERR_INVALID;
    uint8_t pdu[1024];
    size_t pdu_len = snmp_asn_encode_pdu(pdu, sizeof(pdu), 0xA1, get_version_val(self->version), get_sec_name(self), oid, 0, 0, NULL);
    ssize_t sent = self->snmp_sender->send(self->snmp_sender, pdu, pdu_len, ip, self->agent_port);
    if (sent < 0) return CORE_ERR_NETWORK;
    int port;
    ssize_t recvd = self->snmp_sender->recv(self->snmp_sender, out, sz, (char*)ip, &port);
    if (recvd > 0) {
        if (out_len) *out_len = (size_t)recvd;
        return CORE_OK;
    }
    return CORE_ERR_TIMEOUT;
}

static CoreResult sendGetBulk_impl(CoreSnmp* self, const char* ip, const char* oid, int non_repeaters, int max_repetitions, ArrayList* out_varbinds) {
    if (!self || !self->snmp_sender) return CORE_ERR_INVALID;
    uint8_t pdu[1024];
    size_t pdu_len = snmp_asn_encode_pdu(pdu, sizeof(pdu), 0xA5, get_version_val(self->version), get_sec_name(self), oid, non_repeaters, max_repetitions, NULL);
    ssize_t sent = self->snmp_sender->send(self->snmp_sender, pdu, pdu_len, ip, self->agent_port);
    if (sent < 0) return CORE_ERR_NETWORK;
    uint8_t raw_out[4096];
    int port;
    ssize_t recvd = self->snmp_sender->recv(self->snmp_sender, raw_out, sizeof(raw_out), (char*)ip, &port);
    if (recvd > 0) {
        if (out_varbinds) snmp_asn_decode_response(raw_out, (size_t)recvd, out_varbinds);
        return CORE_OK;
    }
    return CORE_ERR_TIMEOUT;
}

static CoreResult sendSet_impl(CoreSnmp* self, const char* ip, const char* oid, const char* value) {
    if (!self || !self->snmp_sender) return CORE_ERR_INVALID;
    uint8_t pdu[1024];
    size_t pdu_len = snmp_asn_encode_pdu(pdu, sizeof(pdu), 0xA3, get_version_val(self->version), get_sec_name(self), oid, 0, 0, value);
    ssize_t sent = self->snmp_sender->send(self->snmp_sender, pdu, pdu_len, ip, self->agent_port);
    if (sent < 0) return CORE_ERR_NETWORK;
    uint8_t dummy[512];
    int port;
    ssize_t recvd = self->snmp_sender->recv(self->snmp_sender, dummy, sizeof(dummy), (char*)ip, &port);
    return (recvd > 0) ? CORE_OK : CORE_ERR_TIMEOUT;
}

static CoreResult sendTrap_impl(CoreSnmp* self, const char* ip, const char* oid) {
    if (!self || !self->snmp_sender) return CORE_ERR_INVALID;
    uint8_t pdu[1024];
    size_t pdu_len = snmp_asn_encode_pdu(pdu, sizeof(pdu), 0xA4, get_version_val(self->version), get_sec_name(self), oid, 0, 0, NULL);
    ssize_t sent = self->snmp_sender->send(self->snmp_sender, pdu, pdu_len, ip, self->trap_port);
    return (sent > 0) ? CORE_OK : CORE_ERR_NETWORK;
}

static CoreResult sendInform_impl(CoreSnmp* self, const char* ip, const char* oid) {
    if (!self || !self->snmp_sender) return CORE_ERR_INVALID;
    uint8_t pdu[1024];
    size_t pdu_len = snmp_asn_encode_pdu(pdu, sizeof(pdu), 0xA6, get_version_val(self->version), get_sec_name(self), oid, 0, 0, NULL);
    ssize_t sent = self->snmp_sender->send(self->snmp_sender, pdu, pdu_len, ip, self->trap_port);
    if (sent < 0) return CORE_ERR_NETWORK;
    uint8_t ack[512];
    int port;
    ssize_t recvd = self->snmp_sender->recv(self->snmp_sender, ack, sizeof(ack), (char*)ip, &port);
    return (recvd > 0) ? CORE_OK : CORE_ERR_TIMEOUT;
}

static bool setOid_impl(CoreSnmp* self, const char* oid, const char* desc) {
    if (!self || !oid || !desc) return false;
    self->oid_lock->writeLock(self->oid_lock);
    String* desc_str = new_String(desc);
    bool res = self->oid_map->put(self->oid_map, oid, (Object*)desc_str);
    RELEASE((Object*)desc_str);
    self->oid_lock->writeUnlock(self->oid_lock);
    return res;
}

static String* getOidDesc_impl(CoreSnmp* self, const char* oid) {
    if (!self || !oid) return NULL;
    self->oid_lock->readLock(self->oid_lock);
    String* desc = (String*)self->oid_map->get(self->oid_map, oid);
    if (desc) RETAIN((Object*)desc);
    self->oid_lock->readUnlock(self->oid_lock);
    return desc;
}

static bool walkOid_impl(CoreSnmp* self, const char* ip, const char* root_oid) {
    if (!self || !self->snmp_sender) return false;
    size_t dummy_len; uint8_t buf[2048];
    return (self->sendGetNext(self, ip, root_oid, buf, 2048, &dummy_len) == CORE_OK);
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

    if (transport == SNMP_TRANS_UDP) {
        self->trap_receiver = createServer("udp://0.0.0.0:162", NULL);
        self->snmp_sender   = createClient("udp://", NULL);
    } else {
        self->trap_receiver = createServer("tcp://0.0.0.0:162", NULL);
        self->snmp_sender   = createClient("tcp://", NULL);
    }
    self->oid_map  = new_HashMap(16);
    self->oid_lock = new_RWLock();
    if (!self->trap_receiver || !self->snmp_sender || !self->oid_map || !self->oid_lock)
      return false;
    self->trap_port  = 162;
    self->agent_port = 161;
    atomic_init(&self->trap_count, 0);
    return true;
}

CoreSnmp* new_Snmp(SnmpTransport transport, const char* version_str, const char* community) {
    if (!version_str || !community) return NULL;
    SnmpVersion pv;
    if (strcmp(version_str, "2c") == 0) pv = SNMP_V2C;
    else if (strcmp(version_str, "1") == 0) pv = SNMP_V1;
    else return NULL;
    CoreSnmp* self = (CoreSnmp*)calloc(1, sizeof(CoreSnmp));
    if (!self) return NULL;
    if (!CoreSnmp_init_common(self, transport)) { RELEASE((Object*)self); return NULL; }
    self->version = pv;
    strncpy(self->community, community, sizeof(self->community) - 1);
    self->community[sizeof(self->community) - 1] = '\0';
    return self;
}

CoreSnmp* new_SnmpV3(SnmpTransport transport, const char* uname, SnmpSecLevel sl,
                     SnmpAuthProto ap, const uint8_t* ak, size_t akl,
                     SnmpPrivProto pp, const uint8_t* pk, size_t pkl) {
    if (!uname) return NULL;
    if (ak && akl > 32) return NULL;
    if (pk && pkl > 32) return NULL;
    CoreSnmp* self = (CoreSnmp*)calloc(1, sizeof(CoreSnmp));
    if (!self) return NULL;
    if (!CoreSnmp_init_common(self, transport)) {
      RELEASE((Object*)self);
      return NULL;
    }
    self->version = SNMP_V3;
    self->sec_level = sl;
    self->auth_proto = ap;
    self->priv_proto = pp;
    strncpy(self->username, uname, sizeof(self->username) - 1);
    self->username[sizeof(self->username) - 1] = '\0';
    if (ak && akl > 0) memcpy(self->auth_key, ak, akl);
    if (pk && pkl > 0) memcpy(self->priv_key, pk, pkl);
    return self;
}