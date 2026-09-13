#pragma once
#include "toos_type_room.h"
#include "toos_type_spawn.h"
#include "toos_type_judge.h"
#include "toos_type_sentence.h"

typedef struct {
    bool occupied;
    uint32_t player_no;
    uint64_t join_seq;

    bool connected;
    bool ready;

    size_t sentence_cursor;

    uint64_t active_sentence_id;
    uint64_t sentence_spawn_ms;
    uint64_t sentence_deadline_ms;

    bool awaiting_game_end;

    uint32_t correct_count;
    uint32_t incorrect_count;
    uint32_t expired_count;
    uint32_t phase_correct[TOOS_TYPE_MAX_PHASES];
    uint32_t correct_chars;

    uint64_t played_ms;

    uint32_t play_score;
    uint32_t phase_score;
    uint32_t accuracy_score;
    uint32_t accuracy_x100;
    uint32_t total_score;
    uint32_t wpm;

    ToosTypeFinishStatus finish_status;

    void *conn_ptr;
} ToosTypePlayerState;

typedef enum {
    TOOS_TYPE_EVENT_JOIN_ACK,
    TOOS_TYPE_EVENT_ROOM_CONFIG,
    TOOS_TYPE_EVENT_GAME_STATE,
    TOOS_TYPE_EVENT_COUNTDOWN,
    TOOS_TYPE_EVENT_PHASE_CHANGED,
    TOOS_TYPE_EVENT_SENTENCE_DROP,
    TOOS_TYPE_EVENT_SENTENCE_RESULT,
    TOOS_TYPE_EVENT_SCORE_UPDATE,
    TOOS_TYPE_EVENT_GAME_OVER,
    TOOS_TYPE_EVENT_FINAL_RANKING,
    TOOS_TYPE_EVENT_ERROR
} ToosTypeEventType;

typedef struct {
    uint32_t rank;
    uint32_t player_no;
    ToosTypeFinishStatus status;
    uint64_t played_ms;
    uint32_t play_score;
    uint32_t phase_score;
    uint32_t accuracy_score;
    uint32_t accuracy_x100;
    uint32_t total_score;
    uint32_t wpm;
} ToosTypeRankingEntry;

typedef struct {
    ToosTypeEventType type;
    void *target_conn;
    union {
        struct {
            uint32_t player_no;
            bool is_host;
        } join;

        //[패치 완료] room_id, state 포함 완벽 동기화!
        struct {
            uint64_t room_id;
            ToosTypeState state;
            ToosTypeLanguage lang;
            bool locked;
            uint32_t host_no;
        } room;

        struct {
            ToosTypeState state;
            uint32_t current_phase;
            uint64_t game_remaining_ms;
        } state;

        struct {
            uint64_t countdown_remaining_ms;
        } countdown;

        struct {
            uint32_t phase;
            uint64_t phase_timeout_ms;
            uint64_t game_remaining_ms;
        } phase_changed;
        struct {
            uint64_t sentence_id;
            const char *text;
            uint64_t sentence_remaining_ms;
        } drop;
        struct {
            uint64_t sentence_id;
            ToosTypeJudgeResult result;
        } result;

        struct {
            uint32_t pno;
            uint64_t sid;
            uint32_t play;
            uint32_t phase;
            uint32_t acc_score;
            uint32_t acc_x100;
            uint32_t total;
            uint32_t wpm;
        } score;

        struct {
            const char *reason;
        } game_over;

        struct {
            uint32_t count;
            ToosTypeRankingEntry entries[TOOS_TYPE_MAX_ROOM_PLAYERS];
        } ranking;

        struct {
            const char *message;
        } error;

    } payload;
} ToosTypeEvent;

typedef void (*ToosTypeEventDispatcher)(const ToosTypeEvent *ev, void *user_data);

typedef struct {
    ToosTypeRoom room;
    sqlite3 *db;
    ToosTypeSpawnEngine spawn;

    ToosTypePlayerState players[TOOS_TYPE_MAX_ROOM_PLAYERS];
    uint32_t active_player_count;
    uint64_t next_join_seq;

    uint64_t last_tick_ms;

    ToosTypeEventDispatcher dispatch;
    void *dispatch_user_data;
} ToosTypeGameContext;

void ToosTypeGameContext_init(ToosTypeGameContext *ctx, sqlite3 *db, uint64_t room_id, const ToosTypeGameProfile *profile);
void ToosTypeGameContext_deinit(ToosTypeGameContext *ctx);

bool ToosTypeGameContext_join(ToosTypeGameContext *ctx, void *conn_ptr);
bool ToosTypeGameContext_set_language(ToosTypeGameContext *ctx, void *conn_ptr, ToosTypeLanguage lang);
bool ToosTypeGameContext_set_ready(ToosTypeGameContext *ctx, void *conn_ptr, uint64_t now_ms);
bool ToosTypeGameContext_submit(ToosTypeGameContext *ctx, void *conn_ptr, uint64_t sentence_id, const char *input, size_t len, uint64_t now_ms);
bool ToosTypeGameContext_disconnect(ToosTypeGameContext *ctx, void *conn_ptr, uint64_t now_ms);

void ToosTypeGameContext_tick(ToosTypeGameContext *ctx, uint64_t now_ms);