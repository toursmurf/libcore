#pragma once
#include "toos_type_types.h"
#include <sqlite3.h>

#define TOOS_TYPE_TEXT_MAX_BYTES 512

typedef struct {
    uint64_t id;
    uint64_t category_id;
    uint8_t difficulty_ko;
    uint8_t difficulty_en;
    char ko_text[TOOS_TYPE_TEXT_MAX_BYTES];
    char en_text[TOOS_TYPE_TEXT_MAX_BYTES];
} ToosTypeSentence;

typedef bool (*ToosTypeSentenceVisitor)(
    const ToosTypeSentence *sentence,
    void *user_data
);

bool toos_type_sentence_visit_by_difficulty(
    sqlite3 *db,
    ToosTypeLanguage language,
    uint8_t difficulty,
    ToosTypeSentenceVisitor callback,
    void *user_data
);