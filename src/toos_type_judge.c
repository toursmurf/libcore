#include "toos_type_judge.h"
#include <string.h>
#include <stdlib.h>

void toos_type_player_state_init(ToosTypePlayerState *player, uint64_t player_id) {
    if (!player) return;
    memset(player, 0, sizeof(ToosTypePlayerState));
    player->player_id = player_id;
}

void toos_type_player_state_reset_for_match(ToosTypePlayerState *player) {
    if (!player) return;
    uint64_t pid = player->player_id;
    memset(player, 0, sizeof(ToosTypePlayerState));
    player->player_id = pid;
}

// 내부 헬퍼: 플레이어의 특정 문장 기록 조회
static ToosTypePlayerSentenceRecord* find_player_record(ToosTypePlayerState *player, uint64_t sentence_id) {
    if (!player) return NULL;
    for (uint32_t i = 0; i < player->record_count && i < TOOS_TYPE_MAX_PLAYER_SENTENCES; i++) {
        if (player->records[i].sentence_id == sentence_id) {
            return &player->records[i];
        }
    }
    return NULL;
}

// 내부 헬퍼: Protocol v0.3 EN 모델을 위한 엄격한 ASCII 검증 (비ASCII UTF-8 차단)
static bool is_ascii_text(const char *text) {
    if (!text) return false;
    const unsigned char *p = (const unsigned char *)text;
    while (*p) {
        if (*p > 0x7F) {
            return false;
        }
        p++;
    }
    return true;
}

bool toos_type_player_sentence_activate(ToosTypePlayerState *player, uint64_t sentence_id, uint8_t spawn_phase) {
    if (!player || sentence_id == 0) return false;

    if (spawn_phase < 1 || spawn_phase > 3) {
        return false;
    }

    ToosTypePlayerSentenceRecord *record = find_player_record(player, sentence_id);
    if (record) {
        if (record->status == TOOS_TYPE_PLAYER_SENTENCE_UNUSED) {
            record->status = TOOS_TYPE_PLAYER_SENTENCE_ACTIVE;
            record->spawn_phase = spawn_phase;
            return true;
        }
        return false;
    }

    if (player->record_count >= TOOS_TYPE_MAX_PLAYER_SENTENCES) {
        return false;
    }

    record = &player->records[player->record_count++];
    record->sentence_id = sentence_id;
    record->status = TOOS_TYPE_PLAYER_SENTENCE_ACTIVE;
    record->spawn_phase = spawn_phase;
    record->cleared_at_ms = 0;
    record->earned_score = 0;
    record->wpm = 0;

    return true;
}

bool toos_type_player_sentence_mark_missed(ToosTypePlayerState *player, uint64_t sentence_id) {
    if (!player || sentence_id == 0) return false;

    ToosTypePlayerSentenceRecord *record = find_player_record(player, sentence_id);
    if (!record) {
        return false;
    }

    switch (record->status) {
        case TOOS_TYPE_PLAYER_SENTENCE_ACTIVE:
            record->status = TOOS_TYPE_PLAYER_SENTENCE_MISSED;
            return true;

        case TOOS_TYPE_PLAYER_SENTENCE_CLEARED:
        case TOOS_TYPE_PLAYER_SENTENCE_MISSED:
            return true; // 멱등성 유지 (No-op)

        case TOOS_TYPE_PLAYER_SENTENCE_UNUSED:
        default:
            return false;
    }
}

// Protocol v0.3 정수형 점수 계산기 (Phase 1~3 배율 적용)
static uint32_t calculate_protocol_score(uint32_t wpm, uint8_t phase) {
    uint32_t score = 100; // Exact Match 기본 100점

    if (wpm >= 100) {
        score += 50;
    } else if (wpm >= 80) {
        score += 30;
    }

    switch (phase) {
        case 1:
            break;
        case 2:
            score = (score * 12U) / 10U; // ×1.2
            break;
        case 3:
            score = (score * 15U) / 10U; // ×1.5
            break;
        default:
            return 0;
    }

    return score;
}

