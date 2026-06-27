// ==========================================
// include/locale.h
// ==========================================
#ifndef LOCALE_H
#define LOCALE_H

#include "object.h"
#include "string_obj.h"

typedef struct Locale Locale;

struct Locale {
    Object base;

    char* language;
    char* country;
    char* charset;

    // [BORROWED]
    const char* (*getLanguage)(Locale* self);
    const char* (*getCountry)(Locale* self);
    const char* (*getCharset)(Locale* self);

    //[OWNED] Fallback 로직 체인 (예: "ko_KR" -> "ko" -> "en_US")
    // 반환된 새 Locale 객체는 호출자가 해제해야 합니다.
    Locale* (*getFallback)(Locale* self);

    //[설계 철학] v1.x 지원 언어 한정: ar(아랍어), he(히브리어), fa(페르시아어), ur(우르두어)
    bool (*isRTL)(Locale* self);
};

//[OWNED] 생성된 Locale 객체는 호출자가 RELEASE 해야 합니다.
Locale* new_Locale(const char* lang, const char* country);

//[OWNED] 내부적으로 new_Locale("en", "US")를 호출하여 새 객체를 반환하므로,
// 정적 캐싱 객체가 아니며 호출자에게 해제(RELEASE) 책임이 따릅니다.
Locale* get_default_locale(void);

//[주의] 단순 길이 측정용이며, UTF-8 유효성(Validation) 검사는 수행하지 않음.
int     utf8_strlen(const char* str);

//[OWNED] 반환된 String 객체는 호출자가 RELEASE 해야 합니다.
String* utf8_substr(const char* str, int start, int len);

#endif // LOCALE_H