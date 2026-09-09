#include "toos_type_spawn.h"
#include <stdlib.h>
#include <string.h>

// --------------------------------------------------------
// Static Internal Helpers
// --------------------------------------------------------

// 🔴 [수정 1] 공통 Fail-Stop 헬퍼 추가
static void fail_stop(ToosTypeSpawnEngine *engine) {
    if (!engine) return;
    engine->started = false;
    engine->next_spawn_ms = 0;
    engine->rematch_required = true;
}

static bool make_deadline(uint64_t now_ms, uint32_t duration_ms, uint64_t *out_deadline) {
    if (!out_deadline) return false;
    if (UINT64_MAX - now_ms < duration_ms) return false;
    *out_deadline = now_ms + duration_ms;
    return true;
}

static uint32_t local_rand_next(uint64_t *state) {
    if (!state) return 0;
    *state = (*state * 6364136223846793005ULL + 1442695040888963407ULL);
    return (uint32_t)(*state >> 32);
}

static uint32_t bounded_rand(uint64_t *state, uint32_t bound) {
    if (!state || bound == 0) return 0;
    if (bound == 1) return 0;
    uint32_t threshold = (uint32_t)(-bound) % bound;
    uint32_t r;
    do {
        r = local_rand_next(state);
    } while (r < threshold);
    return r % bound;
}

static void shuffle_pool_indices(ToosTypeSentencePool *pool, uint64_t *rng_state) {
    if (pool->count == 0) return;
    for (uint32_t i = pool->count - 1; i > 0; i--) {
        uint32_t j = bounded_rand(rng_state, i + 1);
        uint32_t temp = pool->indices[i];
        pool->indices[i] = pool->indices[j];
        pool->indices[j] = temp;
    }
    pool->head = 0;
}

static bool compute_next_spawn_ms(uint64_t current_spawn_ms, uint32_t interval_ms, uint64_t now_ms, uint64_t *out_next) {
    if (!out_next || interval_ms == 0) return false;
    uint64_t next = current_spawn_ms;
    do {
        if (!make_deadline(next, interval_ms, &next)) return false;
    } while (next <= now_ms);
    *out_next = next;
    return true;
}

static bool advance_spawn_schedule(ToosTypeSpawnEngine *engine, uint64_t now_ms) {
    if (!engine) return false;
    uint64_t next = 0;

    // 🔴 [수정 1] 스케줄 실패 시 완벽한 fail_stop
    if (!compute_next_spawn_ms(engine->next_spawn_ms, engine->spawn_interval_ms, now_ms, &next)) {
        fail_stop(engine);
        return false;
    }

    engine->next_spawn_ms = next;
    return true;
}

static bool pool_collect_callback(const ToosTypeSentence *sentence, void *user_data) {
    if (!sentence || !user_data) return false;
    ToosTypeSpawnEngine *engine = (ToosTypeSpawnEngine *)user_data;
    ToosTypeSentencePool *pool = &engine->pool;

    if (engine->total_db_rows_visited == UINT32_MAX) return false;

    engine->total_db_rows_visited++;

    if (pool->count < TOOS_TYPE_MAX_POOL_SIZE) {
        pool->sentences[pool->count] = *sentence;
        pool->indices[pool->count] = pool->count;
        pool->count++;
    } else {
        uint32_t j = bounded_rand(&engine->rng_state, engine->total_db_rows_visited);
        if (j < TOOS_TYPE_MAX_POOL_SIZE) {
            pool->sentences[j] = *sentence;
        }
    }
    return true;
}

// --------------------------------------------------------
// API Implementation
// --------------------------------------------------------

void toos_type_spawn_engine_init(ToosTypeSpawnEngine *engine, uint32_t interval_ms, uint32_t fall_duration_ms, uint8_t max_active, uint64_t seed) {
    if (!engine) return;
    memset(engine, 0, sizeof(ToosTypeSpawnEngine));
    engine->spawn_interval_ms = interval_ms;
    engine->fall_duration_ms = fall_duration_ms;
    engine->max_active = max_active;
    engine->rng_state = (seed == 0) ? 12345ULL : seed;
    engine->loaded = false;
    engine->started = false;
    engine->rematch_required = false;
}

