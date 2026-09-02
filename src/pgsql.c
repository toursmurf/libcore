#include "db.h"
#include <libpq-fe.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <ctype.h>
#include <errno.h>

static void DB_setOption_pg(DBClient *self, int option, const void *arg, size_t arg_size) {
    (void)self; (void)option; (void)arg; (void)arg_size;
}

static int db_has_kw_pg(const char *s, const char *kw) {
    if (!s || !kw) return 0;
    size_t n = strlen(kw);
    if (n == 0) return 0;
    for (const char *p = s; *p; p++) {
        if (strncasecmp(p, kw, n) == 0) return 1;
    }
    return 0;
}

/* 🛡️ [FINAL] Identifier 방어 (길이 상한 63바이트 포함) */
static int DB_identifier_valid_pg(const char *s) {
    if (!s) return 0;
    size_t len = strnlen(s, 64);
    if (len == 0 || len >= 64) return 0;

    unsigned char c = (unsigned char)s[0];
    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_')) {
        return 0;
    }
    for (const char *p = s + 1; *p; p++) {
        c = (unsigned char)*p;
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_')) {
            return 0;
        }
    }
    return 1;
}

/* 🛡️ [FINAL] cond == NULL 일 경우 MySQL과 동일하게 1 반환 (의미론적 일관성 유지) */
static int db_cond_is_valid_pg(DBClient *self, const char *fn, const char *cond) {
    if (!cond) return 1;

    const char *p = cond;
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p == '\0') {
        if (self && self->writeLog) self->writeLog(self, "cond 가 빈 문자열입니다.", fn, 1);
        return 0;
    }
    if (strpbrk(p, "=<>")) return 1;
    if (db_has_kw_pg(p, " in ") || db_has_kw_pg(p, " in(")) return 1;
    if (db_has_kw_pg(p, " like ")) return 1;
    if (db_has_kw_pg(p, " is ")) return 1;
    if (db_has_kw_pg(p, " between ")) return 1;

    if (self && self->writeLog) {
        char m[512];
        snprintf(m, sizeof(m), "cond 에 비교 연산자가 없습니다(fail-closed): \"%s\"", p);
        self->writeLog(self, m, fn, 1);
    }
    return 0;
}

static void DB_schema_cache_clear_locked_pg(DBClient *self) {
    if (!self || !self->schema_cache) return;
    RELEASE(self->schema_cache);
    self->schema_cache = NULL;
}

static void DB_schema_cache_drop_pg(DBClient *self) {
    if (!self) return;
    pthread_mutex_lock(&self->lock);
    DB_schema_cache_clear_locked_pg(self);
    pthread_mutex_unlock(&self->lock);
}

/* =========================================================
 * 🛡️ [FINAL] 동적 스키마 메타데이터 추출 (Primary Key 자동 탐색)
 * ========================================================= */
static HashMap* DB_schema_meta_locked_pg(DBClient *self, const char* table) {
    if (!self || !table || !self->conn) return NULL;
    if (!DB_identifier_valid_pg(table)) return NULL;

    if (!self->schema_cache) {
        self->schema_cache = new_HashMap(16);
        if (!self->schema_cache) return NULL;
    }

    HashMap *meta = (HashMap*)self->schema_cache->get(self->schema_cache, table);
    if (meta) return (HashMap*)RETAIN(meta);

    meta = new_HashMap(4);
    ArrayList *cols = new_ArrayList(16);
    if (!meta || !cols) {
        if (meta) RELEASE(meta);
        if (cols) RELEASE(cols);
        return NULL;
    }

    char q[1024];
    int n = snprintf(q, sizeof(q), "SELECT column_name FROM information_schema.columns WHERE table_schema = 'public' AND table_name = '%s'", table);
    if (n < 0 || (size_t)n >= sizeof(q)) {
        RELEASE(cols); RELEASE(meta); return NULL;
    }

    PGresult *res = PQexec((PGconn*)self->conn, q);
    if (res) {
        if (PQresultStatus(res) == PGRES_TUPLES_OK) {
            int rows = PQntuples(res);
            for (int i = 0; i < rows; i++) {
                String *s = new_String(PQgetvalue(res, i, 0));
                if (s) { cols->add(cols, (Object*)s); RELEASE(s); }
            }
        } else {
            PQclear(res); RELEASE(cols); RELEASE(meta); return NULL;
        }
        PQclear(res);
    } else {
        RELEASE(cols); RELEASE(meta); return NULL;
    }

    if (cols->getSize(cols) == 0) { RELEASE(cols); RELEASE(meta); return NULL; }

    meta->put(meta, "cols", (Object*)cols);
    RELEASE(cols);

    /* 🛡️ Primary Key 동적 추출 쿼리 */
    n = snprintf(q, sizeof(q),
        "SELECT kcu.column_name "
        "FROM information_schema.table_constraints tco "
        "JOIN information_schema.key_column_usage kcu "
        "ON kcu.constraint_name = tco.constraint_name "
        "AND kcu.constraint_schema = tco.constraint_schema "
        "AND kcu.table_name = tco.table_name "
        "WHERE tco.constraint_type = 'PRIMARY KEY' "
        "AND kcu.table_name = '%s' AND kcu.table_schema = 'public' "
        "ORDER BY kcu.ordinal_position LIMIT 1", table);

    if (n < 0 || (size_t)n >= sizeof(q)) {
        RELEASE(meta); return NULL;
    }

    res = PQexec((PGconn*)self->conn, q);
    if (res) {
        if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
            String *pk_str = new_String(PQgetvalue(res, 0, 0));
            if (pk_str) { meta->put(meta, "pk", (Object*)pk_str); RELEASE(pk_str); }
        }
        PQclear(res);
    }

    self->schema_cache->put(self->schema_cache, table, (Object*)meta);
    return meta;
}

