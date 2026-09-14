#include "toos_type_game_context.h"
#include <string.h>
#include <stdlib.h>
#include "logger.h"

static void emit_event(ToosTypeGameContext *ctx, const ToosTypeEvent *ev) {
    if (ctx && ctx->dispatch) ctx->dispatch(ev, ctx->dispatch_user_data);
}

static void emit_error(ToosTypeGameContext *ctx, void *conn, const char *msg) {
    ToosTypeEvent err;
    memset(&err, 0, sizeof(err));
    err.type = TOOS_TYPE_EVENT_ERROR;
    err.target_conn = conn;
    err.payload.error.message = msg;
    emit_event(ctx, &err);
}

static void emit_room_config(ToosTypeGameContext *ctx, void *target_conn) {
    ToosTypeEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = TOOS_TYPE_EVENT_ROOM_CONFIG;
    ev.target_conn = target_conn;
    ev.payload.room.room_id = ctx->room.room_id;
    ev.payload.room.state = ctx->room.state;
    ev.payload.room.lang = ctx->room.language;
    ev.payload.room.locked = ctx->room.language_locked;
    ev.payload.room.host_no = ctx->room.host_player_no;
    emit_event(ctx, &ev);
}

static ToosTypePlayerState* find_player_by_conn(ToosTypeGameContext *ctx, void *conn_ptr) {
    if (!ctx || !conn_ptr) return NULL;
    for (uint32_t i = 0; i < TOOS_TYPE_MAX_ROOM_PLAYERS; i++) {
        if (ctx->players[i].occupied && ctx->players[i].conn_ptr == conn_ptr) return &ctx->players[i];
    }
    return NULL;
}

static void recalculate_host(ToosTypeGameContext *ctx) {
    if (!ctx) return;
    uint32_t oldest_no = 0;
    uint64_t min_join_seq = UINT64_MAX;
    for (uint32_t i = 0; i < TOOS_TYPE_MAX_ROOM_PLAYERS; i++) {
        if (ctx->players[i].occupied && ctx->players[i].connected) {
            if (ctx->players[i].join_seq < min_join_seq) {
                min_join_seq = ctx->players[i].join_seq;
                oldest_no = ctx->players[i].player_no;
            }
        }
    }
    ctx->room.host_player_no = oldest_no;
}

static bool try_start_countdown(ToosTypeGameContext *ctx, uint64_t now_ms) {
    uint32_t connected_count = 0, ready_count = 0;
    LOG_INFO(logger,
    "[ToosType] READY CHECK connected=%u ready=%u state=%d lang=%d locked=%d",
    connected_count,
    ready_count,
    ctx->room.state,
    ctx->room.language,
    ctx->room.language_locked);

    for (uint32_t i = 0; i < TOOS_TYPE_MAX_ROOM_PLAYERS; i++) {
        ToosTypePlayerState *p = &ctx->players[i];
        if (p->occupied && p->connected) {
            connected_count++;
            if (p->ready) ready_count++;
        }
    }

    LOG_INFO(logger,
    "[ToosType] READY CHECK connected=%u ready=%u state=%d lang=%d locked=%d",
    connected_count,
    ready_count,
    ctx->room.state,
    ctx->room.language,
    ctx->room.language_locked);

    if (connected_count < TOOS_TYPE_MIN_ROOM_PLAYERS || ready_count != connected_count) {
        LOG_INFO(logger,
       "[ToosType] WAIT: players=%u ready=%u minimum=%u",
       connected_count,
       ready_count,
       TOOS_TYPE_MIN_ROOM_PLAYERS);
        return true;
    }

    LOG_INFO(logger,
    "[ToosType] ALL READY confirmed. Building shared deck...");

    bool deck_ok =ToosTypeSpawnEngine_build_deck(&ctx->spawn,ctx->db,ctx->room.language);

    LOG_INFO(logger,
        "[ToosType] build_deck returned=%d loaded=%d count=%u",
        deck_ok ? 1 : 0,
        ctx->spawn.loaded ? 1 : 0,
        ctx->spawn.base_count);

    if (!deck_ok) {
        emit_error(ctx, NULL, "ERROR_DECK_BUILD_FAILED");
        return false;
    }

    LOG_INFO(logger,
        "[ToosType] Starting countdown now_ms=%llu",
        (unsigned long long)now_ms);

    if (!ToosTypeRoom_start_countdown(&ctx->room, now_ms)) {
        LOG_ERROR(logger,
            "[ToosType] start_countdown FAILED state=%d",
            ctx->room.state);
        return false;
    }

    LOG_INFO(logger,
        "[ToosType] COUNTDOWN STARTED end=%llu",
        (unsigned long long)ctx->room.countdown_ends_at_ms);

    if (!ctx->spawn.loaded) {
        if (!ToosTypeSpawnEngine_build_deck(&ctx->spawn, ctx->db, ctx->room.language)) {
            emit_error(ctx, NULL, "FAILED_TO_BUILD_SHARED_DECK");
            return false;
        }
    }

    LOG_INFO(logger,
    "[ToosType] COUNTDOWN STARTED players=%u ready=%u end=%llu",
    connected_count,
    ready_count,
    (unsigned long long)ctx->room.countdown_ends_at_ms);

    ToosTypeEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = TOOS_TYPE_EVENT_COUNTDOWN;
    ev.target_conn = NULL;
    ev.payload.countdown.countdown_remaining_ms = ctx->room.countdown_ends_at_ms > now_ms ? ctx->room.countdown_ends_at_ms - now_ms : 0;
    emit_event(ctx, &ev);

    emit_room_config(ctx, NULL);
    return true;
}

