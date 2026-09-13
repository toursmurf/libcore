#pragma once
#include "toos_type_sentence.h"

typedef enum {
    TOOS_TYPE_JUDGE_CORRECT = 0,
    TOOS_TYPE_JUDGE_INCORRECT,
    TOOS_TYPE_JUDGE_EXPIRED,
    TOOS_TYPE_JUDGE_STALE,
    TOOS_TYPE_JUDGE_ERROR
} ToosTypeJudgeResult;

ToosTypeJudgeResult ToosTypeJudge_submit(
    const ToosTypeSentence *target_sentence,
    uint64_t expected_sentence_id,
    uint64_t submitted_sentence_id,
    const char *typed_text,
    size_t typed_len,
    uint64_t now_ms,
    uint64_t sentence_deadline_ms,
    ToosTypeLanguage lang
);

uint32_t ToosTypeJudge_count_chars(
    const char *text,
    size_t len,
    ToosTypeLanguage lang
);