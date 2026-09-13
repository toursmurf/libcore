#include "toos_type_spawn.h"
#include <string.h>
#include <stdlib.h>
#include  "logger.h"

// --------------------------------------------------------
// Static Helpers: 32-bit & 64-bit Deterministic RNG (LCG)
// --------------------------------------------------------

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

//[패치 2] 64-bit Reservoir Sampling을 위한 고정밀 난수 생성기
static uint64_t local_rand_next64(uint64_t *state) {
    if (!state) return 0;
    *state = (*state * 6364136223846793005ULL) + 1442695040888963407ULL;
    return *state;
}

static uint64_t bounded_rand64(uint64_t *state, uint64_t bound) {
    if (!state || bound == 0) return 0;
    if (bound == 1) return 0;

    uint64_t threshold = (uint64_t)(-bound) % bound;
    uint64_t r;
    do {
        r = local_rand_next64(state);
    } while (r < threshold);
    return r % bound;
}

// --------------------------------------------------------
// API Implementation
// --------------------------------------------------------

void ToosTypeSpawnEngine_init(ToosTypeSpawnEngine *engine, uint64_t seed) {
    if (!engine) return;
    memset(engine, 0, sizeof(ToosTypeSpawnEngine));
    engine->rng_state = seed;
    engine->loaded = false;
}

void ToosTypeSpawnEngine_deinit(ToosTypeSpawnEngine *engine) {
    if (!engine) return;

    if (engine->base_pool) {
        free(engine->base_pool);
        engine->base_pool = NULL;
    }
    if (engine->sequence) {
        free(engine->sequence);
        engine->sequence = NULL;
    }
    memset(engine, 0, sizeof(ToosTypeSpawnEngine));
}

typedef struct {
    ToosTypeSentence *pool;
    uint32_t pool_capacity;
    uint32_t pool_count;
    uint64_t seen_count;
    uint64_t rng_state;
} ReservoirCollector;

//[패치 2] 64-bit 난수를 사용하여 확률 붕괴 차단
static bool pool_collect_callback(const ToosTypeSentence *sentence, void *user_data) {
    if (!sentence || !user_data) return false;
    ReservoirCollector *collector = (ReservoirCollector *)user_data;

    if (collector->seen_count == UINT64_MAX) return false;

    collector->seen_count++;

    if (collector->pool_count < collector->pool_capacity) {
        collector->pool[collector->pool_count] = *sentence;
        collector->pool_count++;
    } else {
        uint64_t j = bounded_rand64(&collector->rng_state, collector->seen_count);
        if (j < (uint64_t)collector->pool_capacity) {
            collector->pool[(size_t)j] = *sentence;
        }
    }
    return true;
}

bool ToosTypeSpawnEngine_build_deck(ToosTypeSpawnEngine *engine, sqlite3 *db, ToosTypeLanguage language) {
    if (!engine || !db) return false;
    if (engine->loaded) return false;

    ToosTypeSentence *temp_pool = (ToosTypeSentence *)calloc(TOOS_TYPE_MAX_POOL_SIZE, sizeof(ToosTypeSentence));
    if (!temp_pool) return false;

    ReservoirCollector collector;
    collector.pool = temp_pool;
    collector.pool_capacity = TOOS_TYPE_MAX_POOL_SIZE;
    collector.pool_count = 0;
    collector.seen_count = 0;
    collector.rng_state = engine->rng_state;

    // [패치 1] 단 하나의 에러라도 발생 시 임시 메모리 반환 및 상태 보존(Failure Atomicity)
    for (uint8_t diff = 1; diff <= 3; ++diff) {
        uint64_t seen_before = collector.seen_count;
        uint32_t pool_before = collector.pool_count;

        LOG_INFO(logger,
            "[ToosType Spawn] loading difficulty=%u lang=%d",
            diff,
            language);

        bool ok = toos_type_sentence_visit_by_difficulty(
            db,
            language,
            diff,
            pool_collect_callback,
            &collector);

        LOG_INFO(logger,
            "[ToosType Spawn] difficulty=%u result=%d "
            "seen_before=%llu seen_after=%llu "
            "pool_before=%u pool_after=%u",
            diff,
            ok ? 1 : 0,
            (unsigned long long)seen_before,
            (unsigned long long)collector.seen_count,
            pool_before,
            collector.pool_count);

        if (!ok) {
            LOG_ERROR(logger,
                "[ToosType Spawn] Repository visit failed at difficulty=%u",
                diff);

            free(temp_pool);
            return false;
        }
    }

    if (collector.pool_count == 0) {
        free(temp_pool);
        return false;
    }

    uint32_t *temp_seq = (uint32_t *)malloc(collector.pool_count * sizeof(uint32_t));
    if (!temp_seq) {
        free(temp_pool);
        return false;
    }

    // --- Commit Phase ---
    engine->rng_state = collector.rng_state;
    engine->base_pool = temp_pool;
    engine->base_count = collector.pool_count;

    engine->sequence = temp_seq;
    engine->sequence_capacity = collector.pool_count;
    engine->sequence_count = 0;

    if (!ToosTypeSpawnEngine_ensure_cursor(engine, 0)) {
        ToosTypeSpawnEngine_deinit(engine);
        return false;
    }

    engine->loaded = true;
    return true;
}

bool ToosTypeSpawnEngine_ensure_cursor(ToosTypeSpawnEngine *engine, size_t cursor) {
    if (!engine || engine->base_count == 0) return false;

    if (cursor < engine->sequence_count) {
        return true;
    }

    size_t needed_epochs = (cursor / engine->base_count) + 1;
    size_t needed_capacity = needed_epochs * engine->base_count;

    if (needed_capacity > engine->sequence_capacity) {
        uint32_t *new_seq = (uint32_t *)realloc(engine->sequence, needed_capacity * sizeof(uint32_t));
        if (!new_seq) return false;

        engine->sequence = new_seq;
        engine->sequence_capacity = needed_capacity;
    }

    while (engine->sequence_count < needed_capacity) {
        size_t start_idx = engine->sequence_count;

        for (uint32_t i = 0; i < engine->base_count; i++) {
            engine->sequence[start_idx + i] = i;
        }

        // 블록 내부 셔플 (32-bit PRNG로 충분)
        for (uint32_t i = engine->base_count - 1; i > 0; i--) {
            uint32_t j = bounded_rand(&engine->rng_state, i + 1);

            uint32_t temp = engine->sequence[start_idx + i];
            engine->sequence[start_idx + i] = engine->sequence[start_idx + j];
            engine->sequence[start_idx + j] = temp;
        }

        engine->sequence_count += engine->base_count;
    }

    return true;
}

const ToosTypeSentence* ToosTypeSpawnEngine_sentence_at(const ToosTypeSpawnEngine *engine, size_t cursor) {
    if (!engine || !engine->loaded || engine->base_count == 0) return NULL;
    if (cursor >= engine->sequence_count) return NULL;

    uint32_t mapped_index = engine->sequence[cursor];
    return &engine->base_pool[mapped_index];
}