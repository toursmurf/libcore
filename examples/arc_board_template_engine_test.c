#include <stdio.h>
#include <stdlib.h>
#include "board_template_engine.h"
#include "json.h"

int main() {
    printf("=== TemplateEngine V1.7.2 Final Test ===\n");

    TemplateEngine* engine = new_TemplateEngine();
    JSONNode* ctx = new_JSON_Object();

    /* 1. XSS 공격이 포함된 스칼라 변수 주입 */
    JSONNode* title = new_JSON_String("<Hello> & \"Welcome\"");
    ctx->put(ctx, "title", (Object*)title);
    RELEASE((Object*)title);

    /* 2. 게시글 목록 배열 주입 */
    JSONNode* posts = new_JSON_Array();

    JSONNode* p1 = new_JSON_Object();
    JSONNode* id1 = (JSONNode*)new_json_number(1);
    JSONNode* t1 = new_JSON_String("<첫 번째 글>");
    p1->put(p1, "id", (Object*)id1);
    p1->put(p1, "title", (Object*)t1);
    RELEASE((Object*)id1);
    RELEASE((Object*)t1);

    JSONNode* p2 = new_JSON_Object();
    JSONNode* id2 = (JSONNode*)new_json_number(2);
    JSONNode* t2 = new_JSON_String("두 번째 글");
    p2->put(p2, "id", (Object*)id2);
    p2->put(p2, "title", (Object*)t2);
    RELEASE((Object*)id2);
    RELEASE((Object*)t2);

    posts->add(posts, (Object*)p1);
    posts->add(posts, (Object*)p2);
    RELEASE((Object*)p1);
    RELEASE((Object*)p2);

    ctx->put(ctx, "posts", (Object*)posts);
    RELEASE((Object*)posts);

    /* 3. 렌더링 실행 (Mustache Subset + XSS 방어) */
    const char* tpl =
        "<h1>{{title}}</h1>\n"
        "<ul>\n"
        "{{#posts}}\n"
        "  <li>ID: {{id}} | Title: {{title}}</li>\n"
        "{{/posts}}\n"
        "</ul>\n";

    char* out = engine->render(engine, tpl, ctx);

    printf("\n[렌더링 결과]\n");
    printf("%s", out);

    /* 메모리 해제 */
    free(out);
    RELEASE((Object*)ctx);
    RELEASE((Object*)engine);

    printf("\n=== Test Completed ===\n");
    return 0;
}