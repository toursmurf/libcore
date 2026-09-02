#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h> 

static char* trim(char* s) {
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '\0') return s;
    char* end = s + strlen(s) - 1;
    while (end > s && (*end == ' ' || *end == '\t')) {
        *end-- = '\0';
    }
    return s;
}


static const char* config_get(Config* self, const char* key) {
    // 💡 char* 키 직접 전달 (임시 객체 없음)
    String* v = (String*)self->map->get(self->map, key);
    return v ? v->value : NULL; 
}

static int config_getInt(Config* self, const char* key, int def) {
    const char* v = self->get(self, key);
    return v ? atoi(v) : def;
}

static const char* config_getString(Config* self, const char* key, const char* def) {
    const char* v = self->get(self, key);
    return v ? v : def; 
}
static bool config_getBool(Config* self, const char* key, bool def) {
    const char* v = self->get(self, key);
    if (!v) return def;
    
    return (strcasecmp(v, "true") == 0 || 
            strcmp(v, "1") == 0 || 
            strcasecmp(v, "yes") == 0);
}

static bool config_load(Config* self, const char* path) {
    FILE* fp = fopen(path, "r");
    if (!fp) return false;

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == ';' || line[0] == '\n' || line[0] == '\r') continue;

        char* eq = strchr(line, '=');
        if (eq) {
            *eq = '\0'; 
            char* raw_val = eq + 1;

            /* 1. 엔터(\r, \n)부터 완벽하게 제거 (가장 끝부분 파괴) */
            raw_val[strcspn(raw_val, "\r\n")] = '\0';
            char* key_trimmed = trim(line);
            char* val_trimmed = trim(raw_val);
            //val_trimmed[strcspn(val_trimmed, "\r\n")] = '\0';

            // 💡 Value만 String 객체로 생성
            String* v = new_String(val_trimmed);
            
            // HashMap 삽입 (Key: char*, Value: Object*)
            self->map->put(self->map, key_trimmed, (Object*)v);
            
            RELEASE((Object*)v);
        }
    }
    fclose(fp);
    return true;
}

static void config_finalize(Object* obj) {
    Config* self = (Config*)obj;
    if (self->map) {
        RELEASE((Object*)self->map); 
    }
}

static const Class configClass = {
	.name = "Config",
	.size = sizeof(Config),
	.finalize = config_finalize };

Config* new_Config(void) {
    Config* self = (Config*)calloc(1, sizeof(Config));
    Object_Init(&self->base, &configClass);

    self->map = new_HashMap(16); 

    self->get     = config_get;
    self->getInt  = config_getInt;
    self->getBool = config_getBool;
    self->getString = config_getString;
    self->load    = config_load;

    return self;
}
