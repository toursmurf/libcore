#include "toos_type_sentence.h"

#include <string.h>
#include <stdint.h>

/*
 * ToosType Sentence Repository
 *
 * Responsibilities:
 *   - SQLite row retrieval only
 *   - strict row validation
 *   - deterministic ORDER BY id
 *   - BORROWED visitor callback
 *
 * Game policy, deck policy, randomization and scoring do not belong here.
 */

/* --------------------------------------------------------
 * Internal validation helpers
 * -------------------------------------------------------- */

static bool column_positive_u64(
    sqlite3_stmt *stmt,
    int column,
    uint64_t *out)
{
    if (!stmt || !out) {
        return false;
    }

    if (sqlite3_column_type(stmt, column) == SQLITE_NULL) {
        return false;
    }

    sqlite3_int64 value = sqlite3_column_int64(stmt, column);

    /*
     * SQLite INTEGER is signed 64-bit.
     * ToosType IDs must be in [1, INT64_MAX].
     */
    if (value <= 0) {
        return false;
    }

    *out = (uint64_t)value;
    return true;
}

static bool column_difficulty(
    sqlite3_stmt *stmt,
    int column,
    uint8_t *out)
{
    if (!stmt || !out) {
        return false;
    }

    if (sqlite3_column_type(stmt, column) == SQLITE_NULL) {
        return false;
    }

    int value = sqlite3_column_int(stmt, column);

    /*
     * Current ToosType sentence dataset uses difficulty 1..3.
     * This is independent from the four gameplay phases.
     */
    if (value < 1 || value > 3) {
        return false;
    }

    *out = (uint8_t)value;
    return true;
}

static bool copy_column_text(
    sqlite3_stmt *stmt,
    int column,
    char *dst,
    size_t dst_size)
{
    if (!stmt || !dst || dst_size == 0) {
        return false;
    }

    dst[0] = '\0';

    if (sqlite3_column_type(stmt, column) == SQLITE_NULL) {
        return false;
    }

    const unsigned char *src =
        sqlite3_column_text(stmt, column);

    if (!src) {
        return false;
    }

    int bytes = sqlite3_column_bytes(stmt, column);

    if (bytes < 0) {
        return false;
    }

    /*
     * Need room for trailing NUL.
     */
    if ((size_t)bytes >= dst_size) {
        return false;
    }

    /*
     * Repository contract:
     * stored TEXT must not contain embedded NUL.
     */
    if (memchr(src, '\0', (size_t)bytes) != NULL) {
        return false;
    }

    if (bytes > 0) {
        memcpy(dst, src, (size_t)bytes);
    }

    dst[bytes] = '\0';

    return true;
}

static bool read_sentence_row(
    sqlite3_stmt *stmt,
    ToosTypeSentence *out)
{
    if (!stmt || !out) {
        return false;
    }

    memset(out, 0, sizeof(*out));

    if (!column_positive_u64(
            stmt,
            0,
            &out->id)) {
        return false;
    }

    if (!column_positive_u64(
            stmt,
            1,
            &out->category_id)) {
        return false;
    }

    if (!column_difficulty(
            stmt,
            2,
            &out->difficulty_ko)) {
        return false;
    }

    if (!column_difficulty(
            stmt,
            3,
            &out->difficulty_en)) {
        return false;
    }

    if (!copy_column_text(
            stmt,
            4,
            out->ko_text,
            sizeof(out->ko_text))) {
        return false;
    }

    if (!copy_column_text(
            stmt,
            5,
            out->en_text,
            sizeof(out->en_text))) {
        return false;
    }

    return true;
}

/* --------------------------------------------------------
 * Public Repository API
 * -------------------------------------------------------- */

bool toos_type_sentence_visit_by_difficulty(
    sqlite3 *db,
    ToosTypeLanguage language,
    uint8_t difficulty,
    ToosTypeSentenceVisitor callback,
    void *user_data)
{
    if (!db || !callback) {
        return false;
    }

    if (language != TOOS_TYPE_LANG_KO &&
        language != TOOS_TYPE_LANG_EN) {
        return false;
    }

    /*
     * Current ToosType dataset difficulty range.
     */
    if (difficulty < 1 || difficulty > 3) {
        return false;
    }

    const char *sql = NULL;

    if (language == TOOS_TYPE_LANG_KO) {
        sql =
            "SELECT "
            "id, "
            "category_id, "
            "difficulty_ko, "
            "difficulty_en, "
            "ko_text, "
            "en_text "
            "FROM toos_type_sentences "
            "WHERE enabled = 1 "
            "AND difficulty_ko = ? "
            "ORDER BY id;";
    } else {
        sql =
            "SELECT "
            "id, "
            "category_id, "
            "difficulty_ko, "
            "difficulty_en, "
            "ko_text, "
            "en_text "
            "FROM toos_type_sentences "
            "WHERE enabled = 1 "
            "AND difficulty_en = ? "
            "ORDER BY id;";
    }

    sqlite3_stmt *stmt = NULL;

    int rc = sqlite3_prepare_v2(
        db,
        sql,
        -1,
        &stmt,
        NULL);

    if (rc != SQLITE_OK || !stmt) {
        if (stmt) {
            sqlite3_finalize(stmt);
        }
        return false;
    }

    rc = sqlite3_bind_int(
        stmt,
        1,
        (int)difficulty);

    if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return false;
    }

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        ToosTypeSentence sentence;

        if (!read_sentence_row(
                stmt,
                &sentence)) {
            sqlite3_finalize(stmt);
            return false;
        }

        /*
         * sentence is valid only during this synchronous callback.
         * Visitor must copy it if it needs longer lifetime.
         */
        if (!callback(
                &sentence,
                user_data)) {
            sqlite3_finalize(stmt);
            return false;
        }
    }

    bool ok = (rc == SQLITE_DONE);

    sqlite3_finalize(stmt);

    return ok;
}