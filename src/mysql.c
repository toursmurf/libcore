#include "db.h"
#include <mysql/mysql.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void DB_apply_saved_options(DBClient *self) {
    if (!self ||!self->conn) return;
    for (int i = 0; i < self->option_count; i++) {
        mysql_options((MYSQL*)self->conn, (enum mysql_option)self->options[i].option,
            self->options[i].value_size > 0? self->options[i].value : NULL);
    }
}

static int DB_reconnect_internal(DBClient *self) {
    if (!self) return 0;
    if (self->conn) { mysql_close((MYSQL*)self->conn); self->conn = NULL; }
    self->conn = (void *)mysql_init(NULL);
    if (!self->conn) return 0;
    DB_apply_saved_options(self);
    if (mysql_real_connect((MYSQL*)self->conn, self->host, self->dbid, self->dbpass, self->dbname, self->port, NULL, 0)) {
        mysql_set_character_set((MYSQL*)self->conn, self->charset);
        self->isConnected = 1;
    } else {
        self->isConnected = 0;
    }
    return self->isConnected;
}

static void DB_setOption_my(DBClient *self, int option, const void *arg, size_t arg_size) {
    if (!self) return;
    pthread_mutex_lock(&self->lock);
    if (!self->conn) self->conn = (void *)mysql_init(NULL);
    if (self->conn) mysql_options((MYSQL*)self->conn, (enum mysql_option)option, arg);
    if (self->option_count < 16) {
        self->options[self->option_count].option = option;
        size_t cp = (arg_size < 64)? arg_size : 63;
        self->options[self->option_count].value_size = cp;
        memset(self->options[self->option_count].value, 0, 64);
        if (arg && cp > 0) memcpy(self->options[self->option_count].value, arg, cp);
        self->option_count++;
    }
    pthread_mutex_unlock(&self->lock);
}

/*
   // main 시작
   mysql_library_init(0, NULL, NULL);

   DBClient *db = new_DBClientDirect(...);
   db->connect(db);
   // ... 작업 ...
   db->disconnect(db);
   RELEASE((Object*)db);

   // main 종료 직전
   mysql_library_end();
*/
static int DB_connect_my(DBClient *self) {
    if (!self) return 0;
    int res = 0;
    pthread_mutex_lock(&self->lock);
    if (!self->conn) {
        self->conn = (void *)mysql_init(NULL);
        DB_apply_saved_options(self);
    }
    if (self->conn && mysql_real_connect((MYSQL*)self->conn, self->host, self->dbid, self->dbpass, self->dbname, self->port, NULL, 0)) {
        mysql_set_character_set((MYSQL*)self->conn, self->charset);
        self->isConnected = 1;
        res = 1;
    } else {
        if (self->conn) { mysql_close((MYSQL*)self->conn); self->conn = NULL; }
        self->isConnected = 0;
        res = 0;
    }
    pthread_mutex_unlock(&self->lock);
    return res;
}

static int DB_reconnect_my(DBClient *self) {
    if (!self) return 0;
    pthread_mutex_lock(&self->lock);
    if (self->conn) { mysql_close((MYSQL*)self->conn); self->conn = NULL; }
    self->conn = (void *)mysql_init(NULL);
    if (self->conn) DB_apply_saved_options(self);
    if (self->conn && mysql_real_connect((MYSQL*)self->conn, self->host, self->dbid, self->dbpass, self->dbname, self->port, NULL, 0)) {
        mysql_set_character_set((MYSQL*)self->conn, self->charset);
        self->isConnected = 1;
    } else {
        if (self->conn) { mysql_close((MYSQL*)self->conn); self->conn = NULL; }
        self->isConnected = 0;
    }
    pthread_mutex_unlock(&self->lock);
    return self->isConnected;
}

static void DB_disconnect_my(DBClient *self) {
    if (!self) return;
    pthread_mutex_lock(&self->lock);
    if (self->isConnected && self->conn) {
        mysql_close((MYSQL*)self->conn);
        self->conn = NULL;
        self->isConnected = 0;
    }
    pthread_mutex_unlock(&self->lock);
}

