#include "app_context.h"
#include "logger.h"
#include <stdlib.h>

// 🚨 로거는 메인에서 초기화될 예정이므로 extern으로만 참조
extern Logger *logger;

// 🚨 g_app 전역 변수 실체화
AppContext* g_app = NULL;

static void AppContext_finalize(Object* obj) {
    AppContext* self = (AppContext*)obj;

    // 🚨 Logger 유언장 선출력 (하위 컨텍스트 소멸 전에 기록 보장)
    if (logger && logger->info) {
        logger->info(logger, "[APP] AppContext destroyed. The Empire rests.");
    }

    // 🚨 의존성 역순에 따른 RELEASE_NULL 체인 (Dangling Pointer 원천 차단)
    // ❌ 캐스팅((Object**)&) 완전 제거!!
    RELEASE_NULL(self->reg);
    RELEASE_NULL(self->runtime);
    RELEASE_NULL(self->config);
}

// 🚨 static const Class 적용 (.rodata 읽기 전용 메모리 박제)
static const Class AppContext_Class = {
    .name = "AppContext",
    .size = sizeof(AppContext),
    .finalize = AppContext_finalize
};

static bool AppContext_init(AppContext* self, const char* config_file) {
    if (!self) return false;

    // TODO: config_file을 파싱하여 self->config에 적재 (내일 연차 이후 구현!)

    self->initialized = true;
    if (logger && logger->info) {
        logger->info(logger, "[APP] AppContext initialized successfully with [%s].",
                     config_file ? config_file : "default");
    }
    return true;
}

static void AppContext_destroy_all(AppContext* self) {
    if (!self) return;
    if (g_app == self) g_app = NULL; // 🚨 안전장치: 전역 포인터 먼저 해제
    RELEASE((Object*)self);          // 그 후 본체 소각
}

static void AppContext_setConfig(AppContext* self, const char* key, const char* val) {
    if (self && self->config) self->config->setString(self->config, key, val);
}

static String* AppContext_getConfig(AppContext* self, const char* key) {
    return (self && self->config) ? self->config->getString(self->config, key) : NULL;
}

static int AppContext_getConfigInt(AppContext* self, const char* key) {
    return (self && self->config) ? self->config->getInt(self->config, key) : 0;
}

static void AppContext_registerService(AppContext* self, const Class* cls, Object* service) {
    if (!self || !cls || !service) return;

    // 🚨 Fail-Fast! 초기화 검증 로직
    if (!self->initialized) {
        if (logger && logger->error) {
            logger->error(logger, "[APP] ERROR: AppContext::init() must be called before registerService!");
        }
        return;
    }

    if (self->reg) self->reg->register_s(self->reg, cls, service);
}

static Object* AppContext_getService(AppContext* self, const Class* cls) {
    return (self && self->reg) ? self->reg->get(self->reg, cls) : NULL;
}

AppContext* new_AppContext(void) {
    AppContext* self = (AppContext*)calloc(1, sizeof(AppContext));
    if (!self) return NULL;

    Object_Init((Object*)self, &AppContext_Class);

    self->config  = new_Context();
    self->runtime = new_Context();
    self->reg     = new_ServiceRegistry();

    // 🚨 [최종 승인 OOM 방어선]: 내부 모듈 할당 실패 시 즉각 롤백
    if (!self->config || !self->runtime || !self->reg) {
        if (logger && logger->error) {
            logger->error(logger, "[APP] FATAL: OOM during AppContext internal allocation. Rolling back.");
        }
        // 하위 모듈이 부분적으로 할당되었더라도 RELEASE 단일 호출로 finalize 내에서 안전하게 소각됨
        RELEASE((Object*)self);
        return NULL;
    }

    self->initialized = false;

    self->init = AppContext_init;
    self->destroy_all = AppContext_destroy_all;
    self->setConfig = AppContext_setConfig;
    self->getConfig = AppContext_getConfig;
    self->getConfigInt = AppContext_getConfigInt;
    self->registerService = AppContext_registerService;
    self->getService = AppContext_getService;

    return self;
}