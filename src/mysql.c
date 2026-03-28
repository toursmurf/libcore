#include "db.h"
#include <mysql/mysql.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 오리지널 매크로 (다형성 호출용)
#define GET_CLASS(obj) ( *( (const Class**) (obj) ) )

/* =========================================================
 * [1] 연결 및 기초 유틸리티
 * ========================================================= */
static int DB_connect_my(DBClient *self) {
    pthread_mutex_lock(&self->lock);

    MYSQL *conn = mysql_init(NULL);
    self->conn = (void *)conn;

    if (mysql_real_connect(conn, self->host, self->dbid, self->dbpass, self->dbname, self->port, NULL, 0)) {
        mysql_set_character_set(conn, strlen(self->charset) > 0 ? self->charset : "utf8mb4");
        self->isConnected = 1;
    }
    pthread_mutex_unlock(&self->lock);

    if (self->isConnected) {
        if (self->save_log) self->writeLog(self, "MySQL Connected", NULL, 0);
    } else {
        self->writeLog(self, mysql_error((MYSQL*)self->conn), "Connect Fail", 1);
    }
    return self->isConnected;
}

static void DB_disconnect_my(DBClient *self) {
    int was_connected = 0;
    pthread_mutex_lock(&self->lock);
    if (self->isConnected && self->conn) {
        mysql_close((MYSQL*)self->conn);
        mysql_library_end();
        self->isConnected = 0;
        self->conn = NULL;
        was_connected = 1;
    }
    pthread_mutex_unlock(&self->lock);

    if (was_connected) {
        self->writeLog(self, "MySQL Disconnected", NULL, 0);
    }
}

static int DB_sqlQuery_my(DBClient *self, const char* sql) {
    pthread_mutex_lock(&self->lock);
    snprintf(self->last_query, sizeof(self->last_query), "%s", sql);
    int res = mysql_query((MYSQL*)self->conn, sql);
    pthread_mutex_unlock(&self->lock);

    if (res == 0) {
        if (self->save_log) self->writeLog(self, "QUERY_OK", sql, 0);
    } else {
        self->writeLog(self, mysql_error((MYSQL*)self->conn), sql, 1);
    }
    return (res == 0);
}

static char* DB_escape_string_my(DBClient *self, const char* str) {
    if (!str) return strdup("");
    char *out = malloc(strlen(str) * 2 + 1);
    pthread_mutex_lock(&self->lock);
    mysql_real_escape_string((MYSQL*)self->conn, out, str, strlen(str));
    pthread_mutex_unlock(&self->lock);
    return out;
}

/* =========================================================
 * [2] 트랜잭션 (START / COMMIT / ROLLBACK)
 * ========================================================= */
static int DB_beginTransaction_my(DBClient *self) { return self->sqlQuery(self, "START TRANSACTION"); }
static int DB_commit_my(DBClient *self) { return self->sqlQuery(self, "COMMIT"); }
static int DB_rollback_my(DBClient *self) { return self->sqlQuery(self, "ROLLBACK"); }

/* =========================================================
 * [3] 스키마 검증 및 정보 조회
 * ========================================================= */
static HashMap* DB_validateFields_my(DBClient *self, const char* table, HashMap* raw_data) {
    char q[512];
    snprintf(q, sizeof(q), "DESC `%s`", table);

    // 🚀 임시 컬럼 리스트 생성 (0바이트 방어)
    ArrayList *valid_cols = new_ArrayList(16);
    pthread_mutex_lock(&self->lock);
    if (mysql_query((MYSQL*)self->conn, q) == 0) {
        MYSQL_RES *res = mysql_store_result((MYSQL*)self->conn);
        if (res) {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res))) {
                String *col_name = new_String(row[0]);
                valid_cols->add(valid_cols, (Object *)col_name);
                RELEASE(col_name);
            }
            mysql_free_result(res);
        }
    }
    pthread_mutex_unlock(&self->lock);

    HashMap *clean_data = new_HashMap(16);
    // raw_data를 순회하며 유효한 필드만 clean_data에 복사
    int b;
    for (b = 0; b < raw_data->capacity; b++) {
        HashNode *node = raw_data->buckets[b];
        while (node) {
            for (int j = 0; j < valid_cols->getSize(valid_cols); j++) {
                String *col = (String *)valid_cols->get(valid_cols, j);
                if (strcmp(node->key, col->value) == 0) {
                    clean_data->put(clean_data, node->key, node->value);
                    break;
                }
            }
            node = node->next;
        }
    }
    RELEASE(valid_cols); // 🚀 임시 리스트 소각 (내부 스트링들도 연쇄 소각)
    return clean_data;
}

