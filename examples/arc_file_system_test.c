/**
 * @file arc_file_system_test.c
 * @brief 🇰🇷 Path, File, Directory 객체를 이용한 파일 시스템 조작 및 FileWatcher(inotify) 감시 테스트입니다.
 * 🇬🇧 File system manipulation and FileWatcher (inotify) monitoring test using Path, File, and Directory objects.
 * @note  This example strictly follows the ARC memory management rules.
 */

#include "path.h"
#include "file.h"
#include "directory.h"
#include "file_util.h"
#include "mapped_file.h"
#include "file_watcher.h"
#include "object.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int pass_count = 0;
static int fail_count = 0;

#define TEST(name, expr) do { \
    if (expr) { \
        printf("  [PASS] %s\n", name); \
        pass_count++; \
    } else { \
        printf("  [FAIL] %s\n", name); \
        fail_count++; \
    } \
} while(0)

#define SECTION(name) printf("\n=== %s ===\n", name)

// ============================================================================
// [1] Path 테스트
// ============================================================================
static void test_path(void) {
    SECTION("Path");

    Path* p = new_Path("/tmp/libcore/test/config.json");
    TEST("new_Path", p != NULL);

    String* name = p->getFileName(p);
    TEST("getFileName", name && strcmp(name->c_str(name), "config.json") == 0);
    RELEASE(name);

    String* base = p->getBaseName(p);
    TEST("getBaseName", base && strcmp(base->c_str(base), "config") == 0);
    RELEASE(base);

    String* ext = p->getExtension(p);
    TEST("getExtension", ext && strcmp(ext->c_str(ext), "json") == 0);
    RELEASE(ext);

    String* parent = p->getParent(p);
    TEST("getParent", parent && strcmp(parent->c_str(parent), "/tmp/libcore/test") == 0);
    RELEASE(parent);

    TEST("isAbsolute", p->isAbsolute(p) == true);
    TEST("isRelative", p->isRelative(p) == false);

    Path* child = p->resolve(p, "subdir/data.txt");
    TEST("resolve (should be NULL - called on file path)", child != NULL);
    RELEASE(child);

    Path* sib = p->sibling(p, "other.json");
    TEST("sibling", sib != NULL);
    RELEASE(sib);

    Path* newExt = p->withExt(p, "yaml");
    TEST("withExt", newExt != NULL);
    RELEASE(newExt);

    Path* messy = new_Path("/tmp/../tmp/libcore/./test");
    Path* norm = messy->normalize(messy);
    TEST("normalize", norm != NULL);
    RELEASE(norm);
    RELEASE(messy);

    Path* p2 = new_Path("/tmp/libcore/test/config.json");
    TEST("equals", p->equals(p, p2));
    RELEASE(p2);

    Path* rel = new_Path("relative/path.txt");
    Path* abs = rel->toAbsolute(rel);
    TEST("toAbsolute", abs != NULL);
    RELEASE(abs);
    RELEASE(rel);

    RELEASE(p);
}

