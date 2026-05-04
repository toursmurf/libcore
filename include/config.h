#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>
#include "object.h"
#include "hashmap.h"
#include "string_obj.h"

typedef struct _Config Config;

struct _Config {
    Object base;        

    /* 내부 상태 (Private) */
    HashMap* map;       

    /* 메서드 포인터 (Java-Style OOP) */
    const char* (*get)    (Config* self, const char* key);
    const char* (*getString)    (Config* self, const char* key, const char *def);
    int         (*getInt) (Config* self, const char* key, int def);
    bool        (*getBool)(Config* self, const char* key, bool def);
    bool        (*load)   (Config* self, const char* path);
};

/* 생성자 */
Config* new_Config(void);

#endif // CONFIG_H
