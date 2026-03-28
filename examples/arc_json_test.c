/*
 * File: arc_json_test.c
 * Project: libcore v0.5 (Iron Fortress)
 * Author: InDong KIM (idong322@naver.com) - @toursmurf
 * Copyright (c) 2026 Toos IT Holdings. All rights reserved.
 * License: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libcore.h"

/* =========================================================
 * [긴급 패치] 멤버 이름을 몰라도 Class를 가져오는 의장님 오리지널 매크로!
 * (원래 내부용이었으나, 테스트를 위해 글로벌 개방!)
 * ========================================================= */
#ifndef GET_CLASS
#define GET_CLASS(obj) ( *( (const Class**) (obj) ) )
#endif

/* =========================================================
 * [Test 1] JSON Creation (Java Style)
 * ========================================================= */
void test_json_creation() {
    printf("\n--- [Test 1] JSON Creation (Java Style) ---\n");

    JSONNode *root = new_JSON(NULL);

    Object *name_val = (Object*)new_json_string("InDong KIM");
    root->put(root, "architect", name_val);
    RELEASE(name_val); // ARC가 작동하므로 소유권 해제!

    Object *company_val = (Object*)new_json_string("Toos IT Holdings");
    root->put(root, "company", company_val);
    RELEASE(company_val);

    Object *version_val = (Object*)new_json_number(0.5);
    root->put(root, "version", version_val);
    RELEASE(version_val);

    JSONNode *arr = new_JSON("[]");
    
    Object *skill1 = (Object*)new_json_string("C");
    Object *skill2 = (Object*)new_json_string("Java");
    Object *skill3 = (Object*)new_json_string("ARC");
    
    arr->add(arr, skill1); RELEASE(skill1);
    arr->add(arr, skill2); RELEASE(skill2);
    arr->add(arr, skill3); RELEASE(skill3);

    root->put(root, "skills", (Object*)arr);
    RELEASE(arr);

    char *json_str = root->toString(root);
    printf("[Result] Created JSON:\n%s\n", json_str);
    free(json_str);

    RELEASE(root);
    printf("[Clean] JSON Creation test memory released.\n");
}

/* =========================================================
 * [Test 2] JSON Parsing & Data Extraction
 * ========================================================= */
void test_json_parsing() {
    printf("\n--- [Test 2] JSON Parsing & Data Extraction ---\n");

    const char *payload = 
        "{\"status\":\"SUCCESS\", \"code\":200, \"data\":[\"Google\", \"Microsoft\", \"Anthropic\", \"xAI\"]}";

    JSONNode *parsed = new_JSON(payload);

    if (parsed->isObject(parsed)) {
        printf("[Parse] Root is recognized as a JSON Object.\n");

        const char *status = parsed->getString(parsed, "status");
        int code = parsed->getInt(parsed, "code");
        
        printf(" -> Status : %s\n", status ? status : "null");
        printf(" -> Code   : %d\n", code);

        Object *data_obj = parsed->get(parsed, "data");
        
        // [보안 패치] strncmp를 이용한 철벽 타입 검사!
        if (data_obj && strncmp(GET_CLASS(data_obj)->name, "ArrayList", sizeof("ArrayList")) == 0) {
            ArrayList *list = (ArrayList*)data_obj;
            printf(" -> Data Array Length: %d\n", list->getSize(list));
            
            for (int i = 0; i < list->getSize(list); i++) {
                JsonValue *jv = (JsonValue*)list->get(list, i);
                if (jv->type == J_STRING) {
                    printf("    [%d] %s\n", i, jv->string);
                }
            }
        }
    }

    RELEASE(parsed);
    printf("[Clean] JSON Parsing test memory released.\n");
}

/* =========================================================
 * [Test 3] Native Collection Serialization
 * ========================================================= */
void test_native_collection_serialization() {
    printf("\n--- [Test 3] ObjectMapper Serialization ---\n");

    HashMap *user_map = new_HashMap(16);
    
    Object *id_val = (Object*)new_json_string("toursmurf");
    user_map->put(user_map, "github_id", id_val);
    RELEASE(id_val);

    ArrayList *repo_list = new_ArrayList(10);
    Object *repo_val = (Object*)new_json_string("libcore");
    repo_list->add(repo_list, repo_val);
    RELEASE(repo_val);

    user_map->put(user_map, "repositories", (Object*)repo_list);
    RELEASE(repo_list);

    char *serialized = GetObjectMapper()->writeValueAsString((Object*)user_map);
    printf("[ObjectMapper] Native HashMap to JSON:\n%s\n", serialized);
    free(serialized);

    RELEASE(user_map);
    printf("[Clean] Native Collection test memory released.\n");
}

/* =========================================================
 * Main Entry Point
 * ========================================================= */
int main() {
    printf("==================================================\n");
    printf(" Toos IT Holdings - libcore JSON Engine Test\n");
    printf(" Author: InDong KIM (@toursmurf)\n");
    printf("==================================================\n");

    test_json_creation();
    test_json_parsing();
    test_native_collection_serialization();

    printf("\n==================================================\n");
    printf(" [SUCCESS] All JSON tests completed gracefully.\n");
    printf("==================================================\n");

    return 0;
}