static void update_player_stats(ToosTypeGameContext *ctx, ToosTypePlayerState *p, uint64_t now_ms) {
    if (p->finish_status == TOOS_TYPE_FINISH_NONE &&
       (ctx->room.state == TOOS_TYPE_STATE_PLAYING || ctx->room.state == TOOS_TYPE_STATE_COUNTDOWN)) {
        if (now_ms > ctx->room.game_start_ms) {
            p->played_ms = now_ms - ctx->room.game_start_ms;
            if (p->played_ms > (ctx->room.game_end_ms - ctx->room.game_start_ms)) {
                p->played_ms = ctx->room.game_end_ms - ctx->room.game_start_ms;
            }
        }
    }

    uint32_t played_sec = (uint32_t)(p->played_ms / 1000ULL);
    p->play_score = played_sec * ctx->room.profile.play_score_per_sec;

    uint32_t attempts = p->correct_count + p->incorrect_count + p->expired_count;
    if (attempts > 0) p->accuracy_x100 = (p->correct_count * 10000ULL) / attempts;
    else p->accuracy_x100 = 0;

    p->accuracy_score = (p->accuracy_x100 * ctx->room.profile.accuracy_score_multiplier) / 100U;
    p->total_score = p->play_score + p->phase_score + p->accuracy_score;

    if (p->played_ms > 0) {
        uint64_t numerator = (uint64_t)p->correct_chars * 12000ULL;
        p->wpm = (uint32_t)(numerator / p->played_ms);
    } else {
        p->wpm = 0;
    }
}

static void issue_next_sentence(ToosTypeGameContext *ctx, ToosTypePlayerState *p, uint64_t now_ms) {
    uint64_t next_timeout = ToosTypeRoom_sentence_timeout_ms(&ctx->room, now_ms);

    if (now_ms >= ctx->room.game_end_ms || (ctx->room.game_end_ms - now_ms) < next_timeout) {
        p->awaiting_game_end = true;
        p->active_sentence_id = 0;
        return;
    }

    ToosTypeSpawnEngine_ensure_cursor(&ctx->spawn, p->sentence_cursor);
    const ToosTypeSentence *sent = ToosTypeSpawnEngine_sentence_at(&ctx->spawn, p->sentence_cursor);

    if (sent) {
        p->active_sentence_id = sent->id;
        p->sentence_spawn_ms = now_ms;
        p->sentence_deadline_ms = now_ms + next_timeout;

        ToosTypeEvent drop_ev;
        memset(&drop_ev, 0, sizeof(drop_ev));
        drop_ev.type = TOOS_TYPE_EVENT_SENTENCE_DROP;
        drop_ev.target_conn = p->conn_ptr;
        drop_ev.payload.drop.sentence_id = sent->id;
        drop_ev.payload.drop.text = (ctx->room.language == TOOS_TYPE_LANG_EN) ? sent->en_text : sent->ko_text;
        drop_ev.payload.drop.sentence_remaining_ms = next_timeout;
        emit_event(ctx, &drop_ev);
    } else {
        p->awaiting_game_end = true;
        p->active_sentence_id = 0;
    }
}

