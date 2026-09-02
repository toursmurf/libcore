#include "db.h"
#include "logger.h"
#include "arraylist.h"
#include "hashmap.h"
#include "string_obj.h"
#ifdef HAVE_SQLITE
#include <sqlite3.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <pthread.h>
#include <limits.h>
#include <errno.h>

static long long DB_changes_sqlite(sqlite3 *db) {
#if SQLITE_VERSION_NUMBER >= 3037000
    return (long long)sqlite3_changes64(db);
#else
    return (long long)sqlite3_changes(db);
#endif
}

static size_t DB_strnlen_sqlite(const char *s, size_t max_len) {
    size_t i;
    if (!s)
        return 0;
    for (i = 0; i < max_len; i++) {
        if (s[i] == '\0')
            return i;
    }
    return max_len;
}

static int DB_identifier_valid_sqlite(const char *s) {
    size_t len;
    size_t i;
    unsigned char c;

    if (!s || s[0] == '\0')
        return 0;
    len = DB_strnlen_sqlite(s, 128);
    if (len == 0 || len >= 128)
        return 0;
    c = (unsigned char)s[0];
    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'))
        return 0;
    for (i = 1; i < len; i++) {
        c = (unsigned char)s[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
            return 0;
    }
    return 1;
}

static int _db_cond_is_valid_sqlite(DBClient *self, const char *caller, const char *cond) {
    if (!cond)
        return 1;
    if (cond[0] == '\0') {
        if (self && self->writeLog) {
            char msg[256];
            int n = snprintf(msg, sizeof(msg), "%s blocked: empty condition string", caller ? caller : "SQLite");
            if (n >= 0 && (size_t)n < sizeof(msg))
                self->writeLog(self, msg, cond, 1);
        }
        return 0;
    }
    return 1;
}

static int DB_parse_ll_sqlite(const char *s, long long *out) {
    char *end = NULL;
    long long v;

    if (!s || !out || s[0] == '\0')
        return 0;
    errno = 0;
    v = strtoll(s, &end, 10);
    if (errno != 0)
        return 0;
    if (end == s || *end != '\0')
        return 0;
    *out = v;
    return 1;
}

static void DB_snapshot_error_sqlite(sqlite3 *conn, char *err_msg, char *dst, size_t dst_size) {
    const char *src = NULL;
    if (!dst || dst_size == 0)
        return;
    dst[0] = '\0';
    if (err_msg)
        src = err_msg;
    else if (conn)
        src = sqlite3_errmsg(conn);
    if (src) {
        int n = snprintf(dst, dst_size, "%s", src);
        if (n < 0 || (size_t)n >= dst_size)
            dst[dst_size - 1] = '\0';
    }
}

static int DB_connect_sqlite(DBClient *self) {
    sqlite3 *conn = NULL;
    char err_snap[512] = {0};
    int rc;
    int connected = 0;

    if (!self ||  self->dbname[0] == '\0')
        return 0;
    pthread_mutex_lock(&self->lock);
    if (self->conn) {
        sqlite3_close_v2((sqlite3*)self->conn);
        self->conn = NULL;
    }
    self->isConnected = 0;
    self->in_transaction = 0;
    self->affected_rows = -1;
    self->last_insert_id = 0;
    self->last_idx = 0;
    rc = sqlite3_open_v2(self->dbname, &conn, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, NULL);
    if (rc != SQLITE_OK || !conn) {
        if (conn) {
            DB_snapshot_error_sqlite(conn, NULL, err_snap, sizeof(err_snap));
            sqlite3_close_v2(conn);
            conn = NULL;
        }
        else
            snprintf(err_snap, sizeof(err_snap), "%s", "sqlite3_open_v2 returned NULL");
        pthread_mutex_unlock(&self->lock);
        if (self->writeLog) {
            char msg[768];
            int n = snprintf(msg, sizeof(msg), "SQLite Connect Failed: %s", err_snap[0] ? err_snap : "unknown error");
            if (n >= 0 && (size_t)n < sizeof(msg))
                self->writeLog(self, msg, "CONNECT", 1);
        }
        return 0;
    }
    rc = sqlite3_busy_timeout(conn, 5000);
    if (rc != SQLITE_OK) {
        DB_snapshot_error_sqlite(conn, NULL, err_snap, sizeof(err_snap));
        sqlite3_close_v2(conn); conn = NULL;
        pthread_mutex_unlock(&self->lock);
        if (self->writeLog) {
            char msg[768];
            int n = snprintf(msg, sizeof(msg), "SQLite busy_timeout failed: %s", err_snap[0] ? err_snap : "unknown error");
            if (n >= 0 && (size_t)n < sizeof(msg))
                self->writeLog(self, msg, "CONNECT", 1);
        }
        return 0;
    }
    rc = sqlite3_exec(conn, "PRAGMA foreign_keys = ON;", NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
        DB_snapshot_error_sqlite(conn, NULL, err_snap, sizeof(err_snap));
        sqlite3_close_v2(conn); conn = NULL;
        pthread_mutex_unlock(&self->lock);
        if (self->writeLog) {
            char msg[768];
            int n = snprintf(msg, sizeof(msg), "SQLite foreign_keys PRAGMA failed: %s", err_snap[0] ? err_snap : "unknown error");
            if (n >= 0 && (size_t)n < sizeof(msg))
                self->writeLog(self, msg, "CONNECT", 1);
        }
        return 0;
    }
    (void)sqlite3_exec(conn, "PRAGMA journal_mode = WAL;", NULL, NULL, NULL);
    self->conn = conn;
    self->isConnected = 1;
    self->in_transaction = 0;
    connected = 1;
    pthread_mutex_unlock(&self->lock);
    if (connected && self->writeLog) {
        char msg[512];
        int n = snprintf(msg, sizeof(msg), "SQLite Connected: %s", self->dbname);
        if (n >= 0 && (size_t)n < sizeof(msg))
            self->writeLog(self, msg, "CONNECT", 0);
    }
    return connected;
}

static void DB_disconnect_sqlite(DBClient *self) {
    if (!self) return;
    pthread_mutex_lock(&self->lock);
    if (self->conn) {
        sqlite3_close_v2((sqlite3*)self->conn);
        self->conn = NULL;
    }
    self->isConnected = 0;
    self->in_transaction = 0;
    pthread_mutex_unlock(&self->lock);
}

static int DB_reconnect_sqlite(DBClient *self) {
    if (!self)
        return 0;
    return DB_connect_sqlite(self);
}

static int DB_sqlQuery_sqlite(DBClient *self, const char *query) {
    sqlite3 *conn;
    char *err_msg = NULL;
    char err_snap[512] = {0};
    int rc;

    if (!self || !query) return 0;
    pthread_mutex_lock(&self->lock);
    if (!self->conn) {
        pthread_mutex_unlock(&self->lock);
        return 0;
    }
    conn = (sqlite3*)self->conn;
    snprintf(self->last_query, sizeof(self->last_query), "%s", query);
    self->affected_rows = -1;
    rc = sqlite3_exec(conn, query, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK)
        DB_snapshot_error_sqlite(conn, err_msg, err_snap, sizeof(err_snap));
    if (err_msg) {
        sqlite3_free(err_msg);
        err_msg = NULL;
    }
    if (rc == SQLITE_OK)
        self->affected_rows = DB_changes_sqlite(conn);
    pthread_mutex_unlock(&self->lock);
    if (rc != SQLITE_OK) {
        if (self->writeLog) {
            char msg[768];
            int n = snprintf(msg, sizeof(msg), "SQLite Query Error: %s", err_snap[0] ? err_snap : "unknown error");
            if (n >= 0 && (size_t)n < sizeof(msg))
                self->writeLog(self, msg, query, 1);
        }
        return 0;
    }
    if (self->writeLog)
        self->writeLog(self, "QUERY_OK", query, 0);
    return 1;
}

static int DB_tx_query_sqlite(DBClient *self, const char *sql, int target_state) {
    sqlite3 *conn;
    char *err_msg = NULL;
    char err_snap[512] = {0};
    int rc;
    if (!self || !sql) return 0;

    pthread_mutex_lock(&self->lock);
    if (!self->conn) {
        pthread_mutex_unlock(&self->lock);
        return 0;
    }
    conn = (sqlite3*)self->conn;
    snprintf(self->last_query, sizeof(self->last_query), "%s", sql);
    self->affected_rows = -1;
    rc = sqlite3_exec(conn, sql, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK)
        DB_snapshot_error_sqlite(conn, err_msg, err_snap, sizeof(err_snap));
    if (err_msg) {
        sqlite3_free(err_msg);
        err_msg = NULL;
    }
    if (rc == SQLITE_OK)
        self->in_transaction = target_state;
    pthread_mutex_unlock(&self->lock);
    if (rc != SQLITE_OK) {
        if (self->writeLog) {
            char msg[768];
            int n = snprintf(msg, sizeof(msg), "SQLite TX Error: %s", err_snap[0] ? err_snap : "unknown error");
            if (n >= 0 && (size_t)n < sizeof(msg))
                self->writeLog(self, msg, sql, 1);
        }
        return 0;
    }
    if (self->writeLog)
        self->writeLog(self, "TX_OK", sql, 0);
    return 1;
}

static int DB_beginTransaction_sqlite(DBClient *self) {
    return DB_tx_query_sqlite(self, "BEGIN TRANSACTION;", 1);
}
static int DB_commit_sqlite(DBClient *self) {
    return DB_tx_query_sqlite(self, "COMMIT;", 0);
}
static int DB_rollback_sqlite(DBClient *self) {
    return DB_tx_query_sqlite(self, "ROLLBACK;", 0);
}

static ArrayList* DB_getRecordsFromQuery_sqlite(DBClient *self, const char *query) {
    sqlite3 *conn;
    sqlite3_stmt *stmt = NULL;
    char err_snap[512] = {0};
    int step_rc = SQLITE_DONE;
    int cols;
    int mem_fail = 0;
    long long rows = 0;
    ArrayList *list = NULL;
    if (!self || !query) return NULL;

    pthread_mutex_lock(&self->lock);
    if (!self->conn) {
        pthread_mutex_unlock(&self->lock);
        return NULL;
    }
    conn = (sqlite3*)self->conn;
    snprintf(self->last_query, sizeof(self->last_query), "%s", query);
    self->affected_rows = -1;
    if (sqlite3_prepare_v2(conn, query, -1, &stmt, NULL) != SQLITE_OK) {
        DB_snapshot_error_sqlite(conn, NULL, err_snap, sizeof(err_snap));
        pthread_mutex_unlock(&self->lock);
        if (self->writeLog) {
            char msg[768];
            int n = snprintf(msg, sizeof(msg), "SQLite prepare failed: %s", err_snap[0] ? err_snap : "unknown error");
            if (n >= 0 && (size_t)n < sizeof(msg))
                self->writeLog(self, msg, query, 1);
        }
        return NULL;
    }
    list = new_ArrayList(16);
    if (!list) {
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&self->lock);
        return NULL;
    }
    cols = sqlite3_column_count(stmt);
    while ((step_rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        HashMap *row; int i;
        row = new_HashMap(16);
        if (!row) {
            mem_fail = 1;
            break;
        }
        for (i = 0; i < cols; i++) {
            const char *col_name;
            const unsigned char *value;
            String *s;

            col_name = sqlite3_column_name(stmt, i);
            value = sqlite3_column_text(stmt, i);
            if (!col_name) {
                mem_fail = 1;
                break;
            }
            s = new_String(value ? (const char*)value : "");
            if (!s) {
                mem_fail = 1;
                break;
            }
            row->put(row, col_name, (Object*)s);
            RELEASE(s);
        }
        if (mem_fail) {
            RELEASE(row);
            break;
        }
        list->add(list, (Object*)row);
        RELEASE(row);
        rows++;
    }
    if (mem_fail || step_rc != SQLITE_DONE) {
        if (!mem_fail)
            DB_snapshot_error_sqlite(conn, NULL, err_snap, sizeof(err_snap));
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&self->lock);
        RELEASE(list);
        if (self->writeLog) {
            char msg[768];
            int n;
            if (mem_fail) n = snprintf(msg, sizeof(msg), "%s", "SQLite step failed: Out of Memory");
            else n = snprintf(msg, sizeof(msg), "SQLite step failed: %s", err_snap[0] ? err_snap : "unknown error");
            if (n >= 0 && (size_t)n < sizeof(msg))
                self->writeLog(self, msg, query, 1);
        }
        return NULL;
    }
    self->affected_rows = rows;
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&self->lock);
    return list;
}

