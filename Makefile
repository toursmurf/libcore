CC = gcc
CFLAGS = -Wall -Wextra  -Wunused-value   -O2 -D_FORTIFY_SOURCE=2 -fstack-protector-strong    -pthread  -I/usr/include/mysql -I/usr/include/mysql/mysql  -fsanitize=address  -fsanitize=undefined
SRC_DIR = src
INC_DIR = include
LIB_DIR = lib
TEST_DIR = tests
EXAMPLE_DIR = examples
CLIBS=-L/usr/lib64/ -lmariadb  -lcurl

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
$(EXAMPLE_DIR)/%: $(EXAMPLE_DIR)/%.c $(LIB_DIR)/libcore.a
	@echo "🛠️  Building 예제: $@"
	$(CC) $(CFLAGS) -I$(INC_DIR) $(CLIBS)  $< $(LIB_DIR)/libcore.a -o $@ -lm
# ----------------------------------------

clean:
	rm -f $(SRC_DIR)/*.o
	rm -f $(LIB_DIR)/libcore.a
	rm -f $(TEST_DIR)/run_test
	rm -f $(EXAMPLE_BINS)
	@echo "🧹 클린 완료!"
