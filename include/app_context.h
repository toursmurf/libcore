#ifndef APP_CONTEXT_H
#define APP_CONTEXT_H

#include "object.h"
#include "context.h"
#include "config.h"
#include "service_registry.h"
#include <stdbool.h>

typedef struct AppContext AppContext;

struct AppContext {
    Object base;

    Config*          config;   /* [OWNED] 설정 파일 파싱용 */
    Context*         runtime;  /* [OWNED] 런타임 상태 저장용 */
    ServiceRegistry* reg;      /* [OWNED] 서비스 레지스트리 */

    bool initialized;

    bool (*init)       (AppContext* self, const char* config_path);
    void (*destroy_all)(AppContext* self);

    void        (*setConfig)   (AppContext* self, const char* key, const char* val);
    const char* (*getConfig)   (AppContext* self, const char* key);
    int         (*getConfigInt)(AppContext* self, const char* key);

    void    (*registerService)(AppContext* self, const Class* cls, Object* service);
    Object* (*getService)     (AppContext* self, const Class* cls);
};

AppContext* new_AppContext(void);

/* 유일한 전역 사령관 */
extern AppContext* g_app;

#endif /* APP_CONTEXT_H */