static HashMap* DB_getRecordFromQuery_sqlite(DBClient *self, const char *sql) {
    ArrayList *list;
    HashMap *row = NULL;
    if (!self || !sql) return NULL;
    list = self->getRecordsFromQuery(self, sql);
    if (list && list->getSize(list) > 0)
        row = (HashMap*)RETAIN(list->get(list, 0));
    if (list)
        RELEASE(list);
    return row;
}

static ArrayList* DB_getRecords_sqlite(DBClient *self, const char *table, const char *cond, const char *fields) {
    char q[4096];
    int has_cond;
    int has_fields;
    int n;
    if (!self || !DB_identifier_valid_sqlite(table))
        return NULL;
    has_cond = cond && cond[0] != '\0';
    has_fields = fields && fields[0] != '\0';
    n = snprintf(q, sizeof(q), "SELECT %s FROM \"%s\" %s%s;", has_fields ? fields : "*", table, has_cond ? "WHERE " : "", has_cond ? cond : "");
    if (n < 0 || (size_t)n >= sizeof(q))
        return NULL;
    return DB_getRecordsFromQuery_sqlite(self, q);
}

static HashMap* DB_getRecord_sqlite(DBClient *self, const char *table, const char *cond, const char *fields) {
    char q[4096];
    int has_cond;
    int has_fields;
    int n;
    if (!self || !DB_identifier_valid_sqlite(table))
        return NULL;
    has_cond = cond && cond[0] != '\0';
    has_fields = fields && fields[0] != '\0';
    n = snprintf(q, sizeof(q), "SELECT %s FROM \"%s\" %s%s LIMIT 1;", has_fields ? fields : "*", table, has_cond ? "WHERE " : "", has_cond ? cond : "");
    if (n < 0 || (size_t)n >= sizeof(q))
        return NULL;
    return DB_getRecordFromQuery_sqlite(self, q);
}