/* =========================================================
 * 🛡️ [FINAL] PQconnectdbParams 도입
 * ========================================================= */
static PGconn* DB_open_connection_pg(DBClient *self) {
    const char *keywords[] = { "host", "port", "dbname", "user", "password", "client_encoding", NULL };
    char port_buf[16];
    int n = snprintf(port_buf, sizeof(port_buf), "%d", self->port);
    if (n < 0 || (size_t)n >= sizeof(port_buf)) return NULL;

    const char *values[] = {
        self->host,
        port_buf,
        self->dbname,
        self->dbid,
        self->dbpass,
        self->charset[0] != '\0' ? self->charset : "UTF8",
        NULL
    };

    return PQconnectdbParams(keywords, values, 0);
}

static int DB_connect_pg(DBClient *self) {
    if (!self) return 0;
    pthread_mutex_lock(&self->lock);

    /* 🚨 TX Guard 복원 */
    if (self->in_transaction) {
        pthread_mutex_unlock(&self->lock);
        return 0;
    }

    DB_schema_cache_clear_locked_pg(self);

    if (self->conn) {
        PQfinish((PGconn*)self->conn);
        self->conn = NULL;
    }
    self->isConnected = 0;
    self->in_transaction = 0;

    PGconn *conn = DB_open_connection_pg(self);
    self->conn = (void*)conn;
    int connected = 0;
    char err_msg[512] = {0};

    if (!conn) {
        snprintf(err_msg, sizeof(err_msg), "PostgreSQL PQconnectdbParams returned NULL");
    } else if (PQstatus(conn) == CONNECTION_OK) {
        self->isConnected = 1;
        connected = 1;
    } else {
        snprintf(err_msg, sizeof(err_msg), "PostgreSQL Connect Fail - %s", PQerrorMessage(conn));
    }
    pthread_mutex_unlock(&self->lock);

    if (connected) {
        char msg[256];
        snprintf(msg, sizeof(msg), "PostgreSQL Connected (Host: %s, Port: %d)", self->host, self->port);
        if (self->save_log) self->writeLog(self, msg, NULL, 0);
    } else {
        self->writeLog(self, err_msg, NULL, 1);
    }
    return connected;
}

static void DB_disconnect_pg(DBClient *self) {
    if (!self) return;
    pthread_mutex_lock(&self->lock);

    /* 🚨 TX Guard 복원 */
    if (self->in_transaction) {
        pthread_mutex_unlock(&self->lock);
        return;
    }

    DB_schema_cache_clear_locked_pg(self);

    if (self->conn) {
        PQfinish((PGconn*)self->conn);
        self->conn = NULL;
    }
    self->isConnected = 0;
    self->in_transaction = 0;
    pthread_mutex_unlock(&self->lock);

    self->writeLog(self, "PostgreSQL Disconnected", NULL, 0);
}

static int DB_reconnect_internal_pg(DBClient *self) {
    DB_schema_cache_clear_locked_pg(self);
    if (self->conn) {
        PQfinish((PGconn*)self->conn);
        self->conn = NULL;
    }
    self->isConnected = 0;
    self->in_transaction = 0;

    PGconn *conn = DB_open_connection_pg(self);
    self->conn = (void*)conn;

    if (!conn) return 0;
    if (PQstatus(conn) == CONNECTION_OK) {
        self->isConnected = 1;
        return 1;
    }
    return 0;
}

static int DB_reconnect_pg(DBClient *self) {
    if (!self) return 0;
    pthread_mutex_lock(&self->lock);

    /* 🚨 TX Guard (기존 유지) */
    if (self->in_transaction) {
        pthread_mutex_unlock(&self->lock);
        return 0;
    }
    int res = DB_reconnect_internal_pg(self);
    pthread_mutex_unlock(&self->lock);

    if (res) self->writeLog(self, "PostgreSQL Reconnected", NULL, 0);
    else self->writeLog(self, "PostgreSQL Reconnect Fail", NULL, 1);
    return res;
}

static int DB_sqlQuery_pg(DBClient *self, const char* sql) {
    char err_msg[1024] = {0};
    int success = 0;
    int needs_reconnect_log = 0, reconnect_failed = 0, tx_lost = 0;

    pthread_mutex_lock(&self->lock);
    snprintf(self->last_query, sizeof(self->last_query), "%s", sql);
    self->affected_rows = -1;

    if (!self->conn) {
        pthread_mutex_unlock(&self->lock);
        return 0;
    }

    PGresult *res = PQexec((PGconn*)self->conn, sql);
    if (res) {
        ExecStatusType status = PQresultStatus(res);
        success = (status == PGRES_COMMAND_OK || status == PGRES_TUPLES_OK);
    }

    if (!success && PQstatus((PGconn*)self->conn) == CONNECTION_BAD) {
        if (self->in_transaction) {
            self->isConnected = 0;
            tx_lost = 1;
        } else {
            needs_reconnect_log = 1;
            if (DB_reconnect_internal_pg(self)) {
                if (res) { PQclear(res); res = NULL; }
                res = PQexec((PGconn*)self->conn, sql);
                if (res) {
                    ExecStatusType status = PQresultStatus(res);
                    success = (status == PGRES_COMMAND_OK || status == PGRES_TUPLES_OK);
                }
            } else {
                reconnect_failed = 1;
            }
        }
    }

    if (!success && !reconnect_failed) {
        snprintf(err_msg, sizeof(err_msg), "%s", PQerrorMessage((PGconn*)self->conn));
    } else if (success && res) {
        char *tuples = PQcmdTuples(res);
        if (tuples && tuples[0] != '\0')
            self->affected_rows = atoll(tuples);
    }

    if (res) PQclear(res);
    pthread_mutex_unlock(&self->lock);

    if (tx_lost) self->writeLog(self, "트랜잭션 도중 접속이 끊겼습니다. 재접속 차단.", sql, 1);
    if (needs_reconnect_log) {
        if (reconnect_failed) self->writeLog(self, "DB Connection lost. Reconnect Fail.", sql, 1);
        else self->writeLog(self, "DB Connection lost. Reconnected.", sql, 0);
    }
    if (!success && err_msg[0] != '\0') self->writeLog(self, err_msg, sql, 1);
    else if (success && self->save_log) self->writeLog(self, "QUERY_OK", sql, 0);

    return success;
}

