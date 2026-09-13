#include "toos_type_judge.h"
#include <string.h>

/*
 * ToosType Judge
 *
 * Stateless judge.
 *
 * Player progression / counters / cursor / retry policy are owned by
 * ToosTypeGameContext.
 *
 * Judge responsibilities:
 *   - server-side target consistency validation
 *   - stale packet isolation
 *   - absolute deadline validation
 *   - exact text comparison
 *   - UTF-8 character counting for WPM statistics
 */

static bool bounded_text_len(
    const char *text,
    size_t capacity,
    size_t *out_len)
{
    if (!text || !out_len || capacity == 0) {
        return false;
    }

    for (size_t i = 0; i < capacity; i++) {
        if (text[i] == '\0') {
            *out_len = i;
            return true;
        }
    }

    /* Repository contract violation: no NUL terminator in fixed buffer. */
    return false;
}

ToosTypeJudgeResult ToosTypeJudge_submit(
    const ToosTypeSentence *target_sentence,
    uint64_t expected_sentence_id,
    uint64_t submitted_sentence_id,
    const char *typed_text,
    size_t typed_len,
    uint64_t now_ms,
    uint64_t sentence_deadline_ms,
    ToosTypeLanguage lang)
{
    /*
     * Internal arguments represent server authority.
     * Invalid authority state is an internal ERROR, not a client mistake.
     */
    if (!target_sentence ||
        !typed_text ||
        expected_sentence_id == 0 ||
        sentence_deadline_ms == 0) {
        return TOOS_TYPE_JUDGE_ERROR;
    }

    if (lang != TOOS_TYPE_LANG_EN &&
        lang != TOOS_TYPE_LANG_KO) {
        return TOOS_TYPE_JUDGE_ERROR;
    }

    /*
     * Server internal consistency guard.
     *
     * GameContext says sentence X is active, but the supplied target object
     * says sentence Y. This must never be treated as STALE because the
     * server itself is inconsistent.
     */
    if (target_sentence->id != expected_sentence_id) {
        return TOOS_TYPE_JUDGE_ERROR;
    }

    /*
     * STALE isolation must happen BEFORE deadline processing.
     *
     * A late packet for an old sentence must never expire or otherwise
     * mutate the currently active sentence.
     */
    if (submitted_sentence_id != expected_sentence_id) {
        return TOOS_TYPE_JUDGE_STALE;
    }

    /*
     * Absolute monotonic deadline.
     *
     * At or beyond deadline the sentence is expired.
     */
    if (now_ms >= sentence_deadline_ms) {
        return TOOS_TYPE_JUDGE_EXPIRED;
    }

    const char *expected_text =
        (lang == TOOS_TYPE_LANG_EN)
            ? target_sentence->en_text
            : target_sentence->ko_text;

    size_t expected_len = 0;

    if (!bounded_text_len(
            expected_text,
            TOOS_TYPE_TEXT_MAX_BYTES,
            &expected_len)) {
        return TOOS_TYPE_JUDGE_ERROR;
    }

    /*
     * Exact match.
     *
     * No trimming.
     * No case folding.
     * No Unicode normalization.
     * No sscanf/strlen dependency on client supplied typed_text.
     *
     * typed_len is authoritative for the submitted byte sequence.
     */
    if (typed_len != expected_len) {
        return TOOS_TYPE_JUDGE_INCORRECT;
    }

    if (expected_len == 0) {
        return TOOS_TYPE_JUDGE_CORRECT;
    }

    if (memcmp(typed_text, expected_text, expected_len) == 0) {
        return TOOS_TYPE_JUDGE_CORRECT;
    }

    return TOOS_TYPE_JUDGE_INCORRECT;
}

uint32_t ToosTypeJudge_count_chars(
    const char *text,
    size_t len,
    ToosTypeLanguage lang)
{
    if (!text || len == 0) {
        return 0;
    }

    if (lang != TOOS_TYPE_LANG_EN &&
        lang != TOOS_TYPE_LANG_KO) {
        return 0;
    }

    /*
     * Count UTF-8 code points by counting bytes which are not continuation
     * bytes.
     *
     * This function is called for CORRECT input, therefore text is expected
     * to be the same validated UTF-8 data stored in the Sentence repository.
     *
     * ASCII/English:
     *     "Hello" -> 5
     *
     * UTF-8/Korean:
     *     "안녕" -> 2
     */
    uint32_t count = 0;

    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)text[i];

        if ((c & 0xC0U) != 0x80U) {
            if (count == UINT32_MAX) {
                return UINT32_MAX;
            }
            count++;
        }
    }

    return count;
}