static char* DB_escape_string_sqlite(DBClient *self, const char *str) {
    char *escaped;
    char *result;
    size_t len;
    (void)self;
    if (!str)
        return NULL;
    escaped = sqlite3_mprintf("%q", str);
    if (!escaped)
        return NULL;
    len = strlen(escaped);
    result = (char*)malloc(len + 1);
    if (result)
        memcpy(result, escaped, len + 1);
    sqlite3_free(escaped);
    return result;
}

static ArrayList* DB_descTable_sqlite(DBClient *self, const char *table_name) {
    char query[256];
    int n;
    if (!self || !DB_identifier_valid_sqlite(table_name))
        return NULL;
    n = snprintf(query, sizeof(query), "PRAGMA table_info('%s');", table_name);
    if (n < 0 || (size_t)n >= sizeof(query))
        return NULL;
    return DB_getRecordsFromQuery_sqlite(self, query);
}

static int DB_table_exists_sqlite(DBClient *self, const char *table) {
    char query[512];
    HashMap *row;
    int n;
    if (!self || !DB_identifier_valid_sqlite(table))
        return 0;
    n = snprintf(query, sizeof(query), "SELECT name FROM sqlite_master WHERE type='table' AND name='%s';", table);
    if (n < 0 || (size_t)n >= sizeof(query))
        return 0;
    row = DB_getRecordFromQuery_sqlite(self, query);
    if (!row)
        return 0;
    RELEASE(row);
    return 1;
}

