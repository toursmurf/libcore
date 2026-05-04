#include "app_context.h"
#include "logger.h"
#include <stdlib.h>

// 🚨 로거는 메인에서 초기화될 예정이므로 extern으로만 참조 ✅
extern Logger *logger;

// 🚨 g_app 전역 변수 실체화 ✅
AppContext* g_app = NULL;

static void AppContext_finalize(Object* obj) {
    AppContext* self = (AppContext*)obj;
    if (self->config)  RELEASE((Object*)self->config);
    if (self->runtime) RELEASE((Object*)self->runtime);
    if (self->reg)     RELEASE((Object*)self->reg);

    // 🚨 로거 API 통일 스타일 (Null Guard 적용) ✅
    if (logger && logger->info) {
        logger->info(logger, "[APP] AppContext destroyed. The Empire rests.");
    }
}

static Class AppContext_Class = {
	.name = "AppContext",
	.size = sizeof(AppContext),
	.finalize = AppContext_finalize
};

static bool AppContext_init(AppContext* self) {
    if (!self) return false;
    self->initialized = true;
    if (logger && logger->info) {
        logger->info(logger, "[APP] AppContext initialized successfully.");
    }
    return true;
}

static void AppContext_destroy_all(AppContext* self) {
    if (!self) return;
    if (g_app == self) g_app = NULL;
    RELEASE((Object*)self);
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
    if (self && self->reg) self->reg->register_s(self->reg, cls, service);
}
static Object* AppContext_getService(AppContext* self, const Class* cls) {
    return (self && self->reg) ? self->reg->get(self->reg, cls) : NULL;
}

AppContext* new_AppContext() {
    AppContext* self = (AppContext*)calloc(1, sizeof(AppContext));
    if (!self) return NULL;
    Object_Init((Object*)self, &AppContext_Class);

    self->config  = new_Context();
    self->runtime = new_Context();
    self->reg     = new_ServiceRegistry();
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