#include "regex_obj.h"

static void Regex_finalize(Object* self) {
    Regex* r = (Regex*)self;

    if (r->is_compiled) regfree(&r->compiled);
    if (r->original_pattern) free(r->original_pattern);
}

static void Regex_toString(Object* self, char* buffer, size_t len) {
    Regex* r = (Regex*)self;

    snprintf(buffer, len, "/%s/ (flags:%d)", r->original_pattern ? r->original_pattern : "NULL", r->cflags);
}

static bool Regex_equals(Object* self, Object* other) {
    if (!instanceOf(other, self->type)) return false;

    Regex* r1 = (Regex*)self;
    Regex* r2 = (Regex*)other;

    if (!r1->original_pattern || !r2->original_pattern) return false;

    return (strcmp(r1->original_pattern, r2->original_pattern) == 0) && (r1->cflags == r2->cflags);
}

static int Regex_hashCode(Object* self) {
    Regex* r = (Regex*)self;

    if (!r->original_pattern) return 0;

    int hash = 5381;
    char* str = r->original_pattern;
    int c;

    while ((c = *str++)) hash = ((hash << 5) + hash) + c;

    return (int)(hash ^ r->cflags);
}

static Class _RegexClass = {
    .name = "Regex",
    .size = sizeof(Regex),
    .toString = Regex_toString,
    .equals = Regex_equals,
    .hashCode = Regex_hashCode,
    .finalize = Regex_finalize
};

static bool reg_matches(Regex* self, const char* str) {
    if (!self->is_compiled || !str) return false;

    return regexec(&self->compiled, str, 0, NULL, 0) == 0;
}

static int reg_search(Regex* self, const char* str) {
    if (!self->is_compiled || !str) return -1;

    regmatch_t pmatch[1];

    if (regexec(&self->compiled, str, 1, pmatch, 0) == 0) return pmatch[0].rm_so;

    return -1;
}

static ArrayList* reg_findAll(Regex* self, const char* text) {
    ArrayList* list = new_ArrayList(10);

    if (!list) return NULL;
    if (!self->is_compiled || !text) return list;

    regmatch_t pmatch[1];
    const char* p = text;

    while (regexec(&self->compiled, p, 1, pmatch, 0) == 0) {
        int len = pmatch[0].rm_eo - pmatch[0].rm_so;

        /* [OPTIMIZATION] new_StringN 호출로 strndup 낭비 원천 차단 */
        String* s_obj = new_StringN(p + pmatch[0].rm_so, len);

        if (s_obj) {
            list->add(list, (Object*)s_obj);
            RELEASE(s_obj);
        }

        p += pmatch[0].rm_eo;

        if (pmatch[0].rm_so == pmatch[0].rm_eo) {
            if (*p == '\0') break;
            p++;
        }
    }

    return list;
}

static int reg_matchCount(Regex* self, const char* text) {
    if (!self->is_compiled || !text) return 0;

    int count = 0;
    regmatch_t pmatch[1];
    const char* p = text;

    while (regexec(&self->compiled, p, 1, pmatch, 0) == 0) {
        count++;
        p += pmatch[0].rm_eo;

        if (pmatch[0].rm_so == pmatch[0].rm_eo) {
            if (*p == '\0') break;
            p++;
        }
    }

    return count;
}

static const char* reg_getPattern(Regex* self) {
    return self->original_pattern;
}

static int reg_getFlags(Regex* self) {
    return self->cflags;
}

Regex* new_Regex(const char* pattern, int flags, int* err) {
    if (!pattern) return NULL;

    Regex* r = (Regex*)calloc(1, sizeof(Regex));

    if (!r) return NULL;

    Object_Init((Object*)r, &_RegexClass);

    r->original_pattern = strdup(pattern);

    r->cflags = flags;
    r->is_compiled = false;

    if (!r->original_pattern) {
        RELEASE(r);
        if (err) *err = -1;
        return NULL;
    }

    int err_code = regcomp(&r->compiled, pattern, flags);

    if (err) *err = err_code;

    if (err_code != 0) {
        RELEASE(r);
        return NULL;
    }

    r->is_compiled = true;
    r->matches = reg_matches;
    r->search = reg_search;
    r->findAll = reg_findAll;
    r->matchCount = reg_matchCount;
    r->getPattern = reg_getPattern;
    r->getFlags = reg_getFlags;

    return r;
}