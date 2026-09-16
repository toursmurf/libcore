#pragma once

#include "toos_type_types.h"
#include "toos_type_sentence.h"

#define TOOS_TYPE_MAX_POOL_SIZE 1000

#define TOOS_TYPE_DIFFICULTY_MIN   1
#define TOOS_TYPE_DIFFICULTY_MAX   3
#define TOOS_TYPE_DIFFICULTY_COUNT 3

#if TOOS_TYPE_MAX_PHASES != TOOS_TYPE_DIFFICULTY_COUNT
#error "ToosType phase count must match difficulty count"
#endif

typedef struct {
    ToosTypeSentence sentences[TOOS_TYPE_MAX_POOL_SIZE];
    uint32_t count;
} ToosTypeSentencePool;

typedef struct {
    uint64_t rng_state;
    uint64_t sequence_rng_state[TOOS_TYPE_DIFFICULTY_COUNT];
    bool loaded;
    ToosTypeSentence *base_pool;
    uint32_t base_count;
    uint32_t difficulty_start[TOOS_TYPE_DIFFICULTY_COUNT];
    uint32_t difficulty_count[TOOS_TYPE_DIFFICULTY_COUNT];

    uint32_t *sequence[TOOS_TYPE_DIFFICULTY_COUNT];
    size_t sequence_count[TOOS_TYPE_DIFFICULTY_COUNT];
    size_t sequence_capacity[TOOS_TYPE_DIFFICULTY_COUNT];

} ToosTypeSpawnEngine;
void ToosTypeSpawnEngine_init(ToosTypeSpawnEngine *engine, uint64_t seed);
void ToosTypeSpawnEngine_deinit(ToosTypeSpawnEngine *engine);
bool ToosTypeSpawnEngine_build_deck(ToosTypeSpawnEngine *engine, sqlite3 *db, ToosTypeLanguage language);

/*
 * difficulty별 공통 sequence가 cursor 위치까지 존재하도록 보장한다.
 * difficulty:
 *   1 = Easy
 *   2 = Medium
 *   3 = Hard
 */
bool ToosTypeSpawnEngine_ensure_cursor(ToosTypeSpawnEngine *engine, uint32_t difficulty, size_t cursor);
/*
 * difficulty별 공통 deck에서 cursor 위치의 문장을 반환한다.
 * 반환 포인터는 SpawnEngine 소유(BORROWED).
 */
const ToosTypeSentence*  ToosTypeSpawnEngine_sentence_at(const ToosTypeSpawnEngine *engine, uint32_t difficulty, size_t cursor);