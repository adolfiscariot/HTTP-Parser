CC = clang

CFLAGS = -Wall -Werror -Wextra -Wpedantic -O0 \
         -g \
         -fsanitize=address,undefined \
         -fno-omit-frame-pointer



SRC = src/parser.c
TEST = tests/test_parser.c

BUILD_DIR = build
TEST_BIN = $(BUILD_DIR)/test_parser

all: $(TEST_BIN)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TEST_BIN): $(SRC) $(TEST) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SRC) $(TEST) -o $(TEST_BIN)

run: $(TEST_BIN)
	LSAN_OPTIONS=verbosity=1:log_threads=1 ./$(TEST_BIN)

clean:
	rm -rf $(BUILD_DIR)

analyze:
	clang --analyze $(SRC)

format:
	clang-format -i src/*.c tests/*.c

.PHONY: all clean run analyze format
