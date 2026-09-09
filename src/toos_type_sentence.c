#include "toos_type_sentence.h"
#include <string.h>
// stdio.h 제거 완료!

// --------------------------------------------------------
// Static Internal Helpers
// --------------------------------------------------------

static bool valid_sqlite_id(uint64_t id) {
    return id >= 1 && id <= (uint64_t)INT64_MAX;
}

static bool column_positive_u64(sqlite3_stmt *stmt, int column, uint64_t *out) {
    sqlite3_int64 value;
    if (!stmt || !out) {
        return false;
    }
    value = sqlite3_column_int64(stmt, column);
    if (value <= 0) {
        return false;
    }
    *out = (uint64_t)value;
    return true;
}

static bool column_difficulty(sqlite3_stmt *stmt, int column, uint8_t *out) {
    int value;
    if (!stmt || !out) {
        return false;
    }
    value = sqlite3_column_int(stmt, column);
    if (value < 1 || value > 3) {
        return false;
    }
    *out = (uint8_t)value;
    return true;
}

static bool copy_column_text(sqlite3_stmt *stmt, int column, char *dst, size_t dst_size) {
    const unsigned char *src;
    int bytes;

    if (!stmt || !dst || dst_size == 0) {
        return false;
    }

    if (sqlite3_column_type(stmt, column) == SQLITE_NULL) {
        dst[0] = '\0';
        return false;
    }

    src = sqlite3_column_text(stmt, column);
    if (!src) {
        dst[0] = '\0';
        return false;
    }

    bytes = sqlite3_column_bytes(stmt, column);
    if (bytes < 0 || (size_t)bytes >= dst_size) {
        dst[0] = '\0';
        return false;
    }

    if (memchr(src, '\0', (size_t)bytes) != NULL) {
        dst[0] = '\0';
        return false;
    }

    memcpy(dst, src, (size_t)bytes);
    dst[bytes] = '\0';

    return true;
}

// --------------------------------------------------------
// Repository API Implementation
// --------------------------------------------------------

bool toos_type_sentence_get_categories(sqlite3 *db, ToosTypeCategoryVisitor callback, void *user_data) {
    if (!db || !callback) return false;

    const char *sql =
        "SELECT c.id, c.code, c.name_ko, c.name_en, c.sort_order, COUNT(s.id) "
        "FROM toos_type_categories c "
        "LEFT JOIN toos_type_sentences s ON s.category_id = c.id AND s.enabled = 1 "
        "WHERE c.enabled = 1 "
        "GROUP BY c.id "
        "ORDER BY c.sort_order, c.id;";

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return false;
    }

    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        ToosTypeCategory cat;
        sqlite3_int64 sort_order;
        sqlite3_int64 sentence_count;

        memset(&cat, 0, sizeof(cat));

        if (!column_positive_u64(stmt, 0, &cat.id) ||
            !copy_column_text(stmt, 1, cat.code, sizeof(cat.code)) ||
            !copy_column_text(stmt, 2, cat.name_ko, sizeof(cat.name_ko)) ||
            !copy_column_text(stmt, 3, cat.name_en, sizeof(cat.name_en))) {
            sqlite3_finalize(stmt);
            return false;
        }

        sort_order = sqlite3_column_int64(stmt, 4);
        sentence_count = sqlite3_column_int64(stmt, 5);

        if (sort_order < 0 || sort_order > UINT32_MAX || sentence_count < 0) {
            sqlite3_finalize(stmt);
            return false;
        }

        cat.sort_order = (uint32_t)sort_order;
        cat.sentence_count = (uint64_t)sentence_count;

        if (!callback(&cat, user_data)) {
            sqlite3_finalize(stmt);
            return false;
        }
    }

    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}