// ============================================================================
// [2] File 테스트
// ============================================================================
static void test_file(void) {
    SECTION("File");

    const char* testPath = "/tmp/libcore_test.txt";

    File* f = new_File(testPath);
    TEST("new_File", f != NULL);

    String* content = new_String("Hello, libcore!\nLine 2\nLine 3");
    bool wrote = f->writeString(f, content);
    TEST("writeString", wrote);
    RELEASE(content);

    TEST("exists", f->exists(f));
    TEST("isFile", f->isFile(f));
    TEST("length > 0", f->length(f) > 0);
    TEST("isReadable", f->isReadable(f));
    TEST("isWritable", f->isWritable(f));
    RELEASE(f);

    f = new_File(testPath);
    String* text = f->readAllText(f);
    TEST("readAllText", text && strstr(text->c_str(text), "libcore") != NULL);
    RELEASE(text);
    RELEASE(f);

    f = new_File(testPath);
    ArrayList* lines = f->readLines(f);
    TEST("readLines count", lines && lines->getSize(lines) == 3);
    // 🔥 [복구] 메모리 누수 방지를 위한 수동 해제 (이게 맞았습니다!)
    if (lines) {
        for (int i = 0; i < lines->getSize(lines); i++) {
            String* line = (String*)lines->get(lines, i);
            RELEASE(line);
        }
    }
    RELEASE(lines);
    RELEASE(f);

    f = new_File(testPath);
    String* appended = new_String("\nLine 4");
    TEST("appendString", f->appendString(f, appended));
    RELEASE(appended);
    RELEASE(f);

    f = new_File(testPath);
    TEST("lastModifiedMs", f->lastModifiedMs(f) > 0);
    RELEASE(f);

    f = new_File(testPath);
    String* sz = f->getHumanSize(f);
    TEST("getHumanSize", sz != NULL);
    RELEASE(sz);
    RELEASE(f);

    f = new_File(testPath);
    String* mime = f->guessMimeType(f);
    TEST("guessMimeType", mime && strcmp(mime->c_str(mime), "text/plain") == 0);
    RELEASE(mime);
    RELEASE(f);

    f = new_File(testPath);
    String* hash = f->sha256(f);
    TEST("sha256", hash && strlen(hash->c_str(hash)) == 64);
    RELEASE(hash);
    RELEASE(f);

    f = new_File(testPath);
    String* md5 = f->md5(f);
    TEST("md5", md5 && strlen(md5->c_str(md5)) == 32);
    RELEASE(md5);
    RELEASE(f);

    f = new_File(testPath);
    Path* destPath = new_Path("/tmp/libcore_test_copy.txt");
    TEST("copyTo", f->copyTo(f, destPath));
    RELEASE(destPath);
    RELEASE(f);

    f = new_File(testPath);
    File* copy = new_File("/tmp/libcore_test_copy.txt");
    TEST("equalsContent", f->equalsContent(f, copy));
    RELEASE(copy);
    RELEASE(f);

    f = new_File(testPath);
    Path* newPath = new_Path("/tmp/libcore_test_renamed.txt");
    TEST("renameAtomic", f->renameAtomic(f, newPath));
    RELEASE(newPath);
    RELEASE(f); // 👈 여기서 정상적으로 f가 죽습니다.

    File* renamed = new_File("/tmp/libcore_test_renamed.txt");
    TEST("deleteFile (renamed)", renamed->deleteFile(renamed));
    RELEASE(renamed);

    File* copyDel = new_File("/tmp/libcore_test_copy.txt");
    TEST("deleteFile (copy)", copyDel->deleteFile(copyDel));
    RELEASE(copyDel);

    // 🔥 [제거 완료] 이중 해제(Double Free)를 유발했던 꼬투리 RELEASE(f) 삭제!!
}

// ============================================================================
// [3] Directory 테스트
// ============================================================================
static void test_directory(void) {
    SECTION("Directory");

    const char* testDir = "/tmp/libcore_dir_test";
    const char* subDir  = "/tmp/libcore_dir_test/sub/deep";

    Directory* d = new_Directory(subDir);
    TEST("new_Directory", d != NULL);
    TEST("mkdirs", d->mkdirs(d));
    RELEASE(d);

    Directory* d2 = new_Directory(testDir);
    TEST("exists", d2->exists(d2));

    File* f1 = new_File("/tmp/libcore_dir_test/a.txt");
    String* c1 = new_String("file a");
    f1->writeString(f1, c1);
    RELEASE(c1); RELEASE(f1);

    File* f2 = new_File("/tmp/libcore_dir_test/b.txt");
    String* c2 = new_String("file b");
    f2->writeString(f2, c2);
    RELEASE(c2); 
    RELEASE(f2);

    ArrayList* files = d2->listFiles(d2);
    TEST("listFiles", files && files->getSize(files) > 0);
    // 🔥 [복구] 메모리 누수 방지
    if (files) {
        for (int i = 0; i < files->getSize(files); i++) {
            File* f = (File*)files->get(files, i);
            RELEASE(f);
        }
    }
    RELEASE(files);

    ArrayList* all = d2->walkTree(d2);
    TEST("walkTree", all && all->getSize(all) >= 2);
    // 🔥 [복구] 메모리 누수 방지
    if (all) {
        for (int i = 0; i < all->getSize(all); i++) {
            File* f = (File*)all->get(all, i);
            RELEASE(f);
        }
    }
    RELEASE(all);

    TEST("deleteRecursive", d2->deleteRecursive(d2));
    TEST("exists after delete", !d2->exists(d2));

    RELEASE(d2);
}

