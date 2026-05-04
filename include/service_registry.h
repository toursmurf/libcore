#ifndef SERVICE_REGISTRY_H
#define SERVICE_REGISTRY_H

#include "object.h"
#include "hashmap.h"
#include <pthread.h>
#include <stdbool.h>

typedef struct ServiceRegistry ServiceRegistry;

struct ServiceRegistry {
    Object base;
    HashMap* services;
    pthread_mutex_t lock; // 직접 내장

    void    (*register_s)(ServiceRegistry* self, const Class* cls, Object* service);
    Object* (*get)      (ServiceRegistry* self, const Class* cls);
    bool    (*has)      (ServiceRegistry* self, const Class* cls);
    void    (*unregister)(ServiceRegistry* self, const Class* cls);
};

ServiceRegistry* new_ServiceRegistry();

// 🚨 매크로: 타입 안전 조회 ✅
#define REG_GET(reg, TYPE) \
    ((TYPE*) (reg)->get((reg), &TYPE##_Class))

#endif