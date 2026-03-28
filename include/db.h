#ifndef TOOS_DB_H
#define TOOS_DB_H

#include <pthread.h>
#include <stdint.h>
#include "object.h"
#include "string.h" // string.h 대신 우리 명품 string_obj 사용
#include "hashmap.h"
#include "arraylist.h"
#include "json.h"

#ifndef DEFAULT_DB_CONFIG
#define DEFAULT_DB_CONFIG "./dbconfig.conf"
#endif

#ifndef DEFAULT_LOG_DIR
#define DEFAULT_LOG_DIR "./log"
#endif

extern const Class dbClientClass;

typedef struct _DBClient DBClient;
struct _DBClient {
    Object base;            // 🚀 [상속] ARC 심장 장착!
    pthread_mutex_t *unused_ptr; // 기존 정렬 유지용 (실제는 아래 lock 사용)
    pthread_mutex_t lock;
    void *conn;             // 네이티브 핸들 은닉
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

    // --- 다형성 인터페이스 (함수 포인터) ---
    void       (*setSaveLog)(DBClient *self, int enable);
    int        (*connect)(DBClient *self);
    void       (*disconnect)(DBClient *self);
    void       (*writeLog)(DBClient *self, const char* msg, const char* sql, int is_error);
    int        (*sqlQuery)(DBClient *self, const char* sql);
    char* (*escape_string)(DBClient *self, const char* str);

    int        (*beginTransaction)(DBClient *self);
    int        (*commit)(DBClient *self);
    int        (*rollback)(DBClient *self);

    HashMap* (*validateFields)(DBClient *self, const char* table, HashMap* raw_data);
    int        (*table_exists)(DBClient *self, const char* table);
    int        (*fieldExists)(DBClient *self, const char* table, const char* field);
    long long  (*getNextIdx)(DBClient *self, const char* table);

    int        (*insertTable)(DBClient *self, const char* table, HashMap* data);
    int        (*updateTable)(DBClient *self, const char* table, HashMap* data, const char* cond);
    int        (*replaceTable)(DBClient *self, const char* table, HashMap* data);
    int        (*deleteTable)(DBClient *self, const char* table, const char* cond);
    int        (*all_delete_table)(DBClient *self, const char* table);

    HashMap* (*getRecordFromQuery)(DBClient *self, const char* sql);
    ArrayList* (*getRecordsFromQuery)(DBClient *self, const char* sql);
    HashMap* (*getRecord)(DBClient *self, const char* table, const char* cond, const char* field);
    ArrayList* (*getRecords)(DBClient *self, const char* table, const char* cond, const char* fields);

    int        (*getDataCount)(DBClient *self, const char* table, const char* cond);
    long long  (*getDataSum)(DBClient *self, const char* table, const char* field, const char* cond);
    long long  (*getDataMax)(DBClient *self, const char* table, const char* field, const char* cond);
    long long  (*getDataMin)(DBClient *self, const char* table, const char* field, const char* cond);

    int        (*copyTable)(DBClient *self, const char* newT, const char* orgT, int copyData);
    char* (*makeTable)(DBClient *self, const char* tablename);
    int        (*renameTable)(DBClient *self, const char* old_table, const char* new_table);

    ArrayList* (*descTable)(DBClient *self, const char* table);
    long long  (*getTableSize)(DBClient *self, const char* table);
    int        (*dropTable)(DBClient *self, const char *table_name);
    int        (*initTable)(DBClient *self, const char *table_name);
};

void safe_append(char *dest, size_t dest_size, const char *src);

// 생성자 (코딩 표준 8호)
DBClient* new_DBClient(void);
DBClient* new_DBClientDirect(
    const char* host, const char* dbname, const char* dbid,
    const char* dbpass, int port, const char* charset, const char* db_type
);

void bind_mysql(DBClient *db);

#endif