void ToosTypeGameContext_init(ToosTypeGameContext *ctx, sqlite3 *db, uint64_t room_id, const ToosTypeGameProfile *profile) {
    if (!ctx || !db || !profile) return;
    memset(ctx, 0, sizeof(ToosTypeGameContext));
    ctx->db = db;
    ToosTypeRoom_init(&ctx->room, room_id, profile);
    ToosTypeSpawnEngine_init(&ctx->spawn, profile->random_seed);
    ctx->next_join_seq = 1;
}

void ToosTypeGameContext_deinit(ToosTypeGameContext *ctx) {
    if (!ctx) return;
    ToosTypeSpawnEngine_deinit(&ctx->spawn);
    memset(ctx, 0, sizeof(ToosTypeGameContext));
}

bool ToosTypeGameContext_join(ToosTypeGameContext *ctx, void *conn_ptr) {
    if (!ctx || !conn_ptr) return false;

    if (ctx->room.state != TOOS_TYPE_STATE_WAITING) {
        emit_error(ctx, conn_ptr, "ERROR_GAME_ALREADY_STARTED");
        return false;
    }
    if (find_player_by_conn(ctx, conn_ptr)) {
        emit_error(ctx, conn_ptr, "ERROR_ALREADY_JOINED");
        return false;
    }

    int target_slot = -1;
    uint32_t assigned_no = 0;
    for (uint32_t i = 0; i < TOOS_TYPE_MAX_ROOM_PLAYERS; i++) {
        if (!ctx->players[i].occupied) {
            target_slot = (int)i;
            assigned_no = i + 1;
            break;
        }
    }

    if (target_slot == -1) {
        emit_error(ctx, conn_ptr, "ERROR_ROOM_FULL");
        return false;
    }

    ToosTypePlayerState *p = &ctx->players[target_slot];
    memset(p, 0, sizeof(ToosTypePlayerState));

    p->occupied = true;
    p->player_no = assigned_no;
    p->join_seq = ctx->next_join_seq++;
    p->connected = true;
    p->ready = false;
    p->conn_ptr = conn_ptr;

    ctx->active_player_count++;
    recalculate_host(ctx);

    ToosTypeEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = TOOS_TYPE_EVENT_JOIN_ACK;
    ev.target_conn = conn_ptr;
    ev.payload.join.player_no = p->player_no;
    ev.payload.join.is_host = (p->player_no == ctx->room.host_player_no);
    emit_event(ctx, &ev);

    //  [패치 5] 중복 전송 제거 (1명일 때 브로드캐스트 하던 부분 삭제)
    emit_room_config(ctx, conn_ptr);

    return true;
}

bool ToosTypeGameContext_set_language(ToosTypeGameContext *ctx, void *conn_ptr, ToosTypeLanguage lang) {
    if (!ctx || !conn_ptr) return false;
    if (ctx->room.state != TOOS_TYPE_STATE_WAITING) {
        emit_error(ctx, conn_ptr, "ERROR_GAME_ALREADY_STARTED"); return false;
    }
    if (ctx->room.language_locked) {
        emit_error(ctx, conn_ptr, "ERROR_LANGUAGE_LOCKED"); return false;
    }

    ToosTypePlayerState *p = find_player_by_conn(ctx, conn_ptr);
    if (!p || p->player_no != ctx->room.host_player_no) {
        emit_error(ctx, conn_ptr, "ERROR_NOT_HOST"); return false;
    }
    if (lang != TOOS_TYPE_LANG_EN && lang != TOOS_TYPE_LANG_KO) {
        emit_error(ctx, conn_ptr, "ERROR_INVALID_LANGUAGE"); return false;
    }

    ctx->room.language = lang;
    emit_room_config(ctx, NULL);
    return true;
}

bool ToosTypeGameContext_set_ready(ToosTypeGameContext *ctx, void *conn_ptr, uint64_t now_ms) {
    if (!ctx || !conn_ptr) return false;
    if (now_ms == 0) now_ms = ctx->last_tick_ms;

    if (ctx->room.state != TOOS_TYPE_STATE_WAITING) return false;

    if (ctx->room.language == TOOS_TYPE_LANG_NONE) {
        emit_error(ctx, conn_ptr, "ERROR_LANGUAGE_NOT_SELECTED");
        return false;
    }

    ToosTypePlayerState *p = find_player_by_conn(ctx, conn_ptr);
    if (!p) return false;

    if (!ctx->room.language_locked) {
        ctx->room.language_locked = true;
        emit_room_config(ctx, NULL);
    }

    p->ready = true;
    return try_start_countdown(ctx, now_ms);
}