static int DB_tx_query_pg(DBClient *self, const char *sql) {
    char err_msg[1024] = {0};
    int success = 0;

    pthread_mutex_lock(&self->lock);
    snprintf(self->last_query, sizeof(self->last_query), "%s", sql);
    self->affected_rows = -1;

    if (!self->conn) { pthread_mutex_unlock(&self->lock); return 0; }

    PGresult *res = PQexec((PGconn*)self->conn, sql);
    if (res) {
        ExecStatusType status = PQresultStatus(res);
        success = (status == PGRES_COMMAND_OK || status == PGRES_TUPLES_OK);
    }

    if (!success && PQstatus((PGconn*)self->conn) == CONNECTION_BAD) {
        self->isConnected = 0;
    }

    if (!success) {
        snprintf(err_msg, sizeof(err_msg), "%s", PQerrorMessage((PGconn*)self->conn));
    } else if (res) {
        char *tuples = PQcmdTuples(res);
        if (tuples && tuples[0] != '\0') self->affected_rows = atoll(tuples);
    }

    if (res) PQclear(res);
    pthread_mutex_unlock(&self->lock);

    if (!success && err_msg[0] != '\0') self->writeLog(self, err_msg, sql, 1);
    else if (success && self->save_log) self->writeLog(self, "QUERY_OK", sql, 0);
    return success;
}

/* =========================================================
 * 🚨 [TX Serialization 복원]
 * ========================================================= */
static int DB_beginTransaction_pg(DBClient *self) {
    if (!self) return 0;

    pthread_mutex_lock(&self->lock);   /* depth 1 */

    if (self->in_transaction) {
        pthread_mutex_unlock(&self->lock);
        return 0;
    }

    int ok = self->sqlQuery(self, "BEGIN");
    /* sqlQuery가 recursive lock depth 2를 사용하고 다시 depth 1 */

    if (!ok) {
        pthread_mutex_unlock(&self->lock);
        return 0;
    }

    self->in_transaction = 1;

    return 1;   /* 👈 depth 1을 일부러 유지 (Outer Lock 보유) */
}

static int DB_commit_pg(DBClient *self) {
    if (!self) return 0;

    pthread_mutex_lock(&self->lock);   /* retained depth 1 -> local depth 2 */

    if (!self->in_transaction) {
        pthread_mutex_unlock(&self->lock);
        return 0;
    }

    int ok = DB_tx_query_pg(self, "COMMIT");

    if (!ok) {
        (void)DB_tx_query_pg(self, "ROLLBACK");
    }

    self->in_transaction = 0;

    pthread_mutex_unlock(&self->lock); /* local depth 2 -> 1 */
    pthread_mutex_unlock(&self->lock); /* retained BEGIN depth 1 -> 0 */

    return ok;
}

static int DB_rollback_pg(DBClient *self) {
    if (!self) return 0;

    pthread_mutex_lock(&self->lock);   /* retained depth 1 -> local depth 2 */

    if (!self->in_transaction) {
        pthread_mutex_unlock(&self->lock);
        return 0;
    }

    int ok = DB_tx_query_pg(self, "ROLLBACK");
    self->in_transaction = 0;

    pthread_mutex_unlock(&self->lock); /* local depth 2 -> 1 */
    pthread_mutex_unlock(&self->lock); /* retained BEGIN depth 1 -> 0 */

    return ok;
}

static char* DB_escape_string_pg(DBClient *self, const char* str) {
    if (!str || str[0] == '\0') {
        char* empty = malloc(1);
        if (empty) empty[0] = '\0';
        return empty;
    }
    size_t len = strlen(str);
    char *out = (char*)malloc(len * 2 + 1);
    if (!out) return NULL;

    int error = 0;
    pthread_mutex_lock(&self->lock);
    if (!self->conn) {
        pthread_mutex_unlock(&self->lock);
        free(out);
        return NULL;
    }

    PQescapeStringConn((PGconn*)self->conn, out, str, len, &error);
    pthread_mutex_unlock(&self->lock);

    if (error) { free(out); return NULL; }
    return out;
}

