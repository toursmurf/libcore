#ifndef APP_CONTEXT_H
#define APP_CONTEXT_H

#include "object.h"
#include "context.h"
#include "service_registry.h"
#include <stdbool.h>

typedef struct AppContext AppContext;

struct AppContext {
    Object base;

    Context* config;
    Context* runtime;
    ServiceRegistry* reg;

    bool initialized;

    bool (*init)       (AppContext* self, const char* config_path); // 🚀 파라미터 일치!
    void (*destroy_all)(AppContext* self);

    void    (*setConfig)   (AppContext* self, const char* key, const char* val);
    String* (*getConfig)   (AppContext* self, const char* key);
    int     (*getConfigInt)(AppContext* self, const char* key);

    void    (*registerService)(AppContext* self, const Class* cls, Object* service);
    Object* (*getService)     (AppContext* self, const Class* cls);
};

AppContext* new_AppContext();

// 🚨 유일한 전역 사령관 선언 ✅
extern AppContext* g_app;

#endif // APP_CONTEXT_H