bool ToosTypeGameContext_submit(ToosTypeGameContext *ctx, void *conn_ptr, uint64_t sentence_id, const char *input, size_t len, uint64_t now_ms) {
    if (!ctx || !conn_ptr || !input || ctx->room.state != TOOS_TYPE_STATE_PLAYING) return false;
    if (now_ms == 0) now_ms = ctx->last_tick_ms;

    ToosTypePlayerState *p = find_player_by_conn(ctx, conn_ptr);
    if (!p || !p->connected || p->finish_status != TOOS_TYPE_FINISH_NONE) return false;

    if (p->awaiting_game_end || p->active_sentence_id == 0) {
        ToosTypeEvent res_ev;
        memset(&res_ev, 0, sizeof(res_ev));
        res_ev.type = TOOS_TYPE_EVENT_SENTENCE_RESULT;
        res_ev.target_conn = conn_ptr;
        res_ev.payload.result.sentence_id = sentence_id;
        res_ev.payload.result.result = TOOS_TYPE_JUDGE_STALE;
        emit_event(ctx, &res_ev);
        return true;
    }

    const ToosTypeSentence *target_sent = ToosTypeSpawnEngine_sentence_at(&ctx->spawn, p->sentence_cursor);
    if (!target_sent) return false;

    ToosTypeJudgeResult jres = ToosTypeJudge_submit(
        target_sent, p->active_sentence_id, sentence_id,
        input, len, now_ms, p->sentence_deadline_ms, ctx->room.language
    );

    if (jres == TOOS_TYPE_JUDGE_ERROR) {
        emit_error(ctx, conn_ptr, "JUDGE_INTERNAL_ERROR");
        return false;
    }

    if (jres == TOOS_TYPE_JUDGE_STALE) {
        ToosTypeEvent res_ev;
        memset(&res_ev, 0, sizeof(res_ev));
        res_ev.type = TOOS_TYPE_EVENT_SENTENCE_RESULT;
        res_ev.target_conn = conn_ptr;
        res_ev.payload.result.sentence_id = sentence_id;
        res_ev.payload.result.result = TOOS_TYPE_JUDGE_STALE;
        emit_event(ctx, &res_ev);
        return true;
    }

    bool advance_sentence = false;
    if (jres == TOOS_TYPE_JUDGE_CORRECT) {
        p->correct_count++;
        uint32_t exact_phase = ToosTypeRoom_phase_at(&ctx->room, now_ms);
        uint32_t phase_idx = exact_phase - 1;
        p->phase_correct[phase_idx]++;
        p->phase_score += ctx->room.profile.phases[phase_idx].correct_score;
        p->correct_chars += ToosTypeJudge_count_chars(input, len, ctx->room.language);
        advance_sentence = true;
    }
    else if (jres == TOOS_TYPE_JUDGE_INCORRECT) {
        p->incorrect_count++; // v1.3 정책: 커서 이동 안 함 (Retry)
    }
    else if (jres == TOOS_TYPE_JUDGE_EXPIRED) {
        p->expired_count++;
        advance_sentence = true;
    }

    update_player_stats(ctx, p, now_ms);

    ToosTypeEvent res_ev;
    memset(&res_ev, 0, sizeof(res_ev));
    res_ev.type = TOOS_TYPE_EVENT_SENTENCE_RESULT;
    res_ev.target_conn = conn_ptr;
    res_ev.payload.result.sentence_id = sentence_id;
    res_ev.payload.result.result = jres;
    emit_event(ctx, &res_ev);

    ToosTypeEvent score_ev;
    memset(&score_ev, 0, sizeof(score_ev));
    score_ev.type = TOOS_TYPE_EVENT_SCORE_UPDATE;
    score_ev.target_conn = conn_ptr;
    score_ev.payload.score.pno = p->player_no;
    score_ev.payload.score.sid = sentence_id;
    score_ev.payload.score.play = p->play_score;
    score_ev.payload.score.phase = p->phase_score;
    score_ev.payload.score.acc_score = p->accuracy_score;
    score_ev.payload.score.acc_x100 = p->accuracy_x100;
    score_ev.payload.score.total = p->total_score;
    score_ev.payload.score.wpm = p->wpm;
    emit_event(ctx, &score_ev);

    if (advance_sentence) {
        p->sentence_cursor++;
        issue_next_sentence(ctx, p, now_ms);
    }
    return true;
}