static int DB_fieldExists_sqlite(DBClient *self, const char *table, const char *field) {
    ArrayList *schema;
    int exists = 0;
    int size;
    int i;
    if (!self || !DB_identifier_valid_sqlite(table) || !DB_identifier_valid_sqlite(field))
        return 0;
    schema = DB_descTable_sqlite(self, table);
    if (!schema)
        return 0;
    size = schema->getSize(schema);
    for (i = 0; i < size; i++) {
        HashMap *col; String *name_val;
        col = (HashMap*)schema->get(schema, i);
        if (!col) continue;
        name_val = (String*)col->get(col, "name");
        if (name_val && name_val->value && strcasecmp(name_val->value, field) == 0) {
            exists = 1;
            break;
        }
    }
    RELEASE(schema);
    return exists;
}

static long long DB_getDataAggregate_sqlite(DBClient *self, const char *table, const char *field, const char *cond, const char *func) {
    char query[2048];
    HashMap *row;
    int is_star;
    int has_cond;
    int n;
    long long result = 0;

    if (!self || !func || !DB_identifier_valid_sqlite(table))
        return 0;
    is_star = field && field[0] == '*' && field[1] == '\0';
    if (!is_star && (!field || !DB_identifier_valid_sqlite(field)))
        return 0;
    has_cond = cond && cond[0] != '\0';
    n = snprintf(query, sizeof(query), "SELECT %s(%s) AS agg FROM \"%s\" %s%s;", func, field, table, has_cond ? "WHERE " : "", has_cond ? cond : "");
    if (n < 0 || (size_t)n >= sizeof(query)) return 0;
    row = DB_getRecordFromQuery_sqlite(self, query);
    if (!row)
        return 0;
    {
        String *s = (String*)row->get(row, "agg");
        if (s && s->value) {
            long long parsed;
            if (DB_parse_ll_sqlite(s->value, &parsed))
                result = parsed;
        }
    }
    RELEASE(row);
    return result;
}

static int DB_getDataCount_sqlite(DBClient *self, const char *table, const char *cond) {
    long long v = DB_getDataAggregate_sqlite(self, table, "*", cond, "COUNT");
    if (v < 0 || v > INT_MAX)
        return 0;
    return (int)v;
}
static long long DB_getDataSum_sqlite(DBClient *self, const char *table, const char *field, const char *cond) {
    return DB_getDataAggregate_sqlite(self, table, field, cond, "SUM");
}
static long long DB_getDataMax_sqlite(DBClient *self, const char *table, const char *field, const char *cond) {
    return DB_getDataAggregate_sqlite(self, table, field, cond, "MAX");
}
static long long DB_getDataMin_sqlite(DBClient *self, const char *table, const char *field, const char *cond) {
    return DB_getDataAggregate_sqlite(self, table, field, cond, "MIN");
}

