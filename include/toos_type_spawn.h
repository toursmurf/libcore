#pragma once

#include "toos_type_sentence.h"
#include <stdint.h>
#include <stdbool.h>

#define TOOS_TYPE_MAX_POOL_SIZE 200
#define TOOS_TYPE_ACTIVE_CAPACITY 10

// --------------------------------------------------------
// 1. Data Structures
// --------------------------------------------------------

typedef struct {
    uint64_t sentence_id;
    uint64_t spawned_at_ms;
    uint64_t deadline_at_ms;
    uint32_t pool_index;
    bool is_active;
} ToosTypeActiveSentence;

typedef struct {
    ToosTypeSentence sentences[TOOS_TYPE_MAX_POOL_SIZE];
    uint32_t indices[TOOS_TYPE_MAX_POOL_SIZE];
    uint32_t count;
    uint32_t head;
} ToosTypeSentencePool;

typedef struct {
    ToosTypeSentencePool pool;
    ToosTypeActiveSentence active_list[TOOS_TYPE_ACTIVE_CAPACITY];

    uint64_t next_spawn_ms;
    uint32_t spawn_interval_ms;
    uint32_t fall_duration_ms;    // 경기 내내 고정 (Protocol v0.3)

    uint32_t total_db_rows_visited;
    uint8_t max_active;
    uint64_t rng_state;

    bool loaded;
    bool started;
    bool rematch_required;        // Lifecycle Guard
} ToosTypeSpawnEngine;

typedef enum {
    TOOS_TYPE_SPAWN_NONE = 0,
    TOOS_TYPE_SPAWNED,
    TOOS_TYPE_SPAWN_ERROR
} ToosTypeSpawnResult;

// --------------------------------------------------------
// 2. Lifecycle & Setup API
// --------------------------------------------------------

// 🟡 seed == 0 이면 내부적으로 deterministic 기본값(12345)을 사용합니다.
void toos_type_spawn_engine_init(ToosTypeSpawnEngine *engine, uint32_t interval_ms, uint32_t fall_duration_ms, uint8_t max_active, uint64_t seed);
bool toos_type_spawn_engine_load(ToosTypeSpawnEngine *engine, sqlite3 *db, ToosTypeLanguage lang, uint8_t difficulty);
bool toos_type_spawn_engine_start(ToosTypeSpawnEngine *engine, uint64_t game_start_ms);

void toos_type_spawn_engine_stop(ToosTypeSpawnEngine *engine);

// 🔴 [수정 2] Atomicity 보장 및 상태 엄격 체크가 적용된 Rematch
bool toos_type_spawn_engine_reset_for_rematch(ToosTypeSpawnEngine *engine, uint64_t new_game_start_ms, uint32_t initial_interval_ms, uint8_t initial_max_active);

bool toos_type_spawn_engine_set_phase(ToosTypeSpawnEngine *engine, uint64_t now_ms, uint32_t new_interval_ms, uint8_t new_max_active);

// --------------------------------------------------------
// 3. Runtime Tick & Event API
// --------------------------------------------------------
bool toos_type_spawn_engine_pop_expired(ToosTypeSpawnEngine *engine, uint64_t now_ms, ToosTypeActiveSentence *out_expired);
ToosTypeSpawnResult toos_type_spawn_engine_tick(ToosTypeSpawnEngine *engine, uint64_t now_ms, ToosTypeActiveSentence *out_spawned);
bool toos_type_spawn_engine_find_active(const ToosTypeSpawnEngine *engine, uint64_t sentence_id, ToosTypeActiveSentence *out_active);
bool toos_type_spawn_engine_retire_shared(ToosTypeSpawnEngine *engine, uint64_t sentence_id);
const ToosTypeSentence* toos_type_spawn_engine_get_sentence(const ToosTypeSpawnEngine *engine, const ToosTypeActiveSentence *active);