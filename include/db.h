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

// 🚀 [Zero-Malloc] 64바이트 고정 버퍼로 재접속 옵션 사수!
typedef struct {
    int    option;
    char   value[64];
    size_t value_size;
} DBOption;

typedef struct _DBClient DBClient;
struct _DBClient {
    Object base;
    pthread_mutex_t *unused_ptr; // 🚀 [Alignment] 호환성 유지용
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
    int      option_count;

    // --- 다형성 인터페이스 (V-Table: 35개 풀 바인딩) ---
    void       (*setSaveLog)(DBClient *self, int enable);
    void       (*writeLog)(DBClient *self, const char* msg, const char* sql, int is_error);
    void       (*setOption)(DBClient *self, int option, const void *arg, size_t arg_size);

    int        (*connect)(DBClient *self);
    int        (*reconnect)(DBClient *self);
    void       (*disconnect)(DBClient *self);
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
DBClient* new_DBClient(void);
DBClient* new_DBClientDirect(const char* host, const char* dbname, const char* id, const char* pw, int port, const char* cs, const char* type);
void bind_mysql(DBClient *db);

#endif