static int DB_sqlQuery_my(DBClient *self, const char* sql) {
    if (!self ||!sql) return 0;
    char err_snapshot[512] = {0};
    int res;
    pthread_mutex_lock(&self->lock);
    snprintf(self->last_query, sizeof(self->last_query), "%s", sql);
    if (!self->conn) { pthread_mutex_unlock(&self->lock); return 0; }
    res = mysql_query((MYSQL*)self->conn, sql);
    if (res!= 0) {
        unsigned int en = mysql_errno((MYSQL*)self->conn);
        if ((en == 2006 || en == 2013) && DB_reconnect_internal(self)) {
            res = mysql_query((MYSQL*)self->conn, sql);
        }
        if (res!= 0) snprintf(err_snapshot, sizeof(err_snapshot), "%s", mysql_error((MYSQL*)self->conn));
    }
    pthread_mutex_unlock(&self->lock);
    if (res!= 0) self->writeLog(self, err_snapshot, sql, 1);
    else if (self->save_log) self->writeLog(self, "QUERY_OK", sql, 0);
    return (res == 0);
}

static char* DB_escape_string_my(DBClient *self, const char* str) {
    if (!self ||!str || str[0] == '\0') {
        char* e = malloc(1);
        if (e) e[0] = '\0';
        return e;
    }
    char *out = malloc(strlen(str) * 2 + 1);
    if (!out) return NULL;
    pthread_mutex_lock(&self->lock);
    if (self->conn) {
        mysql_real_escape_string((MYSQL*)self->conn, out, str, strlen(str));
    } else {
        out[0] = '\0';
    }
    pthread_mutex_unlock(&self->lock);
    return out;
}

static int DB_beginTransaction_my(DBClient *self) {
    if (!self) return 0;
    return self->sqlQuery(self, "START TRANSACTION");
}
static int DB_commit_my(DBClient *self) {
    if (!self) return 0;
    return self->sqlQuery(self, "COMMIT");
}
static int DB_rollback_my(DBClient *self) {
    if (!self) return 0;
    return self->sqlQuery(self, "ROLLBACK");
}

static HashMap* DB_validateFields_my(DBClient *self, const char* table, HashMap* raw) {
    if (!self ||!table ||!raw) return NULL;
    char q[512];
    snprintf(q, sizeof(q), "DESC `%s`", table);
    ArrayList *vcols = new_ArrayList(16);
    if (!vcols) return NULL;
    pthread_mutex_lock(&self->lock);
    if (self->conn && mysql_query((MYSQL*)self->conn, q) == 0) {
        MYSQL_RES *rs = mysql_store_result((MYSQL*)self->conn);
        if (rs) {
            MYSQL_ROW r;
            while ((r = mysql_fetch_row(rs))) {
                String *s = new_String(r[0]);
                if (s) { vcols->add(vcols, (Object*)s); RELEASE(s); }
            }
            mysql_free_result(rs);
        }
    }
    pthread_mutex_unlock(&self->lock);
    HashMap *clean_map = new_HashMap(16);
    if (!clean_map) { RELEASE(vcols); return NULL; }
    for (int b = 0; b < raw->capacity; b++) {
        HashNode *n = raw->buckets[b];
        while (n) {
            for (int j = 0; j < vcols->getSize(vcols); j++) {
                String *c = (String*)vcols->get(vcols, j);
                if (c && strcmp(n->key, c->value) == 0) {
                    clean_map->put(clean_map, n->key, n->value);
                    break;
                }
            }
            n = n->next;
        }
    }
    RELEASE(vcols);
    return clean_map;
}