bool ToosTypeGameContext_disconnect(ToosTypeGameContext *ctx, void *conn_ptr, uint64_t now_ms) {
    if (!ctx || !conn_ptr) return false;
    if (now_ms == 0) now_ms = ctx->last_tick_ms;

    ToosTypePlayerState *p = find_player_by_conn(ctx, conn_ptr);
    if (!p) return false;

    uint32_t old_host = ctx->room.host_player_no;

    if (ctx->room.state == TOOS_TYPE_STATE_WAITING || ctx->room.state == TOOS_TYPE_STATE_COUNTDOWN) {
        bool was_countdown = (ctx->room.state == TOOS_TYPE_STATE_COUNTDOWN);
        memset(p, 0, sizeof(*p));
        ctx->active_player_count--;

        //  [패치 2] WAITING / COUNTDOWN 때만 방장 재선출
        recalculate_host(ctx);

        if (old_host != ctx->room.host_player_no) emit_room_config(ctx, NULL);

        if (was_countdown) {
            uint32_t connected_count = 0, ready_count = 0;
            for (uint32_t i = 0; i < TOOS_TYPE_MAX_ROOM_PLAYERS; i++) {
                if (ctx->players[i].occupied && ctx->players[i].connected) {
                    connected_count++;
                    if (ctx->players[i].ready) ready_count++;
                }
            }
            if (connected_count < TOOS_TYPE_MIN_ROOM_PLAYERS || ready_count != connected_count) {
                ToosTypeRoom_cancel_countdown(&ctx->room, now_ms);

                ToosTypeEvent state_ev;
                memset(&state_ev, 0, sizeof(state_ev));
                state_ev.type = TOOS_TYPE_EVENT_GAME_STATE;
                state_ev.target_conn = NULL;
                state_ev.payload.state.state = TOOS_TYPE_STATE_WAITING;
                emit_event(ctx, &state_ev);
                emit_room_config(ctx, NULL);
            }
        } else {
            try_start_countdown(ctx, now_ms);
        }
        return true;
    }

    // PLAYING 또는 RESULT 중 퇴장
    p->connected = false;
    p->conn_ptr = NULL;

    if (p->finish_status == TOOS_TYPE_FINISH_NONE) {
        uint64_t end_ms = now_ms;
        if (end_ms > ctx->room.game_end_ms) end_ms = ctx->room.game_end_ms;
        p->played_ms = (end_ms > ctx->room.game_start_ms) ? (end_ms - ctx->room.game_start_ms) : 0;
        p->finish_status = TOOS_TYPE_FINISH_DNF;
        update_player_stats(ctx, p, now_ms);
    }

    //  [패치 2] 게임 시작 후엔 방장 재선출 없음! (recalculate_host 삭제)
    return true;
}

// --------------------------------------------------------
// Tick & Ranking Engine
// --------------------------------------------------------