static ArrayList* DB_getRecordsFromQuery_pg(DBClient *self, const char* sql) {
    if (!self || !sql) return NULL;
    ArrayList *list = new_ArrayList(16);
    if (!list) return NULL;

    char err_msg[1024] = {0};
    int success = 0;
    int needs_reconnect_log = 0, reconnect_failed = 0, tx_lost = 0;

    pthread_mutex_lock(&self->lock);
    self->affected_rows = -1;

    if (!self->conn) {
        pthread_mutex_unlock(&self->lock);
        RELEASE(list);
        return NULL;
    }

    PGresult *res = PQexec((PGconn*)self->conn, sql);
    if (res) {
        ExecStatusType status = PQresultStatus(res);
        success = (status == PGRES_TUPLES_OK);
    }

    if (!success && PQstatus((PGconn*)self->conn) == CONNECTION_BAD) {
        if (self->in_transaction) {
            self->isConnected = 0;
            tx_lost = 1;
        } else {
            needs_reconnect_log = 1;
            if (DB_reconnect_internal_pg(self)) {
                if (res) {
                    PQclear(res);
                    res = NULL;
                }
                res = PQexec((PGconn*)self->conn, sql);
                if (res) {
                    ExecStatusType status = PQresultStatus(res);
                    success = (status == PGRES_TUPLES_OK);
                }
            } else {
                reconnect_failed = 1;
            }
        }
    }

    if (!success && !reconnect_failed && self->conn) {
        snprintf(err_msg, sizeof(err_msg), "%s", PQerrorMessage((PGconn*)self->conn));
    }

    if (success && res) {
        self->affected_rows = (long long)PQntuples(res);
        int rows = PQntuples(res);
        int cols = PQnfields(res);
        for (int i = 0; i < rows; i++) {
            HashMap *m = new_HashMap(16);
            if (!m) {
                PQclear(res);
                pthread_mutex_unlock(&self->lock);
                RELEASE(list);
                return NULL;
            }

            for (int j = 0; j < cols; j++) {
                const char *field = PQfname(res, j);
                const char *val = PQgetvalue(res, i, j);
                String *str = new_String(val ? val : "");
                if (!str) {
                    RELEASE(m);
                    PQclear(res);
                    pthread_mutex_unlock(&self->lock);
                    RELEASE(list);
                    return NULL;
                }
                m->put(m, field, (Object *)str);
                RELEASE(str);
            }
            list->add(list, (Object *)m);
            RELEASE(m);
        }
    }

    if (res) PQclear(res);
    pthread_mutex_unlock(&self->lock);

    if (tx_lost && self->writeLog)
        self->writeLog(self, "트랜잭션 도중 접속이 끊겼습니다. 재접속 차단.", sql, 1);
    if (needs_reconnect_log && self->writeLog) {
        if (reconnect_failed)
            self->writeLog(self, "DB Connection lost. Reconnect Fail.", sql, 1);
        else
            self->writeLog(self, "DB Connection lost. Reconnected.", sql, 0);
    }

    if (!success) {
        if (err_msg[0] != '\0' && self->writeLog)
            self->writeLog(self, err_msg, sql, 1);
        RELEASE(list);
        return NULL;
    }

    return list;
}

static HashMap* DB_getRecordFromQuery_pg(DBClient *self, const char* sql) {
    ArrayList *list = self->getRecordsFromQuery(self, sql);
    HashMap *res_map = NULL;
    if (list && list->getSize(list) > 0) {
        res_map = (HashMap *)RETAIN(list->get(list, 0));
    }
    if (list) RELEASE(list);
    return res_map;
}

static HashMap* DB_getRecord_pg(DBClient *self, const char* table, const char* cond, const char* field) {
    if (!DB_identifier_valid_pg(table)) return NULL;
    char q[4096];
    int n = snprintf(q, sizeof(q), "SELECT %s FROM \"%s\" %s %s LIMIT 1",
             field && strlen(field) > 0 ? field : "*", table,
             cond && strlen(cond) > 0 ? "WHERE " : "", cond ? cond : "");
    if (n < 0 || (size_t)n >= sizeof(q)) return NULL;
    return self->getRecordFromQuery(self, q);
}

static ArrayList* DB_getRecords_pg(DBClient *self, const char* table, const char* cond, const char* fields) {
    if (!DB_identifier_valid_pg(table)) return NULL;
    char q[4096];
    int n = snprintf(q, sizeof(q), "SELECT %s FROM \"%s\" %s %s",
             fields && strlen(fields) > 0 ? fields : "*", table,
             cond && strlen(cond) > 0 ? "WHERE " : "", cond ? cond : "");
    if (n < 0 || (size_t)n >= sizeof(q)) return NULL;
    return self->getRecordsFromQuery(self, q);
}

static HashMap* DB_validateFields_pg(DBClient *self, const char* table, HashMap* raw_data) {
    if (!self || !table || !raw_data) return NULL;
    if (!DB_identifier_valid_pg(table)) return NULL;

    pthread_mutex_lock(&self->lock);
    HashMap *meta = DB_schema_meta_locked_pg(self, table);
    pthread_mutex_unlock(&self->lock);

    if (!meta) return NULL;

    ArrayList *vcols = (ArrayList*)meta->get(meta, "cols");
    if (!vcols) {
        RELEASE(meta);
        return NULL;
    }

    HashMap *clean_data = new_HashMap(16);
    if (!clean_data) {
        RELEASE(meta);
        return NULL;
    }

    for (int b = 0; b < raw_data->capacity; b++) {
        HashNode *node = raw_data->buckets[b];
        while (node != NULL) {
            for (int j = 0; j < vcols->getSize(vcols); j++) {
                String *col = (String *)vcols->get(vcols, j);
                if (col && strcmp(node->key, col->value) == 0) {
                    clean_data->put(clean_data, node->key, node->value);
                    break;
                }
            }
            node = node->next;
        }
    }
    RELEASE(meta);
    return clean_data;
}

/* =========================================================
 * 🛡️ [FINAL] INSERT (초기화, Identifier 방어, 동적 PK RETURNING 처리)
 * ========================================================= */
