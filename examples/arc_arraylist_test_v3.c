/**
 * @file arc_arraylist_v3_test.c
 * @brief ArrayList V3 회귀 테스트 (11/17 A안: clear() → capacity 0)
 *
 * 검증 항목
 *   [1] clear 직후 add (capacity 0 → 재확장)
 *   [2] 빈 리스트 clear / 이중 clear 후 add
 *   [3] removeResult = remove (요소 RELEASE 확인)
 *   [4] forEach 콜백 안에서 clear  (RETAIN 스냅샷 → UAF 없어야 함)
 *   [5] forEach 콜백 안에서 remove(0)
 *   [6] sort 오름차순
 *   [7] 멀티스레드: add ×4 + clear + forEach + sort + getSize 동시 실행 (TSan 용)
 *   [8] 최종 누수 검사: 생성 객체 수 == finalize 호출 수
 *
 * 공개 API만 사용합니다. 종료 코드: 전부 OK → 0, FAIL 있음 → 1
 *
 * 실행:
 *   ASan/UBSan : make clean examples DEBUG=1 USE_ASAN=1 && ./examples/arc_arraylist_v3_test
 *   TSan       : -fsanitize=thread 로 별도 빌드 후 같은 바이너리 실행
 *   macOS 누수 : leaks --atExit -- ./examples/arc_arraylist_v3_test
 *   Rocky      : valgrind --leak-check=full --error-exitcode=9 ./examples/arc_arraylist_v3_test
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <pthread.h>
#include <sched.h>
#include "libcore.h"

/* ---------------------------------------------------------------------------
 * 결과 집계
 * ------------------------------------------------------------------------- */
static int g_ok = 0;
static int g_fail = 0;

#define CHECK(cond, msg) do {                                        \
    if (cond) { g_ok++;   printf("  [OK] %s\n", msg); }              \
    else      { g_fail++; printf("  [FAIL] %s (line %d)\n", msg, __LINE__); } \
} while (0)

/* ---------------------------------------------------------------------------
 * 테스트용 객체: 생성/소멸 개수를 센다
 * ------------------------------------------------------------------------- */
typedef struct {
    Object base;
    int val;
} TObj;

static atomic_int g_created = 0;
static atomic_int g_finalized = 0;

static void TObj_fin(Object* o) {
    (void)o;
    atomic_fetch_add(&g_finalized, 1);
}

static void TObj_str(Object* o, char* b, size_t l) {
    snprintf(b, l, "%d", ((TObj*)o)->val);
}

static const Class tObjClass = {
    .name = "TObj", .size = sizeof(TObj),
    .toString = TObj_str, .finalize = TObj_fin
};

static TObj* new_TObj(int v) {
    TObj* o = (TObj*)calloc(1, sizeof(TObj));
    if (!o) { perror("calloc"); exit(2); }
    Object_Init(&o->base, &tObjClass);
    o->val = v;
    atomic_fetch_add(&g_created, 1);
    return o;
}

/** 새 객체를 만들어 리스트에 넣고 내 소유권은 바로 포기 (리스트만 소유) */
static void add_new(ArrayList* list, int v) {
    TObj* o = new_TObj(v);
    list->add(list, (Object*)o);
    RELEASE((Object*)o);
}

static int val_at(ArrayList* list, int i) {
    TObj* o = (TObj*)list->get(list, i);
    return o ? o->val : -1;
}

/* ---------------------------------------------------------------------------
 * [1] clear 직후 add
 * ------------------------------------------------------------------------- */
static void test_clear_then_add(void) {
    printf("\n[1] clear 직후 add (capacity 0 → 재확장)\n");
    ArrayList* list = new_ArrayList(4);

    for (int i = 0; i < 5; i++) add_new(list, i);
    CHECK(list->getSize(list) == 5, "add 5개 → size=5");

    list->clear(list);
    CHECK(list->getSize(list) == 0, "clear → size=0");
    CHECK(list->isEmpty(list), "clear → isEmpty");
    CHECK(list->get(list, 0) == NULL, "clear 후 get(0)=NULL");

    add_new(list, 100);
    CHECK(list->getSize(list) == 1 && val_at(list, 0) == 100, "clear 직후 add 1개");

    for (int i = 1; i < 100; i++) add_new(list, 100 + i);
    int ok = (list->getSize(list) == 100);
    for (int i = 0; ok && i < 100; i++) ok = (val_at(list, i) == 100 + i);
    CHECK(ok, "이어서 99개 add → 100개 순서·값 일치 (realloc 연속 확장)");

    RELEASE((Object*)list);
}

/* ---------------------------------------------------------------------------
 * [2] 빈 리스트 clear / 이중 clear
 * ------------------------------------------------------------------------- */
