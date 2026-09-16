#include "toos_type_spawn.h"

#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include "logger.h"

/* --------------------------------------------------------
 * Static Helpers
 * -------------------------------------------------------- */

static bool difficulty_to_index(uint32_t difficulty, uint32_t *out_index) {
    if (!out_index) return false;

    if (difficulty < TOOS_TYPE_DIFFICULTY_MIN ||
        difficulty > TOOS_TYPE_DIFFICULTY_MAX) {
        return false;
    }

    *out_index = difficulty - TOOS_TYPE_DIFFICULTY_MIN;
    return true;
}

/* --------------------------------------------------------
 * Deterministic Local RNG
 * -------------------------------------------------------- */

static uint32_t local_rand_next(uint64_t *state) {
    if (!state) return 0;

    *state = (*state * 6364136223846793005ULL) + 1442695040888963407ULL;

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

static uint64_t local_rand_next64(uint64_t *state)  {
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

static uint64_t derive_sequence_seed(uint64_t seed, uint32_t diff_index) {
    uint64_t z = seed + 0x9E3779B97F4A7C15ULL * (uint64_t)(diff_index + 1);

    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    z ^= z >> 31;

    if (z == 0) {
        z = 0x9E3779B97F4A7C15ULL;
    }

    return z;
}

/* --------------------------------------------------------
 * Reservoir Collector
 * -------------------------------------------------------- */

typedef struct {
    ToosTypeSentence *pool;
    uint8_t *difficulty;

    uint32_t pool_capacity;
    uint32_t pool_count;

    uint64_t seen_count;
    uint64_t rng_state;

    uint8_t current_difficulty;

} ReservoirCollector;

static bool pool_collect_callback(const ToosTypeSentence *sentence, void *user_data)  {
    if (!sentence || !user_data) return false;

    ReservoirCollector *collector = (ReservoirCollector *)user_data;

    if (collector->seen_count == UINT64_MAX) {
        return false;
    }

    collector->seen_count++;

    size_t target_index = 0;

    if (collector->pool_count < collector->pool_capacity) {
        target_index = collector->pool_count;
        collector->pool_count++;
    } else {
        uint64_t j = bounded_rand64(&collector->rng_state, collector->seen_count);

        if (j >= (uint64_t)collector->pool_capacity) {
            return true;
        }

        target_index = (size_t)j;
    }

    collector->pool[target_index] = *sentence;
    collector->difficulty[target_index] = collector->current_difficulty;

    return true;
}

/* --------------------------------------------------------
 * Sequence Helpers
 * -------------------------------------------------------- */

static bool append_epoch(ToosTypeSpawnEngine *engine, uint32_t diff_index)  {
    if (!engine) return false;

    if (diff_index >= TOOS_TYPE_DIFFICULTY_COUNT) {
        return false;
    }

    uint32_t count = engine->difficulty_count[diff_index];

    if (count == 0) {
        return false;
    }

    if (!engine->sequence[diff_index]) {
        return false;
    }

    size_t start = engine->sequence_count[diff_index];

    if (start > SIZE_MAX - (size_t)count) {
        return false;
    }

    size_t end = start + (size_t)count;

    if (end > engine->sequence_capacity[diff_index]) {
        return false;
    }

    /*
     * sequence에는 base_pool의 절대 index가 아니라
     * 해당 difficulty range 안에서의 상대 index를 저장한다.
     */
    for (uint32_t i = 0; i < count; i++) {
        engine->sequence[diff_index][start + i] = i;
    }

    /*
     * Fisher-Yates Shuffle
     */
    for (uint32_t i = count - 1; i > 0; i--) {
        uint32_t j = bounded_rand(&engine->sequence_rng_state[diff_index], i + 1);

        uint32_t temp = engine->sequence[diff_index][start + i];
        engine->sequence[diff_index][start + i] = engine->sequence[diff_index][start + j];
        engine->sequence[diff_index][start + j] = temp;
    }

    engine->sequence_count[diff_index] = end;

    return true;
}

/* --------------------------------------------------------
 * Public API
 * -------------------------------------------------------- */

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

    for (uint32_t i = 0; i < TOOS_TYPE_DIFFICULTY_COUNT; i++) {
        if (engine->sequence[i]) {
            free(engine->sequence[i]);
            engine->sequence[i] = NULL;
        }
    }

    memset(engine, 0, sizeof(ToosTypeSpawnEngine));
}

bool ToosTypeSpawnEngine_build_deck(ToosTypeSpawnEngine *engine, sqlite3 *db, ToosTypeLanguage language) {
    if (!engine || !db) return false;

    if (engine->loaded) {
        return false;
    }

    if (engine->base_pool != NULL) {
        return false;
    }

    for (uint32_t i = 0; i < TOOS_TYPE_DIFFICULTY_COUNT; i++) {
        if (engine->sequence[i] != NULL) {
            return false;
        }
    }

    if (language != TOOS_TYPE_LANG_EN && language != TOOS_TYPE_LANG_KO) {
        return false;
    }

    ToosTypeSentence *sample_pool = (ToosTypeSentence *)calloc(TOOS_TYPE_MAX_POOL_SIZE, sizeof(ToosTypeSentence));

    if (!sample_pool) {
        return false;
    }

    uint8_t *sample_difficulty = (uint8_t *)calloc(TOOS_TYPE_MAX_POOL_SIZE, sizeof(uint8_t));

    if (!sample_difficulty) {
        free(sample_pool);
        return false;
    }

    uint64_t base_seed = engine->rng_state;

    ReservoirCollector collector;
    memset(&collector, 0, sizeof(collector));

    collector.pool = sample_pool;
    collector.difficulty = sample_difficulty;
    collector.pool_capacity = TOOS_TYPE_MAX_POOL_SIZE;
    collector.rng_state = base_seed;

    for (uint8_t diff = TOOS_TYPE_DIFFICULTY_MIN; diff <= TOOS_TYPE_DIFFICULTY_MAX; diff++) {

        uint64_t seen_before = collector.seen_count;
        uint32_t pool_before = collector.pool_count;

        collector.current_difficulty = diff;

        LOG_INFO(logger, "[ToosType Spawn] loading difficulty=%u lang=%d", diff, language);

        bool ok = toos_type_sentence_visit_by_difficulty(db, language, diff, pool_collect_callback, &collector);

        LOG_INFO(logger, "[ToosType Spawn] difficulty=%u result=%d seen_before=%llu seen_after=%llu pool_before=%u pool_after=%u",
                 diff, ok ? 1 : 0, (unsigned long long)seen_before, (unsigned long long)collector.seen_count, pool_before, collector.pool_count);

        if (!ok) {
            LOG_ERROR(logger, "[ToosType Spawn] Repository visit failed at difficulty=%u", diff);
            free(sample_difficulty);
            free(sample_pool);
            return false;
        }
    }

    if (collector.pool_count == 0) {
        free(sample_difficulty);
        free(sample_pool);
        return false;
    }

    uint32_t difficulty_count[TOOS_TYPE_DIFFICULTY_COUNT];
    memset(difficulty_count, 0, sizeof(difficulty_count));

    for (uint32_t i = 0; i < collector.pool_count; i++) {
        uint32_t diff_index = 0;
        if (!difficulty_to_index(sample_difficulty[i], &diff_index)) {
            free(sample_difficulty);
            free(sample_pool);
            return false;
        }
        difficulty_count[diff_index]++;
    }

    for (uint32_t i = 0; i < TOOS_TYPE_DIFFICULTY_COUNT; i++) {
        if (difficulty_count[i] == 0) {
            LOG_ERROR(logger, "[ToosType Spawn] no sentence for difficulty=%u", i + 1);
            free(sample_difficulty);
            free(sample_pool);
            return false;
        }
    }

    ToosTypeSentence *grouped_pool = (ToosTypeSentence *)calloc(collector.pool_count, sizeof(ToosTypeSentence));

    if (!grouped_pool) {
        free(sample_difficulty);
        free(sample_pool);
        return false;
    }

    uint32_t difficulty_start[TOOS_TYPE_DIFFICULTY_COUNT];
    difficulty_start[0] = 0;

    for (uint32_t i = 1; i < TOOS_TYPE_DIFFICULTY_COUNT; i++) {
        difficulty_start[i] = difficulty_start[i - 1] + difficulty_count[i - 1];
    }

    uint32_t write_pos[TOOS_TYPE_DIFFICULTY_COUNT];
    memcpy(write_pos, difficulty_start, sizeof(write_pos));

    for (uint32_t i = 0; i < collector.pool_count; i++) {
        uint32_t diff_index = 0;
        if (!difficulty_to_index(sample_difficulty[i], &diff_index)) {
            free(grouped_pool);
            free(sample_difficulty);
            free(sample_pool);
            return false;
        }

        uint32_t pos = write_pos[diff_index]++;
        grouped_pool[pos] = sample_pool[i];
    }

    free(sample_difficulty);
    sample_difficulty = NULL;

    free(sample_pool);
    sample_pool = NULL;

    ToosTypeSpawnEngine temp;
    memset(&temp, 0, sizeof(temp));

    temp.rng_state = collector.rng_state;
    temp.base_pool = grouped_pool;
    temp.base_count = collector.pool_count;

    for (uint32_t i = 0; i < TOOS_TYPE_DIFFICULTY_COUNT; i++) {
        temp.sequence_rng_state[i] = derive_sequence_seed(base_seed, i);

        temp.difficulty_start[i] = difficulty_start[i];
        temp.difficulty_count[i] = difficulty_count[i];

        size_t count = (size_t)difficulty_count[i];

        if (count > SIZE_MAX / sizeof(uint32_t)) {
            ToosTypeSpawnEngine_deinit(&temp);
            return false;
        }

        temp.sequence[i] = (uint32_t *)malloc(count * sizeof(uint32_t));

        if (!temp.sequence[i]) {
            ToosTypeSpawnEngine_deinit(&temp);
            return false;
        }

        temp.sequence_capacity[i] = count;
        temp.sequence_count[i] = 0;

        if (!append_epoch(&temp, i)) {
            ToosTypeSpawnEngine_deinit(&temp);
            return false;
        }
    }

    temp.loaded = true;
    *engine = temp;

    LOG_INFO(logger, "[ToosType Spawn] deck ready total=%u easy=%u medium=%u hard=%u",
             engine->base_count,
             engine->difficulty_count[0],
             engine->difficulty_count[1],
             engine->difficulty_count[2]);

    return true;
}

bool ToosTypeSpawnEngine_ensure_cursor(ToosTypeSpawnEngine *engine, uint32_t difficulty, size_t cursor) {
    if (!engine || !engine->loaded) {
        return false;
    }

    uint32_t diff_index = 0;

    if (!difficulty_to_index(difficulty, &diff_index)) {
        return false;
    }

    uint32_t difficulty_count = engine->difficulty_count[diff_index];

    if (difficulty_count == 0) {
        return false;
    }

    if (!engine->sequence[diff_index]) {
        return false;
    }

    if (cursor < engine->sequence_count[diff_index]) {
        return true;
    }

    size_t count = (size_t)difficulty_count;
    size_t epoch_index = cursor / count;

    if (epoch_index == SIZE_MAX) {
        return false;
    }

    size_t needed_epochs = epoch_index + 1;

    if (needed_epochs > SIZE_MAX / count) {
        return false;
    }

    size_t needed_capacity = needed_epochs * count;

    if (needed_capacity > SIZE_MAX / sizeof(uint32_t)) {
        return false;
    }

    if (needed_capacity > engine->sequence_capacity[diff_index]) {
        uint32_t *new_sequence = (uint32_t *)realloc(engine->sequence[diff_index], needed_capacity * sizeof(uint32_t));

        if (!new_sequence) {
            return false;
        }

        engine->sequence[diff_index] = new_sequence;
        engine->sequence_capacity[diff_index] = needed_capacity;
    }

    while (engine->sequence_count[diff_index] < needed_capacity) {
        if (!append_epoch(engine, diff_index)) {
            return false;
        }
    }

    return true;
}

const ToosTypeSentence* ToosTypeSpawnEngine_sentence_at(const ToosTypeSpawnEngine *engine, uint32_t difficulty, size_t cursor) {
    if (!engine || !engine->loaded || !engine->base_pool) {
        return NULL;
    }

    uint32_t diff_index = 0;

    if (!difficulty_to_index(difficulty, &diff_index)) {
        return NULL;
    }

    if (!engine->sequence[diff_index]) {
        return NULL;
    }

    if (cursor >= engine->sequence_count[diff_index]) {
        return NULL;
    }

    uint32_t relative_index = engine->sequence[diff_index][cursor];
    uint32_t count = engine->difficulty_count[diff_index];

    if (relative_index >= count) {
        return NULL;
    }

    uint32_t start = engine->difficulty_start[diff_index];
    uint64_t absolute_index = (uint64_t)start + (uint64_t)relative_index;

    if (absolute_index >= engine->base_count) {
        return NULL;
    }

    return &engine->base_pool[(size_t)absolute_index];
}