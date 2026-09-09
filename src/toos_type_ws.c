#include "toos_type_ws.h"
#include "logger.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

extern Logger* logger; // 🔴 [수정] libcore 공식 로거 참조

bool toos_type_game_context_init(ToosTypeGameContext *ctx, sqlite3 *db, uint64_t room_id, uint8_t difficulty) {
    if (!ctx || !db) return false;
    memset(ctx, 0, sizeof(ToosTypeGameContext));

    //ctx->db_client 대신 순정 sqlite3* 핸들인 ctx->db에 바로 대입
    ctx->db = db;

    ctx->room = new_ToosTypeRoom(room_id);
    if (!ctx->room) {
        LOG_ERROR(logger, "[ToosType] Failed to create ToosTypeRoom.");
        return false;
    }

    toos_type_spawn_engine_init(&ctx->spawn, 1000, 5000, 1, 0);

    ctx->language = TOOS_TYPE_LANGUAGE_EN;
    ctx->difficulty = difficulty;

    LOG_INFO(logger, "[ToosType] GameContext initialized for Room %llu.", (unsigned long long)room_id);
    return true;
}

void toos_type_game_context_deinit(ToosTypeGameContext *ctx) {
    if (!ctx) return;

    for (uint32_t i = 0; i < TOOS_TYPE_MAX_ROOM_PLAYERS; i++) {
        if (ctx->players[i].in_use && ctx->players[i].conn) {
            if (ctx->players[i].conn->ws_user_data == &ctx->players[i]) {
                ctx->players[i].conn->ws_user_data = NULL;
            }
        }
    }

    if (ctx->room) {
        RELEASE((Object *)ctx->room);
        ctx->room = NULL;
    }
    LOG_INFO(logger, "[ToosType] GameContext deinitialized.");
}

ToosTypeClientSession* toos_type_game_find_session_by_conn(ToosTypeGameContext *ctx, HttpConnection *conn) {
    if (!ctx || !conn) return NULL;
    for (uint32_t i = 0; i < TOOS_TYPE_MAX_ROOM_PLAYERS; i++) {
        if (ctx->players[i].in_use && ctx->players[i].conn == conn) {
            return &ctx->players[i];
        }
    }
    return NULL;
}

bool toos_type_game_add_player(ToosTypeGameContext *ctx, uint64_t player_id, HttpConnection *conn) {
    if (!ctx || player_id == 0 || !conn) return false;
    if (!ctx->room || ctx->room->state != TOOS_TYPE_STATE_WAITING) return false;

    for (uint32_t i = 0; i < TOOS_TYPE_MAX_ROOM_PLAYERS; i++) {
        if (ctx->players[i].in_use) {
            if (ctx->players[i].player_id == player_id || ctx->players[i].conn == conn) {
                LOG_WARN(logger, "[ToosType] Duplicate player_id (%llu) or connection attempt.", (unsigned long long)player_id);
                return false;
            }
        }
    }

    int target_slot = -1;
    for (uint32_t i = 0; i < TOOS_TYPE_MAX_ROOM_PLAYERS; i++) {
        if (!ctx->players[i].in_use) {
            target_slot = (int)i;
            break;
        }
    }

    if (target_slot == -1) {
        LOG_WARN(logger, "[ToosType] Room is full. Cannot add player %llu.", (unsigned long long)player_id);
        return false;
    }

    ToosTypeClientSession *session = &ctx->players[target_slot];
    session->in_use = true;
    session->ready = false;
    session->player_id = player_id;
    session->conn = conn;

    toos_type_player_state_init(&session->state, player_id);

    conn->ws_user_data = session;
    ctx->active_player_count++;

    LOG_INFO(logger, "[ToosType] Player %llu added to Room %llu (Slot: %d).", (unsigned long long)player_id, (unsigned long long)ctx->room->room_id, target_slot);
    return true;
}