static int DB_insertTable_my(DBClient *self, const char* table, HashMap* data) {
    if (!self ||!table ||!data) return 0;
    HashMap *clean = self->validateFields(self, table, data);
    if (!clean || clean->getSize(clean) == 0) { if (clean) RELEASE(clean); return 0; }
    char k[4096] = {0}, v[8192] = {0};
    int t = clean->getSize(clean), c = 0;
    for (int b = 0; b < clean->capacity; b++) {
        HashNode *n = clean->buckets[b];
        while (n) {
            String *vs = (String*)n->value;
            char *esc = self->escape_string(self, vs? vs->value : "");
            if (!esc) { RELEASE(clean); return 0; }
            safe_append(k, sizeof(k), "`");
            safe_append(k, sizeof(k), n->key);
            safe_append(k, sizeof(k), "`");
            safe_append(v, sizeof(v), "'");
            safe_append(v, sizeof(v), esc);
            safe_append(v, sizeof(v), "'");
            if (++c < t) { safe_append(k, sizeof(k), ", "); safe_append(v, sizeof(v), ", "); }
            free(esc);
            n = n->next;
        }
    }
    char q[16384];
    snprintf(q, sizeof(q), "INSERT INTO `%s` (%s) VALUES (%s)", table, k, v);
    RELEASE(clean);
    int res = self->sqlQuery(self, q);
    if (res) {
        pthread_mutex_lock(&self->lock);
        if (self->conn) self->last_insert_id = (long long)mysql_insert_id((MYSQL*)self->conn);
        pthread_mutex_unlock(&self->lock);
    }
    return res;
}

static int DB_updateTable_my(DBClient *self, const char* table, HashMap* data, const char* cond) {
    if (!self ||!table ||!data) return 0;
    HashMap *clean = self->validateFields(self, table, data);
    if (!clean || clean->getSize(clean) == 0) { if (clean) RELEASE(clean); return 0; }
    char s[8192] = {0};
    int t = clean->getSize(clean), c = 0;
    for (int b = 0; b < clean->capacity; b++) {
        HashNode *n = clean->buckets[b];
        while (n) {
            String *vs = (String*)n->value;
            char *esc = self->escape_string(self, vs? vs->value : "");
            if (!esc) { RELEASE(clean); return 0; }
            safe_append(s, sizeof(s), "`");
            safe_append(s, sizeof(s), n->key);
            safe_append(s, sizeof(s), "`='");
            safe_append(s, sizeof(s), esc);
            safe_append(s, sizeof(s), "'");
            if (++c < t) safe_append(s, sizeof(s), ", ");
            free(esc);
            n = n->next;
        }
    }
    char q[16384];
    snprintf(q, sizeof(q), "UPDATE `%s` SET %s %s %s", table, s, cond? "WHERE" : "", cond? cond : "");
    RELEASE(clean);
    return self->sqlQuery(self, q);
}

static int DB_replaceTable_my(DBClient *self, const char* table, HashMap* data) {
    if (!self ||!table ||!data) return 0;
    HashMap *clean = self->validateFields(self, table, data);
    if (!clean || clean->getSize(clean) == 0) { if (clean) RELEASE(clean); return 0; }
    char s[8192] = {0};
    int t = clean->getSize(clean), c = 0;
    for (int b = 0; b < clean->capacity; b++) {
        HashNode *n = clean->buckets[b];
        while (n) {
            String *vs = (String*)n->value;
            char *esc = self->escape_string(self, vs? vs->value : "");
            if (!esc) { RELEASE(clean); return 0; }
            safe_append(s, sizeof(s), "`");
            safe_append(s, sizeof(s), n->key);
            safe_append(s, sizeof(s), "`='");
            safe_append(s, sizeof(s), esc);
            safe_append(s, sizeof(s), "'");
            if (++c < t) safe_append(s, sizeof(s), ", ");
            free(esc);
            n = n->next;
        }
    }
    char q[16500];
    snprintf(q, sizeof(q), "INSERT INTO `%s` SET %s ON DUPLICATE KEY UPDATE %s", table, s, s);
    RELEASE(clean);
    int res = self->sqlQuery(self, q);
    if (res) {
        pthread_mutex_lock(&self->lock);
        if (self->conn) self->last_insert_id = (long long)mysql_insert_id((MYSQL*)self->conn);
        pthread_mutex_unlock(&self->lock);
    }
    return res;
}

