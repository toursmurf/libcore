#include "db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <ctype.h>

/* =========================================================
 * off-by-one 제거, strncat 대신 memcpy 사용으로 정확한 공간 계산
 * ========================================================= */
int safe_append(char *dest, size_t dest_size, const char *src) {
    if (!dest || !src || dest_size == 0)
        return 0;

    size_t current_len = strnlen(dest, dest_size);
    if (current_len >= dest_size)
        return 0;

    size_t src_len = strlen(src);
    if (src_len > dest_size - current_len - 1)
        return 0;

    memcpy(dest + current_len, src, src_len + 1);
    return 1;
}

static void DB_setSaveLog_core(DBClient *self, int enable) {
    if (!self) return;
    self->save_log = enable;
}

static void DB_writeLog_core(DBClient *self, const char* msg, const char* sql, int is_error) {
    if (!self || !msg) return;
    if (!is_error && !self->save_log) return;

    pthread_mutex_lock(&self->lock);
    struct stat st = {0};
    if (stat(DEFAULT_LOG_DIR, &st) == -1) mkdir(DEFAULT_LOG_DIR, 0755);

    time_t t = time(NULL);
    struct tm ti_buf;
    struct tm *ti = localtime_r(&t, &ti_buf);
    if (!ti) { pthread_mutex_unlock(&self->lock); return; }

    char log_file[256];
    snprintf(log_file, sizeof(log_file), "%s/daemon.%04d%02d%02d.log",
        DEFAULT_LOG_DIR, ti->tm_year + 1900, ti->tm_mon + 1, ti->tm_mday);

    FILE *fp = fopen(log_file, "a+");
    if (fp) {
        fprintf(fp, "%04d-%02d-%02d %02d:%02d:%02d\t[%s]\t[%s@%s:%d]\t[%s]\t%s\t%s\n",
            ti->tm_year + 1900, ti->tm_mon + 1, ti->tm_mday,
            ti->tm_hour, ti->tm_min, ti->tm_sec,
            self->db_type, self->dbid, self->host, self->port,
            is_error ? "ERROR" : "INFO", msg, sql ? sql : "");
        fclose(fp);
    }
    pthread_mutex_unlock(&self->lock);
}

static void DBClient_Finalize(Object *obj) {
    if (!obj) return;
    DBClient *self = (DBClient*)obj;

    if (self->disconnect) {
        self->disconnect(self);
    }

    if (self->schema_cache) {
        RELEASE(self->schema_cache);
        self->schema_cache = NULL;
    }
    pthread_mutex_destroy(&self->lock);
}

const Class dbClientClass = {
   .name = "DBClient",
   .size = sizeof(DBClient),
   .finalize = DBClient_Finalize
};

static void _init_base_methods(DBClient *db) {
    if (!db) return;
    db->setSaveLog = DB_setSaveLog_core;
    db->writeLog = DB_writeLog_core;
    db->save_log = 0;
    db->last_insert_id = 0;
    db->last_idx = 0;
    db->affected_rows = -1;
    db->in_transaction = 0;
    db->schema_cache = NULL;
    db->option_count = 0;

    /* 🚨 [FINAL] 일반 뮤텍스를 Recursive 뮤텍스로 교체! (데드락 방지) */
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&db->lock, &attr);
    pthread_mutexattr_destroy(&attr);
}