static int DB_insertTable_pg(DBClient *self, const char* table, HashMap* data) {
    if (!self || !table || !data) return 0;

    pthread_mutex_lock(&self->lock);
    self->last_insert_id = 0;
    self->last_idx = 0;
    pthread_mutex_unlock(&self->lock);

    if (!DB_identifier_valid_pg(table)) return 0;

    HashMap *clean = self->validateFields(self, table, data);
    if (!clean || clean->getSize(clean) == 0) { if (clean) RELEASE(clean); return 0; }

    char pk_col[128] = {0};
    pthread_mutex_lock(&self->lock);
    HashMap *meta = DB_schema_meta_locked_pg(self, table);
    if (meta) {
        String *pk_str = (String*)meta->get(meta, "pk");
        /* 🛡️ 메타데이터에서 실제 PK 이름 동적 추출 및 Identifier 방어 강제 */
        if (pk_str && pk_str->value[0] != '\0' && DB_identifier_valid_pg(pk_str->value)) {
            int n = snprintf(pk_col, sizeof(pk_col), "%s", pk_str->value);
            if (n < 0 || (size_t)n >= sizeof(pk_col)) pk_col[0] = '\0';
        }
        RELEASE(meta);
    }
    pthread_mutex_unlock(&self->lock);

    char fields[4096] = {0}, values[8192] = {0};
    int total = clean->getSize(clean), count = 0;

    for (int b = 0; b < clean->capacity; b++) {
        HashNode *node = clean->buckets[b];
        while (node != NULL) {
            if (!DB_identifier_valid_pg(node->key)) goto fail;

            if (!safe_append(fields, sizeof(fields), "\"")) goto fail;
            if (!safe_append(fields, sizeof(fields), node->key)) goto fail;
            if (!safe_append(fields, sizeof(fields), "\"")) goto fail;
            if (!safe_append(values, sizeof(values), "'")) goto fail;

            char *esc = self->escape_string(self, ((String*)node->value)->value);
            if (!esc) goto fail;
            if (!safe_append(values, sizeof(values), esc)) { free(esc); goto fail; }
            if (!safe_append(values, sizeof(values), "'")) { free(esc); goto fail; }
            free(esc);

            if (++count < total) {
                if (!safe_append(fields, sizeof(fields), ", ")) goto fail;
                if (!safe_append(values, sizeof(values), ", ")) goto fail;
            }
            node = node->next;
        }
    }

    char q[16384];

    /* 🛡️ 하드코딩된 idx 제거! 메타데이터에서 추출한 pk_col 사용 */
    if (pk_col[0] != '\0') {
        int n = snprintf(q, sizeof(q), "INSERT INTO \"%s\" (%s) VALUES (%s) RETURNING \"%s\"", table, fields, values, pk_col);
        if (n < 0 || (size_t)n >= sizeof(q)) goto fail;
        RELEASE(clean);

        HashMap *res = self->getRecordFromQuery(self, q);
        if (!res) return 0;

        String *v = (String*)res->get(res, pk_col);
        if (v && v->value[0] != '\0') {
            errno = 0;
            char *end = NULL;
            long long val = strtoll(v->value, &end, 10);
            if (errno == 0 && end != v->value && *end == '\0' && val > 0) {
                pthread_mutex_lock(&self->lock);
                self->last_insert_id = val;
                self->last_idx = val;
                pthread_mutex_unlock(&self->lock);
            }
        }
        RELEASE(res);
        return 1;
    } else {
        int n = snprintf(q, sizeof(q), "INSERT INTO \"%s\" (%s) VALUES (%s)", table, fields, values);
        if (n < 0 || (size_t)n >= sizeof(q)) goto fail;
        RELEASE(clean);
        return self->sqlQuery(self, q);
    }

fail:
    if (clean) RELEASE(clean);
    self->writeLog(self, "INSERT buffer truncation, escape fail, or invalid identifier", NULL, 1);
    return 0;
}

static int DB_updateTable_pg(DBClient *self, const char* table, HashMap* data, const char* where) {
    if (!DB_identifier_valid_pg(table)) return 0;
    if (!db_cond_is_valid_pg(self, "updateTable", where)) return 0;

    HashMap *clean = self->validateFields(self, table, data);
    if (!clean || clean->getSize(clean) == 0) { if (clean) RELEASE(clean); return 0; }

    char sets[8192] = {0};
    int total = clean->getSize(clean), count = 0;

    for (int b = 0; b < clean->capacity; b++) {
        HashNode *node = clean->buckets[b];
        while (node != NULL) {
            if (!DB_identifier_valid_pg(node->key)) goto fail;

            if (!safe_append(sets, sizeof(sets), "\"")) goto fail;
            if (!safe_append(sets, sizeof(sets), node->key)) goto fail;
            if (!safe_append(sets, sizeof(sets), "\" = '")) goto fail;

            char *esc = self->escape_string(self, ((String*)node->value)->value);
            if (!esc) goto fail;
            if (!safe_append(sets, sizeof(sets), esc)) { free(esc); goto fail; }
            if (!safe_append(sets, sizeof(sets), "'")) { free(esc); goto fail; }
            free(esc);

            if (++count < total) {
                if (!safe_append(sets, sizeof(sets), ", ")) goto fail;
            }
            node = node->next;
        }
    }

    char q[16384];
    int n = snprintf(q, sizeof(q), "UPDATE \"%s\" SET %s %s %s", table, sets, where ? "WHERE " : "", where ? where : "");
    if (n < 0 || (size_t)n >= sizeof(q)) goto fail;

    RELEASE(clean);
    return self->sqlQuery(self, q);

fail:
    if (clean) RELEASE(clean);
    self->writeLog(self, "UPDATE buffer truncation, escape fail, or invalid identifier", NULL, 1);
    return 0;
}

