#include "toos_type_ws.h"
#include "json.h"
#include "logger.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

extern Logger* logger;
static ToosTypeGameContext *g_ctx = NULL;

static bool get_current_ms(uint64_t *out_ms) {
    if (!out_ms) return false;
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return false;
    *out_ms = (uint64_t)(ts.tv_sec) * 1000ULL + (uint64_t)(ts.tv_nsec) / 1000000ULL;
    return true;
}

//  [패치 3] 완벽한 4-State 스위치 매퍼!
static const char *state_to_string(ToosTypeState s) {
    switch (s) {
        case TOOS_TYPE_STATE_WAITING:   return "WAITING";
        case TOOS_TYPE_STATE_COUNTDOWN: return "COUNTDOWN";
        case TOOS_TYPE_STATE_PLAYING:   return "PLAYING";
        case TOOS_TYPE_STATE_RESULT:    return "RESULT";
        default:                        return "UNKNOWN";
    }
}

static const char* judge_result_to_string(ToosTypeJudgeResult res) {
    switch(res) {
        case TOOS_TYPE_JUDGE_CORRECT:   return "CORRECT";
        case TOOS_TYPE_JUDGE_INCORRECT: return "INCORRECT";
        case TOOS_TYPE_JUDGE_EXPIRED:   return "EXPIRED";
        case TOOS_TYPE_JUDGE_STALE:     return "STALE";
        case TOOS_TYPE_JUDGE_ERROR:     return "ERROR";
        default:                        return "UNKNOWN";
    }
}

static const char* status_to_string(ToosTypeFinishStatus status) {
    switch(status) {
        case TOOS_TYPE_FINISH_FINISHED: return "FINISHED";
        case TOOS_TYPE_FINISH_DNF:      return "DNF";
        default:                        return "NONE";
    }
}

static bool str_eq_exact(const char *s, size_t len, const char *literal) {
    size_t lit_len = strlen(literal);
    return s && len == lit_len && memcmp(s, literal, lit_len) == 0;
}

static bool parse_u64_decimal_n(const char *s, size_t len, uint64_t *out) {
    if (!s || !out || len == 0) return false;
    uint64_t value = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < '0' || c > '9') return false;
        uint64_t digit = (uint64_t)(c - '0');
        if (value > (UINT64_MAX - digit) / 10ULL) return false;
        value = value * 10ULL + digit;
    }
    if (value == 0) return false;
    *out = value;
    return true;
}

static void broadcast_ws_message(ToosTypeGameContext *ctx, const char *msg) {
    for (uint32_t i = 0; i < TOOS_TYPE_MAX_ROOM_PLAYERS; i++) {
        if (ctx->players[i].occupied && ctx->players[i].conn_ptr) {
            HttpConnection_ws_send((HttpConnection *)ctx->players[i].conn_ptr, msg);
        }
    }
}