static int DB_table_exists_my(DBClient *self, const char* table) {
    char q[512];
    snprintf(q, sizeof(q), "SHOW TABLES LIKE '%s'", table);
    int exists = 0;
    pthread_mutex_lock(&self->lock);
    if (mysql_query((MYSQL*)self->conn, q) == 0) {
        MYSQL_RES *res = mysql_store_result((MYSQL*)self->conn);
        if (res) { exists = (mysql_num_rows(res) > 0); mysql_free_result(res); }
    }
    pthread_mutex_unlock(&self->lock);
    return exists;
}

static int DB_fieldExists_my(DBClient *self, const char* table, const char* field) {
    char q[512];
    snprintf(q, sizeof(q), "SHOW COLUMNS FROM `%s` LIKE '%s'", table, field);
    int exists = 0;
    pthread_mutex_lock(&self->lock);
    if (mysql_query((MYSQL*)self->conn, q) == 0) {
        MYSQL_RES *res = mysql_store_result((MYSQL*)self->conn);
        if (res) { exists = (mysql_num_rows(res) > 0); mysql_free_result(res); }
    }
    pthread_mutex_unlock(&self->lock);
    return exists;
}

static long long DB_getNextIdx_my(DBClient *self, const char* table) {
    char q[512];
    snprintf(q, sizeof(q), "SELECT MAX(idx) + 1 AS next_idx FROM `%s`", table);
    HashMap *rec = self->getRecordFromQuery(self, q);
    long long next_idx = 1;
    if (rec) {
        String *v = (String *)rec->get(rec, "next_idx");
        if (v && strlen(v->value) > 0) next_idx = atoll(v->value);
        RELEASE(rec); // 🚀 사용 후 즉시 소각!
    }
    return next_idx;
}

/* =========================================================
 * [4] 강력한 C.R.U.D 처리 (Insert, Update, Delete)
 * ========================================================= */
static int DB_insertTable_my(DBClient *self, const char* table, HashMap* raw_data) {
    HashMap *data = self->validateFields(self, table, raw_data);
    int total = data->getSize(data);
    if (total == 0) { RELEASE(data); return 0; }

    char k_buf[4096] = {0}, v_buf[8192] = {0};
    int count = 0;
    for (int b = 0; b < data->capacity; b++) {
        HashNode *node = data->buckets[b];
        while (node) {
            String *v = (String *)node->value;
            char *esc = self->escape_string(self, v->value);
            safe_append(k_buf, sizeof(k_buf), "`"); safe_append(k_buf, sizeof(k_buf), node->key); safe_append(k_buf, sizeof(k_buf), "`");
            safe_append(v_buf, sizeof(v_buf), "'"); safe_append(v_buf, sizeof(v_buf), esc); safe_append(v_buf, sizeof(v_buf), "'");
            if (++count < total) { safe_append(k_buf, sizeof(k_buf), ", "); safe_append(v_buf, sizeof(v_buf), ", "); }
            free(esc); node = node->next;
        }
    }
    char q[16384]; snprintf(q, sizeof(q), "INSERT INTO `%s` (%s) VALUES (%s)", table, k_buf, v_buf);
    int res = self->sqlQuery(self, q);
    if (res) {
        pthread_mutex_lock(&self->lock);
        self->last_insert_id = (long long)mysql_insert_id((MYSQL*)self->conn);
        pthread_mutex_unlock(&self->lock);
    }
    RELEASE(data); return res;
}

static int DB_updateTable_my(DBClient *self, const char* table, HashMap* raw_data, const char* cond) {
    HashMap *data = self->validateFields(self, table, raw_data);
    int total = data->getSize(data);
    if (total == 0) { RELEASE(data); return 0; }

    char s_buf[8192] = {0};
    int count = 0;
    for (int b = 0; b < data->capacity; b++) {
        HashNode *node = data->buckets[b];
        while (node) {
            String *v = (String *)node->value;
            char *esc = self->escape_string(self, v->value);
            safe_append(s_buf, sizeof(s_buf), "`"); safe_append(s_buf, sizeof(s_buf), node->key);
            safe_append(s_buf, sizeof(s_buf), "`='"); safe_append(s_buf, sizeof(s_buf), esc); safe_append(s_buf, sizeof(s_buf), "'");
            if (++count < total) safe_append(s_buf, sizeof(s_buf), ", ");
            free(esc); node = node->next;
        }
    }
    char q[16384]; snprintf(q, sizeof(q), "UPDATE `%s` SET %s %s %s", table, s_buf, cond ? "WHERE " : "", cond ? cond : "");
    RELEASE(data); return self->sqlQuery(self, q);
}