static HashMap* DB_validateFields_sqlite(DBClient *self, const char *table, HashMap *raw) {
    ArrayList *schema;
    ArrayList *keys;
    HashMap *clean;
    int schema_size;
    int keys_size;
    int i;

    if (!self || !raw || !DB_identifier_valid_sqlite(table)) return NULL;
    schema = DB_descTable_sqlite(self, table);
    if (!schema)
        return NULL;
    clean = new_HashMap(16);
    if (!clean) { RELEASE(schema); return NULL; }
    keys = raw->keys(raw);
    if (!keys) {
        RELEASE(clean);
        RELEASE(schema);
        return NULL;
    }
    schema_size = schema->getSize(schema);
    keys_size = keys->getSize(keys);
    for (i = 0; i < keys_size; i++) {
        String *k; int exists = 0; int j;
        k = (String*)keys->get(keys, i);
        if (!k || !k->value || !DB_identifier_valid_sqlite(k->value)) continue;
        for (j = 0; j < schema_size; j++) {
            HashMap *col; String *name_val;
            col = (HashMap*)schema->get(schema, j);
            if (!col)
                continue;
            name_val = (String*)col->get(col, "name");
            if (name_val && name_val->value && strcasecmp(name_val->value, k->value) == 0) {
                exists = 1;
                break;
            }
        }
        if (exists) {
            Object *val = raw->get(raw, k->value);
            if (val) {
                RETAIN(val);
                clean->put(clean, k->value, val);
                RELEASE(val);
            }
        }
    }
    RELEASE(keys);
    RELEASE(schema);
    return clean;
}

static void _DB_get_pk_info_sqlite(DBClient *self, const char *table_name, int *is_single_pk, int *is_integer_pk) {
    ArrayList *schema;
    int pk_count = 0;
    int integer_pk_count = 0;
    int size;
    int i;
    if (!is_single_pk || !is_integer_pk)
        return;
    *is_single_pk = 0;
    *is_integer_pk = 0;
    if (!self || !DB_identifier_valid_sqlite(table_name))
        return;
    schema = DB_descTable_sqlite(self, table_name);
    if (!schema)
        return;
    size = schema->getSize(schema);
    for (i = 0; i < size; i++) {
        HashMap *col; String *pk_val;
        col = (HashMap*)schema->get(schema, i);
        if (!col)
            continue;
        pk_val = (String*)col->get(col, "pk");
        if (!pk_val || !pk_val->value || pk_val->value[0] < '1' || pk_val->value[0] > '9')
            continue;
        pk_count++;
        {
            String *type_val = (String*)col->get(col, "type");
            if (type_val && type_val->value && strcasecmp(type_val->value, "INTEGER") == 0)
                integer_pk_count++;
        }
    }
    RELEASE(schema);
    if (pk_count == 1) {
        *is_single_pk = 1;
        if (integer_pk_count == 1)
            *is_integer_pk = 1;
    }
}

static int _DB_get_single_integer_pk_sqlite(DBClient *self, const char *table, char *pk_col, size_t pk_col_size) {
    ArrayList *schema;
    int pk_count = 0;
    int integer_pk = 0;
    int size;
    int i;
    char temp_pk[128] = {0};

    if (!self || !DB_identifier_valid_sqlite(table) || !pk_col || pk_col_size == 0)
        return 0;
    pk_col[0] = '\0';
    schema = DB_descTable_sqlite(self, table);
    if (!schema)
        return 0;
    size = schema->getSize(schema);
    for (i = 0; i < size; i++) {
        HashMap *col; String *pk_val; String *type_val; String *name_val;
        col = (HashMap*)schema->get(schema, i);
        if (!col)
            continue;
        pk_val = (String*)col->get(col, "pk");
        if (!pk_val || !pk_val->value || pk_val->value[0] < '1' || pk_val->value[0] > '9')
            continue;
        pk_count++;
        type_val = (String*)col->get(col, "type");
        name_val = (String*)col->get(col, "name");
        if (type_val && type_val->value && name_val && name_val->value && strcasecmp(type_val->value, "INTEGER") == 0 && DB_identifier_valid_sqlite(name_val->value)) {
            int n = snprintf(temp_pk, sizeof(temp_pk), "%s", name_val->value);
            if (n >= 0 && (size_t)n < sizeof(temp_pk))
                integer_pk = 1;
            else {
                temp_pk[0] = '\0';
                integer_pk = 0;
            }
        }
    }
    RELEASE(schema);
    if (pk_count != 1 || !integer_pk || temp_pk[0] == '\0')
        return 0;
    {
        int n = snprintf(pk_col, pk_col_size, "%s", temp_pk);
        if (n < 0 || (size_t)n >= pk_col_size) {
            pk_col[0] = '\0';
            return 0;
        }
    }
    return 1;
}

