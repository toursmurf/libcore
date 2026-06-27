#include "coresnmp.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

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

    if (!self) {
        return NULL;
    }

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
    if (!self) {
        return ERR_INVALID;
    }

    if (self->trap_receiver) {
        RELEASE_NULL(self->trap_receiver);
    }

    char url[64];
    snprintf(url, sizeof(url), "%s0.0.0.0:%d", (self->transport == SNMP_TRANS_UDP ? "udp://" : "tcp://"), port);

    self->trap_receiver = createServer(url, NULL);

    if (!self->trap_receiver) {
        return ERR_NET_CONNECT;
    }

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
    if (self) {
        self->trap_port = port;
    }
}

static void setAgentPort_impl(CoreSnmp* self, int port) {
    if (self) {
        self->agent_port = port;
    }
}

static inline int get_version_val(SnmpVersion v) {
    return (v == SNMP_V3) ? 3 : (v == SNMP_V2C) ? 1 : 0;
}

static inline const char* get_sec_name(CoreSnmp* self) {
    return (self->version == SNMP_V3) ? self->username : self->community;
}

static ErrorCode sendGet_impl(CoreSnmp* self, const char* ip, const char* oid, void* out, size_t sz, size_t* out_len) {
    if (!self || !self->snmp_sender || !ip || !oid) {
        return ERR_INVALID;
    }

    uint8_t pdu[1024];
    size_t pdu_len = snmp_asn_encode_pdu(pdu, sizeof(pdu), 0xA0, get_version_val(self->version), get_sec_name(self), oid, 0, 0, NULL);

    if (self->snmp_sender->send(self->snmp_sender, pdu, pdu_len, ip, self->agent_port) < 0) {
        return ERR_NET_CONNECT;
    }

    char recv_ip[64];
    int port;
    ssize_t recvd = self->snmp_sender->recv(self->snmp_sender, out, sz, recv_ip, &port);

    if (recvd > 0) {
        if (out_len) {
            *out_len = (size_t)recvd;
        }
        return OK;
    }

    return ERR_NET_TIMEOUT;
}

static ErrorCode sendGetNext_impl(CoreSnmp* self, const char* ip, const char* oid, void* out, size_t sz, size_t* out_len) {
    if (!self || !self->snmp_sender || !ip || !oid) {
        return ERR_INVALID;
    }

    uint8_t pdu[1024];
    size_t pdu_len = snmp_asn_encode_pdu(pdu, sizeof(pdu), 0xA1, get_version_val(self->version), get_sec_name(self), oid, 0, 0, NULL);

    if (self->snmp_sender->send(self->snmp_sender, pdu, pdu_len, ip, self->agent_port) < 0) {
        return ERR_NET_CONNECT;
    }

    char recv_ip[64];
    int port;
    ssize_t recvd = self->snmp_sender->recv(self->snmp_sender, out, sz, recv_ip, &port);

    if (recvd > 0) {
        if (out_len) {
            *out_len = (size_t)recvd;
        }
        return OK;
    }

    return ERR_NET_TIMEOUT;
}

/* 🚨 재시도 루프 완전 제거 적용 완료! */
static ErrorCode sendGetBulk_impl(CoreSnmp* self, const char* ip, const char* oid, int non_repeaters, int max_repetitions, ArrayList* out_varbinds) {
    if (!self || !self->snmp_sender || !ip || !oid) {
        return ERR_INVALID;
    }

    uint8_t pdu[1024];
    size_t pdu_len = snmp_asn_encode_pdu(pdu, sizeof(pdu), 0xA5, get_version_val(self->version), get_sec_name(self), oid, non_repeaters, max_repetitions, NULL);

    if (self->snmp_sender->send(self->snmp_sender, pdu, pdu_len, ip, self->agent_port) < 0) {
        return ERR_NET_CONNECT;
    }

    char recv_ip[64];
    uint8_t raw_out[4096];
    int port;

    ssize_t recvd = self->snmp_sender->recv(self->snmp_sender, raw_out, sizeof(raw_out), recv_ip, &port);

    if (recvd > 0) {
        if (out_varbinds) {
            snmp_asn_decode_response(raw_out, (size_t)recvd, out_varbinds);
        }

        if (out_varbinds && out_varbinds->getSize(out_varbinds) == 0) {
            return ERR_NET_TIMEOUT;
        }

        return OK;
    }

    return ERR_NET_TIMEOUT;
}

static bool is_prefix_match(const char* root_oid, const char* current_oid) {
    return (strncmp(root_oid, current_oid, strlen(root_oid)) == 0);
}

static ErrorCode snmpWalk_impl(CoreSnmp* self, const char* ip, const char* root_oid, ArrayList* out_all) {
    if (!self || !ip || !root_oid || !out_all) {
        return ERR_INVALID;
    }

    char last_oid[256];
    strncpy(last_oid, root_oid, sizeof(last_oid) - 1);

    while(true) {
        ArrayList* bulk = new_ArrayList(32);

        ErrorCode ret = self->sendGetBulk(self, ip, last_oid, 0, 32, bulk);

        if (ret != OK) {
            RELEASE_NULL(bulk);
            return ret;
        }

        bool keep = false;

        for(int i = 0; i < bulk->getSize(bulk); i++) {
            SnmpVarBind* vb = (SnmpVarBind*)bulk->get(bulk, i);

            if(is_prefix_match(root_oid, vb->oid)) {
                SnmpVarBind* copy = new_SnmpVarBind(vb->tag, vb->oid, vb->value_str);

                out_all->add(out_all, (Object*)copy);
                RELEASE_NULL(copy);

                size_t len = strlen(vb->oid);

                if (len >= sizeof(last_oid))
                  len = sizeof(last_oid) - 1;

                memcpy(last_oid, vb->oid, len);
                last_oid[len] = '\0';
                keep = true;
            } else {
                keep = false;
                break;
            }
        }

        RELEASE_NULL(bulk);

        if (!keep) {
            break;
        }
    }

    return OK;
}