static void test_empty_and_double_clear(void) {
    printf("\n[2] 빈 리스트 clear / 이중 clear\n");
    ArrayList* list = new_ArrayList(0);

    list->clear(list);
    CHECK(list->getSize(list) == 0, "빈 리스트 clear");

    add_new(list, 1);
    list->clear(list);
    list->clear(list);
    CHECK(list->getSize(list) == 0, "이중 clear");

    add_new(list, 2);
    CHECK(list->getSize(list) == 1 && val_at(list, 0) == 2, "이중 clear 후 add");

    list->trimToSize(list);
    add_new(list, 3);
    CHECK(list->getSize(list) == 2 && val_at(list, 1) == 3, "trimToSize 후 add");

    list->clear(list);
    list->trimToSize(list);
    add_new(list, 4);
    CHECK(list->getSize(list) == 1 && val_at(list, 0) == 4, "clear → trimToSize → add");

    RELEASE((Object*)list);
}

/* ---------------------------------------------------------------------------
 * [3] removeResult = remove (V3 공개 API 동작 변경)
 * ------------------------------------------------------------------------- */
static void test_remove_result(void) {
    printf("\n[3] removeResult = remove\n");
    ArrayList* list = new_ArrayList(4);
    add_new(list, 10);
    add_new(list, 20);
    add_new(list, 30);

    int before = atomic_load(&g_finalized);
    list->removeResult(list, 1);
    int after = atomic_load(&g_finalized);
    CHECK(after - before == 1, "removeResult 시 요소 RELEASE (finalize 1회)");
    CHECK(list->getSize(list) == 2 && val_at(list, 0) == 10 && val_at(list, 1) == 30,
          "removeResult 후 순서 유지");

    before = atomic_load(&g_finalized);
    list->remove(list, 0);
    after = atomic_load(&g_finalized);
    CHECK(after - before == 1, "remove 시 요소 RELEASE (finalize 1회)");

    list->removeResult(list, 99);
    list->remove(list, -1);
    CHECK(list->getSize(list) == 1, "범위 밖 remove/removeResult 무시");

    RELEASE((Object*)list);
}

/* ---------------------------------------------------------------------------
 * [4][5] forEach 콜백 안에서 리스트 변경
 * ------------------------------------------------------------------------- */
static ArrayList* g_cb_list = NULL;
static int g_cb_calls = 0;
static long g_cb_sum = 0;

static void cb_clear_then_read(Object* item) {
    if (g_cb_calls == 0) g_cb_list->clear(g_cb_list);
    /* V3: 스냅샷이 RETAIN 되어 있으므로 clear 이후에도 item 은 살아 있어야 함 */
    g_cb_sum += ((TObj*)item)->val;
    g_cb_calls++;
}

static void cb_remove_first_then_read(Object* item) {
    g_cb_list->remove(g_cb_list, 0);
    g_cb_sum += ((TObj*)item)->val;
    g_cb_calls++;
}

static void test_foreach_mutation(void) {
    printf("\n[4] forEach 콜백 안에서 clear\n");
    ArrayList* list = new_ArrayList(4);
    for (int i = 1; i <= 5; i++) add_new(list, i);

    g_cb_list = list; g_cb_calls = 0; g_cb_sum = 0;
    list->forEach(list, cb_clear_then_read);
    CHECK(g_cb_calls == 5, "스냅샷 5개 모두 콜백 호출");
    CHECK(g_cb_sum == 15, "clear 후에도 스냅샷 값 유효 (합=15)");
    CHECK(list->getSize(list) == 0, "forEach 종료 후 size=0");

    add_new(list, 7);
    CHECK(list->getSize(list) == 1, "forEach 안 clear 이후 add");
    RELEASE((Object*)list);

    printf("\n[5] forEach 콜백 안에서 remove(0)\n");
    list = new_ArrayList(4);
    for (int i = 1; i <= 4; i++) add_new(list, i * 10);

    g_cb_list = list; g_cb_calls = 0; g_cb_sum = 0;
    list->forEach(list, cb_remove_first_then_read);
    CHECK(g_cb_calls == 4, "스냅샷 4개 모두 콜백 호출");
    CHECK(g_cb_sum == 100, "remove 후에도 스냅샷 값 유효 (합=100)");
    CHECK(list->getSize(list) == 0, "forEach 종료 후 size=0");

    g_cb_list = NULL;
    RELEASE((Object*)list);
}

/* ---------------------------------------------------------------------------
 * [6] sort
 * ------------------------------------------------------------------------- */
/* qsort 비교 함수: 인자는 Object** 를 가리킨다 */
static int cmp_tobj(const void* a, const void* b) {
    const TObj* x = *(const TObj* const*)a;
    const TObj* y = *(const TObj* const*)b;
    return (x->val > y->val) - (x->val < y->val);
}

