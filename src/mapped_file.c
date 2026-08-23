#define _GNU_SOURCE
#include "mapped_file.h"
#include "object.h"      // Object 시스템 연동
#include "path.h"        // Path 시스템 연동
#include "bytebuffer.h"  // ByteBuffer 연동
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <errno.h>

// ============================================================================
// 1. finalize (ARC)
// ============================================================================
static void MappedFile_finalize(Object * obj) { // 인자 타입을 void*로 통일
    MappedFile* self = (MappedFile*)obj;
    if (!self) return;

    // 매핑 해제 및 리소스 정리
    if (self->mapAddress && self->mappedSize > 0) {
        munmap(self->mapAddress, self->mappedSize);
        self->mapAddress = NULL; 
        self->mappedSize = 0;
    }

    if (self->targetPath) { 
        // [경고 해결] RELEASE 명칭 통일 및 캐스팅
        RELEASE((Object*)self->targetPath); 
        self->targetPath = NULL; 
    }
}

// ============================================================================
// 2. map (메모리 매핑 실행)
// ============================================================================
static bool MappedFile_map(MappedFile* self, bool readOnly) {
    if (!self || !self->targetPath || self->mapAddress) return false;

    int fd = open(self->targetPath->path, readOnly ? O_RDONLY : O_RDWR);
    if (fd < 0) return false;

    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size == 0) { 
        close(fd); 
        return false; 
    }

    int prot = readOnly ? PROT_READ : (PROT_READ | PROT_WRITE);
    void* addr = mmap(NULL, (size_t)st.st_size, prot, MAP_SHARED, fd, 0);
    close(fd);

    if (addr == MAP_FAILED) return false;

    self->mapAddress = addr; 
    self->mappedSize = (size_t)st.st_size;
    return true;
}

// ============================================================================
// 3. unmap (매핑 해제)
// ============================================================================
static void MappedFile_unmap(MappedFile* self) {
    if (!self || !self->mapAddress) return;
    
    munmap(self->mapAddress, self->mappedSize);
    self->mapAddress = NULL; 
    self->mappedSize = 0;
}

// ============================================================================
// 4. sync (msync)
// ============================================================================
static bool MappedFile_sync(MappedFile* self) {
    if (!self || !self->mapAddress || self->mappedSize == 0) return false;
    
    // msync를 통해 메모리 변경사항을 디스크로 강제 동기화
    return (msync(self->mapAddress, self->mappedSize, MS_SYNC) == 0);
}

// ============================================================================
// 5. asByteBuffer (ByteBuffer 변환 - v1.0은 Copy 방식)
// ============================================================================
static ByteBuffer* MappedFile_asByteBuffer(MappedFile* self) {
    if (!self || !self->mapAddress || self->mappedSize == 0) return NULL;

    ByteBuffer* buf = new_ByteBuffer(self->mappedSize);
    if (!buf) return NULL;

    // [경고 해결] void* mapAddress를 uint8_t* 기대 인자에 맞춰 캐스팅 (필요 시)
    // ByteBuffer->write 인자가 void* 인지 uint8_t* 인지에 따라 맞춰줌
    if (buf->write(buf, (const uint8_t*)self->mapAddress, self->mappedSize) != 0) {
        RELEASE((Object*)buf); 
        return NULL;
    }

    return buf;
}

// ============================================================================
// MappedFile_Class 정의 (런타임 사이즈 누락 해결!!)
// ============================================================================
const Class MappedFile_Class = {
    .name = "MappedFile",
    .size = sizeof(MappedFile),
    .finalize = MappedFile_finalize
};

// ============================================================================
// 6. 생성자 (Constructor)
// ============================================================================
MappedFile* new_MappedFile(const char* pathStr) {
    if (!pathStr) return NULL;

    MappedFile* self = (MappedFile*)calloc(1, sizeof(MappedFile));
    if (!self) return NULL;

    // [경고 해결] Object_Init 명칭 통일 및 캐스팅
    Object_Init((Object*)self, &MappedFile_Class);

    // 💡 new_Path가 ref=1로 반환하므로 현재 구조에서는 온전한 소유권 인정 (OK!!)
    self->targetPath = new_Path(pathStr);
    if (!self->targetPath) {
        free(self);
        return NULL;
    }

    self->mapAddress = NULL;
    self->mappedSize = 0;

    // VTable 매핑
    self->map          = MappedFile_map;
    self->unmap        = MappedFile_unmap;
    self->sync         = MappedFile_sync;
    self->asByteBuffer = MappedFile_asByteBuffer;

    return self;
}