#pragma once

#include "toos_type_spawn.h"
#include "toos_type_sentence.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h> // size_t 보장용

// 판정 결과 코드 (Protocol v0.3)
typedef enum {
    TOOS_TYPE_JUDGE_CORRECT = 0,          // 정답 (CLEARED 전환 및 점수 획득)
    TOOS_TYPE_JUDGE_INCORRECT,          // 오타 (ACTIVE 유지, 재시도 가능)
    TOOS_TYPE_JUDGE_EXPIRED,            // 데드라인 초과 또는 이미 MISSED 처리됨
    TOOS_TYPE_JUDGE_ALREADY_CLEARED,    // 이미 클리어한 단어 (중복 방어)
    TOOS_TYPE_JUDGE_NOT_FOUND,          // 존재하지 않거나 알 수 없는 sentence_id
    TOOS_TYPE_JUDGE_ERROR               // 인자 오류, 비정상 엔진 상태, 언어/인코딩 불일치 등 시스템 에러
} ToosTypeJudgeResult;

// 플레이어별 문장 상태 머신 (FSM)
typedef enum {
    TOOS_TYPE_PLAYER_SENTENCE_UNUSED = 0,
    TOOS_TYPE_PLAYER_SENTENCE_ACTIVE,
    TOOS_TYPE_PLAYER_SENTENCE_CLEARED,
    TOOS_TYPE_PLAYER_SENTENCE_MISSED
} ToosTypePlayerSentenceStatus;

// 플레이어별 개별 문장 기록 (Pool 최대 크기인 200개 완벽 추적)
typedef struct {
    uint64_t sentence_id;
    ToosTypePlayerSentenceStatus status;
    uint8_t spawn_phase;          // 문장이 스폰된 순간의 페이즈 (1 ~ 3)
    uint64_t cleared_at_ms;
    uint32_t earned_score;
    uint32_t wpm;
} ToosTypePlayerSentenceRecord;

#define TOOS_TYPE_MAX_PLAYER_SENTENCES TOOS_TYPE_MAX_POOL_SIZE

// 플레이어 상태 구조체
typedef struct {
    uint64_t player_id;
    uint32_t total_score;
    uint32_t correct_count;

    ToosTypePlayerSentenceRecord records[TOOS_TYPE_MAX_PLAYER_SENTENCES];
    uint32_t record_count;
} ToosTypePlayerState;

// --------------------------------------------------------
// Judge API
// --------------------------------------------------------

void toos_type_player_state_init(ToosTypePlayerState *player, uint64_t player_id);

// 재경기(Rematch) 시 유저 장부 초기화 API
void toos_type_player_state_reset_for_match(ToosTypePlayerState *player);

// 1. Room이 SPAWNED 발생 시 플레이어별로 호출하여 ACTIVE 상태로 등록 (Phase 1~3 검증)
bool toos_type_player_sentence_activate(ToosTypePlayerState *player, uint64_t sentence_id, uint8_t spawn_phase);

// 2. Room이 만료(Expiration) 발생 시 플레이어별로 호출하여 MISSED 상태로 전환 (멱등성 보장)
bool toos_type_player_sentence_mark_missed(ToosTypePlayerState *player, uint64_t sentence_id);

// 3. 핵심 판정 함수: typed_len 기반 안전한 Exact Match 및 프로토콜 v0.3 규칙 검증
ToosTypeJudgeResult toos_type_judge_submit(
    const ToosTypeSpawnEngine *spawn_engine,
    ToosTypePlayerState *player_state,
    ToosTypeLanguage language,
    uint64_t sentence_id,
    const char *typed_text,
    size_t typed_len,
    uint64_t now_ms,
    uint32_t *out_earned_score,
    uint32_t *out_wpm
);