static int DB_replaceTable_pg(DBClient *self, const char* table, HashMap* data) {
    if (!DB_identifier_valid_pg(table)) return 0;
    HashMap *clean = self->validateFields(self, table, data);
    if (!clean || clean->getSize(clean) == 0) { if (clean) RELEASE(clean); return 0; }

    char pk_col[128] = {0};
    pthread_mutex_lock(&self->lock);
    HashMap *meta = DB_schema_meta_locked_pg(self, table);
    if (meta) {
        String *pk_str = (String*)meta->get(meta, "pk");
        /* 🛡️ 하드코딩 제거: 메타데이터에서 실제 PK 추출 */
        if (pk_str && pk_str->value[0] != '\0' && DB_identifier_valid_pg(pk_str->value)) {
            int n = snprintf(pk_col, sizeof(pk_col), "%s", pk_str->value);
            if (n < 0 || (size_t)n >= sizeof(pk_col)) pk_col[0] = '\0';
        }
        RELEASE(meta);
    }
    pthread_mutex_unlock(&self->lock);

    char fields[4096] = {0}, values[8192] = {0}, sets[8192] = {0};
    int total = clean->getSize(clean), count = 0;

    for (int b = 0; b < clean->capacity; b++) {
        HashNode *node = clean->buckets[b];
        while (node != NULL) {
            if (!DB_identifier_valid_pg(node->key)) goto fail;

            if (!safe_append(fields, sizeof(fields), "\"")) goto fail;
            if (!safe_append(fields, sizeof(fields), node->key)) goto fail;
            if (!safe_append(fields, sizeof(fields), "\"")) goto fail;
            if (!safe_append(values, sizeof(values), "'")) goto fail;

            char *esc = self->escape_string(self, ((String*)node->value)->value);
            if (!esc) goto fail;
            if (!safe_append(values, sizeof(values), esc)) { free(esc); goto fail; }
            if (!safe_append(values, sizeof(values), "'")) { free(esc); goto fail; }

            if (!safe_append(sets, sizeof(sets), "\"")) { free(esc); goto fail; }
            if (!safe_append(sets, sizeof(sets), node->key)) { free(esc); goto fail; }
            if (!safe_append(sets, sizeof(sets), "\" = '")) { free(esc); goto fail; }
            if (!safe_append(sets, sizeof(sets), esc)) { free(esc); goto fail; }
            if (!safe_append(sets, sizeof(sets), "'")) { free(esc); goto fail; }
            free(esc);

            if (++count < total) {
                if (!safe_append(fields, sizeof(fields), ", ")) goto fail;
                if (!safe_append(values, sizeof(values), ", ")) goto fail;
                if (!safe_append(sets, sizeof(sets), ", ")) goto fail;
            }
            node = node->next;
        }
    }

    char q[16384];
    if (pk_col[0] != '\0') {
        int n = snprintf(q, sizeof(q), "INSERT INTO \"%s\" (%s) VALUES (%s) ON CONFLICT (\"%s\") DO UPDATE SET %s", table, fields, values, pk_col, sets);
        if (n < 0 || (size_t)n >= sizeof(q)) goto fail;
    } else {
        goto fail;
    }

    RELEASE(clean);
    return self->sqlQuery(self, q);

fail:
    if (clean) RELEASE(clean);
    self->writeLog(self, "UPSERT buffer truncation, escape fail, or invalid identifier", NULL, 1);
    return 0;
}

static int DB_deleteTable_pg(DBClient *self, const char* table, const char* where) {
    if (!DB_identifier_valid_pg(table)) return 0;
    if (!db_cond_is_valid_pg(self, "deleteTable", where)) return 0;
    char q[4096];
    int n = snprintf(q, sizeof(q), "DELETE FROM \"%s\" %s %s", table, where ? "WHERE " : "", where ? where : "");
    if (n < 0 || (size_t)n >= sizeof(q)) {
        self->writeLog(self, "DELETE SQL buffer truncation (fail-closed)", NULL, 1);
        return 0;
    }
    return self->sqlQuery(self, q);
}

static int DB_getDataCount_pg(DBClient *self, const char* table, const char* cond) {
    if (!DB_identifier_valid_pg(table)) return 0;
    char q[2048];
    int n = snprintf(q, sizeof(q), "SELECT COUNT(*) AS cnt FROM \"%s\" %s %s", table, cond && strlen(cond) > 0 ? "WHERE " : "", cond ? cond : "");
    if (n < 0 || (size_t)n >= sizeof(q)) return 0;

    HashMap* res = self->getRecordFromQuery(self, q);
    int count = 0;
    if (res) {
        String* cnt_str = (String*)res->get(res, "cnt");
        if (cnt_str) count = atoi(cnt_str->value);
        RELEASE(res);
    }
    return count;
}

static int DB_dropTable_pg(DBClient *self, const char *table_name) {
    if (!DB_identifier_valid_pg(table_name)) return 0;
    char q[512];
    int n = snprintf(q, sizeof(q), "DROP TABLE IF EXISTS \"%s\"", table_name);
    if (n < 0 || (size_t)n >= sizeof(q)) return 0;

    int res = self->sqlQuery(self, q);
    DB_schema_cache_drop_pg(self);
    return res;
}

