#include "app_context.h"
#include "config.h"
#include "path_validator.h"
#include "db.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>

extern Logger* logger;

AppContext* g_app = NULL;

static void AppContext_finalize(Object* obj) {
    AppContext* self = (AppContext*)obj;
    if (logger && logger->info) {
        logger->info(logger, "[APP] AppContext destroyed. The Empire rests.");
    }
    RELEASE_NULL(self->reg);
    RELEASE_NULL(self->runtime);
    RELEASE_NULL(self->config);
}

static const Class AppContext_Class = {
    .name     = "AppContext",
    .size     = sizeof(AppContext),
    .finalize = AppContext_finalize
};

static bool AppContext_init(AppContext* self,
                             const char* config_file) {
    if (!self) {
        return false;
    }

    self->initialized = true;

    /* ── 1. 설정 파일 파싱 ── */
    if (config_file) {
        if (!self->config->load(self->config, config_file)) {
            if (logger && logger->warn) {
                logger->warn(logger,
                    "[APP] Config file not found: %s — using defaults.",
                    config_file);
            }
        }
    }

    /* ── 2. PathValidator 싱글턴 등록 ── */
    PathValidator* pv = new_PathValidator();
    if (!pv) {
        if (logger && logger->error) {
            logger->error(logger,
                "[APP] FATAL: PathValidator allocation failed.");
        }
        self->initialized = false;
        return false;
    }
    self->registerService(self, &PathValidator_Class, (Object*)pv);
    RELEASE((Object*)pv);

    /* ── 3. DB 연결 (선택사항 — db_host 설정 시에만) ── */
    const char* db_host = self->config->getString(
        self->config, "db_host", NULL);

    if (db_host) {
        const char* db_name = self->config->getString(
            self->config, "db_name", "toostalk");
        const char* db_user = self->config->getString(
            self->config, "db_user", "root");
        const char* db_pass = self->config->getString(
            self->config, "db_pass", "");
        const char* db_charset = self->config->getString(
            self->config, "db_charset", "utf8mb4");
        const char* db_type = self->config->getString(
            self->config, "db_type", "MYSQL");
        int db_port = self->config->getInt(
            self->config, "db_port", 3306);

        DBClient* db = new_DBClientDirect(
            db_host, db_name, db_user, db_pass,
            db_port, db_charset, db_type);

        if (!db || db->isConnected == 0) {
            if (logger && logger->error) {
                logger->error(logger,
                    "[APP] FATAL: DB connection failed (%s:%d/%s).",
                    db_host, db_port, db_name);
            }
            if (db) {
                RELEASE((Object*)db);
            }
            self->initialized = false;
            return false;
        }
        self->registerService(self, &DBClient_Class, (Object*)db);
        RELEASE((Object*)db);

        if (logger && logger->info) {
            logger->info(logger,
                "[APP] DB connected: %s:%d/%s", db_host, db_port, db_name);
        }
    } else {
        if (logger && logger->info) {
            logger->info(logger,
                "[APP] No db_host config — DB skipped.");
        }
    }

    /* ── 4. 업로드 디렉토리 runtime 등록 ── */
    const char* upload_dir = self->config->getString(
        self->config, "upload_dir", "/var/toostalk/uploads");
    self->runtime->setString(self->runtime, "upload_dir", upload_dir);

    if (logger && logger->info) {
        logger->info(logger, "[APP] AppContext initialized.");
    }

    return true;
}

static void AppContext_destroy_all(AppContext* self) {
    if (!self) {
        return;
    }
    if (g_app == self) {
        g_app = NULL;
    }
    RELEASE((Object*)self);
}

static void AppContext_setConfig(AppContext* self,
                                  const char* key,
                                  const char* val) {
    if (!self || !self->config) {
        return;
    }
    self->config->setString(self->config, key, val);
}

static const char* AppContext_getConfig(AppContext* self,
                                         const char* key) {
    if (!self || !self->config) {
        return NULL;
    }
    return self->config->getString(self->config, key, NULL);
}

static int AppContext_getConfigInt(AppContext* self,
                                    const char* key) {
    if (!self || !self->config) {
        return 0;
    }
    return self->config->getInt(self->config, key, 0);
}

static void AppContext_registerService(AppContext*   self,
                                        const Class* cls,
                                        Object*      service) {
    if (!self || !cls || !service) {
        return;
    }
    if (!self->initialized) {
        if (logger && logger->error) {
            logger->error(logger,
                "[APP] ERROR: init() must be called before registerService!");
        }
        return;
    }
    if (self->reg) {
        self->reg->register_s(self->reg, cls, service);
    }
}

static Object* AppContext_getService(AppContext*   self,
                                      const Class* cls) {
    if (!self || !self->reg) {
        return NULL;
    }
    return self->reg->get(self->reg, cls);
}

AppContext* new_AppContext(void) {
    AppContext* self = (AppContext*)calloc(1, sizeof(AppContext));
    if (!self) {
        return NULL;
    }

    Object_Init((Object*)self, &AppContext_Class);

    self->config  = new_Config();
    self->runtime = new_Context();
    self->reg     = new_ServiceRegistry();

    if (!self->config || !self->runtime || !self->reg) {
        if (logger && logger->error) {
            logger->error(logger,
                "[APP] FATAL: OOM during AppContext allocation.");
        }
        RELEASE((Object*)self);
        return NULL;
    }

    self->initialized = false;

    self->init            = AppContext_init;
    self->destroy_all     = AppContext_destroy_all;
    self->setConfig       = AppContext_setConfig;
    self->getConfig       = AppContext_getConfig;
    self->getConfigInt    = AppContext_getConfigInt;
    self->registerService = AppContext_registerService;
    self->getService      = AppContext_getService;

    return self;
}