static void ws_event_dispatcher(const ToosTypeEvent *ev, void *user_data) {
    ToosTypeGameContext *ctx = (ToosTypeGameContext *)user_data;
    if (!ctx || !ev) return;

    JSONNode *root = new_JSON_Object();
    if (!root) return;

    const char *type_str = "UNKNOWN";
    switch(ev->type) {
        case TOOS_TYPE_EVENT_JOIN_ACK:        type_str = "JOIN_ACK"; break;
        case TOOS_TYPE_EVENT_ROOM_CONFIG:     type_str = "ROOM_CONFIG"; break;
        case TOOS_TYPE_EVENT_GAME_STATE:      type_str = "GAME_STATE"; break;
        case TOOS_TYPE_EVENT_COUNTDOWN:       type_str = "COUNTDOWN"; break;
        case TOOS_TYPE_EVENT_PHASE_CHANGED:   type_str = "PHASE_CHANGED"; break;
        case TOOS_TYPE_EVENT_SENTENCE_DROP:   type_str = "SENTENCE_DROP"; break;
        case TOOS_TYPE_EVENT_SENTENCE_RESULT: type_str = "SENTENCE_RESULT"; break;
        case TOOS_TYPE_EVENT_SCORE_UPDATE:    type_str = "SCORE_UPDATE"; break;
        case TOOS_TYPE_EVENT_GAME_OVER:       type_str = "GAME_OVER"; break;
        case TOOS_TYPE_EVENT_FINAL_RANKING:   type_str = "FINAL_RANKING"; break;
        case TOOS_TYPE_EVENT_ERROR:           type_str = "ERROR"; break;
    }
    JsonValue *v_type = new_json_string(type_str);
    root->put(root, "type", (Object *)v_type);
    RELEASE((Object *)v_type);

    switch(ev->type) {
        case TOOS_TYPE_EVENT_JOIN_ACK: {
            JsonValue *v_pno = new_json_number((double)ev->payload.join.player_no);
            JsonValue *v_host = new_json_bool(ev->payload.join.is_host);
            root->put(root, "playerNo", (Object *)v_pno);
            root->put(root, "isHost", (Object *)v_host);
            RELEASE((Object *)v_pno); RELEASE((Object *)v_host);
            break;
        }
        case TOOS_TYPE_EVENT_ROOM_CONFIG: {
            const char *lstr = (ev->payload.room.lang == TOOS_TYPE_LANG_EN) ? "EN" :
                               (ev->payload.room.lang == TOOS_TYPE_LANG_KO) ? "KO" : "NONE";
            const char *state_str = state_to_string(ev->payload.room.state);

            JsonValue *v_rid = new_json_number((double)ev->payload.room.room_id);
            JsonValue *v_host = new_json_number((double)ev->payload.room.host_no);
            JsonValue *v_lang = new_json_string(lstr);
            JsonValue *v_lock = new_json_bool(ev->payload.room.locked);
            JsonValue *v_state = new_json_string(state_str);

            root->put(root, "roomId", (Object *)v_rid);
            root->put(root, "hostPlayerNo", (Object *)v_host);
            root->put(root, "language", (Object *)v_lang);
            root->put(root, "languageLocked", (Object *)v_lock);
            root->put(root, "roomState", (Object *)v_state);

            RELEASE((Object *)v_rid); RELEASE((Object *)v_host); RELEASE((Object *)v_lang);
            RELEASE((Object *)v_lock); RELEASE((Object *)v_state);
            break;
        }
        case TOOS_TYPE_EVENT_GAME_STATE: {
            const char *sstr = state_to_string(ev->payload.state.state);
            JsonValue *v_st = new_json_string(sstr);
            JsonValue *v_ph = new_json_number((double)ev->payload.state.current_phase);
            JsonValue *v_rem = new_json_number((double)ev->payload.state.game_remaining_ms);
            root->put(root, "state", (Object *)v_st);
            root->put(root, "phase", (Object *)v_ph);
            root->put(root, "gameRemainingMs", (Object *)v_rem);
            RELEASE((Object *)v_st); RELEASE((Object *)v_ph); RELEASE((Object *)v_rem);
            break;
        }
        case TOOS_TYPE_EVENT_COUNTDOWN: {
            JsonValue *v_rem = new_json_number((double)ev->payload.countdown.countdown_remaining_ms);
            root->put(root, "countdownRemainingMs", (Object *)v_rem);
            RELEASE((Object *)v_rem);
            break;
        }
        case TOOS_TYPE_EVENT_PHASE_CHANGED: {
            JsonValue *v_ph = new_json_number((double)ev->payload.phase_changed.phase);
            JsonValue *v_to = new_json_number((double)ev->payload.phase_changed.phase_timeout_ms);
            JsonValue *v_rem = new_json_number((double)ev->payload.phase_changed.game_remaining_ms);
            root->put(root, "phase", (Object *)v_ph);
            root->put(root, "phaseTimeoutMs", (Object *)v_to);
            root->put(root, "gameRemainingMs", (Object *)v_rem);
            RELEASE((Object *)v_ph); RELEASE((Object *)v_to); RELEASE((Object *)v_rem);
            break;
        }
        case TOOS_TYPE_EVENT_SENTENCE_DROP: {
            char sid_buf[32]; snprintf(sid_buf, sizeof(sid_buf), "%llu", (unsigned long long)ev->payload.drop.sentence_id);
            JsonValue *v_sid = new_json_string(sid_buf);
            JsonValue *v_txt = new_json_string(ev->payload.drop.text);
            JsonValue *v_rem = new_json_number((double)ev->payload.drop.sentence_remaining_ms);
            root->put(root, "sentenceId", (Object *)v_sid);
            root->put(root, "text", (Object *)v_txt);
            root->put(root, "sentenceRemainingMs", (Object *)v_rem);
            RELEASE((Object *)v_sid); RELEASE((Object *)v_txt); RELEASE((Object *)v_rem);
            break;
        }
        case TOOS_TYPE_EVENT_SENTENCE_RESULT: {
            char sid_buf[32]; snprintf(sid_buf, sizeof(sid_buf), "%llu", (unsigned long long)ev->payload.result.sentence_id);
            JsonValue *v_sid = new_json_string(sid_buf);
            JsonValue *v_res = new_json_string(judge_result_to_string(ev->payload.result.result));
            root->put(root, "sentenceId", (Object *)v_sid);
            root->put(root, "result", (Object *)v_res);
            RELEASE((Object *)v_sid); RELEASE((Object *)v_res);
            break;
        }
        case TOOS_TYPE_EVENT_SCORE_UPDATE: {
            JsonValue *v_pno = new_json_number((double)ev->payload.score.pno);
            char sid_buf[32]; snprintf(sid_buf, sizeof(sid_buf), "%llu", (unsigned long long)ev->payload.score.sid);
            JsonValue *v_sid = new_json_string(sid_buf);
            JsonValue *v_play = new_json_number((double)ev->payload.score.play);
            JsonValue *v_phase = new_json_number((double)ev->payload.score.phase);
            JsonValue *v_acc_s = new_json_number((double)ev->payload.score.acc_score);
            JsonValue *v_acc_x = new_json_number((double)ev->payload.score.acc_x100);
            JsonValue *v_tot = new_json_number((double)ev->payload.score.total);
            JsonValue *v_wpm = new_json_number((double)ev->payload.score.wpm);
            root->put(root, "playerNo", (Object *)v_pno);
            root->put(root, "sentenceId", (Object *)v_sid);
            root->put(root, "playScore", (Object *)v_play);
            root->put(root, "phaseScore", (Object *)v_phase);
            root->put(root, "accuracyScore", (Object *)v_acc_s);
            root->put(root, "accuracyX100", (Object *)v_acc_x);
            root->put(root, "totalScore", (Object *)v_tot);
            root->put(root, "wpm", (Object *)v_wpm);
            RELEASE((Object *)v_pno); RELEASE((Object *)v_sid); RELEASE((Object *)v_play);
            RELEASE((Object *)v_phase); RELEASE((Object *)v_acc_s); RELEASE((Object *)v_acc_x);
            RELEASE((Object *)v_tot); RELEASE((Object *)v_wpm);
            break;
        }
        case TOOS_TYPE_EVENT_GAME_OVER: {
            JsonValue *v_rsn = new_json_string(ev->payload.game_over.reason);
            root->put(root, "reason", (Object *)v_rsn);
            RELEASE((Object *)v_rsn);
            break;
        }
        case TOOS_TYPE_EVENT_FINAL_RANKING: {
            JSONNode *arr = new_JSON_Array();
            for (uint32_t i = 0; i < ev->payload.ranking.count; i++) {
                const ToosTypeRankingEntry *e = &ev->payload.ranking.entries[i];
                JSONNode *obj = new_JSON_Object();

                JsonValue *v_rk = new_json_number((double)e->rank);
                JsonValue *v_pno = new_json_number((double)e->player_no);
                JsonValue *v_st = new_json_string(status_to_string(e->status));
                JsonValue *v_pl = new_json_number((double)e->played_ms);
                JsonValue *v_ps = new_json_number((double)e->play_score);
                JsonValue *v_phs = new_json_number((double)e->phase_score);
                JsonValue *v_as = new_json_number((double)e->accuracy_score);
                JsonValue *v_ax = new_json_number((double)e->accuracy_x100);
                JsonValue *v_tot = new_json_number((double)e->total_score);
                JsonValue *v_wpm = new_json_number((double)e->wpm);

                obj->put(obj, "rank", (Object *)v_rk);
                obj->put(obj, "playerNo", (Object *)v_pno);
                obj->put(obj, "status", (Object *)v_st);
                obj->put(obj, "playedMs", (Object *)v_pl);
                obj->put(obj, "playScore", (Object *)v_ps);
                obj->put(obj, "phaseScore", (Object *)v_phs);
                obj->put(obj, "accuracyScore", (Object *)v_as);
                obj->put(obj, "accuracyX100", (Object *)v_ax);
                obj->put(obj, "totalScore", (Object *)v_tot);
                obj->put(obj, "wpm", (Object *)v_wpm);

                arr->add(arr, (Object *)obj);

                RELEASE((Object *)v_rk); RELEASE((Object *)v_pno); RELEASE((Object *)v_st);
                RELEASE((Object *)v_pl); RELEASE((Object *)v_ps); RELEASE((Object *)v_phs);
                RELEASE((Object *)v_as); RELEASE((Object *)v_ax); RELEASE((Object *)v_tot); RELEASE((Object *)v_wpm);
                RELEASE((Object *)obj);
            }
            root->put(root, "entries", (Object *)arr);
            RELEASE((Object *)arr);
            break;
        }
        case TOOS_TYPE_EVENT_ERROR: {
            JsonValue *v_msg = new_json_string(ev->payload.error.message);
            root->put(root, "message", (Object *)v_msg);
            RELEASE((Object *)v_msg);
            break;
        }
    }

    char *json_str = root->toString(root);
    if (json_str) {
        if (ev->target_conn) {
            HttpConnection_ws_send((HttpConnection *)ev->target_conn, json_str);
        } else {
            broadcast_ws_message(ctx, json_str);
        }
        free(json_str);
    }
    RELEASE((Object *)root);
}

