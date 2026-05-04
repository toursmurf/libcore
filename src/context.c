#include "context.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static void Context_finalize(Object* obj) {
    Context* self = (Context*)obj;
    if (self->data) RELEASE((Object*)self->data);
    pthread_mutex_destroy(&self->lock);
}

static Class Context_Class = { .name = "Context", .size = sizeof(Context), .finalize = Context_finalize };

static void Context_set(Context* self, const char* key, Object* value) {
    if (!self || !key || !value) return;
    pthread_mutex_lock(&self->lock);
    self->data->put(self->data, key, value);
    pthread_mutex_unlock(&self->lock);
}

static Object* Context_get(Context* self, const char* key) {
    if (!self || !key) return NULL;
    pthread_mutex_lock(&self->lock);
    Object* val = self->data->get(self->data, key);
    pthread_mutex_unlock(&self->lock);
    return val;
}

static Object* Context_remove(Context* self, const char* key) {
    if (!self || !key) return NULL;
    pthread_mutex_lock(&self->lock);
    Object* removed = self->data->detach(self->data, key);
    pthread_mutex_unlock(&self->lock);
    return removed;
}

static bool Context_has(Context* self, const char* key) {
    if (!self || !key) return false;
    pthread_mutex_lock(&self->lock);
    bool exists = self->data->hasKey(self->data, key); // containsKey -> hasKey ✅
    pthread_mutex_unlock(&self->lock);
    return exists;
}

static void Context_clear(Context* self) {
    if (!self) return;
    pthread_mutex_lock(&self->lock);
    self->data->clear(self->data);
    pthread_mutex_unlock(&self->lock);
}

static int Context_getSize(Context* self) {
    if (!self) return 0;
    pthread_mutex_lock(&self->lock);
    int sz = self->data->getSize(self->data);
    pthread_mutex_unlock(&self->lock);
    return sz;
}

static void Context_setString(Context* self, const char* key, const char* val) {
    if (!self || !key || !val) return;
    String* str_obj = new_String(val);
    if (str_obj) {
        self->set(self, key, (Object*)str_obj);
        RELEASE((Object*)str_obj);
    }
}

static String* Context_getString(Context* self, const char* key) {
    Object* obj = self->get(self, key);
    if (obj && strcmp(obj->type->name, "String") == 0) return (String*)obj;
    return NULL;
}

static void Context_setInt(Context* self, const char* key, int val) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", val);
    self->setString(self, key, buf);
}

static int Context_getInt(Context* self, const char* key) {
    String* str_obj = self->getString(self, key);
    if (str_obj && str_obj->value) return atoi(str_obj->value); // val -> value ✅
    return 0;
}

Context* new_Context() {
    Context* self = (Context*)calloc(1, sizeof(Context));
    if (!self) return NULL;
    Object_Init((Object*)self, &Context_Class);
    self->data = new_HashMap(16);
    pthread_mutex_init(&self->lock, NULL);

    self->set = Context_set;
    self->get = Context_get;
    self->remove = Context_remove;
    self->has = Context_has;
    self->clear = Context_clear;
    self->getSize = Context_getSize;
    self->setString = Context_setString;
    self->getString = Context_getString;
    self->setInt = Context_setInt;
    self->getInt = Context_getInt;
    return self;
}