bool toos_type_spawn_engine_load(ToosTypeSpawnEngine *engine, sqlite3 *db, ToosTypeLanguage lang, uint8_t difficulty) {
    if (!engine || !db || engine->started) return false;

    memset(engine->active_list, 0, sizeof(engine->active_list));
    engine->next_spawn_ms = 0;
    engine->pool.count = 0;
    engine->pool.head = 0;
    engine->total_db_rows_visited = 0;
    engine->loaded = false;

    bool ok = toos_type_sentence_visit_by_difficulty(db, lang, difficulty, pool_collect_callback, engine);

    if (!ok || engine->pool.count == 0) {
        memset(&engine->pool, 0, sizeof(engine->pool));
        memset(engine->active_list, 0, sizeof(engine->active_list));
        engine->total_db_rows_visited = 0;
        engine->next_spawn_ms = 0;
        engine->loaded = false;
        engine->started = false;
        engine->rematch_required = false;
        return false;
    }

    shuffle_pool_indices(&engine->pool, &engine->rng_state);
    engine->loaded = true;
    engine->rematch_required = false;
    return true;
}

bool toos_type_spawn_engine_start(ToosTypeSpawnEngine *engine, uint64_t game_start_ms) {
    if (!engine || !engine->loaded || engine->started) return false;
    if (engine->rematch_required) return false;

    if (engine->spawn_interval_ms == 0 || engine->fall_duration_ms == 0) return false;
    if (engine->max_active == 0 || engine->max_active > TOOS_TYPE_ACTIVE_CAPACITY) return false;

    uint64_t next_spawn_ms = 0;
    if (!make_deadline(game_start_ms, engine->spawn_interval_ms, &next_spawn_ms)) {
        return false;
    }

    engine->next_spawn_ms = next_spawn_ms;
    engine->started = true;
    return true;
}

void toos_type_spawn_engine_stop(ToosTypeSpawnEngine *engine) {
    if (!engine) return;
    engine->started = false;
    engine->next_spawn_ms = 0;
    memset(engine->active_list, 0, sizeof(engine->active_list));
    engine->rematch_required = true;
}

// 🔴 [수정 2] 원자성 보장: Fallible calculation first -> Commit
bool toos_type_spawn_engine_reset_for_rematch(ToosTypeSpawnEngine *engine, uint64_t new_game_start_ms, uint32_t initial_interval_ms, uint8_t initial_max_active) {
    // 1. 상태 및 입력값 엄격 검증
    if (!engine || !engine->loaded || engine->started || !engine->rematch_required) return false;
    if (initial_interval_ms == 0) return false;
    if (engine->fall_duration_ms == 0) return false;
    if (initial_max_active == 0 || initial_max_active > TOOS_TYPE_ACTIVE_CAPACITY) return false;

    // 2. Fallible calculation (에러 나면 기존 상태 완벽 보존)
    uint64_t next_spawn_ms = 0;
    if (!make_deadline(new_game_start_ms, initial_interval_ms, &next_spawn_ms)) {
        return false;
    }

    // 3. Commit begins here (실패 불가능)
    engine->spawn_interval_ms = initial_interval_ms;
    engine->max_active = initial_max_active;

    memset(engine->active_list, 0, sizeof(engine->active_list));
    engine->pool.head = 0;

    shuffle_pool_indices(&engine->pool, &engine->rng_state);

    engine->next_spawn_ms = next_spawn_ms;
    engine->started = true;
    engine->rematch_required = false;

    return true;
}

bool toos_type_spawn_engine_set_phase(ToosTypeSpawnEngine *engine, uint64_t now_ms, uint32_t new_interval_ms, uint8_t new_max_active) {
    if (!engine) return false;
    if (new_interval_ms == 0) return false;
    if (new_max_active == 0 || new_max_active > TOOS_TYPE_ACTIVE_CAPACITY) return false;

    if (engine->started) {
        uint64_t new_next = 0;
        if (!make_deadline(now_ms, new_interval_ms, &new_next)) return false;
        engine->next_spawn_ms = new_next;
    }

    engine->spawn_interval_ms = new_interval_ms;
    engine->max_active = new_max_active;
    return true;
}