static int DB_replaceTable_my(DBClient *self, const char* table, HashMap* raw_data) {
    HashMap *data = self->validateFields(self, table, raw_data);
    int total = data->getSize(data);
    if (total == 0) { RELEASE(data); return 0; }

    char s_buf[8192] = {0};
    int count = 0;
    for (int b = 0; b < data->capacity; b++) {
        HashNode *node = data->buckets[b];
        while (node) {
            String *v = (String *)node->value;
            char *esc = self->escape_string(self, v->value);
            safe_append(s_buf, sizeof(s_buf), "`"); safe_append(s_buf, sizeof(s_buf), node->key);
            safe_append(s_buf, sizeof(s_buf), "`='"); safe_append(s_buf, sizeof(s_buf), esc); safe_append(s_buf, sizeof(s_buf), "'");
            if (++count < total) safe_append(s_buf, sizeof(s_buf), ", ");
            free(esc); node = node->next;
        }
    }
    char q[16384]; snprintf(q, sizeof(q), "INSERT INTO `%s` SET %s ON DUPLICATE KEY UPDATE %s", table, s_buf, s_buf);
    int res = self->sqlQuery(self, q);
    if (res) {
        pthread_mutex_lock(&self->lock);
        self->last_insert_id = (long long)mysql_insert_id((MYSQL*)self->conn);
        pthread_mutex_unlock(&self->lock);
    }
    RELEASE(data); return res;
}

static int DB_deleteTable_my(DBClient *self, const char* table, const char* cond) {
    char q[4096]; snprintf(q, sizeof(q), "DELETE FROM `%s` %s %s", table, cond ? "WHERE " : "", cond ? cond : "");
    return self->sqlQuery(self, q);
}

static int DB_all_delete_table_my(DBClient *self, const char* table) {
    char q[512]; snprintf(q, sizeof(q), "TRUNCATE TABLE `%s`", table);
    return self->sqlQuery(self, q);
}

/* =========================================================
 * [5] 데이터 조회 (Select) - ARC 연쇄 소각의 정수!
 * ========================================================= */
static ArrayList* DB_getRecordsFromQuery_my(DBClient *self, const char* sql) {
    ArrayList *list = new_ArrayList(16);
    pthread_mutex_lock(&self->lock);
    snprintf(self->last_query, sizeof(self->last_query), "%s", sql);

    if (mysql_query((MYSQL*)self->conn, sql) == 0) {
        MYSQL_RES *result = mysql_store_result((MYSQL*)self->conn);
        if (result) {
            int nf = mysql_num_fields(result);
            MYSQL_FIELD *fs = mysql_fetch_fields(result);
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(result))) {
                HashMap *m = new_HashMap(16);
                for (int i = 0; i < nf; i++) {
                    String *s = new_String(row[i] ? row[i] : "");
                    m->put(m, fs[i].name, (Object *)s);
                    RELEASE(s); // 🚀 맵이 멱살 잡았으니 로컬 해제
                }
                list->add(list, (Object *)m);
                RELEASE(m); // 🚀 리스트가 멱살 잡았으니 로컬 해제
            }
            mysql_free_result(result);
        }
    }
    pthread_mutex_unlock(&self->lock);
    return list; // 👈 의장님은 이 녀석 하나만 RELEASE 하시면 끝!
}

static HashMap* DB_getRecordFromQuery_my(DBClient *self, const char* sql) {
    ArrayList *l = self->getRecordsFromQuery(self, sql);
    HashMap *res_map = NULL;
    if (l && l->getSize(l) > 0) {
        res_map = (HashMap *)RETAIN(l->get(l, 0)); // 0번 레코드 소유권 획득
    }
    RELEASE(l); // 리스트와 그 안의 다른 맵들 소각!
    return res_map;
}

static ArrayList* DB_getRecords_my(DBClient *self, const char* table, const char* cond, const char* fields) {
    char q[4096]; snprintf(q, sizeof(q), "SELECT %s FROM `%s` %s %s", fields ? fields : "*", table, cond ? "WHERE " : "", cond ? cond : "");
    return self->getRecordsFromQuery(self, q);
}

static HashMap* DB_getRecord_my(DBClient *self, const char* table, const char* cond, const char* field) {
    char q[4096]; snprintf(q, sizeof(q), "SELECT %s FROM `%s` %s %s LIMIT 1", field ? field : "*", table, cond ? "WHERE " : "", cond ? cond : "");
    return self->getRecordFromQuery(self, q);
}

/* =========================================================
 * [6] 수치 연산 및 테이블 제어
 * ========================================================= */
