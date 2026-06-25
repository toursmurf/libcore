# ==========================================
# [1] liburing 자동 감지 (Feature Detection)
# ==========================================
# pkg-config를 사용하여 시스템에 liburing이 설치되어 있는지 확인합니다.
HAS_URING := $(shell pkg-config --exists liburing 2>/dev/null && echo 1 || echo 0)

CC = gcc
CFLAGS = -Wall -Wextra  -Wunused-value   -O2 -D_FORTIFY_SOURCE=2  -fsanitize=address,undefined   -fstack-protector-strong    -pthread  -I/usr/include/mysql -I/usr/include/mysql/mysql
SRC_DIR = src
INC_DIR = include
LIB_DIR = lib
TEST_DIR = tests
EXAMPLE_DIR = examples

# 🚨 기본 라이브러리에서 하드코딩된 -luring을 제거했습니다!
LIBS = -L/usr/lib64/ -lmariadb -lcurl -lssl -lcrypto -lrt

# ==========================================
# [2] 환경별 컴파일 옵션 (CFLAGS, LIBS) 동적 할당
# ==========================================
ifeq ($(HAS_URING),1)
    $(info 🚀 [Build] liburing 감지됨! io_uring 초고속 백엔드로 빌드합니다!)
    CFLAGS += -DHAS_LIBURING
    LIBS += -luring
else
    $(info 🛡️ [Build] liburing 없음! epoll 안전 백엔드로 폴백합니다!)
endif

# 코어 라이브러리 소스 및 오브젝트
SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(SRCS:.c=.o)

# 예제 소스 및 실행 파일 목록 동적 생성!
EXAMPLE_SRCS = $(wildcard $(EXAMPLE_DIR)/*.c)
EXAMPLE_BINS = $(EXAMPLE_SRCS:.c=)

.PHONY: all clean test examples

all: $(LIB_DIR)/libcore.a

$(LIB_DIR)/libcore.a: $(OBJS)
	mkdir -p $(LIB_DIR)
	ar rcs $@ $(OBJS)
	@echo "✅ libcore.a 빌드 완료!"

$(SRC_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -I$(INC_DIR) -c $< -o $@

test: $(LIB_DIR)/libcore.a
	mkdir -p $(TEST_DIR)
	$(CC) $(CFLAGS) -I$(INC_DIR) $(EXAMPLE_DIR)/all_test_v2.c $(LIB_DIR)/libcore.a -o $(TEST_DIR)/run_test -lm
	@echo "🔥 테스트 실행!"
	./$(TEST_DIR)/run_test

# ----- 🚀 예제 컴파일 타겟 수정 부분 -----
examples: $(EXAMPLE_BINS)
	@echo "🚀 모든 예제 컴파일 완벽 성공!"

# 개별 예제 파일을 실행 파일로 컴파일하는 패턴 룰!
# 🚨 링커 에러를 방지하기 위해 $(LIBS)의 위치를 오브젝트 뒤로 안전하게 배치했습니다!
$(EXAMPLE_DIR)/%: $(EXAMPLE_DIR)/%.c $(LIB_DIR)/libcore.a
	@echo "🛠️  Building 예제: $@"
	$(CC) $(CFLAGS) -I$(INC_DIR) $< $(LIB_DIR)/libcore.a $(LIBS) -o $@ -lm
# ----------------------------------------

clean:
	rm -f $(SRC_DIR)/*.o
	rm -f $(LIB_DIR)/libcore.a
	rm -f $(TEST_DIR)/run_test
	rm -f $(EXAMPLE_BINS)
	@echo "🧹 클린 완료!"