static void on_ws_open(HttpConnection *conn) {
    LOG_INFO(logger, "[ToosType WS] Client connected (fd: %d). Waiting for JOIN_GAME.", conn->sock->fd);
}

static void on_ws_close(HttpConnection *conn) {
    if (!g_ctx) return;

    uint64_t now_ms = 0;
    //  [패치 3] 시계 실패 시 return 생략! 무조건 0을 넘겨 연결을 떼어내고(dangling 방지) Fallback 유도
    if (!get_current_ms(&now_ms)) {
        LOG_ERROR(logger, "[ToosType WS] clock failed; using GameContext fallback.");
        now_ms = 0;
    }

    ToosTypeGameContext_disconnect(g_ctx, conn, now_ms);
    LOG_INFO(logger, "[ToosType WS] Client disconnected (fd: %d).", conn->sock->fd);
}

static void on_ws_message(HttpConnection *conn, const char *msg, size_t len) {
    if (!g_ctx || !msg || len == 0) return;

    if (memchr(msg, '\0', len) != NULL) {
        LOG_WARN(logger, "[ToosType WS] Embedded NUL in payload. Dropped.");
        return;
    }

    char *json_buf = (char *)malloc(len + 1);
    if (!json_buf) return;
    memcpy(json_buf, msg, len);
    json_buf[len] = '\0';

    JSONNode *root = new_JSON(json_buf);
    free(json_buf);

    if (!root) return;

    size_t type_len = 0;
    const char *type = root->getStringLen(root, "type", &type_len);
    if (!type) {
        RELEASE(root);
        return;
    }

    uint64_t now_ms = 0;
    if (!get_current_ms(&now_ms)) {
        LOG_ERROR(logger, "[ToosType WS] Failed to obtain monotonic time. Message dropped.");
        RELEASE(root);
        return;
    }

    if (str_eq_exact(type, type_len, "JOIN_GAME")) {
        ToosTypeGameContext_join(g_ctx, conn);
    }
    else if (str_eq_exact(type, type_len, "SET_LANGUAGE")) {
        size_t lang_len = 0;
        const char *lang_str = root->getStringLen(root, "language", &lang_len);
        ToosTypeLanguage lang = TOOS_TYPE_LANG_NONE;

        if (lang_str) {
            if (str_eq_exact(lang_str, lang_len, "EN")) lang = TOOS_TYPE_LANG_EN;
            else if (str_eq_exact(lang_str, lang_len, "KO")) lang = TOOS_TYPE_LANG_KO;
        }
        ToosTypeGameContext_set_language(g_ctx, conn, lang);
    }
    else if (str_eq_exact(type, type_len, "READY")) {
        ToosTypeGameContext_set_ready(g_ctx, conn, now_ms);
    }
    else if (str_eq_exact(type, type_len, "TYPE_RESULT")) {
        size_t id_len = 0;
        const char *sid_str = root->getStringLen(root, "sentenceId", &id_len);
        size_t input_len = 0;
        const char *input = root->getStringLen(root, "input", &input_len);

        uint64_t sid = 0;
        if (sid_str && input && parse_u64_decimal_n(sid_str, id_len, &sid)) {
            ToosTypeGameContext_submit(g_ctx, conn, sid, input, input_len, now_ms);
        }
    }

    RELEASE(root);
}

void toos_type_ws_bind(HttpServer *server, ToosTypeGameContext *ctx) {
    if (!server || !ctx) return;
    g_ctx = ctx;
    ctx->dispatch = ws_event_dispatcher;
    ctx->dispatch_user_data = ctx;
    server->on_ws_open = on_ws_open;
    server->on_ws_message = on_ws_message;
    server->on_ws_close = on_ws_close;
    LOG_INFO(logger, "[ToosType WS] WebSocket Adapter successfully bound to HttpServer.");
}