static int DB_initTable_pg(DBClient *self, const char* table) {
    if (!DB_identifier_valid_pg(table)) return 0;
    char q[256];
    int n = snprintf(q, sizeof(q), "TRUNCATE TABLE \"%s\" RESTART IDENTITY", table);
    if (n < 0 || (size_t)n >= sizeof(q)) return 0;
    return self->sqlQuery(self, q);
}

static int DB_all_delete_table_pg(DBClient *self, const char* table) {
    if (!DB_identifier_valid_pg(table)) return 0;
    char q[1024];
    int n = snprintf(q, sizeof(q), "TRUNCATE TABLE \"%s\" RESTART IDENTITY", table);
    if (n < 0 || (size_t)n >= sizeof(q)) return 0;
    return self->sqlQuery(self, q);
}

static int DB_copyTable_pg(DBClient *self, const char* newT, const char* orgT, int copyData) {
    if (!DB_identifier_valid_pg(newT) || !DB_identifier_valid_pg(orgT)) return 0;
    char q1[1024];
    int n = snprintf(q1, sizeof(q1), "CREATE TABLE IF NOT EXISTS \"%s\" (LIKE \"%s\" INCLUDING ALL)", newT, orgT);
    if (n < 0 || (size_t)n >= sizeof(q1)) return 0;

    if (!self->sqlQuery(self, q1)) return 0;
    DB_schema_cache_drop_pg(self);

    if (copyData) {
        char q2[1024];
        n = snprintf(q2, sizeof(q2), "INSERT INTO \"%s\" SELECT * FROM \"%s\"", newT, orgT);
        if (n < 0 || (size_t)n >= sizeof(q2)) return 0;
        return self->sqlQuery(self, q2);
    }
    return 1;
}

static int DB_renameTable_pg(DBClient *self, const char* old_table, const char* new_table) {
    if (!DB_identifier_valid_pg(old_table) || !DB_identifier_valid_pg(new_table)) return 0;
    char q[512];
    int n = snprintf(q, sizeof(q), "ALTER TABLE \"%s\" RENAME TO \"%s\"", old_table, new_table);
    if (n < 0 || (size_t)n >= sizeof(q)) return 0;

    int res = self->sqlQuery(self, q);
    DB_schema_cache_drop_pg(self);
    return res;
}

static char* DB_makeTable_pg(DBClient *self, const char* tablename) {
    (void) self;
    if (!DB_identifier_valid_pg(tablename)) return NULL;
    time_t t = time(NULL);
    struct tm ti_buf;
    struct tm *ti = localtime_r(&t, &ti_buf);
    if (!ti) return NULL;
    char *new_name = (char*)malloc(256);
    if (!new_name) return NULL;
    snprintf(new_name, 256, "%s_%04d%02d", tablename, ti->tm_year + 1900, ti->tm_mon + 1);
    return new_name;
}

static long long DB_getTableSize_pg(DBClient *self, const char* table) {
    if (!DB_identifier_valid_pg(table)) return 0;
    char q[512];
    int n = snprintf(q, sizeof(q), "SELECT pg_total_relation_size('\"%s\"') AS tsize", table);
    if (n < 0 || (size_t)n >= sizeof(q)) return 0;

    HashMap* res = self->getRecordFromQuery(self, q);
    long long size = 0;
    if (res) {
        String* size_str = (String*)res->get(res, "tsize");
        if (size_str) size = atoll(size_str->value);
        RELEASE(res);
    }
    return size;
}

static int DB_table_exists_pg(DBClient *self, const char* table) {
    if (!DB_identifier_valid_pg(table)) return 0;
    char q[1024];
    int n = snprintf(q, sizeof(q), "SELECT 1 FROM information_schema.tables WHERE table_schema = 'public' AND table_name = '%s'", table);
    if (n < 0 || (size_t)n >= sizeof(q)) return 0;

    HashMap *res = self->getRecordFromQuery(self, q);
    if (res) { RELEASE(res); return 1; }
    return 0;
}

static int DB_fieldExists_pg(DBClient *self, const char* table, const char* field) {
    if (!DB_identifier_valid_pg(table) || !DB_identifier_valid_pg(field)) return 0;
    char q[1024];
    int n = snprintf(q, sizeof(q), "SELECT 1 FROM information_schema.columns WHERE table_schema = 'public' AND table_name = '%s' AND column_name = '%s'", table, field);
    if (n < 0 || (size_t)n >= sizeof(q)) return 0;

    HashMap *res = self->getRecordFromQuery(self, q);
    if (res) { RELEASE(res); return 1; }
    return 0;
}

static long long DB_getNextIdx_pg(DBClient *self, const char* table) {
    if (!self) return 0;

    pthread_mutex_lock(&self->lock);
    self->last_idx = 0;
    pthread_mutex_unlock(&self->lock);

    if (!DB_identifier_valid_pg(table)) return 0;

    char pk_col[128] = {0};
    pthread_mutex_lock(&self->lock);
    HashMap *meta = DB_schema_meta_locked_pg(self, table);
    if (meta) {
        String *pk = (String*)meta->get(meta, "pk");
        /* 🛡️ 하드코딩 제거: 메타데이터에서 실제 PK 추출 */
        if (pk && pk->value[0] != '\0' && DB_identifier_valid_pg(pk->value)) {
            int n = snprintf(pk_col, sizeof(pk_col), "%s", pk->value);
            if (n < 0 || (size_t)n >= sizeof(pk_col)) pk_col[0] = '\0';
        }
        RELEASE(meta);
    }
    pthread_mutex_unlock(&self->lock);

    if (pk_col[0] == '\0') return 0;

    char q[512];
    int n = snprintf(q, sizeof(q), "SELECT COALESCE(MAX(\"%s\"), 0) + 1 AS nextval FROM \"%s\"", pk_col, table);
    if (n < 0 || (size_t)n >= sizeof(q)) return 0;

    HashMap* res = self->getRecordFromQuery(self, q);
    long long idx = 0;
    if (res) {
        String* v = (String*)res->get(res, "nextval");
        if (v && v->value[0] != '\0') {
            errno = 0;
            char *end = NULL;
            long long val = strtoll(v->value, &end, 10);
            if (errno == 0 && end != v->value && *end == '\0' && val > 0) {
                idx = val;
            }
        }
        RELEASE(res);
    }

    if (idx > 0) {
        pthread_mutex_lock(&self->lock);
        self->last_idx = idx;
        pthread_mutex_unlock(&self->lock);
    }
    return idx > 0 ? idx : 0;
}