static int DB_deleteTable_my(DBClient *self, const char* table, const char* cond) {
    if (!self ||!table) return 0;
    char q[4096];
    snprintf(q, sizeof(q), "DELETE FROM `%s` %s %s", table, cond? "WHERE " : "", cond? cond : "");
    return self->sqlQuery(self, q);
}

static int DB_all_delete_table_my(DBClient *self, const char* table) {
    if (!self ||!table) return 0;
    char q[512];
    snprintf(q, sizeof(q), "TRUNCATE TABLE `%s`", table);
    return self->sqlQuery(self, q);
}

static ArrayList* DB_getRecordsFromQuery_my(DBClient *self, const char* sql) {
    if (!self ||!sql) return NULL;
    ArrayList *list = new_ArrayList(16);
    if (!list) return NULL;
    char em[512] = {0};
    pthread_mutex_lock(&self->lock);
    int res = -1;
    if (self->conn) res = mysql_query((MYSQL*)self->conn, sql);
    if (res!= 0 && self->conn) {
        if (mysql_errno((MYSQL*)self->conn) == 2006 && DB_reconnect_internal(self)) {
            res = mysql_query((MYSQL*)self->conn, sql);
        }
        if (res!= 0) snprintf(em, sizeof(em), "%s", mysql_error((MYSQL*)self->conn));
    }
    if (res == 0 && self->conn) {
        MYSQL_RES *rs = mysql_store_result((MYSQL*)self->conn);
        if (rs) {
            int nf = mysql_num_fields(rs);
            MYSQL_FIELD *fs = mysql_fetch_fields(rs);
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(rs))) {
                HashMap *m_node = new_HashMap(16);
                for (int i = 0; i < nf; i++) {
                    String *s = new_String(row[i]? row[i] : "");
                    if (s) { m_node->put(m_node, fs[i].name, (Object*)s); RELEASE(s); }
                }
                list->add(list, (Object*)m_node);
                RELEASE(m_node);
            }
            mysql_free_result(rs);
        }
    }
    pthread_mutex_unlock(&self->lock);
    if (res!= 0) self->writeLog(self, em, sql, 1);
    return list;
}

static HashMap* DB_getRecordFromQuery_my(DBClient *self, const char* sql) {
    if (!self ||!sql) return NULL;
    ArrayList *l = self->getRecordsFromQuery(self, sql);
    HashMap *res_map = NULL;
    if (l && l->getSize(l) > 0) res_map = (HashMap*)RETAIN(l->get(l, 0));
    if (l) RELEASE(l);
    return res_map;
}

static ArrayList* DB_getRecords_my(DBClient *self, const char* t, const char* c, const char* f) {
    if (!self ||!t) return NULL;
    char q[4096];
    snprintf(q, sizeof(q), "SELECT %s FROM `%s` %s %s", f? f : "*", t, c? "WHERE" : "", c? c : "");
    return self->getRecordsFromQuery(self, q);
}

static HashMap* DB_getRecord_my(DBClient *self, const char* t, const char* c, const char* f) {
    if (!self ||!t) return NULL;
    char q[4096];
    snprintf(q, sizeof(q), "SELECT %s FROM `%s` %s %s LIMIT 1", f? f : "*", t, c? "WHERE" : "", c? c : "");
    return self->getRecordFromQuery(self, q);
}

static int DB_getDataCount_my(DBClient *self, const char* t, const char* c) {
    if (!self ||!t) return 0;
    char q[1024];
    snprintf(q, sizeof(q), "SELECT COUNT(*) AS cnt FROM `%s` %s %s", t, c? "WHERE" : "", c? c : "");
    ArrayList *l = self->getRecordsFromQuery(self, q);
    int cnt = 0;
    if (l && l->getSize(l) > 0) {
        HashMap *res_map = (HashMap*)l->get(l, 0);
        String *v = (String*)res_map->get(res_map, "cnt");
        if (v) cnt = atoi(v->value);
    }
    if (l) RELEASE(l);
    return cnt;
}