ToosTypeJudgeResult toos_type_judge_submit(
    const ToosTypeSpawnEngine *spawn_engine,
    ToosTypePlayerState *player_state,
    ToosTypeLanguage language,
    uint64_t sentence_id,
    const char *typed_text,
    size_t typed_len,
    uint64_t now_ms,
    uint32_t *out_earned_score,
    uint32_t *out_wpm)
{
    // 출력값 선제 초기화 (어떤 실패 경로에서도 0 보장)
    if (out_earned_score) *out_earned_score = 0;
    if (out_wpm) *out_wpm = 0;

    if (!spawn_engine || !player_state || !typed_text) {
        return TOOS_TYPE_JUDGE_ERROR;
    }

    // Spawn Engine이 Fatal-Stop(실패 중지) 상태인 경우 Judge 판정 원천 거부
    if (!spawn_engine->started) {
        return TOOS_TYPE_JUDGE_ERROR;
    }

    // Protocol v0.3 WPM 정합성 가드: EN 배틀만 지원
    if (language != TOOS_TYPE_LANGUAGE_EN) {
        return TOOS_TYPE_JUDGE_ERROR;
    }

    // Player FSM을 '최우선'으로 조회
    ToosTypePlayerSentenceRecord *record = find_player_record(player_state, sentence_id);
    if (!record || record->status == TOOS_TYPE_PLAYER_SENTENCE_UNUSED) {
        return TOOS_TYPE_JUDGE_NOT_FOUND;
    }

    // FSM Terminal 상태 선행 검증
    if (record->status == TOOS_TYPE_PLAYER_SENTENCE_CLEARED) {
        return TOOS_TYPE_JUDGE_ALREADY_CLEARED;
    }
    if (record->status == TOOS_TYPE_PLAYER_SENTENCE_MISSED) {
        return TOOS_TYPE_JUDGE_EXPIRED;
    }

    // ACTIVE 상태일 때만 Spawn Engine에서 현재 살아있는 shared sentence 검증
    ToosTypeActiveSentence active;
    if (!toos_type_spawn_engine_find_active(spawn_engine, sentence_id, &active)) {
        return TOOS_TYPE_JUDGE_NOT_FOUND;
    }

    // 데드라인 엄격 검증 (now_ms >= deadline 이면 즉시 MISSED 전환 후 만료 처리)
    if (now_ms >= active.deadline_at_ms) {
        record->status = TOOS_TYPE_PLAYER_SENTENCE_MISSED;
        return TOOS_TYPE_JUDGE_EXPIRED;
    }

    // 시간 역전 방어 및 WPM 계산
    if (now_ms < active.spawned_at_ms) {
        return TOOS_TYPE_JUDGE_ERROR;
    }

    const ToosTypeSentence *sentence = toos_type_spawn_engine_get_sentence(spawn_engine, &active);
    if (!sentence) {
        return TOOS_TYPE_JUDGE_ERROR;
    }

    // EN 모드이므로 en_text 사용
    const char *target = sentence->en_text;
    if (!target || target[0] == '\0') {
        return TOOS_TYPE_JUDGE_ERROR;
    }

    // EN 모델이므로 원문이 순수 ASCII인지 엄격히 검증
    if (!is_ascii_text(target)) {
        return TOOS_TYPE_JUDGE_ERROR;
    }

    uint64_t elapsed_ms = now_ms - active.spawned_at_ms;
    if (elapsed_ms == 0) {
        elapsed_ms = 1;
    }

    size_t target_len = strlen(target);

    // 널 종속성 및 임베디드 NUL 취약점을 방어하는 엄격한 바이트 길이 + memcmp 비교
    if (typed_len != target_len || memcmp(target, typed_text, target_len) != 0) {
        return TOOS_TYPE_JUDGE_INCORRECT;
    }

    uint32_t char_count = (uint32_t)target_len;

    // 64비트 연산을 통한 WPM 오버플로우 원천 방어
    uint64_t wpm64 = ((uint64_t)char_count * 12000ULL) / elapsed_ms;
    if (wpm64 > UINT32_MAX) {
        return TOOS_TYPE_JUDGE_ERROR;
    }
    uint32_t wpm = (uint32_t)wpm64;

    // 정답 점수 산정 (Phase 배율 검증 포함)
    uint32_t earned = calculate_protocol_score(wpm, record->spawn_phase);
    if (earned == 0) {
        return TOOS_TYPE_JUDGE_ERROR;
    }

    record->status = TOOS_TYPE_PLAYER_SENTENCE_CLEARED;
    record->cleared_at_ms = now_ms;
    record->earned_score = earned;
    record->wpm = wpm;

    player_state->total_score += earned;
    player_state->correct_count++;

    if (out_earned_score) {
        *out_earned_score = earned;
    }
    if (out_wpm) {
        *out_wpm = wpm;
    }

    return TOOS_TYPE_JUDGE_CORRECT;
}