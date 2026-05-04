#include "service_registry.h"
#include <stdlib.h>
#include <stdio.h>

static char* make_key(const Class* cls, char* buf, size_t size) {
    if (!cls || !buf || size == 0) return NULL;
    snprintf(buf, size, "%p", (void*)cls);
    return buf;
}

static void ServiceRegistry_finalize(Object* obj) {
    ServiceRegistry* self = (ServiceRegistry*)obj;
    if (self->services) RELEASE((Object*)self->services);
    pthread_mutex_destroy(&self->lock);
}

static Class ServiceRegistry_Class = {
	.name = "ServiceRegistry",
	.size = sizeof(ServiceRegistry),
	.finalize = ServiceRegistry_finalize
};

static void registry_register(ServiceRegistry* self, const Class* cls, Object* service) {
    if (!self || !cls || !service) return;
    char key[32];
    if (!make_key(cls, key, sizeof(key))) return;
    pthread_mutex_lock(&self->lock);
    self->services->put(self->services, key, service);
    pthread_mutex_unlock(&self->lock);
}

static Object* registry_get(ServiceRegistry* self, const Class* cls) {
    if (!self || !cls) return NULL;
    char key[32]; if (!make_key(cls, key, sizeof(key))) return NULL;
    pthread_mutex_lock(&self->lock);
    Object* obj = self->services->get(self->services, key);
    pthread_mutex_unlock(&self->lock);
    return obj;
}

static bool registry_has(ServiceRegistry* self, const Class* cls) {
    if (!self || !cls) return false;
    char key[32]; if (!make_key(cls, key, sizeof(key))) return false;
    pthread_mutex_lock(&self->lock);
    bool exists = self->services->hasKey(self->services, key); // containsKey -> hasKey ✅
    pthread_mutex_unlock(&self->lock);
    return exists;
}

static void registry_unregister(ServiceRegistry* self, const Class* cls) {
    if (!self || !cls) return;
    char key[32]; if (!make_key(cls, key, sizeof(key))) return;
    pthread_mutex_lock(&self->lock);
    self->services->remove(self->services, key);
    pthread_mutex_unlock(&self->lock);
}

ServiceRegistry* new_ServiceRegistry() {
    ServiceRegistry* self = (ServiceRegistry*)calloc(1, sizeof(ServiceRegistry));
    if (!self) return NULL;
    Object_Init((Object*)self, &ServiceRegistry_Class);
    self->services = new_HashMap(32);
    pthread_mutex_init(&self->lock, NULL);

    self->register_s = registry_register;
    self->get = registry_get;
    self->has = registry_has;
    self->unregister = registry_unregister;
    return self;
}