// remove_player 구현부 시그니처에 now_ms 추가
bool toos_type_game_remove_player(ToosTypeGameContext *ctx, HttpConnection *conn, uint64_t now_ms) {
    if (!ctx || !conn) return false;

    ToosTypeClientSession *session = toos_type_game_find_session_by_conn(ctx, conn);
    if (!session) return false;

    if (ctx->room && (ctx->room->state == TOOS_TYPE_STATE_COUNTDOWN || ctx->room->state == TOOS_TYPE_STATE_PLAYING)) {
        LOG_WARN(logger, "[ToosType] Player %llu disconnected during gameplay. Aborting match.", (unsigned long long)session->player_id);
        if (ctx->room->abortGame) {
            ctx->room->abortGame(ctx->room, now_ms);
        } else {
            ctx->room->state = TOOS_TYPE_STATE_RESULT;
            ctx->room->seq++;
        }
        toos_type_spawn_engine_stop(&ctx->spawn);
    }

    LOG_INFO(logger, "[ToosType] Player %llu removed from Room %llu.", (unsigned long long)session->player_id, (unsigned long long)ctx->room->room_id);

    conn->ws_user_data = NULL;
    session->conn = NULL;
    session->in_use = false;
    session->ready = false;

    if (ctx->active_player_count > 0) {
        ctx->active_player_count--;
    }

    return true;
}