static void test_sort(void) {
    printf("\n[6] sort\n");
    ArrayList* list = new_ArrayList(4);
    int src[] = { 42, 7, 19, 3, 88, 7, 0, 55 };
    int n = (int)(sizeof(src) / sizeof(src[0]));
    for (int i = 0; i < n; i++) add_new(list, src[i]);

    list->sort(list, cmp_tobj);
    int ok = (list->getSize(list) == n);
    for (int i = 1; ok && i < n; i++) ok = (val_at(list, i - 1) <= val_at(list, i));
    CHECK(ok, "오름차순 정렬");

    list->clear(list);
    list->sort(list, cmp_tobj);
    CHECK(list->getSize(list) == 0, "빈 리스트 sort");

    add_new(list, 1);
    list->sort(list, cmp_tobj);
    CHECK(list->getSize(list) == 1 && val_at(list, 0) == 1, "1개 sort");

    RELEASE((Object*)list);
}

/* ---------------------------------------------------------------------------
 * [7] 멀티스레드 혼합 (TSan 대상)
 * ------------------------------------------------------------------------- */
#define ADDERS       4
#define ADD_PER      2000
#define CLEAR_LOOPS  300
#define READ_LOOPS   300

static atomic_int g_adders_done = 0;
static atomic_long g_read_sum = 0;

static void cb_read(Object* item) {
    atomic_fetch_add(&g_read_sum, ((TObj*)item)->val);
}

static void* th_adder(void* arg) {
    ArrayList* list = (ArrayList*)arg;
    for (int i = 0; i < ADD_PER; i++) add_new(list, i);
    atomic_fetch_add(&g_adders_done, 1);
    return NULL;
}

static void* th_clearer(void* arg) {
    ArrayList* list = (ArrayList*)arg;
    for (int i = 0; i < CLEAR_LOOPS; i++) { list->clear(list); sched_yield(); }
    return NULL;
}

static void* th_reader(void* arg) {
    ArrayList* list = (ArrayList*)arg;
    for (int i = 0; i < READ_LOOPS; i++) {
        list->forEach(list, cb_read);
        (void)list->getSize(list);
        (void)list->isEmpty(list);
        sched_yield();
    }
    return NULL;
}

static void* th_sorter(void* arg) {
    ArrayList* list = (ArrayList*)arg;
    for (int i = 0; i < READ_LOOPS / 3; i++) { list->sort(list, cmp_tobj); sched_yield(); }
    return NULL;
}

static void test_threads(void) {
    printf("\n[7] 멀티스레드: add×%d + clear + forEach + sort 동시 실행\n", ADDERS);
    ArrayList* list = new_ArrayList(0);
    pthread_t adders[ADDERS], clearer, reader, sorter;

    for (int i = 0; i < ADDERS; i++) pthread_create(&adders[i], NULL, th_adder, list);
    pthread_create(&clearer, NULL, th_clearer, list);
    pthread_create(&reader,  NULL, th_reader,  list);
    pthread_create(&sorter,  NULL, th_sorter,  list);

    for (int i = 0; i < ADDERS; i++) pthread_join(adders[i], NULL);
    pthread_join(clearer, NULL);
    pthread_join(reader,  NULL);
    pthread_join(sorter,  NULL);

    int sz = list->getSize(list);
    CHECK(atomic_load(&g_adders_done) == ADDERS, "adder 스레드 전원 완료");
    CHECK(sz >= 0 && sz <= ADDERS * ADD_PER, "최종 size 범위 정상");

    list->clear(list);
    for (int i = 0; i < 10; i++) add_new(list, i);
    CHECK(list->getSize(list) == 10, "동시 실행 이후 clear → add 정상");

    RELEASE((Object*)list);
}

/* ---------------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------------- */
int main(void) {
    printf("==================================================\n");
    printf("  ArrayList V3 회귀 테스트 (clear → capacity 0)\n");
    printf("==================================================\n");

    test_clear_then_add();
    test_empty_and_double_clear();
    test_remove_result();
    test_foreach_mutation();
    test_sort();
    test_threads();

    printf("\n[8] 객체 누수 검사\n");
    int c = atomic_load(&g_created);
    int f = atomic_load(&g_finalized);
    printf("  생성 %d / finalize %d\n", c, f);
    CHECK(c == f, "생성 수 == finalize 수 (리스트에 남은 객체 없음)");

    printf("\n==================================================\n");
    printf("  결과: [OK] %d  [FAIL] %d\n", g_ok, g_fail);
    printf("==================================================\n");
    return g_fail ? 1 : 0;
}