static long long DB_getNextIdx_sqlite(DBClient *self, const char *table) {
    char pk_col[128] = {0};
    char q[512];
    HashMap *row;
    long long value = 0;
    int n;

    if (!self || !DB_identifier_valid_sqlite(table))
        return 0;
    pthread_mutex_lock(&self->lock);
    self->last_idx = 0;
    pthread_mutex_unlock(&self->lock);

    if (!_DB_get_single_integer_pk_sqlite(self, table, pk_col, sizeof(pk_col)))
        return 0;
    n = snprintf(q, sizeof(q), "SELECT COALESCE(MAX(\"%s\"),0)+1 AS nextval FROM \"%s\";", pk_col, table);

    if (n < 0 || (size_t)n >= sizeof(q))
        return 0;
    row = self->getRecordFromQuery(self, q);
    if (!row)
        return 0;
    {
        String *s = (String*)row->get(row, "nextval");
        if (s && s->value) {
            long long parsed;
            if (DB_parse_ll_sqlite(s->value, &parsed) && parsed > 0)
                value = parsed;
        }
    }
    RELEASE(row);
    if (value > 0) {
        pthread_mutex_lock(&self->lock);
        self->last_idx = value;
        pthread_mutex_unlock(&self->lock);
    }
    return value;
}

static char* _build_insert_replace_sql(DBClient *self, const char *table, HashMap *data, int is_replace) {
    char cols[2048] = {0};
    char vals[4096] = {0};
    ArrayList *keys;
    int size;
    int i;
    char *query;
    const char *keyword;

    if (!self || !data || !DB_identifier_valid_sqlite(table))
        return NULL;
    keys = data->keys(data);
    if (!keys)
        return NULL;
    size = keys->getSize(keys);
    if (size <= 0) {
        RELEASE(keys);
        return NULL;
    }
    for (i = 0; i < size; i++) {
        String *k; Object *v_obj;
        char *escaped = NULL;
        k = (String*)keys->get(keys, i);
        if (!k || !k->value || !DB_identifier_valid_sqlite(k->value))
            goto fail;
        v_obj = data->get(data, k->value);
        if (v_obj) {
            String *v_str = (String*)v_obj;
            escaped = self->escape_string(self, v_str->value);
            if (!escaped)
                goto fail;
        }
        if (i > 0) {
            if (!safe_append(cols, sizeof(cols), ", ") || !safe_append(vals, sizeof(vals), ", ")) {
                if (escaped)
                    free(escaped);
                goto fail;
            }
        }
        if (!safe_append(cols, sizeof(cols), "\"") || !safe_append(cols, sizeof(cols), k->value) || !safe_append(cols, sizeof(cols), "\"")) {
            if (escaped)
                free(escaped);
            goto fail;
        }
        if (escaped) {
            if (!safe_append(vals, sizeof(vals), "'") || !safe_append(vals, sizeof(vals), escaped) || !safe_append(vals, sizeof(vals), "'")) {
                free(escaped);
                goto fail;
            }
            free(escaped);
            escaped = NULL;
        } else {
            if (!safe_append(vals, sizeof(vals), "NULL"))
                goto fail;
        }
    }
    RELEASE(keys);
    query = (char*)malloc(8192);
    if (!query)
        return NULL;
    keyword = is_replace ? "REPLACE" : "INSERT";
    {
        int n = snprintf(query, 8192, "%s INTO \"%s\" (%s) VALUES (%s);", keyword, table, cols, vals);
        if (n < 0 || n >= 8192) {
            free(query);
            return NULL;
        }
    }
    return query;
fail:
    RELEASE(keys);
    return NULL;
}

