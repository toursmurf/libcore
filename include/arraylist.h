#ifndef ARRAYLIST_H  
#define ARRAYLIST_H  
  
#include <pthread.h>  
#include <stdbool.h>  
#include <stdlib.h>  
#include "object.h"

typedef void (*ArrayListActionFunc)(Object* item);  
typedef int (*ArrayListCompareFunc)(const void* itemA, const void* itemB);  

extern const Class arrayListClass;  
extern const Class arrayListIteratorClass;  

typedef struct ArrayListIterator ArrayListIterator;
typedef struct ArrayList ArrayList;

struct ArrayListIterator {  
    Object base;  
    ArrayList *list;  
    int currentIndex;  

    bool (*hasNext)(ArrayListIterator *self);  
    Object* (*next)(ArrayListIterator *self);  
};  

struct ArrayList {  
    Object base;  
    Object** items;
    int size;  
    int capacity;  
    pthread_mutex_t lock;  
    
    void (*add)(ArrayList* self, Object* item);  
    Object* (*get)(ArrayList* self, int index);  
    void (*remove)(ArrayList* self, int index);      
    void (*removeResult)(ArrayList* self, int index);
    int (*getSize)(ArrayList* self);  
    void (*clear)(ArrayList* self);  
    bool (*isEmpty)(ArrayList* self);  
    void (*trimToSize)(ArrayList* self);  
    void (*ensureCapacity)(ArrayList* self, int min_capacity);  
    void (*forEach)(ArrayList* self, ArrayListActionFunc action);
    void* (*find)(ArrayList* self, void* target, ArrayListCompareFunc compare); 
    void (*sort)(ArrayList* self, ArrayListCompareFunc compare);
    ArrayListIterator* (*iterator)(ArrayList* self);  
    void (*destroy)(ArrayList* self);
    Object* (*detach)(ArrayList *self, int index);
    bool (*is_arraylist)(Object *obj);
};  

ArrayList* new_ArrayList(int initial_capacity);  

#endif // ARRAYLIST_H