void ToosTypeGameContext_tick(ToosTypeGameContext *ctx, uint64_t now_ms) {
    if (!ctx) return;
    if (now_ms == 0) now_ms = ctx->last_tick_ms;
    ctx->last_tick_ms = now_ms;

    ToosTypeRoomTickResult tick_res = ToosTypeRoom_tick(&ctx->room, now_ms);

    if (tick_res == TOOS_TYPE_ROOM_TICK_GAME_STARTED) {
        ToosTypeEvent state_ev;
        memset(&state_ev, 0, sizeof(state_ev));
        state_ev.type = TOOS_TYPE_EVENT_GAME_STATE;
        state_ev.target_conn = NULL;
        state_ev.payload.state.state = TOOS_TYPE_STATE_PLAYING;
        state_ev.payload.state.current_phase = ctx->room.current_phase;
        state_ev.payload.state.game_remaining_ms = ctx->room.game_end_ms > now_ms ? ctx->room.game_end_ms - now_ms : 0;
        emit_event(ctx, &state_ev);

        emit_room_config(ctx, NULL);

        for (uint32_t i = 0; i < TOOS_TYPE_MAX_ROOM_PLAYERS; i++) {
            if (ctx->players[i].occupied && ctx->players[i].connected) {
                issue_next_sentence(ctx, &ctx->players[i], now_ms);
            }
        }
    }
    else if (tick_res == TOOS_TYPE_ROOM_TICK_PHASE_CHANGED) {
        ToosTypeEvent phase_ev;
        memset(&phase_ev, 0, sizeof(phase_ev));
        phase_ev.type = TOOS_TYPE_EVENT_PHASE_CHANGED;
        phase_ev.target_conn = NULL;
        phase_ev.payload.phase_changed.phase = ctx->room.current_phase;
        phase_ev.payload.phase_changed.phase_timeout_ms = ToosTypeRoom_sentence_timeout_ms(&ctx->room, now_ms);
        phase_ev.payload.phase_changed.game_remaining_ms = ctx->room.game_end_ms > now_ms ? ctx->room.game_end_ms - now_ms : 0;
        emit_event(ctx, &phase_ev);
    }
    else if (tick_res == TOOS_TYPE_ROOM_TICK_GAME_ENDED) {
        for (uint32_t i = 0; i < TOOS_TYPE_MAX_ROOM_PLAYERS; i++) {
            if (ctx->players[i].occupied && ctx->players[i].finish_status == TOOS_TYPE_FINISH_NONE) {
                ToosTypePlayerState *p = &ctx->players[i];
                if (p->active_sentence_id != 0 && p->sentence_deadline_ms <= ctx->room.game_end_ms) {
                    uint64_t expired_sid = p->active_sentence_id;
                    p->expired_count++;
                    p->active_sentence_id = 0;

                    ToosTypeEvent res_ev;
                    memset(&res_ev, 0, sizeof(res_ev));
                    res_ev.type = TOOS_TYPE_EVENT_SENTENCE_RESULT;
                    res_ev.target_conn = p->conn_ptr;
                    res_ev.payload.result.sentence_id = expired_sid;
                    res_ev.payload.result.result = TOOS_TYPE_JUDGE_EXPIRED;
                    emit_event(ctx, &res_ev);
                }
                p->played_ms = ctx->room.game_end_ms - ctx->room.game_start_ms;
                p->finish_status = TOOS_TYPE_FINISH_FINISHED;
                update_player_stats(ctx, p, ctx->room.game_end_ms);
            }
        }

        ToosTypeRankingEntry entries[TOOS_TYPE_MAX_ROOM_PLAYERS];
        uint32_t entry_count = 0;

        for (uint32_t i = 0; i < TOOS_TYPE_MAX_ROOM_PLAYERS; i++) {
            if (ctx->players[i].occupied) {
                const ToosTypePlayerState *p = &ctx->players[i];
                entries[entry_count].rank = 0;
                entries[entry_count].player_no = p->player_no;
                entries[entry_count].status = p->finish_status;
                entries[entry_count].played_ms = p->played_ms;
                entries[entry_count].play_score = p->play_score;
                entries[entry_count].phase_score = p->phase_score;
                entries[entry_count].accuracy_score = p->accuracy_score;
                entries[entry_count].accuracy_x100 = p->accuracy_x100;
                entries[entry_count].total_score = p->total_score;
                entries[entry_count].wpm = p->wpm;
                entry_count++;
            }
        }

        // 안정 정렬 (player_no 기반) 후 순위 동률 판정 적용
        for (uint32_t i = 0; i < entry_count; i++) {
            for (uint32_t j = i + 1; j < entry_count; j++) {
                bool swap = false;
                const ToosTypeRankingEntry *e1 = &entries[i];
                const ToosTypeRankingEntry *e2 = &entries[j];

                if (e1->status == TOOS_TYPE_FINISH_DNF && e2->status == TOOS_TYPE_FINISH_FINISHED) swap = true;
                else if (e1->status == TOOS_TYPE_FINISH_FINISHED && e2->status == TOOS_TYPE_FINISH_FINISHED) {
                    if (e2->total_score > e1->total_score) swap = true;
                    else if (e2->total_score == e1->total_score) {
                        if (e2->accuracy_x100 > e1->accuracy_x100) swap = true;
                        else if (e2->accuracy_x100 == e1->accuracy_x100) {
                            if (e2->phase_score > e1->phase_score) swap = true;
                            else if (e2->phase_score == e1->phase_score) {
                                if (e2->wpm > e1->wpm) swap = true;
                                else if (e2->wpm == e1->wpm && e2->player_no < e1->player_no) swap = true;
                            }
                        }
                    }
                } else if (e1->status == TOOS_TYPE_FINISH_DNF && e2->status == TOOS_TYPE_FINISH_DNF) {
                    if (e2->total_score > e1->total_score) swap = true;
                    else if (e2->total_score == e1->total_score) {
                        if (e2->played_ms > e1->played_ms) swap = true;
                        else if (e2->played_ms == e1->played_ms) {
                            if (e2->accuracy_x100 > e1->accuracy_x100) swap = true;
                            else if (e2->accuracy_x100 == e1->accuracy_x100) {
                                if (e2->phase_score > e1->phase_score) swap = true;
                                else if (e2->phase_score == e1->phase_score) {
                                    if (e2->wpm > e1->wpm) swap = true;
                                    else if (e2->wpm == e1->wpm && e2->player_no < e1->player_no) swap = true;
                                }
                            }
                        }
                    }
                }
                if (swap) {
                    ToosTypeRankingEntry temp = entries[i];
                    entries[i] = entries[j];
                    entries[j] = temp;
                }
            }
        }

        if (entry_count > 0) entries[0].rank = 1;
        for (uint32_t i = 1; i < entry_count; i++) {
            const ToosTypeRankingEntry *prev = &entries[i - 1];
            ToosTypeRankingEntry *curr = &entries[i];

            if (curr->status == prev->status &&
                curr->total_score == prev->total_score &&
                (curr->status == TOOS_TYPE_FINISH_FINISHED || curr->played_ms == prev->played_ms) &&
                curr->accuracy_x100 == prev->accuracy_x100 &&
                curr->phase_score == prev->phase_score &&
                curr->wpm == prev->wpm)
            {
                curr->rank = prev->rank;
            } else {
                curr->rank = i + 1;
            }
        }

        ToosTypeEvent over_ev;
        memset(&over_ev, 0, sizeof(over_ev));
        over_ev.type = TOOS_TYPE_EVENT_GAME_OVER;
        over_ev.payload.game_over.reason = "NORMAL_END";
        emit_event(ctx, &over_ev);

        ToosTypeEvent rank_ev;
        memset(&rank_ev, 0, sizeof(rank_ev));
        rank_ev.type = TOOS_TYPE_EVENT_FINAL_RANKING;
        rank_ev.payload.ranking.count = entry_count;
        memcpy(rank_ev.payload.ranking.entries, entries, entry_count * sizeof(ToosTypeRankingEntry));
        emit_event(ctx, &rank_ev);

        emit_room_config(ctx, NULL);
        return;
    }

    if (ctx->room.state == TOOS_TYPE_STATE_PLAYING) {
        for (uint32_t i = 0; i < TOOS_TYPE_MAX_ROOM_PLAYERS; i++) {
            if (ctx->players[i].occupied && ctx->players[i].connected) {
                ToosTypePlayerState *p = &ctx->players[i];
                if (p->finish_status != TOOS_TYPE_FINISH_NONE) continue;

                if (p->active_sentence_id != 0 && now_ms >= p->sentence_deadline_ms) {
                    p->expired_count++;
                    uint64_t expired_sid = p->active_sentence_id;
                    update_player_stats(ctx, p, now_ms);

                    ToosTypeEvent res_ev;
                    memset(&res_ev, 0, sizeof(res_ev));
                    res_ev.type = TOOS_TYPE_EVENT_SENTENCE_RESULT;
                    res_ev.target_conn = p->conn_ptr;
                    res_ev.payload.result.sentence_id = expired_sid;
                    res_ev.payload.result.result = TOOS_TYPE_JUDGE_EXPIRED;
                    emit_event(ctx, &res_ev);

                    ToosTypeEvent score_ev;
                    memset(&score_ev, 0, sizeof(score_ev));
                    score_ev.type = TOOS_TYPE_EVENT_SCORE_UPDATE;
                    score_ev.target_conn = p->conn_ptr;
                    score_ev.payload.score.pno = p->player_no;
                    score_ev.payload.score.sid = expired_sid;
                    score_ev.payload.score.play = p->play_score;
                    score_ev.payload.score.phase = p->phase_score;
                    score_ev.payload.score.acc_score = p->accuracy_score;
                    score_ev.payload.score.acc_x100 = p->accuracy_x100;
                    score_ev.payload.score.total = p->total_score;
                    score_ev.payload.score.wpm = p->wpm;
                    emit_event(ctx, &score_ev);

                    p->sentence_cursor++;
                    issue_next_sentence(ctx, p, now_ms);
                }
            }
        }
    }
}