static int DB_insertTable_sqlite_internal(DBClient *self, const char *table, HashMap *raw_data, int is_replace) {
    HashMap *data;
    char *query;
    int is_single_pk = 0;
    int is_integer_pk = 0;
    sqlite3 *conn;
    char *err_msg = NULL;
    char err_snap[512] = {0};
    int rc;

    if (!self || !table || !raw_data)
        return 0;
    pthread_mutex_lock(&self->lock);
    self->last_insert_id = 0;
    self->last_idx = 0;
    pthread_mutex_unlock(&self->lock);
    if (!DB_identifier_valid_sqlite(table))
        return 0;
    data = self->validateFields(self, table, raw_data);
    if (!data || data->getSize(data) == 0) {
        if (data) RELEASE(data);
        return 0;
    }
    query = _build_insert_replace_sql(self, table, data, is_replace);
    RELEASE(data);
    if (!query)
        return 0;
    _DB_get_pk_info_sqlite(self, table, &is_single_pk, &is_integer_pk);
    pthread_mutex_lock(&self->lock);
    if (!self->conn) {
        pthread_mutex_unlock(&self->lock);
        free(query);
        return 0;
    }
    conn = (sqlite3*)self->conn;
    snprintf(self->last_query, sizeof(self->last_query), "%s", query);
    self->affected_rows = -1;
    rc = sqlite3_exec(conn, query, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK)
        DB_snapshot_error_sqlite(conn, err_msg, err_snap, sizeof(err_snap));
    if (err_msg) {
        sqlite3_free(err_msg);
        err_msg = NULL;
    }
    if (rc == SQLITE_OK) {
        self->affected_rows = DB_changes_sqlite(conn);
        if (is_single_pk && is_integer_pk) {
            sqlite3_int64 id = sqlite3_last_insert_rowid(conn);
            if (id > 0) {
                self->last_insert_id = (long long)id;
                self->last_idx = self->last_insert_id;
            }
        }
    }
    pthread_mutex_unlock(&self->lock);
    if (rc != SQLITE_OK) {
        if (self->writeLog) {
            char msg[768];
            int n = snprintf(msg, sizeof(msg), "SQLite %s Error: %s", is_replace ? "Replace" : "Insert", err_snap[0] ? err_snap : "unknown error");
            if (n >= 0 && (size_t)n < sizeof(msg))
                self->writeLog(self, msg, query, 1);
        }
        free(query); return 0;
    }
    if (self->writeLog)
        self->writeLog(self, "QUERY_OK", query, 0);
    free(query);
    return 1;
}

static int DB_insertTable_sqlite(DBClient *self, const char *table, HashMap *raw_data) {
    return DB_insertTable_sqlite_internal(self, table, raw_data, 0);
}
static int DB_replaceTable_sqlite(DBClient *self, const char *table, HashMap *raw_data) {
    return DB_insertTable_sqlite_internal(self, table, raw_data, 1);
}

static char* _build_update_sql(DBClient *self, const char *table, HashMap *data, const char *cond) {
    char sets[4096] = {0};
    ArrayList *keys;
    int size;
    int i;
    char *query;
    int has_cond;
    int n;

    if (!self || !data || !DB_identifier_valid_sqlite(table))
        return NULL;
    keys = data->keys(data);
    if (!keys) return NULL;
    size = keys->getSize(keys);
    if (size <= 0) {
        RELEASE(keys);
        return NULL;
    }
    for (i = 0; i < size; i++) {
        String *k; Object *v_obj;
        char *escaped = NULL;
        k = (String*)keys->get(keys, i);
        if (!k || !k->value || !DB_identifier_valid_sqlite(k->value))
            goto fail;
        v_obj = data->get(data, k->value);
        if (v_obj) {
            String *v_str = (String*)v_obj;
            escaped = self->escape_string(self, v_str->value);
            if (!escaped)
                goto fail;
        }
        if (i > 0) { if (!safe_append(sets, sizeof(sets), ", ")) { if (escaped) free(escaped); goto fail; } }
        if (!safe_append(sets, sizeof(sets), "\"") || !safe_append(sets, sizeof(sets), k->value) || !safe_append(sets, sizeof(sets), "\"=")) {
            if (escaped) free(escaped);
            goto fail;
        }
        if (escaped) {
            if (!safe_append(sets, sizeof(sets), "'") || !safe_append(sets, sizeof(sets), escaped) || !safe_append(sets, sizeof(sets), "'")) {
                free(escaped);
                goto fail;
            }
            free(escaped); escaped = NULL;
        } else {
            if (!safe_append(sets, sizeof(sets), "NULL"))
                goto fail;
        }
    }
    RELEASE(keys);
    query = (char*)malloc(8192);
    if (!query)
        return NULL;
    has_cond = cond && cond[0] != '\0';
    if (has_cond)
        n = snprintf(query, 8192, "UPDATE \"%s\" SET %s WHERE %s;", table, sets, cond);
    else
        n = snprintf(query, 8192, "UPDATE \"%s\" SET %s;", table, sets);
    if (n < 0 || n >= 8192) {
        free(query);
        return NULL;
    }
    return query;
fail:
    RELEASE(keys);
    return NULL;
}