ToosTypeRepoResult toos_type_sentence_get_by_id(sqlite3 *db, uint64_t sentence_id, ToosTypeSentence *out_sentence) {
    if (!db || !out_sentence || !valid_sqlite_id(sentence_id)) {
        return TOOS_TYPE_REPO_ERROR;
    }

    memset(out_sentence, 0, sizeof(*out_sentence));

    const char *sql =
        "SELECT id, category_id, difficulty_ko, difficulty_en, ko_text, en_text "
        "FROM toos_type_sentences "
        "WHERE id = ? AND enabled = 1 LIMIT 1;";

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return TOOS_TYPE_REPO_ERROR;
    }

    if (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)sentence_id) != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return TOOS_TYPE_REPO_ERROR;
    }

    int rc = sqlite3_step(stmt);
    ToosTypeRepoResult result = TOOS_TYPE_REPO_ERROR;

    if (rc == SQLITE_ROW) {
        if (!column_positive_u64(stmt, 0, &out_sentence->id) ||
            !column_positive_u64(stmt, 1, &out_sentence->category_id) ||
            !column_difficulty(stmt, 2, &out_sentence->difficulty_ko) ||
            !column_difficulty(stmt, 3, &out_sentence->difficulty_en) ||
            !copy_column_text(stmt, 4, out_sentence->ko_text, sizeof(out_sentence->ko_text)) ||
            !copy_column_text(stmt, 5, out_sentence->en_text, sizeof(out_sentence->en_text))) {
            result = TOOS_TYPE_REPO_ERROR;
        } else {
            result = TOOS_TYPE_REPO_OK;
        }
    } else if (rc == SQLITE_DONE) {
        result = TOOS_TYPE_REPO_NOT_FOUND;
    }

    sqlite3_finalize(stmt);
    return result;
}

bool toos_type_sentence_visit_by_category(sqlite3 *db, uint64_t category_id, ToosTypeSentenceVisitor callback, void *user_data) {
    if (!db || !callback || !valid_sqlite_id(category_id)) {
        return false;
    }

    const char *sql =
        "SELECT id, category_id, difficulty_ko, difficulty_en, ko_text, en_text "
        "FROM toos_type_sentences "
        "WHERE category_id = ? AND enabled = 1 "
        "ORDER BY id;";

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return false;
    }

    if (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)category_id) != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return false;
    }

    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        ToosTypeSentence s;
        memset(&s, 0, sizeof(s));

        if (!column_positive_u64(stmt, 0, &s.id) ||
            !column_positive_u64(stmt, 1, &s.category_id) ||
            !column_difficulty(stmt, 2, &s.difficulty_ko) ||
            !column_difficulty(stmt, 3, &s.difficulty_en) ||
            !copy_column_text(stmt, 4, s.ko_text, sizeof(s.ko_text)) ||
            !copy_column_text(stmt, 5, s.en_text, sizeof(s.en_text))) {
            sqlite3_finalize(stmt);
            return false;
        }

        if (!callback(&s, user_data)) {
            sqlite3_finalize(stmt);
            return false;
        }
    }

    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}

bool toos_type_sentence_visit_by_difficulty(sqlite3 *db, ToosTypeLanguage language, uint8_t difficulty, ToosTypeSentenceVisitor callback, void *user_data) {
    if (!db || !callback) return false;

    if (language != TOOS_TYPE_LANGUAGE_KO && language != TOOS_TYPE_LANGUAGE_EN) {
        return false;
    }
    if (difficulty < 1 || difficulty > 3) {
        return false;
    }

    const char *sql_ko =
        "SELECT id, category_id, difficulty_ko, difficulty_en, ko_text, en_text "
        "FROM toos_type_sentences "
        "WHERE difficulty_ko = ? AND enabled = 1 ORDER BY id;";

    const char *sql_en =
        "SELECT id, category_id, difficulty_ko, difficulty_en, ko_text, en_text "
        "FROM toos_type_sentences "
        "WHERE difficulty_en = ? AND enabled = 1 ORDER BY id;";

    const char *sql = (language == TOOS_TYPE_LANGUAGE_KO) ? sql_ko : sql_en;

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return false;
    }

    if (sqlite3_bind_int(stmt, 1, difficulty) != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return false;
    }

    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        ToosTypeSentence s;
        memset(&s, 0, sizeof(s));

        if (!column_positive_u64(stmt, 0, &s.id) ||
            !column_positive_u64(stmt, 1, &s.category_id) ||
            !column_difficulty(stmt, 2, &s.difficulty_ko) ||
            !column_difficulty(stmt, 3, &s.difficulty_en) ||
            !copy_column_text(stmt, 4, s.ko_text, sizeof(s.ko_text)) ||
            !copy_column_text(stmt, 5, s.en_text, sizeof(s.en_text))) {
            sqlite3_finalize(stmt);
            return false;
        }

        if (!callback(&s, user_data)) {
            sqlite3_finalize(stmt);
            return false;
        }
    }

    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}