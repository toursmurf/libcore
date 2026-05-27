#include "db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <ctype.h>

void safe_append(char *dest, size_t dest_size, const char *src) {
    if (!dest ||!src || dest_size == 0) return;
    size_t current_len = strlen(dest);
    if (current_len >= dest_size - 1) return;
    strncat(dest, src, dest_size - current_len - 1);
}

static void DB_setSaveLog_core(DBClient *self, int enable) {
    if (!self) return;
    self->save_log = enable;
}

static void DB_writeLog_core(DBClient *self, const char* msg, const char* sql, int is_error) {
    if (!self ||!msg) return;
    if (!is_error &&!self->save_log) return;
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
            is_error? "ERROR" : "INFO", msg, sql? sql : "");
        fclose(fp);
    }
    pthread_mutex_unlock(&self->lock);
}

static void DBClient_Finalize(Object *obj) {
    if (!obj) return;
    DBClient *self = (DBClient*)obj;
    if (self->isConnected && self->disconnect) self->disconnect(self);
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
    //db->unused_ptr = NULL; //미사용 폐기
    db->option_count = 0;
    pthread_mutex_init(&db->lock, NULL);
}

DBClient* new_DBClient() {
    FILE *fp = fopen(DEFAULT_DB_CONFIG, "r");
    if (!fp) return NULL;
    DBClient *db = calloc(1, sizeof(DBClient));
    if (!db) { fclose(fp); return NULL; }
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
    if (idx < 7) { RELEASE((Object*)db); return NULL; }
    bind_mysql(db);
    return db;
}

DBClient* new_DBClientDirect(const char* h, const char* dbn, const char* id, const char* pw, int port, const char* cs, const char* type) {
    DBClient *db = calloc(1, sizeof(DBClient));
    if (!db) return NULL;
    Object_Init((Object*)db, &dbClientClass);
    _init_base_methods(db);
    snprintf(db->host, sizeof(db->host), "%s", h? h : "localhost");
    snprintf(db->dbname, sizeof(db->dbname), "%s", dbn? dbn : "");
    snprintf(db->dbid, sizeof(db->dbid), "%s", id? id : "");
    snprintf(db->dbpass, sizeof(db->dbpass), "%s", pw? pw : "");
    snprintf(db->charset, sizeof(db->charset), "%s", cs? cs : "utf8mb4");
    snprintf(db->db_type, sizeof(db->db_type), "%s", type? type : "MYSQL");
    db->port = (port > 0)? port : 3306;
    bind_mysql(db);
    return db;
}