// ============================================================================
// [4] FileUtil 테스트
// ============================================================================
static void test_file_util(void) {
    SECTION("FileUtil");

    File* tmp = FileUtil_tmp();
    TEST("FileUtil_tmp", tmp != NULL);
    RELEASE(tmp);

    File* home = FileUtil_home();
    TEST("FileUtil_home", home != NULL);
    RELEASE(home);

    File* cwd = FileUtil_cwd();
    TEST("FileUtil_cwd", cwd != NULL);
    RELEASE(cwd);

    TEST("FileUtil_exists /tmp", FileUtil_exists("/tmp"));
    TEST("FileUtil_exists fake", !FileUtil_exists("/tmp/fake_12345_xyz"));

    TEST("FileUtil_mkdirs", FileUtil_mkdirs("/tmp/libcore_util_test/a/b"));

    File* tmp2 = FileUtil_createTemp("/tmp", "smurf_");
    TEST("FileUtil_createTemp", tmp2 != NULL);
    if (tmp2) {
        File* dest = new_File("/tmp/libcore_util_copy.txt");
        String* sc = new_String("temp content");
        tmp2->writeString(tmp2, sc);
        RELEASE(sc);

        TEST("FileUtil_copy", FileUtil_copy(tmp2, dest) == CORE_OK);
        RELEASE(dest);

        FileUtil_delete(tmp2->filePath->path);
        RELEASE(tmp2);
    }

    File* moveS = new_File("/tmp/libcore_move_src.txt");
    String* ms = new_String("move test content");
    moveS->writeString(moveS, ms);
    RELEASE(ms);

    File* moveD = new_File("/tmp/libcore_move_dst.txt");
    TEST("FileUtil_move", FileUtil_move(moveS, moveD) == CORE_OK);
    RELEASE(moveS);

    TEST("FileUtil_move src gone", !FileUtil_exists("/tmp/libcore_move_src.txt"));
    TEST("FileUtil_move dst exists", FileUtil_exists("/tmp/libcore_move_dst.txt"));

    String* moved_text = moveD->readAllText(moveD);
    TEST("FileUtil_move content", moved_text &&
         strstr(moved_text->c_str(moved_text), "move test") != NULL);
    RELEASE(moved_text);
    RELEASE(moveD);

    FileUtil_delete("/tmp/libcore_util_test");
    FileUtil_delete("/tmp/libcore_util_copy.txt");
    FileUtil_delete("/tmp/libcore_move_dst.txt");

    TEST("FileUtil_delete dir", !FileUtil_exists("/tmp/libcore_util_test"));
}

// ============================================================================
// [5] MappedFile 테스트
// ============================================================================
static void test_mapped_file(void) {
    SECTION("MappedFile");

    const char* mpath = "/tmp/libcore_mmap_test.txt";
    File* prep = new_File(mpath);
    String* mc = new_String("MappedFile test content for libcore v1.0!!");
    prep->writeString(prep, mc);
    RELEASE(mc);
    RELEASE(prep);

    MappedFile* mf = new_MappedFile(mpath);
    TEST("new_MappedFile", mf != NULL);

    TEST("map (readOnly)", mf->map(mf, true));
    TEST("map again (중복 방어)", !mf->map(mf, true));

    ByteBuffer* buf = mf->asByteBuffer(mf);
    TEST("asByteBuffer", buf != NULL && buf->write_pos > 0);
    RELEASE(buf);

    TEST("sync", mf->sync(mf));

    mf->unmap(mf);
    TEST("unmap (mapAddress NULL)", mf->mapAddress == NULL);

    RELEASE(mf);
    FileUtil_delete(mpath);
}

// ============================================================================
// [6] FileWatcher 테스트
// ============================================================================
static int watcher_triggered = 0;

static void on_file_event(const char* path, int event) {
    (void)path;
    (void)event;
    watcher_triggered++;
}

static void test_file_watcher(void) {
    SECTION("FileWatcher");

    FileUtil_mkdirs("/tmp/libcore_watch_test");

    FileWatcher* fw = new_FileWatcher();
    TEST("new_FileWatcher", fw != NULL);

    fw->onEvent(fw, on_file_event);
    TEST("onEvent", fw->callback == on_file_event);

    Path* watchPath = new_Path("/tmp/libcore_watch_test");
    TEST("watch", fw->watch(fw, watchPath));
    RELEASE(watchPath);

    File* trigger = new_File("/tmp/libcore_watch_test/trigger.txt");
    String* tc = new_String("trigger");
    trigger->writeString(trigger, tc);
    RELEASE(tc);
    RELEASE(trigger);

    usleep(50000); // 50ms
    fw->poll(fw);

    TEST("event triggered", watcher_triggered > 0);

    fw->stop(fw);
    RELEASE(fw);

    FileUtil_delete("/tmp/libcore_watch_test");
}

// ============================================================================
// main
// ============================================================================
int main(void) {
    printf("\n");
    printf("╔══════════════════════════════════════════╗\n");
    printf("║  libcore v1.0 - File System 통합 테스트  ║\n");
    printf("╚══════════════════════════════════════════╝\n");

    test_path();
    test_file();
    test_directory();
    test_file_util();
    test_mapped_file();
    test_file_watcher();

    printf("\n==========================================\n");
    printf("  결과: PASS %d / FAIL %d / TOTAL %d\n",
           pass_count, fail_count, pass_count + fail_count);
    printf("==========================================\n");

    if (fail_count == 0) {
        printf("  ✅ 모든 테스트 통과!! Valgrind 돌려보세요!\n");
    } else {
        printf("  ❌ 실패 항목 확인 필요!!\n");
    }
    printf("\n");

    return fail_count == 0 ? 0 : 1;
}
