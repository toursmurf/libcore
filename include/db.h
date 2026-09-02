#ifndef DB_H
#define DB_H

#include <pthread.h>
#include <stdint.h>
#include <stddef.h>
#include "object.h"
#include "hashmap.h"
#include "arraylist.h"

#ifndef DEFAULT_DB_CONFIG
#define DEFAULT_DB_CONFIG "./dbconfig.conf"
#endif

#ifndef DEFAULT_LOG_DIR
#define DEFAULT_LOG_DIR "./log"
#endif

extern const Class dbClientClass;

typedef struct {
    int option;
    char value[64];
    size_t value_size;
} DBOption;

typedef struct _DBClient DBClient;
struct _DBClient {
    Object base;
    //미사용 , unused
    //pthread_mutex_t *unused_ptr;
    pthread_mutex_t lock;
    void *conn;
    int isConnected;
    int save_log;

    char host[128];
    char dbname[128];
    char dbid[64];
    char dbpass[64];
    char charset[32];
    char db_type[32];
    int port;

    char last_query[16384];
    long long last_insert_id;
    long long last_idx;

    DBOption options[16];
    int option_count;

    void (*setSaveLog)(DBClient *self, int enable);
    void (*writeLog)(DBClient *self, const char* msg, const char* sql, int is_error);
    void (*setOption)(DBClient *self, int option, const void *arg, size_t arg_size);

    int (*connect)(DBClient *self);
    int (*reconnect)(DBClient *self);
    void (*disconnect)(DBClient *self);
    int (*sqlQuery)(DBClient *self, const char* sql);
    char* (*escape_string)(DBClient *self, const char* str);

    int (*beginTransaction)(DBClient *self);
    int (*commit)(DBClient *self);
    int (*rollback)(DBClient *self);

    HashMap* (*validateFields)(DBClient *self, const char* table, HashMap* raw_data);
    int (*table_exists)(DBClient *self, const char* table);
    int (*fieldExists)(DBClient *self, const char* table, const char* field);
    long long (*getNextIdx)(DBClient *self, const char* table);

    int (*insertTable)(DBClient *self, const char* table, HashMap* data);
    int (*updateTable)(DBClient *self, const char* table, HashMap* data, const char* cond);
    int (*replaceTable)(DBClient *self, const char* table, HashMap* data);
    int (*deleteTable)(DBClient *self, const char* table, const char* cond);
    int (*all_delete_table)(DBClient *self, const char* table);

    HashMap* (*getRecordFromQuery)(DBClient *self, const char* sql);
    ArrayList* (*getRecordsFromQuery)(DBClient *self, const char* sql);
    HashMap* (*getRecord)(DBClient *self, const char* table, const char* cond, const char* field);
    ArrayList* (*getRecords)(DBClient *self, const char* table, const char* cond, const char* fields);

    int (*getDataCount)(DBClient *self, const char* table, const char* cond);
    long long (*getDataSum)(DBClient *self, const char* table, const char* field, const char* cond);
    long long (*getDataMax)(DBClient *self, const char* table, const char* field, const char* cond);
    long long (*getDataMin)(DBClient *self, const char* table, const char* field, const char* cond);

    int (*copyTable)(DBClient *self, const char* newT, const char* orgT, int copyData);
    char* (*makeTable)(DBClient *self, const char* tablename);
    int (*renameTable)(DBClient *self, const char* old_table, const char* new_table);

    ArrayList* (*descTable)(DBClient *self, const char* table);
    long long (*getTableSize)(DBClient *self, const char* table);
    int (*dropTable)(DBClient *self, const char *table_name);
    int (*initTable)(DBClient *self, const char *table_name);

    /* =====================================================
     * v1.7.2 추가 필드 — 구조체 끝에 붙여 기존 오프셋 보존
     * ===================================================== */

    /* 마지막 쿼리의 영향 행 수.
     *   성공 시 >= 0, 실패/미측정 시 -1.
     *   접속 시 CLIENT_FOUND_ROWS 를 켜므로 UPDATE 에서도
     *   "값이 바뀐 행"이 아니라 "WHERE 에 매칭된 행" 수다.
     *   -> 0 이면 대상 행이 정말 없다는 뜻이므로 404 판정에 쓸 수 있다.
     *   SELECT 계열에서는 조회된 행 수. */
    long long affected_rows;

    /* 트랜잭션 진행 중 플래그.
     *   1 인 동안은 접속이 끊겨도 자동 재접속하지 않는다
     *   (부분 커밋 방지). begin 에서 1, commit/rollback 에서 0. */
    int in_transaction;

    /* 테이블별 컬럼 목록 캐시 (mysql.c 소유, HashMap<table, ArrayList<String>>).
     *   재접속 / disconnect / 스키마 변경 DDL 에서 무효화된다. */
    HashMap* schema_cache;
};

    int safe_append(char *dest, size_t dest_size, const char *src);
DBClient* new_DBClient(void);
DBClient* new_DBClientDirect(const char* host, const char* dbname, const char* id, const char* pw, int port, const char* cs, const char* type);

#if defined(HAVE_MYSQL)
    void bind_mysql(DBClient *db);
#endif

#if defined(HAVE_PGSQL)
    void bind_pgsql(DBClient *db);
#endif

#if defined(HAVE_SQLITE)
    void bind_sqlite(DBClient *db);
#endif

#endif