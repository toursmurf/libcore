#pragma once
#include "toos_type_sentence.h"

#define TOOS_TYPE_MAX_POOL_SIZE 1000

typedef struct {
    ToosTypeSentence sentences[TOOS_TYPE_MAX_POOL_SIZE];
    uint32_t count;
} ToosTypeSentencePool;

typedef struct {
    uint64_t rng_state;
    bool loaded;

    ToosTypeSentence *base_pool;
    uint32_t base_count;

    //Append-only 셔플 저장소: 이전 에포크 데이터가 보존되어야 함.
    uint32_t *sequence;
    size_t sequence_count;
    size_t sequence_capacity;
} ToosTypeSpawnEngine;

void ToosTypeSpawnEngine_init(ToosTypeSpawnEngine *engine, uint64_t seed);
void ToosTypeSpawnEngine_deinit(ToosTypeSpawnEngine *engine);

//Ready 완료 시 1회만 수행. (기존 toos_type_sentence_visit_by_difficulty 활용)
bool ToosTypeSpawnEngine_build_deck(ToosTypeSpawnEngine *engine, sqlite3 *db, ToosTypeLanguage language);

bool ToosTypeSpawnEngine_ensure_cursor(ToosTypeSpawnEngine *engine, size_t cursor);
const ToosTypeSentence* ToosTypeSpawnEngine_sentence_at(const ToosTypeSpawnEngine *engine, size_t cursor);