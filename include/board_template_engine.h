#ifndef TEMPLATE_ENGINE_H
#define TEMPLATE_ENGINE_H

#include "object.h"
#include "json.h"

typedef struct TemplateEngine TemplateEngine;

struct TemplateEngine {
    Object base;

    char* (*render)(TemplateEngine* self, const char* template_text, JSONNode* context);
    char* (*renderFile)(TemplateEngine* self, const char* file_path, JSONNode* context);
};

TemplateEngine* new_TemplateEngine(void);

#endif /* TEMPLATE_ENGINE_H */