static long long DB_getDataSum_my(DBClient *self, const char* table, const char* field, const char* cond) {
    if (!self ||!table ||!field) return 0;
    char q[512];
    snprintf(q, sizeof(q), "SELECT SUM(%s) AS val_sum FROM `%s` %s %s", field, table, cond? "WHERE " : "", cond? cond : "");
    HashMap *r = self->getRecordFromQuery(self, q);
    long long sum_val = 0;
    if (r) {
        String *v = (String *)r->get(r, "val_sum");
        if (v) sum_val = atoll(v->value);
        RELEASE(r);
    }
    return sum_val;
}

static long long DB_getDataMax_my(DBClient *self, const char* t, const char* f, const char* c) {
    if (!self ||!t ||!f) return 0;
    char q[1024];
    snprintf(q, sizeof(q), "SELECT MAX(%s) AS mx FROM `%s` %s %s", f, t, c? "WHERE" : "", c? c : "");
    HashMap *res_map = self->getRecordFromQuery(self, q);
    long long val = 0;
    if (res_map) {
        String *v = (String*)res_map->get(res_map, "mx");
        if (v) val = atoll(v->value);
        RELEASE(res_map);
    }
    return val;
}

static long long DB_getDataMin_my(DBClient *self, const char* t, const char* f, const char* c) {
    if (!self ||!t ||!f) return 0;
    char q[1024];
    snprintf(q, sizeof(q), "SELECT MIN(%s) AS mn FROM `%s` %s %s", f, t, c? "WHERE" : "", c? c : "");
    HashMap *res_map = self->getRecordFromQuery(self, q);
    long long val = 0;
    if (res_map) {
        String *v = (String*)res_map->get(res_map, "mn");
        if (v) val = atoll(v->value);
        RELEASE(res_map);
    }
    return val;
}

static int DB_table_exists_my(DBClient *self, const char* t) {
    if (!self ||!t) return 0;
    char q[512];
    snprintf(q, sizeof(q), "SHOW TABLES LIKE '%s'", t);
    int ex = 0;
    pthread_mutex_lock(&self->lock);
    if (self->conn && mysql_query((MYSQL*)self->conn, q) == 0) {
        MYSQL_RES *rs = mysql_store_result((MYSQL*)self->conn);
        if (rs) { ex = (mysql_num_rows(rs) > 0); mysql_free_result(rs); }
    }
    pthread_mutex_unlock(&self->lock);
    return ex;
}

static int DB_fieldExists_my(DBClient *self, const char* t, const char* f) {
    if (!self ||!t ||!f) return 0;
    char q[512];
    snprintf(q, sizeof(q), "SHOW COLUMNS FROM `%s` LIKE '%s'", t, f);
    int ex = 0;
    pthread_mutex_lock(&self->lock);
    if (self->conn && mysql_query((MYSQL*)self->conn, q) == 0) {
        MYSQL_RES *rs = mysql_store_result((MYSQL*)self->conn);
        if (rs) { ex = (mysql_num_rows(rs) > 0); mysql_free_result(rs); }
    }
    pthread_mutex_unlock(&self->lock);
    return ex;
}

static long long DB_getNextIdx_my(DBClient *self, const char* t) {
    if (!self ||!t) return 1;
    char q[1024];
    snprintf(q, sizeof(q), "SELECT MAX(idx) + 1 AS n FROM `%s`", t);
    HashMap *res_map = self->getRecordFromQuery(self, q);
    long long n = 1;
    if (res_map) {
        String *v = (String*)res_map->get(res_map, "n");
        if (v && strlen(v->value) > 0) n = atoll(v->value);
        RELEASE(res_map);
    }
    pthread_mutex_lock(&self->lock);
    self->last_idx = n;
    pthread_mutex_unlock(&self->lock);
    return n;
}

static int DB_copyTable_my(DBClient *self, const char* n, const char* o, int d) {
    if (!self ||!n ||!o) return 0;
    char q1[512];
    snprintf(q1, sizeof(q1), "CREATE TABLE IF NOT EXISTS `%s` LIKE `%s`", n, o);
    if (!self->sqlQuery(self, q1)) return 0;
    if (d) {
        char q2[512];
        snprintf(q2, sizeof(q2), "INSERT IGNORE INTO `%s` SELECT * FROM `%s`", n, o);
        return self->sqlQuery(self, q2);
    }
    return 1;
}

