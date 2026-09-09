#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <sqlite3.h>

/* UTF-8 text buffer: maximum payload 511 bytes + NUL */
#define TOOS_TYPE_TEXT_MAX_BYTES 512

// --------------------------------------------------------
// Enums & Structs
// --------------------------------------------------------
typedef enum {
    TOOS_TYPE_REPO_OK = 0,
    TOOS_TYPE_REPO_NOT_FOUND,
    TOOS_TYPE_REPO_ERROR
} ToosTypeRepoResult;

typedef enum {
    TOOS_TYPE_LANGUAGE_KO = 0,
    TOOS_TYPE_LANGUAGE_EN
} ToosTypeLanguage;

typedef struct {
    uint64_t id;
    char code[32];
    char name_ko[64];
    char name_en[64];
    uint32_t sort_order;
    uint64_t sentence_count;
} ToosTypeCategory;

typedef struct {
    uint64_t id;
    uint64_t category_id;
    uint8_t difficulty_ko;
    uint8_t difficulty_en;
    char ko_text[TOOS_TYPE_TEXT_MAX_BYTES];
    char en_text[TOOS_TYPE_TEXT_MAX_BYTES];
} ToosTypeSentence;


typedef bool (*ToosTypeCategoryVisitor)(const ToosTypeCategory *category, void *user_data);
typedef bool (*ToosTypeSentenceVisitor)(const ToosTypeSentence *sentence, void *user_data);
bool toos_type_sentence_get_categories(sqlite3 *db, ToosTypeCategoryVisitor callback, void *user_data);
ToosTypeRepoResult toos_type_sentence_get_by_id(sqlite3 *db, uint64_t sentence_id, ToosTypeSentence *out_sentence);
bool toos_type_sentence_visit_by_category(sqlite3 *db, uint64_t category_id, ToosTypeSentenceVisitor callback, void *user_data);
bool toos_type_sentence_visit_by_difficulty(sqlite3 *db, ToosTypeLanguage language, uint8_t difficulty, ToosTypeSentenceVisitor callback, void *user_data);