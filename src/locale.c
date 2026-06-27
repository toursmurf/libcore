// ==========================================
// src/locale.c
// ==========================================
#include "locale.h"

static void Locale_finalize(Object* self) {
    Locale* loc = (Locale*)self;
    free(loc->language); free(loc->country); free(loc->charset);
}

static void Locale_toString(Object* self, char* buffer, size_t len) {
    Locale* loc = (Locale*)self;
    if (loc->country && strlen(loc->country) > 0) {
        snprintf(buffer, len, "%s_%s", loc->language, loc->country);
    } else if (loc->language) {
        snprintf(buffer, len, "%s", loc->language);
    }
}

static bool Locale_equals(Object* self, Object* other) {
    if (!instanceOf(other, self->type)) return false;
    Locale* l1 = (Locale*)self; Locale* l2 = (Locale*)other;
    if (!l1->language || !l2->language || !l1->country || !l2->country) return false;
    return (strcmp(l1->language, l2->language) == 0 && strcmp(l1->country, l2->country) == 0);
}

static int Locale_hashCode(Object* self) {
    Locale* loc = (Locale*)self;
    int hash = 5381; int c;
    if (loc->language) {
        char* str = loc->language;
        while ((c = *str++)) hash = ((hash << 5) + hash) + c;
    }
    if (loc->country) {
        char* str = loc->country;
        while ((c = *str++)) hash = ((hash << 5) + hash) + c;
    }
    return hash;
}

static Class _LocaleClass = {
    .name = "Locale", .size = sizeof(Locale), .toString = Locale_toString,
    .equals = Locale_equals, .hashCode = Locale_hashCode, .finalize = Locale_finalize
};

static const char* loc_getLanguage(Locale* self) { return self->language; }
static const char* loc_getCountry(Locale* self) { return self->country; }
static const char* loc_getCharset(Locale* self) { return self->charset; }

static Locale* loc_getFallback(Locale* self) {
    if (self->country && strlen(self->country) > 0) return new_Locale(self->language, "");
    return get_default_locale();
}

static bool loc_isRTL(Locale* self) {
    if (!self->language) return false;
    const char* lang = self->language;
    if (strcmp(lang, "ar") == 0 || strcmp(lang, "he") == 0 ||
        strcmp(lang, "fa") == 0 || strcmp(lang, "ur") == 0) return true;
    return false;
}

Locale* new_Locale(const char* lang, const char* country) {
    Locale* loc = (Locale*)calloc(1, sizeof(Locale));
    if (!loc) return NULL;

    Object_Init((Object*)loc, &_LocaleClass);

    loc->language = safe_strdup(lang ? lang : "", 16);
    loc->country = safe_strdup(country ? country : "", 16);
    loc->charset = safe_strdup("UTF-8", 16);

    if (!loc->language || !loc->country || !loc->charset) {
        RELEASE(loc); return NULL;
    }

    loc->getLanguage = loc_getLanguage;
    loc->getCountry = loc_getCountry;
    loc->getCharset = loc_getCharset;
    loc->getFallback = loc_getFallback;
    loc->isRTL = loc_isRTL;

    return loc;
}

Locale* get_default_locale(void) { return new_Locale("en", "US"); }

int utf8_strlen(const char* str) {
    if (!str) return 0;
    int length = 0;
    while (*str) {
        if ((*str & 0xC0) != 0x80) length++;
        str++;
    }
    return length;
}

String* utf8_substr(const char* str, int start, int len) {
    if (!str) return new_String("");
    int current_char = 0;
    const char* p_start = NULL; const char* p_end = NULL; const char* p = str;

    while (*p) {
        bool is_char_start = ((*p & 0xC0) != 0x80);
        if (is_char_start) {
            if (current_char == start) p_start = p;
            if (current_char == start + len) { p_end = p; break; }
            current_char++;
        }
        p++;
    }

    if (!p_start) return new_String("");
    if (!p_end) p_end = p;

    int byte_len = (int)(p_end - p_start);
    char* buf = (char*)malloc(byte_len + 1);

    if (!buf) return new_String("");

    memcpy(buf, p_start, byte_len);
    buf[byte_len] = '\0';

    String* res = new_String(buf);
    free(buf);
    return res;
}