static ArrayList* DB_descTable_pg(DBClient *self, const char* table) {
    if (!DB_identifier_valid_pg(table)) return NULL;
    char q[512];
    int n = snprintf(q, sizeof(q), "SELECT column_name, data_type, character_maximum_length FROM information_schema.columns WHERE table_schema = 'public' AND table_name = '%s' ORDER BY ordinal_position", table);
    if (n < 0 || (size_t)n >= sizeof(q)) return NULL;
    return self->getRecordsFromQuery(self, q);
}

static long long DB_getDataSum_pg(DBClient *self, const char* table, const char* field, const char* cond) {
    if (!DB_identifier_valid_pg(table) || !DB_identifier_valid_pg(field)) return 0;
    char q[2048];
    int n = snprintf(q, sizeof(q), "SELECT COALESCE(SUM(%s),0) AS s FROM \"%s\" %s %s", field, table, cond && strlen(cond) > 0 ? "WHERE " : "", cond ? cond : "");
    if (n < 0 || (size_t)n >= sizeof(q)) return 0;

    HashMap* res = self->getRecordFromQuery(self, q);
    long long val = 0;
    if (res) {
        String* v = (String*)res->get(res, "s");
        if (v) val = atoll(v->value);
        RELEASE(res);
    }
    return val;
}

static long long DB_getDataMax_pg(DBClient *self, const char* table, const char* field, const char* cond) {
    if (!DB_identifier_valid_pg(table) || !DB_identifier_valid_pg(field)) return 0;
    char q[2048];
    int n = snprintf(q, sizeof(q), "SELECT COALESCE(MAX(%s),0) AS m FROM \"%s\" %s %s", field, table, cond && strlen(cond) > 0 ? "WHERE " : "", cond ? cond : "");
    if (n < 0 || (size_t)n >= sizeof(q)) return 0;

    HashMap* res = self->getRecordFromQuery(self, q);
    long long val = 0;
    if (res) {
        String* v = (String*)res->get(res, "m");
        if (v) val = atoll(v->value);
        RELEASE(res);
    }
    return val;
}

static long long DB_getDataMin_pg(DBClient *self, const char* table, const char* field, const char* cond) {
    if (!DB_identifier_valid_pg(table) || !DB_identifier_valid_pg(field)) return 0;
    char q[2048];
    int n = snprintf(q, sizeof(q), "SELECT COALESCE(MIN(%s),0) AS m FROM \"%s\" %s %s", field, table, cond && strlen(cond) > 0 ? "WHERE " : "", cond ? cond : "");
    if (n < 0 || (size_t)n >= sizeof(q)) return 0;

    HashMap* res = self->getRecordFromQuery(self, q);
    long long val = 0;
    if (res) {
        String* v = (String*)res->get(res, "m");
        if (v) val = atoll(v->value);
        RELEASE(res);
    }
    return val;
}

void bind_pgsql(DBClient *db) {
    if (!db) return;

    db->setOption        = DB_setOption_pg;
    db->connect          = DB_connect_pg;
    db->disconnect       = DB_disconnect_pg;
    db->reconnect        = DB_reconnect_pg;
    db->sqlQuery         = DB_sqlQuery_pg;
    db->escape_string    = DB_escape_string_pg;

    db->beginTransaction = DB_beginTransaction_pg;
    db->commit           = DB_commit_pg;
    db->rollback         = DB_rollback_pg;

    db->validateFields   = DB_validateFields_pg;
    db->table_exists     = DB_table_exists_pg;
    db->fieldExists      = DB_fieldExists_pg;
    db->getNextIdx       = DB_getNextIdx_pg;

    db->getRecordsFromQuery = DB_getRecordsFromQuery_pg;
    db->getRecordFromQuery  = DB_getRecordFromQuery_pg;
    db->getRecord           = DB_getRecord_pg;
    db->getRecords          = DB_getRecords_pg;

    db->insertTable      = DB_insertTable_pg;
    db->updateTable      = DB_updateTable_pg;
    db->deleteTable      = DB_deleteTable_pg;
    db->replaceTable     = DB_replaceTable_pg;

    db->getDataCount     = DB_getDataCount_pg;
    db->getDataSum       = DB_getDataSum_pg;
    db->getDataMax       = DB_getDataMax_pg;
    db->getDataMin       = DB_getDataMin_pg;

    db->dropTable        = DB_dropTable_pg;
    db->initTable        = DB_initTable_pg;
    db->getTableSize     = DB_getTableSize_pg;
    db->makeTable        = DB_makeTable_pg;
    db->copyTable        = DB_copyTable_pg;
    db->renameTable      = DB_renameTable_pg;
    db->descTable        = DB_descTable_pg;
    db->all_delete_table = DB_all_delete_table_pg;
}