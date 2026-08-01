#ifndef APP_CONTEXT_H
#define APP_CONTEXT_H

#include "object.h"
#include "context.h"
#include "config.h"
#include "service_registry.h"
#include "path_validator.h"
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

    /* Config 조회 (읽기 전용) */
    const char* (*getConfig)   (AppContext* self, const char* key);
    int         (*getConfigInt)(AppContext* self, const char* key);

    /* ServiceRegistry 연동 (dbClientClass 같이 extern Class*만 가능) */
    void    (*registerService)(AppContext* self, const Class* cls, Object* service);
    Object* (*getService)     (AppContext* self, const Class* cls);
};

AppContext* new_AppContext(void);

/* PathValidator 싱글턴 접근자 */
/* (_PathValidator_Class 가 static 이라 ServiceRegistry 등록 불가 → 전역 함수로 제공) */
PathValidator* AppContext_getPathValidator(void);

/* 유일한 전역 사령관 */
extern AppContext* g_app;

#endif /* APP_CONTEXT_H */