static int DB_getDataCount_my(DBClient *self, const char* table, const char* cond) {
    char q[512]; snprintf(q, sizeof(q), "SELECT COUNT(*) AS cnt FROM `%s` %s %s", table, cond ? "WHERE " : "", cond ? cond : "");
    HashMap *r = self->getRecordFromQuery(self, q);
    int count = 0;
    if (r) {
        String *v = (String *)r->get(r, "cnt");
        if (v) count = atoi(v->value);
        RELEASE(r);
    }
    return count;
}

static long long DB_getDataSum_my(DBClient *self, const char* table, const char* field, const char* cond) {
    char q[512]; snprintf(q, sizeof(q), "SELECT SUM(%s) AS val_sum FROM `%s` %s %s", field, table, cond ? "WHERE " : "", cond ? cond : "");
    HashMap *r = self->getRecordFromQuery(self, q);
    long long sum_val = 0;
    if (r) {
        String *v = (String *)r->get(r, "val_sum");
        if (v) sum_val = atoll(v->value);
        RELEASE(r);
    }
    return sum_val;
}

static long long DB_getDataMax_my(DBClient *self, const char* table, const char* field, const char* cond) {
    char q[512]; snprintf(q, sizeof(q), "SELECT MAX(%s) AS val_max FROM `%s` %s %s", field, table, cond ? "WHERE " : "", cond ? cond : "");
    HashMap *r = self->getRecordFromQuery(self, q);
    long long max_val = 0;
    if (r) {
        String *v = (String *)r->get(r, "val_max");
        if (v) max_val = atoll(v->value);
        RELEASE(r);
    }
    return max_val;
}

static long long DB_getDataMin_my(DBClient *self, const char* table, const char* field, const char* cond) {
    char q[512]; snprintf(q, sizeof(q), "SELECT MIN(%s) AS val_min FROM `%s` %s %s", field, table, cond ? "WHERE " : "", cond ? cond : "");
    HashMap *r = self->getRecordFromQuery(self, q);
    long long min_val = 0;
    if (r) {
        String *v = (String *)r->get(r, "val_min");
        if (v) min_val = atoll(v->value);
        RELEASE(r);
    }
    return min_val;
}

static int DB_copyTable_my(DBClient *self, const char* newT, const char* orgT, int copyData) {
    char q1[512]; snprintf(q1, sizeof(q1), "CREATE TABLE IF NOT EXISTS `%s` LIKE `%s`", newT, orgT);
    if (!self->sqlQuery(self, q1)) return 0;
    if (copyData) {
        char q2[512]; snprintf(q2, sizeof(q2), "INSERT IGNORE INTO `%s` SELECT * FROM `%s`", newT, orgT);
        return self->sqlQuery(self, q2);
    }
    return 1;
}

static char* DB_makeTable_my(DBClient *self, const char* tablename) {
    time_t t = time(NULL); struct tm *ti = localtime(&t);
    char *new_name = malloc(128);
    (void) self;
    snprintf(new_name, 128, "%s_%04d%02d", tablename, ti->tm_year + 1900, ti->tm_mon + 1);
    return new_name;
}

static int DB_renameTable_my(DBClient *self, const char* oldT, const char* newT) {
    char q[512]; snprintf(q, sizeof(q), "RENAME TABLE `%s` TO `%s`", oldT, newT);
    return self->sqlQuery(self, q);
}

static ArrayList* DB_descTable_my(DBClient *self, const char* table) {
    char q[512]; snprintf(q, sizeof(q), "DESC `%s`", table);
    return self->getRecordsFromQuery(self, q);
}

static long long DB_getTableSize_my(DBClient *self, const char* table) {
    char q[1024];
    snprintf(q, sizeof(q), "SELECT (data_length + index_length) AS size FROM information_schema.tables WHERE table_schema = DATABASE() AND table_name = '%s'", table);
    HashMap *r = self->getRecordFromQuery(self, q);
    long long t_size = 0;
    if (r) {
        String *v = (String *)r->get(r, "size");
        if (v) t_size = atoll(v->value);
        RELEASE(r);
    }
    return t_size;
}

static int DB_dropTable_my(DBClient *self, const char *table_name) {
    char sql[256]; snprintf(sql, sizeof(sql), "DROP TABLE IF EXISTS `%s` ", table_name);
    return self->sqlQuery(self, sql);
}

static int DB_initTable_my(DBClient *self, const char *table_name) {
    char sql[256]; snprintf(sql, sizeof(sql), "TRUNCATE TABLE `%s` ", table_name);
    return self->sqlQuery(self, sql);
}

/* =========================================================
 * [7] 팩토리 바인딩
 * ========================================================= */
void bind_mysql(DBClient *db) {
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