bool toos_type_spawn_engine_pop_expired(ToosTypeSpawnEngine *engine, uint64_t now_ms, ToosTypeActiveSentence *out_expired) {
    if (!engine || !engine->started || !out_expired) return false;

    for (int i = 0; i < TOOS_TYPE_ACTIVE_CAPACITY; i++) {
        if (engine->active_list[i].is_active && now_ms >= engine->active_list[i].deadline_at_ms) {
            *out_expired = engine->active_list[i];
            engine->active_list[i].is_active = false;
            return true;
        }
    }
    return false;
}

ToosTypeSpawnResult toos_type_spawn_engine_tick(ToosTypeSpawnEngine *engine, uint64_t now_ms, ToosTypeActiveSentence *out_spawned) {
    if (!engine || !out_spawned) return TOOS_TYPE_SPAWN_ERROR;
    if (!engine->started) return TOOS_TYPE_SPAWN_NONE;

    if (now_ms < engine->next_spawn_ms) return TOOS_TYPE_SPAWN_NONE;
    if (engine->pool.head >= engine->pool.count) return TOOS_TYPE_SPAWN_NONE;

    int active_count = 0;
    for (int i = 0; i < TOOS_TYPE_ACTIVE_CAPACITY; i++) {
        if (engine->active_list[i].is_active) active_count++;
    }

    if (active_count >= engine->max_active) {
        if (!advance_spawn_schedule(engine, now_ms)) return TOOS_TYPE_SPAWN_ERROR;
        return TOOS_TYPE_SPAWN_NONE;
    }

    ToosTypeActiveSentence *spawned = NULL;
    for (int i = 0; i < TOOS_TYPE_ACTIVE_CAPACITY; i++) {
        if (!engine->active_list[i].is_active) {
            spawned = &engine->active_list[i];
            break;
        }
    }

    // 🔴 [수정 1] 불변식 파괴 에러 방어
    if (!spawned) {
        fail_stop(engine);
        return TOOS_TYPE_SPAWN_ERROR;
    }

    // 🔴 [수정 1] Fallible 로직 모두 fail_stop 연동
    uint64_t deadline = 0;
    if (!make_deadline(now_ms, engine->fall_duration_ms, &deadline)) {
        fail_stop(engine);
        return TOOS_TYPE_SPAWN_ERROR;
    }

    uint64_t next_spawn = 0;
    if (!compute_next_spawn_ms(engine->next_spawn_ms, engine->spawn_interval_ms, now_ms, &next_spawn)) {
        fail_stop(engine);
        return TOOS_TYPE_SPAWN_ERROR;
    }

    // --- Commit ---
    uint32_t pool_idx = engine->pool.indices[engine->pool.head];

    spawned->sentence_id = engine->pool.sentences[pool_idx].id;
    spawned->spawned_at_ms = now_ms;
    spawned->deadline_at_ms = deadline;
    spawned->pool_index = pool_idx;
    spawned->is_active = true;

    engine->pool.head++;
    engine->next_spawn_ms = next_spawn;

    *out_spawned = *spawned;

    return TOOS_TYPE_SPAWNED;
}

bool toos_type_spawn_engine_find_active(const ToosTypeSpawnEngine *engine, uint64_t sentence_id, ToosTypeActiveSentence *out_active) {
    if (!engine || !out_active || sentence_id == 0) return false;

    for (uint32_t i = 0; i < TOOS_TYPE_ACTIVE_CAPACITY; i++) {
        const ToosTypeActiveSentence *active = &engine->active_list[i];
        if (active->is_active && active->sentence_id == sentence_id) {
            *out_active = *active;
            return true;
        }
    }
    return false;
}

bool toos_type_spawn_engine_retire_shared(ToosTypeSpawnEngine *engine, uint64_t sentence_id) {
    if (!engine) return false;
    for (int i = 0; i < TOOS_TYPE_ACTIVE_CAPACITY; i++) {
        if (engine->active_list[i].is_active && engine->active_list[i].sentence_id == sentence_id) {
            engine->active_list[i].is_active = false;
            return true;
        }
    }
    return false;
}

const ToosTypeSentence* toos_type_spawn_engine_get_sentence(const ToosTypeSpawnEngine *engine, const ToosTypeActiveSentence *active) {
    if (!engine || !active) return NULL;
    if (active->pool_index >= engine->pool.count) return NULL;
    return &engine->pool.sentences[active->pool_index];
}