DBClient* new_DBClient() {
    FILE *fp = fopen(DEFAULT_DB_CONFIG, "r");
    if (!fp) return NULL;
    DBClient *db = calloc(1, sizeof(DBClient));
    if (!db) {
        fclose(fp);
        return NULL;
    }
    Object_Init((Object*)db, &dbClientClass);
    _init_base_methods(db);

    char line[256], val[128];
    int idx = 0;
    while (fgets(line, sizeof(line), fp)) {
        char *p = line;
        while (isspace((unsigned char)*p)) p++;
        if (*p == '#' || *p == ';' || *p == '\0') continue;
        if (sscanf(p, "%127s", val) == 1) {
            switch(idx) {
                case 0: snprintf(db->host, sizeof(db->host), "%.127s", val); break;
                case 1: snprintf(db->dbname, sizeof(db->dbname), "%.127s", val); break;
                case 2: snprintf(db->dbid, sizeof(db->dbid), "%.63s", val); break;
                case 3: snprintf(db->dbpass, sizeof(db->dbpass), "%.63s", val); break;
                case 4: db->port = atoi(val); break;
                case 5: snprintf(db->charset, sizeof(db->charset), "%.31s", val); break;
                case 6: snprintf(db->db_type, sizeof(db->db_type), "%.31s", val); break;
            }
            idx++;
        }
    }
    fclose(fp);

    if (idx < 7) {
        RELEASE((Object*)db);
        return NULL;
    }
    #if defined(HAVE_PGSQL)
        if (strncmp(db->db_type, "PGSQL", sizeof("PGSQL")) == 0) {
            bind_pgsql(db);
            return db;
        }
    #endif

    #if defined(HAVE_MYSQL)
        if (strncmp(db->db_type, "MYSQL", sizeof("MYSQL")) == 0) {
            bind_mysql(db);
            return db;
        }
    #endif

    #if defined(HAVE_SQLITE)
        if (strncmp(db->db_type, "SQLITE", sizeof("SQLITE")) == 0) {
            bind_sqlite(db);
            return db;
        }
    #endif

    /* 여기까지 오면 db_type이 지원되지 않거나
     * 해당 backend가 컴파일되지 않은 것 */
    fprintf(stderr,
        "[FATAL] DBClient: db_type '%s' not supported "
        "or not compiled in.\n",
        db->db_type);
    RELEASE((Object*)db);
    return NULL;
}

DBClient* new_DBClientDirect(const char* h, const char* dbn, const char* id, const char* pw, int port, const char* cs, const char* type) {
    DBClient *db = calloc(1, sizeof(DBClient));
    if (!db) return NULL;
    Object_Init((Object*)db, &dbClientClass);
    _init_base_methods(db);

    snprintf(db->host, sizeof(db->host), "%s", h ? h : "localhost");
    snprintf(db->dbname, sizeof(db->dbname), "%s", dbn ? dbn : "");
    snprintf(db->dbid, sizeof(db->dbid), "%s", id ? id : "");
    snprintf(db->dbpass, sizeof(db->dbpass), "%s", pw ? pw : "");
    snprintf(db->charset, sizeof(db->charset), "%s", cs ? cs : "utf8mb4");
    snprintf(db->db_type, sizeof(db->db_type), "%s", type ? type : "MYSQL");
    db->port = (port > 0) ? port : 3306;
    db->save_log=1;
    /* =====================================================
     * db_type 기반 backend 선택
     * 컴파일 타임에 해당 backend가 없으면 FATAL
     * ===================================================== */
    #if defined(HAVE_PGSQL)
        if (strncmp(db->db_type, "PGSQL", sizeof("PGSQL")) == 0) {
            bind_pgsql(db);
            return db;
        }
    #endif

    #if defined(HAVE_MYSQL)
        if (strncmp(db->db_type, "MYSQL", sizeof("MYSQL")) == 0) {
            bind_mysql(db);
            return db;
        }
    #endif

    #if defined(HAVE_SQLITE)
        if (strncmp(db->db_type, "SQLITE", sizeof("SQLITE")) == 0) {
            bind_sqlite(db);
            return db;
        }
    #endif
    /* 여기까지 오면 db_type이 지원되지 않거나
     * 해당 backend가 컴파일되지 않은 것 */
    fprintf(stderr,
        "[FATAL] DBClient: db_type '%s' not supported "
        "or not compiled in.\n",
        db->db_type);
    RELEASE((Object*)db);
    return NULL;
}