static char* DB_makeTable_my(DBClient *self, const char* t) {
    if (!self || !t) return NULL;
    time_t now = time(NULL);
    struct tm ti_buf;
    localtime_r(&now, &ti_buf);
    char *n = malloc(128);
    if (!n) return NULL;
    snprintf(n, 128, "%s_%04d%02d", t, ti_buf.tm_year + 1900, ti_buf.tm_mon + 1);
    return n;
}

static int DB_renameTable_my(DBClient *self, const char* o, const char* n) {
    if (!self ||!o ||!n) return 0;
    char q[512];
    snprintf(q, sizeof(q), "RENAME TABLE `%s` TO `%s`", o, n);
    return self->sqlQuery(self, q);
}

static ArrayList* DB_descTable_my(DBClient *self, const char* t) {
    if (!self ||!t) return NULL;
    char q[512];
    snprintf(q, sizeof(q), "DESC `%s`", t);
    return self->getRecordsFromQuery(self, q);
}

static long long DB_getTableSize_my(DBClient *self, const char* t) {
    if (!self ||!t) return 0;
    char q[1024];
    snprintf(q, sizeof(q), "SELECT (data_length + index_length) AS s FROM information_schema.tables WHERE table_schema = DATABASE() AND table_name = '%s'", t);
    HashMap *res_map = self->getRecordFromQuery(self, q);
    long long s = 0;
    if (res_map) {
        String *v = (String*)res_map->get(res_map, "s");
        if (v) s = atoll(v->value);
        RELEASE(res_map);
    }
    return s;
}

static int DB_dropTable_my(DBClient *self, const char* t) {
    if (!self ||!t) return 0;
    char q[256];
    snprintf(q, sizeof(q), "DROP TABLE IF EXISTS `%s` ", t);
    return self->sqlQuery(self, q);
}

static int DB_initTable_my(DBClient *self, const char* t) {
    if (!self ||!t) return 0;
    char q[256];
    snprintf(q, sizeof(q), "TRUNCATE TABLE `%s` ", t);
    return self->sqlQuery(self, q);
}

void bind_mysql(DBClient *db) {
    if (!db) return;
    db->setOption = DB_setOption_my;
    db->reconnect = DB_reconnect_my;
    db->connect = DB_connect_my;
    db->disconnect = DB_disconnect_my;
    db->sqlQuery = DB_sqlQuery_my;
    db->escape_string = DB_escape_string_my;
    db->beginTransaction = DB_beginTransaction_my;
    db->commit = DB_commit_my;
    db->rollback = DB_rollback_my;
    db->validateFields = DB_validateFields_my;
    db->table_exists = DB_table_exists_my;
    db->fieldExists = DB_fieldExists_my;
    db->getNextIdx = DB_getNextIdx_my;
    db->insertTable = DB_insertTable_my;
    db->updateTable = DB_updateTable_my;
    db->replaceTable = DB_replaceTable_my;
    db->deleteTable = DB_deleteTable_my;
    db->all_delete_table = DB_all_delete_table_my;
    db->getRecordFromQuery = DB_getRecordFromQuery_my;
    db->getRecordsFromQuery = DB_getRecordsFromQuery_my;
    db->getRecord = DB_getRecord_my;
    db->getRecords = DB_getRecords_my;
    db->getDataCount = DB_getDataCount_my;
    db->getDataSum = DB_getDataSum_my;
    db->getDataMax = DB_getDataMax_my;
    db->getDataMin = DB_getDataMin_my;
    db->copyTable = DB_copyTable_my;
    db->makeTable = DB_makeTable_my;
    db->renameTable = DB_renameTable_my;
    db->descTable = DB_descTable_my;
    db->getTableSize = DB_getTableSize_my;
    db->dropTable = DB_dropTable_my;
    db->initTable = DB_initTable_my;
}