static int DB_updateTable_sqlite(DBClient *self, const char *table, HashMap *raw_data, const char *cond) {
    HashMap *data;
    char *query;
    int ok;
    if (!self || !raw_data || !DB_identifier_valid_sqlite(table))
        return 0;
    if (!_db_cond_is_valid_sqlite(self, "updateTable", cond))
        return 0;
    data = self->validateFields(self, table, raw_data);
    if (!data || data->getSize(data) == 0) {
        if (data)
            RELEASE(data);
        return 0;
    }
    query = _build_update_sql(self, table, data, cond);
    RELEASE(data);
    if (!query)
        return 0;
    ok = DB_sqlQuery_sqlite(self, query);
    free(query);
    return ok;
}

static int DB_deleteTable_sqlite(DBClient *self, const char *table, const char *cond) {
    char query[2048];
    int has_cond;
    int n;

    if (!self || !DB_identifier_valid_sqlite(table))
        return 0;
    if (!_db_cond_is_valid_sqlite(self, "deleteTable", cond))
        return 0;
    has_cond = cond && cond[0] != '\0';
    if (has_cond)
        n = snprintf(query, sizeof(query), "DELETE FROM \"%s\" WHERE %s;", table, cond);
    else
        n = snprintf(query, sizeof(query), "DELETE FROM \"%s\";", table);
    if (n < 0 || (size_t)n >= sizeof(query))
        return 0;
    return DB_sqlQuery_sqlite(self, query);
}

static void DB_setOption_sqlite(DBClient *self, int option, const void *arg, size_t arg_size) {
    (void)self;
    (void)option;
    (void)arg;
    (void)arg_size;
}
static int DB_dropTable_stub_sqlite(DBClient *self, const char *table) {
    (void)self;
    (void)table;
    return 0;
}
static int DB_initTable_stub_sqlite(DBClient *self, const char *table) {
    (void)self;
    (void)table;
    return 0;
}
static long long DB_getTableSize_stub_sqlite(DBClient *self, const char *table) {
    (void)self;
    (void)table;
    return 0;
}
static char* DB_makeTable_stub_sqlite(DBClient *self, const char *table) {
    (void)self;
    (void)table;
    return NULL;
}
static int DB_copyTable_stub_sqlite(DBClient *self, const char *newT, const char *orgT, int copyData) {
    (void)self;
    (void)newT;
    (void)orgT;
    (void)copyData; return 0;
}
static int DB_renameTable_stub_sqlite(DBClient *self, const char *oldT, const char *newT) {
    (void)self;
    (void)oldT;
    (void)newT; return 0;
}
static int DB_allDelete_stub_sqlite(DBClient *self, const char *table) {
    (void)self;
    (void)table;
    return 0;
}

void bind_sqlite(DBClient *self) {
    if (!self)
        return;
    self->connect = DB_connect_sqlite;
    self->disconnect = DB_disconnect_sqlite;
    self->reconnect = DB_reconnect_sqlite;
    self->setOption = DB_setOption_sqlite;
    self->sqlQuery = DB_sqlQuery_sqlite;
    self->getRecordFromQuery = DB_getRecordFromQuery_sqlite;
    self->getRecordsFromQuery = DB_getRecordsFromQuery_sqlite;
    self->getRecord = DB_getRecord_sqlite;
    self->getRecords = DB_getRecords_sqlite;
    self->insertTable = DB_insertTable_sqlite;
    self->replaceTable = DB_replaceTable_sqlite;
    self->updateTable = DB_updateTable_sqlite;
    self->deleteTable = DB_deleteTable_sqlite;
    self->getDataCount = DB_getDataCount_sqlite;
    self->getDataSum = DB_getDataSum_sqlite;
    self->getDataMax = DB_getDataMax_sqlite;
    self->getDataMin = DB_getDataMin_sqlite;
    self->getNextIdx = DB_getNextIdx_sqlite;
    self->descTable = DB_descTable_sqlite;
    self->validateFields = DB_validateFields_sqlite;
    self->table_exists = DB_table_exists_sqlite;
    self->fieldExists = DB_fieldExists_sqlite;
    self->dropTable = DB_dropTable_stub_sqlite;
    self->initTable = DB_initTable_stub_sqlite;
    self->makeTable = DB_makeTable_stub_sqlite;
    self->copyTable = DB_copyTable_stub_sqlite;
    self->renameTable = DB_renameTable_stub_sqlite;
    self->all_delete_table = DB_allDelete_stub_sqlite;
    self->getTableSize = DB_getTableSize_stub_sqlite;
    self->escape_string = DB_escape_string_sqlite;
    self->beginTransaction = DB_beginTransaction_sqlite;
    self->commit = DB_commit_sqlite;
    self->rollback = DB_rollback_sqlite;
}
#endif /* HAVE_SQLITE */