static void broadcast_ws_message(ToosTypeGameContext *ctx, const char *msg) {
    if (!ctx || !msg) return;
    for (uint32_t i = 0; i < TOOS_TYPE_MAX_ROOM_PLAYERS; i++) {
        if (ctx->players[i].in_use && ctx->players[i].conn) {
            HttpConnection_ws_send(ctx->players[i].conn, msg);
        }
    }
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

static bool str_eq_exact(const char *s, size_t len, const char *literal) {
    size_t lit_len = strlen(literal);
    return s && len == lit_len && memcmp(s, literal, lit_len) == 0;
}

static ToosTypePlayerSentenceStatus get_player_sentence_status(const ToosTypePlayerState *player, uint64_t sentence_id) {
    if (!player || sentence_id == 0) return TOOS_TYPE_PLAYER_SENTENCE_UNUSED;
    for (uint32_t i = 0; i < player->record_count && i < TOOS_TYPE_MAX_PLAYER_SENTENCES; i++) {
        if (player->records[i].sentence_id == sentence_id) {
            return player->records[i].status;
        }
    }
    return TOOS_TYPE_PLAYER_SENTENCE_UNUSED;
}

// 웹소켓 메시지 수신 핸들러
void toos_type_game_handle_ws_message(ToosTypeGameContext *ctx, HttpConnection *conn, const char *msg, size_t len, uint64_t now_ms) {
    if (!ctx || !conn || !msg || len == 0) return;

    if (memchr(msg, '\0', len) != NULL) {
        LOG_WARN(logger, "[ToosType] Received payload with embedded NUL byte. Dropping.");
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

    // 1. JOIN_GAME
    if (str_eq_exact(type, type_len, "JOIN_GAME")) {
        size_t pid_len = 0;
        const char *pid_str = root->getStringLen(root, "playerId", &pid_len);
        uint64_t pid = 0;
        if (pid_str && parse_u64_decimal_n(pid_str, pid_len, &pid)) {
            if (toos_type_game_add_player(ctx, pid, conn)) {
                JSONNode *resp = new_JSON_Object();
                if (resp) {
                    JsonValue *v1 = new_json_string("GAME_STATE");
                    JsonValue *v2 = new_json_string("WAITING");
                    char seq_buf[32];
                    snprintf(seq_buf, sizeof(seq_buf), "%llu", (unsigned long long)ctx->room->seq);
                    JsonValue *v3 = new_json_string(seq_buf);

                    resp->put(resp, "type", (Object *)v1);
                    resp->put(resp, "state", (Object *)v2);
                    resp->put(resp, "seq", (Object *)v3);

                    RELEASE((Object *)v1);
                    RELEASE((Object *)v2);
                    RELEASE((Object *)v3);

                    char *jstr = resp->toString(resp);
                    if (jstr) {
                        HttpConnection_ws_send(conn, jstr);
                        free(jstr);
                    }
                    RELEASE(resp);
                }
            }
        }
        RELEASE(root);
        return;
    }

    ToosTypeClientSession *session = toos_type_game_find_session_by_conn(ctx, conn);
    if (!session) {
        RELEASE(root);
        return;
    }

    // 2. READY
    if (str_eq_exact(type, type_len, "READY")) {
        if (ctx->room && ctx->room->state == TOOS_TYPE_STATE_WAITING) {
            session->ready = true;
            LOG_INFO(logger, "[ToosType] Player %llu is READY.", (unsigned long long)session->player_id);

            bool all_ready = (ctx->active_player_count >= 2);
            for (uint32_t i = 0; i < TOOS_TYPE_MAX_ROOM_PLAYERS; i++) {
                if (ctx->players[i].in_use && !ctx->players[i].ready) {
                    all_ready = false;
                    break;
                }
            }

            if (all_ready) {
                LOG_INFO(logger, "[ToosType] All players ready. Initiating Countdown.");
                if (ctx->db && toos_type_spawn_engine_load(&ctx->spawn, ctx->db, ctx->language, ctx->difficulty)) {
                    for (uint32_t i = 0; i < TOOS_TYPE_MAX_ROOM_PLAYERS; i++) {
                        if (ctx->players[i].in_use) {
                            toos_type_player_state_reset_for_match(&ctx->players[i].state);
                        }
                    }
                    ToosTypeTransition tr = ctx->room->startCountdown(ctx->room, now_ms);
                    if (tr == TOOS_TYPE_TRANSITION_COUNTDOWN_STARTED) {
                        JSONNode *jnode = new_JSON_Object();
                        if (jnode) {
                            JsonValue *v_type = new_json_string("COUNTDOWN");
                            char seq_buf[32];
                            snprintf(seq_buf, sizeof(seq_buf), "%llu", (unsigned long long)ctx->room->seq);
                            JsonValue *v_seq = new_json_string(seq_buf);
                            JsonValue *v_dur = new_json_number(3000.0);

                            jnode->put(jnode, "type", (Object *)v_type);
                            jnode->put(jnode, "seq", (Object *)v_seq);
                            jnode->put(jnode, "durationMs", (Object *)v_dur);

                            RELEASE((Object *)v_type);
                            RELEASE((Object *)v_seq);
                            RELEASE((Object *)v_dur);

                            char *jstr = jnode->toString(jnode);
                            if (jstr) {
                                broadcast_ws_message(ctx, jstr);
                                free(jstr);
                            }
                            RELEASE((Object *)jnode);
                        }
                    }
                } else {
                    LOG_ERROR(logger, "[ToosType] Failed to load sentences from DB.");
                }
            }
        }
    }
    // 3. TYPE_RESULT
    else if (str_eq_exact(type, type_len, "TYPE_RESULT")) {
        if (ctx->room && ctx->room->state == TOOS_TYPE_STATE_PLAYING) {
            size_t id_len = 0;
            const char *id_str = root->getStringLen(root, "sentenceId", &id_len);
            size_t input_len = 0;
            const char *input = root->getStringLen(root, "input", &input_len);

            uint64_t sentence_id = 0;
            if (id_str && parse_u64_decimal_n(id_str, id_len, &sentence_id) && input) {
                uint32_t earned_score = 0;
                uint32_t wpm = 0;

                ToosTypeJudgeResult jres = toos_type_judge_submit(
                    &ctx->spawn,
                    &session->state,
                    ctx->language,
                    sentence_id,
                    input,
                    input_len,
                    now_ms,
                    &earned_score,
                    &wpm
                );

                const char *res_str = NULL;
                switch (jres) {
                    case TOOS_TYPE_JUDGE_CORRECT:         res_str = "CORRECT"; break;
                    case TOOS_TYPE_JUDGE_INCORRECT:       res_str = "INCORRECT"; break;
                    case TOOS_TYPE_JUDGE_EXPIRED:         res_str = "EXPIRED"; break;
                    case TOOS_TYPE_JUDGE_ALREADY_CLEARED: res_str = "ALREADY_CLEARED"; break;
                    case TOOS_TYPE_JUDGE_NOT_FOUND:       res_str = "NOT_FOUND"; break;
                    case TOOS_TYPE_JUDGE_ERROR:
                    default:
                        LOG_ERROR(logger, "[ToosType] Fatal Judge Error for Player %llu. Aborting.", (unsigned long long)session->player_id);
                        ctx->room->state = TOOS_TYPE_STATE_RESULT;
                        ctx->room->seq++;
                        toos_type_spawn_engine_stop(&ctx->spawn);
                        broadcast_ws_message(ctx, "{\"type\":\"GAME_OVER\",\"reason\":\"JUDGE_SYSTEM_ERROR\"}");
                        RELEASE(root);
                        return;
                }

                // SENTENCE_RESULT
                {
                    JSONNode *sres = new_JSON_Object();
                    if (sres) {
                        JsonValue *v_type = new_json_string("SENTENCE_RESULT");
                        char seq_buf[32];
                        snprintf(seq_buf, sizeof(seq_buf), "%llu", (unsigned long long)ctx->room->seq);
                        JsonValue *v_seq = new_json_string(seq_buf);
                        char sid_buf[32];
                        snprintf(sid_buf, sizeof(sid_buf), "%llu", (unsigned long long)sentence_id);
                        JsonValue *v_sid = new_json_string(sid_buf);
                        JsonValue *v_res = new_json_string(res_str);

                        sres->put(sres, "type", (Object *)v_type);
                        sres->put(sres, "seq", (Object *)v_seq);
                        sres->put(sres, "sentenceId", (Object *)v_sid);
                        sres->put(sres, "result", (Object *)v_res);

                        RELEASE((Object *)v_type);
                        RELEASE((Object *)v_seq);
                        RELEASE((Object *)v_sid);
                        RELEASE((Object *)v_res);

                        char *jstr = sres->toString(sres);
                        if (jstr) {
                            HttpConnection_ws_send(session->conn, jstr);
                            free(jstr);
                        }
                        RELEASE((Object *)sres);
                    }
                }

                // SCORE_UPDATE
                if (jres == TOOS_TYPE_JUDGE_CORRECT) {
                    JSONNode *up = new_JSON_Object();
                    if (up) {
                        JsonValue *v_type = new_json_string("SCORE_UPDATE");
                        char seq_buf[32];
                        snprintf(seq_buf, sizeof(seq_buf), "%llu", (unsigned long long)ctx->room->seq);
                        JsonValue *v_seq = new_json_string(seq_buf);
                        char pid_buf[32];
                        snprintf(pid_buf, sizeof(pid_buf), "%llu", (unsigned long long)session->player_id);
                        JsonValue *v_pid = new_json_string(pid_buf);
                        char sid_buf[32];
                        snprintf(sid_buf, sizeof(sid_buf), "%llu", (unsigned long long)sentence_id);
                        JsonValue *v_sid = new_json_string(sid_buf);

                        JsonValue *v_sco = new_json_number((double)earned_score);
                        JsonValue *v_wpm = new_json_number((double)wpm);
                        JsonValue *v_tot = new_json_number((double)session->state.total_score);

                        up->put(up, "type", (Object *)v_type);
                        up->put(up, "seq", (Object *)v_seq);
                        up->put(up, "playerId", (Object *)v_pid);
                        up->put(up, "sentenceId", (Object *)v_sid);
                        up->put(up, "score", (Object *)v_sco);
                        up->put(up, "wpm", (Object *)v_wpm);
                        up->put(up, "totalScore", (Object *)v_tot);

                        RELEASE((Object *)v_type);
                        RELEASE((Object *)v_seq);
                        RELEASE((Object *)v_pid);
                        RELEASE((Object *)v_sid);
                        RELEASE((Object *)v_sco);
                        RELEASE((Object *)v_wpm);
                        RELEASE((Object *)v_tot);

                        char *jstr = up->toString(up);
                        if (jstr) {
                            broadcast_ws_message(ctx, jstr);
                            free(jstr);
                        }
                        RELEASE((Object *)up);
                    }
                }
            }
        }
    }

    RELEASE(root);
}

void toos_type_game_tick(ToosTypeGameContext *ctx, uint64_t now_ms) {
    if (!ctx || !ctx->room) return;

    for (int i = 0; i < 2; i++) {
        ToosTypeTransition tr = ctx->room->tick(ctx->room, now_ms);

        if (tr == TOOS_TYPE_TRANSITION_GAME_STARTED) {
            if (!toos_type_spawn_engine_start(&ctx->spawn, ctx->room->state_start_ms)) {
                LOG_ERROR(logger, "[ToosType] Failed to start Spawn Engine.");
                toos_type_spawn_engine_stop(&ctx->spawn);
                ctx->room->state = TOOS_TYPE_STATE_RESULT;
                ctx->room->seq++;

                JSONNode *abort_node = new_JSON_Object();
                if (abort_node) {
                    JsonValue *v_type = new_json_string("GAME_OVER");
                    char seq_buf[32];
                    snprintf(seq_buf, sizeof(seq_buf), "%llu", (unsigned long long)ctx->room->seq);
                    JsonValue *v_seq = new_json_string(seq_buf);
                    JsonValue *v_rsn = new_json_string("SPAWN_START_FAILED");
                    abort_node->put(abort_node, "type", (Object *)v_type);
                    abort_node->put(abort_node, "seq", (Object *)v_seq);
                    abort_node->put(abort_node, "reason", (Object *)v_rsn);
                    RELEASE((Object *)v_type);
                    RELEASE((Object *)v_seq);
                    RELEASE((Object *)v_rsn);

                    char *jstr = abort_node->toString(abort_node);
                    if (jstr) {
                        broadcast_ws_message(ctx, jstr);
                        free(jstr);
                    }
                    RELEASE((Object *)abort_node);
                }
                return;
            }

            LOG_INFO(logger, "[ToosType] Game Started!");
            JSONNode *start_node = new_JSON_Object();
            if (start_node) {
                JsonValue *v_type = new_json_string("GAME_STATE");
                JsonValue *v_st = new_json_string("PLAYING");
                char seq_buf[32];
                snprintf(seq_buf, sizeof(seq_buf), "%llu", (unsigned long long)ctx->room->seq);
                JsonValue *v_seq = new_json_string(seq_buf);

                start_node->put(start_node, "type", (Object *)v_type);
                start_node->put(start_node, "state", (Object *)v_st);
                start_node->put(start_node, "seq", (Object *)v_seq);

                RELEASE((Object *)v_type);
                RELEASE((Object *)v_st);
                RELEASE((Object *)v_seq);

                char *jstr = start_node->toString(start_node);
                if (jstr) {
                    broadcast_ws_message(ctx, jstr);
                    free(jstr);
                }
                RELEASE((Object *)start_node);
            }
            continue;
        }

        if (tr == TOOS_TYPE_TRANSITION_GAME_OVER) {
            LOG_INFO(logger, "[ToosType] Game Over via deadline.");
            toos_type_spawn_engine_stop(&ctx->spawn);
            JSONNode *over_node = new_JSON_Object();
            if (over_node) {
                JsonValue *v_type = new_json_string("GAME_OVER");
                char seq_buf[32];
                snprintf(seq_buf, sizeof(seq_buf), "%llu", (unsigned long long)ctx->room->seq);
                JsonValue *v_seq = new_json_string(seq_buf);
                over_node->put(over_node, "type", (Object *)v_type);
                over_node->put(over_node, "seq", (Object *)v_seq);
                RELEASE((Object *)v_type);
                RELEASE((Object *)v_seq);

                char *jstr = over_node->toString(over_node);
                if (jstr) {
                    broadcast_ws_message(ctx, jstr);
                    free(jstr);
                }
                RELEASE((Object *)over_node);
            }
            return;
        }

        if (tr == TOOS_TYPE_TRANSITION_ERROR) {
            LOG_ERROR(logger, "[ToosType] Room FSM Error occurred.");
            toos_type_spawn_engine_stop(&ctx->spawn);
            ctx->room->state = TOOS_TYPE_STATE_RESULT;
            ctx->room->seq++;

            JSONNode *abort_node = new_JSON_Object();
            if (abort_node) {
                JsonValue *v_type = new_json_string("GAME_OVER");
                char seq_buf[32];
                snprintf(seq_buf, sizeof(seq_buf), "%llu", (unsigned long long)ctx->room->seq);
                JsonValue *v_seq = new_json_string(seq_buf);
                JsonValue *v_rsn = new_json_string("ROOM_ERROR");
                abort_node->put(abort_node, "type", (Object *)v_type);
                abort_node->put(abort_node, "seq", (Object *)v_seq);
                abort_node->put(abort_node, "reason", (Object *)v_rsn);
                RELEASE((Object *)v_type);
                RELEASE((Object *)v_seq);
                RELEASE((Object *)v_rsn);

                char *jstr = abort_node->toString(abort_node);
                if (jstr) {
                    broadcast_ws_message(ctx, jstr);
                    free(jstr);
                }
                RELEASE((Object *)abort_node);
            }
            return;
        }

        break;
    }

    if (ctx->room->state == TOOS_TYPE_STATE_PLAYING) {
        ToosTypeActiveSentence expired;
        while (toos_type_spawn_engine_pop_expired(&ctx->spawn, now_ms, &expired)) {
            for (uint32_t j = 0; j < TOOS_TYPE_MAX_ROOM_PLAYERS; j++) {
                if (ctx->players[j].in_use) {
                    ToosTypePlayerSentenceStatus current_status = get_player_sentence_status(&ctx->players[j].state, expired.sentence_id);

                    if (current_status == TOOS_TYPE_PLAYER_SENTENCE_ACTIVE) {
                        if (!toos_type_player_sentence_mark_missed(&ctx->players[j].state, expired.sentence_id)) {
                            LOG_ERROR(logger, "[ToosType] Invariant broken: mark_missed failed for active player.");
                            toos_type_spawn_engine_stop(&ctx->spawn);
                            ctx->room->state = TOOS_TYPE_STATE_RESULT;
                            ctx->room->seq++;
                            return;
                        }

                        JSONNode *sres = new_JSON_Object();
                        if (sres) {
                            JsonValue *v_type = new_json_string("SENTENCE_RESULT");
                            char seq_buf[32];
                            snprintf(seq_buf, sizeof(seq_buf), "%llu", (unsigned long long)ctx->room->seq);
                            JsonValue *v_seq = new_json_string(seq_buf);
                            char sid_buf[32];
                            snprintf(sid_buf, sizeof(sid_buf), "%llu", (unsigned long long)expired.sentence_id);
                            JsonValue *v_sid = new_json_string(sid_buf);
                            JsonValue *v_res = new_json_string("EXPIRED");

                            sres->put(sres, "type", (Object *)v_type);
                            sres->put(sres, "seq", (Object *)v_seq);
                            sres->put(sres, "sentenceId", (Object *)v_sid);
                            sres->put(sres, "result", (Object *)v_res);

                            RELEASE((Object *)v_type);
                            RELEASE((Object *)v_seq);
                            RELEASE((Object *)v_sid);
                            RELEASE((Object *)v_res);

                            char *jstr = sres->toString(sres);
                            if (jstr) {
                                HttpConnection_ws_send(ctx->players[j].conn, jstr);
                                free(jstr);
                            }
                            RELEASE((Object *)sres);
                        }
                    }
                    else if (current_status == TOOS_TYPE_PLAYER_SENTENCE_UNUSED) {
                        LOG_ERROR(logger, "[ToosType] Invariant broken: expired sentence was unused by player.");
                        toos_type_spawn_engine_stop(&ctx->spawn);
                        ctx->room->state = TOOS_TYPE_STATE_RESULT;
                        ctx->room->seq++;
                        return;
                    }
                }
            }
        }

        ToosTypeActiveSentence spawned;
        ToosTypeSpawnResult sres = toos_type_spawn_engine_tick(&ctx->spawn, now_ms, &spawned);

        if (sres == TOOS_TYPE_SPAWNED) {
            bool activation_failed = false;
            for (uint32_t j = 0; j < TOOS_TYPE_MAX_ROOM_PLAYERS; j++) {
                if (ctx->players[j].in_use) {
                    if (!toos_type_player_sentence_activate(&ctx->players[j].state, spawned.sentence_id, (uint8_t)ctx->room->phase)) {
                        activation_failed = true;
                    }
                }
            }

            if (activation_failed) {
                LOG_ERROR(logger, "[ToosType] Failed to activate spawned sentence for players.");
                toos_type_spawn_engine_stop(&ctx->spawn);
                ctx->room->state = TOOS_TYPE_STATE_RESULT;
                ctx->room->seq++;

                JSONNode *abort_node = new_JSON_Object();
                if (abort_node) {
                    JsonValue *v_type = new_json_string("GAME_OVER");
                    char seq_buf[32];
                    snprintf(seq_buf, sizeof(seq_buf), "%llu", (unsigned long long)ctx->room->seq);
                    JsonValue *v_seq = new_json_string(seq_buf);
                    JsonValue *v_rsn = new_json_string("ACTIVATION_FAILED");
                    abort_node->put(abort_node, "type", (Object *)v_type);
                    abort_node->put(abort_node, "seq", (Object *)v_seq);
                    abort_node->put(abort_node, "reason", (Object *)v_rsn);
                    RELEASE((Object *)v_type);
                    RELEASE((Object *)v_seq);
                    RELEASE((Object *)v_rsn);

                    char *jstr = abort_node->toString(abort_node);
                    if (jstr) {
                        broadcast_ws_message(ctx, jstr);
                        free(jstr);
                    }
                    RELEASE((Object *)abort_node);
                }
            } else {
                const ToosTypeSentence *sent = toos_type_spawn_engine_get_sentence(&ctx->spawn, &spawned);

                if (!sent || sent->en_text[0] == '\0') {
                    LOG_ERROR(logger, "[ToosType] Spawned sentence missing text.");
                    toos_type_spawn_engine_stop(&ctx->spawn);
                    ctx->room->state = TOOS_TYPE_STATE_RESULT;
                    ctx->room->seq++;

                    JSONNode *abort_node = new_JSON_Object();
                    if (abort_node) {
                        JsonValue *v_type = new_json_string("GAME_OVER");
                        char seq_buf[32];
                        snprintf(seq_buf, sizeof(seq_buf), "%llu", (unsigned long long)ctx->room->seq);
                        JsonValue *v_seq = new_json_string(seq_buf);
                        JsonValue *v_rsn = new_json_string("SENTENCE_MISSING");
                        abort_node->put(abort_node, "type", (Object *)v_type);
                        abort_node->put(abort_node, "seq", (Object *)v_seq);
                        abort_node->put(abort_node, "reason", (Object *)v_rsn);
                        RELEASE((Object *)v_type);
                        RELEASE((Object *)v_seq);
                        RELEASE((Object *)v_rsn);

                        char *jstr = abort_node->toString(abort_node);
                        if (jstr) {
                            broadcast_ws_message(ctx, jstr);
                            free(jstr);
                        }
                        RELEASE((Object *)abort_node);
                    }
                    return;
                }

                JSONNode *jnode = new_JSON_Object();
                if (jnode) {
                    JsonValue *v_type = new_json_string("SENTENCE_DROP");
                    char seq_buf[32];
                    snprintf(seq_buf, sizeof(seq_buf), "%llu", (unsigned long long)ctx->room->seq);
                    JsonValue *v_seq  = new_json_string(seq_buf);
                    char sid_buf[32];
                    snprintf(sid_buf, sizeof(sid_buf), "%llu", (unsigned long long)spawned.sentence_id);
                    JsonValue *v_sid  = new_json_string(sid_buf);
                    JsonValue *v_txt  = new_json_string(sent->en_text);
                    char dl_buf[32];
                    snprintf(dl_buf, sizeof(dl_buf), "%llu", (unsigned long long)spawned.deadline_at_ms);
                    JsonValue *v_dl   = new_json_string(dl_buf);

                    if (!v_type || !v_seq || !v_sid || !v_txt || !v_dl) {
                        if (v_type) RELEASE((Object *)v_type);
                        if (v_seq)  RELEASE((Object *)v_seq);
                        if (v_sid)  RELEASE((Object *)v_sid);
                        if (v_txt)  RELEASE((Object *)v_txt);
                        if (v_dl)   RELEASE((Object *)v_dl);
                        RELEASE((Object *)jnode);

                        toos_type_spawn_engine_stop(&ctx->spawn);
                        ctx->room->state = TOOS_TYPE_STATE_RESULT;
                        ctx->room->seq++;
                        return;
                    }

                    jnode->put(jnode, "type", (Object *)v_type);
                    jnode->put(jnode, "seq", (Object *)v_seq);
                    jnode->put(jnode, "sentenceId", (Object *)v_sid);
                    jnode->put(jnode, "text", (Object *)v_txt);
                    jnode->put(jnode, "deadline", (Object *)v_dl);

                    RELEASE((Object *)v_type);
                    RELEASE((Object *)v_seq);
                    RELEASE((Object *)v_sid);
                    RELEASE((Object *)v_txt);
                    RELEASE((Object *)v_dl);

                    char *jstr = jnode->toString(jnode);
                    if (jstr) {
                        broadcast_ws_message(ctx, jstr);
                        free(jstr);
                    }
                    RELEASE((Object *)jnode);
                }
            }
        }
        else if (sres == TOOS_TYPE_SPAWN_ERROR) {
            LOG_ERROR(logger, "[ToosType] Fatal Error in Spawn Engine.");
            toos_type_spawn_engine_stop(&ctx->spawn);
            ctx->room->state = TOOS_TYPE_STATE_RESULT;
            ctx->room->seq++;

            JSONNode *abort_node = new_JSON_Object();
            if (abort_node) {
                JsonValue *v_type = new_json_string("GAME_OVER");
                char seq_buf[32];
                snprintf(seq_buf, sizeof(seq_buf), "%llu", (unsigned long long)ctx->room->seq);
                JsonValue *v_seq = new_json_string(seq_buf);
                JsonValue *v_rsn = new_json_string("SPAWN_FATAL_ERROR");
                abort_node->put(abort_node, "type", (Object *)v_type);
                abort_node->put(abort_node, "seq", (Object *)v_seq);
                abort_node->put(abort_node, "reason", (Object *)v_rsn);
                RELEASE((Object *)v_type);
                RELEASE((Object *)v_seq);
                RELEASE((Object *)v_rsn);

                char *jstr = abort_node->toString(abort_node);
                if (jstr) {
                    broadcast_ws_message(ctx, jstr);
                    free(jstr);
                }
                RELEASE((Object *)abort_node);
            }
        }
    }
}