static ErrorCode sendSet_impl(CoreSnmp* self, const char* ip, const char* oid, const char* value) {
    if (!self || !self->snmp_sender || !ip || !oid) {
        return ERR_INVALID;
    }

    uint8_t pdu[1024];
    size_t pdu_len = snmp_asn_encode_pdu(pdu, sizeof(pdu), 0xA3, get_version_val(self->version), get_sec_name(self), oid, 0, 0, value);

    if (self->snmp_sender->send(self->snmp_sender, pdu, pdu_len, ip, self->agent_port) < 0) {
        return ERR_NET_CONNECT;
    }

    char recv_ip[64];
    uint8_t dummy[512];
    int port;

    ssize_t recvd = self->snmp_sender->recv(self->snmp_sender, dummy, sizeof(dummy), recv_ip, &port);

    return (recvd > 0) ? OK : ERR_NET_TIMEOUT;
}

static ErrorCode sendTrap_impl(CoreSnmp* self, const char* ip, const char* oid) {
    if (!self || !self->snmp_sender || !ip || !oid) {
        return ERR_INVALID;
    }

    uint8_t pdu[1024];
    size_t pdu_len = snmp_asn_encode_pdu(pdu, sizeof(pdu), 0xA4, get_version_val(self->version), get_sec_name(self), oid, 0, 0, NULL);

    return (self->snmp_sender->send(self->snmp_sender, pdu, pdu_len, ip, self->trap_port) > 0) ? OK : ERR_NET_CONNECT;
}

static ErrorCode sendInform_impl(CoreSnmp* self, const char* ip, const char* oid) {
    if (!self || !self->snmp_sender || !ip || !oid) {
        return ERR_INVALID;
    }

    uint8_t pdu[1024];
    size_t pdu_len = snmp_asn_encode_pdu(pdu, sizeof(pdu), 0xA6, get_version_val(self->version), get_sec_name(self), oid, 0, 0, NULL);

    if (self->snmp_sender->send(self->snmp_sender, pdu, pdu_len, ip, self->trap_port) < 0) {
        return ERR_NET_CONNECT;
    }

    char recv_ip[64];
    uint8_t ack[512];
    int port;

    ssize_t recvd = self->snmp_sender->recv(self->snmp_sender, ack, sizeof(ack), recv_ip, &port);

    return (recvd > 0) ? OK : ERR_NET_TIMEOUT;
}

static bool setOid_impl(CoreSnmp* self, const char* oid, const char* desc) {
    if (!self || !oid || !desc) {
        return false;
    }

    String* d = new_String(desc);

    if (!d) {
        return false;
    }

    self->oid_map->put(self->oid_map, oid, (Object*)d);
    RELEASE((Object*)d);

    return true;
}

static size_t getTrapCount_impl(CoreSnmp* self) {
    if (!self) {
        return 0;
    }

    return atomic_load(&self->trap_count);
}

static void resetStats_impl(CoreSnmp* self) {
    if (self) {
        atomic_store(&self->trap_count, 0);
    }
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
    self->snmpWalk     = snmpWalk_impl;
    self->sendSet      = sendSet_impl;
    self->sendTrap     = sendTrap_impl;
    self->sendInform   = sendInform_impl;

    self->setOid       = setOid_impl;
    self->getTrapCount = getTrapCount_impl;
    self->resetStats   = resetStats_impl;

    self->transport    = transport;
    self->agent_port   = 161;
    self->snmp_sender  = createSyncClient((transport == SNMP_TRANS_UDP ? "udp://" : "tcp://"), NULL);

    if (self->snmp_sender) {
        int fd = self->snmp_sender->getFD(self->snmp_sender);
        struct timeval tv = {3, 0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }

    self->oid_map = new_HashMap(16);

    return (self->snmp_sender && self->oid_map);
}

CoreSnmp* new_Snmp(SnmpTransport transport, const char* version_str, const char* community) {
    if (!version_str || !community) {
        return NULL;
    }

    CoreSnmp* self = (CoreSnmp*)calloc(1, sizeof(CoreSnmp));

    if (!self || !CoreSnmp_init_common(self, transport)) {
        if (self) {
            RELEASE((Object*)self);
        }
        return NULL;
    }

    self->version = (strcmp(version_str, "2c") == 0) ? SNMP_V2C :
                    (strcmp(version_str, "3") == 0) ? SNMP_V3 : SNMP_V1;

    strncpy(self->community, community, 63);

    return self;
}

CoreSnmp* new_SnmpV3(SnmpTransport transport, const char* uname, SnmpSecLevel sl,
                     SnmpAuthProto ap, const uint8_t* ak, size_t akl,
                     SnmpPrivProto pp, const uint8_t* pk, size_t pkl) {
    if (!uname || (ak && akl > 32) || (pk && pkl > 32)) {
        return NULL;
    }

    CoreSnmp* self = (CoreSnmp*)calloc(1, sizeof(CoreSnmp));

    if (!self || !CoreSnmp_init_common(self, transport)) {
        if (self) {
            RELEASE((Object*)self);
        }
        return NULL;
    }

    self->version = SNMP_V3;
    self->sec_level = sl;
    self->auth_proto = ap;
    self->priv_proto = pp;

    strncpy(self->username, uname, 63);
    self->username[63] = '\0';

    if (ak && akl > 0) {
        memcpy(self->auth_key, ak, akl);
    }

    if (pk && pkl > 0) {
        memcpy(self->priv_key, pk, pkl);
    }

    return self;
}