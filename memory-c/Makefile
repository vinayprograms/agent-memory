# Memory Service Makefile
#
# Targets:
#   make          - Build the service
#   make test     - Run all tests
#   make test-unit - Run unit tests only
#   make coverage - Run tests with coverage
#   make clean    - Clean build artifacts
#   make debug    - Build with debug/sanitizer flags

# Compiler settings
CC := gcc
CFLAGS := -Wall -Wextra -Werror -std=c11 -D_GNU_SOURCE
CFLAGS += -I$(CURDIR)/include -I$(CURDIR)/src

# Platform detection
UNAME_S := $(shell uname -s)
UNAME_M := $(shell uname -m)

ifeq ($(UNAME_S),Darwin)
    PLATFORM := darwin
    # macOS: use clang flags, no -march=native on ARM
    ifeq ($(UNAME_M),arm64)
        CFLAGS += -mcpu=apple-m1
    else
        CFLAGS += -march=native
    endif
    # macOS linker uses -rpath differently
    RPATH_FLAG = -Wl,-rpath,
else
    PLATFORM := linux
    # Linux: architecture-specific optimizations
    ifeq ($(UNAME_M),x86_64)
        CFLAGS += -mavx2
    else ifeq ($(UNAME_M),aarch64)
        CFLAGS += -march=armv8-a
    endif
    RPATH_FLAG = -Wl,-rpath,
endif

# Linker flags
LDFLAGS := -lpthread -lm

# Optional: LMDB
LMDB_CFLAGS := $(shell pkg-config --cflags lmdb 2>/dev/null)
LMDB_LDFLAGS := $(shell pkg-config --libs lmdb 2>/dev/null)
ifneq ($(LMDB_LDFLAGS),)
    CFLAGS += $(LMDB_CFLAGS) -DHAVE_LMDB
    LDFLAGS += $(LMDB_LDFLAGS)
endif

# Optional: libmicrohttpd
HTTPD_CFLAGS := $(shell pkg-config --cflags libmicrohttpd 2>/dev/null)
HTTPD_LDFLAGS := $(shell pkg-config --libs libmicrohttpd 2>/dev/null)
ifneq ($(HTTPD_LDFLAGS),)
    CFLAGS += $(HTTPD_CFLAGS) -DHAVE_MICROHTTPD
    LDFLAGS += $(HTTPD_LDFLAGS)
endif

# Optional: ONNX Runtime
# Auto-detect common installation paths, or set ONNXRUNTIME_ROOT manually
ifndef ONNXRUNTIME_ROOT
    # Try pkg-config first
    ONNX_PKG := $(shell pkg-config --exists libonnxruntime 2>/dev/null && echo yes)
    ifeq ($(ONNX_PKG),yes)
        ONNXRUNTIME_ROOT := pkg-config
    else
        # Auto-detect common paths
        ifeq ($(UNAME_S),Darwin)
            # macOS: check Homebrew paths
            ifeq ($(UNAME_M),arm64)
                # Apple Silicon Homebrew
                ifneq ($(wildcard /opt/homebrew/opt/onnxruntime/lib/libonnxruntime.dylib),)
                    ONNXRUNTIME_ROOT := /opt/homebrew/opt/onnxruntime
                else ifneq ($(wildcard /opt/homebrew/lib/libonnxruntime.dylib),)
                    ONNXRUNTIME_ROOT := /opt/homebrew
                endif
            else
                # Intel Homebrew
                ifneq ($(wildcard /usr/local/opt/onnxruntime/lib/libonnxruntime.dylib),)
                    ONNXRUNTIME_ROOT := /usr/local/opt/onnxruntime
                else ifneq ($(wildcard /usr/local/lib/libonnxruntime.dylib),)
                    ONNXRUNTIME_ROOT := /usr/local
                endif
            endif
        else
            # Linux: check common paths
            ifneq ($(wildcard /usr/local/lib/libonnxruntime.so),)
                ONNXRUNTIME_ROOT := /usr/local
            else ifneq ($(wildcard /usr/lib/libonnxruntime.so),)
                ONNXRUNTIME_ROOT := /usr
            else ifneq ($(wildcard /usr/lib/x86_64-linux-gnu/libonnxruntime.so),)
                # Debian/Ubuntu multiarch
                ONNXRUNTIME_ROOT := debian-multiarch
            endif
        endif
    endif
endif

# Configure ONNX Runtime based on detected/specified path
ifeq ($(ONNXRUNTIME_ROOT),pkg-config)
    ONNX_CFLAGS := $(shell pkg-config --cflags libonnxruntime 2>/dev/null)
    ONNX_LDFLAGS := $(shell pkg-config --libs libonnxruntime 2>/dev/null)
    CFLAGS += $(ONNX_CFLAGS) -DHAVE_ONNXRUNTIME
    LDFLAGS += $(ONNX_LDFLAGS)
else ifeq ($(ONNXRUNTIME_ROOT),debian-multiarch)
    CFLAGS += -DHAVE_ONNXRUNTIME
    LDFLAGS += -lonnxruntime
else ifdef ONNXRUNTIME_ROOT
    # Check for Homebrew-style layout (include/onnxruntime/) vs standard (include/)
    ifneq ($(wildcard $(ONNXRUNTIME_ROOT)/include/onnxruntime/),)
        # Homebrew layout: headers in include/onnxruntime/
        CFLAGS += -I$(ONNXRUNTIME_ROOT)/include/onnxruntime -DHAVE_ONNXRUNTIME
    else
        # Standard layout: headers in include/
        CFLAGS += -I$(ONNXRUNTIME_ROOT)/include -DHAVE_ONNXRUNTIME
    endif
    LDFLAGS += -L$(ONNXRUNTIME_ROOT)/lib -lonnxruntime $(RPATH_FLAG)$(ONNXRUNTIME_ROOT)/lib
endif

# Directories
SRC_DIR := src
BUILD_DIR := build
TEST_DIR := tests
OBJ_DIR := $(BUILD_DIR)/obj
BIN_DIR := $(BUILD_DIR)/bin
COV_DIR := $(BUILD_DIR)/coverage

# Platform source file (must be after SRC_DIR is defined)
PLATFORM_SRC := $(SRC_DIR)/platform/platform_$(PLATFORM).c

# Source files (excluding main.c for tests)
CORE_SRCS := $(wildcard $(SRC_DIR)/core/*.c)
UTIL_SRCS := $(wildcard $(SRC_DIR)/util/*.c)
STORAGE_SRCS := $(wildcard $(SRC_DIR)/storage/*.c)
EMBEDDING_SRCS := $(wildcard $(SRC_DIR)/embedding/*.c)
SEARCH_SRCS := $(wildcard $(SRC_DIR)/search/*.c)
API_SRCS := $(wildcard $(SRC_DIR)/api/*.c)
SESSION_SRCS := $(wildcard $(SRC_DIR)/session/*.c)
EVENTS_SRCS := $(wildcard $(SRC_DIR)/events/*.c)

LIB_SRCS := $(CORE_SRCS) $(UTIL_SRCS) $(STORAGE_SRCS) $(EMBEDDING_SRCS) \
            $(SEARCH_SRCS) $(API_SRCS) $(SESSION_SRCS) $(EVENTS_SRCS) \
            $(PLATFORM_SRC)

# Third party
YYJSON_SRC := third_party/yyjson/yyjson.c
YYJSON_OBJ := $(OBJ_DIR)/yyjson.o

# Object files
LIB_OBJS := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(LIB_SRCS))
MAIN_OBJ := $(OBJ_DIR)/main.o

# Test files
UNIT_TEST_SRCS := $(wildcard $(TEST_DIR)/unit/*.c)
UNIT_TEST_BINS := $(patsubst $(TEST_DIR)/unit/%.c,$(BIN_DIR)/unit/%,$(UNIT_TEST_SRCS))

SYSTEM_TEST_SRCS := $(wildcard $(TEST_DIR)/system/*.c)
SYSTEM_TEST_BINS := $(patsubst $(TEST_DIR)/system/%.c,$(BIN_DIR)/system/%,$(SYSTEM_TEST_SRCS))

INTEG_TEST_SRCS := $(wildcard $(TEST_DIR)/integration/*.c)
INTEG_TEST_BINS := $(patsubst $(TEST_DIR)/integration/%.c,$(BIN_DIR)/integration/%,$(INTEG_TEST_SRCS))

# Main targets
TARGET := $(BIN_DIR)/memory-service
MCP_TARGET := $(BIN_DIR)/memory-mcp
MCP_OBJ := $(OBJ_DIR)/mcp.o

# Default target
.PHONY: all
all: release

# Release build
.PHONY: release
release: CFLAGS += -O2 -DNDEBUG
release: $(TARGET) $(MCP_TARGET)

# Debug build with sanitizers
.PHONY: debug
debug: CFLAGS += -O0 -g3 -DDEBUG
debug: CFLAGS += -fsanitize=address -fsanitize=undefined
debug: LDFLAGS += -fsanitize=address -fsanitize=undefined
debug: $(TARGET) $(MCP_TARGET)

# AddressSanitizer build (optimized with ASAN for finding memory bugs in release-like code)
.PHONY: asan
asan: CFLAGS += -O1 -g -DDEBUG
asan: CFLAGS += -fsanitize=address -fno-omit-frame-pointer -fno-optimize-sibling-calls
asan: LDFLAGS += -fsanitize=address
asan: $(TARGET) $(MCP_TARGET)
	@echo ""
	@echo "Built with AddressSanitizer enabled."
	@echo "Run with: ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 $(TARGET)"

# ThreadSanitizer build (for detecting data races)
.PHONY: tsan
tsan: CFLAGS += -O1 -g -DDEBUG
tsan: CFLAGS += -fsanitize=thread
tsan: LDFLAGS += -fsanitize=thread
tsan: $(TARGET) $(MCP_TARGET)
	@echo ""
	@echo "Built with ThreadSanitizer enabled."
	@echo "Run with: TSAN_OPTIONS=halt_on_error=1 $(TARGET)"

# Memory Sanitizer build (for detecting uninitialized reads - Clang only)
.PHONY: msan
msan: CC := clang
msan: CFLAGS += -O1 -g -DDEBUG
msan: CFLAGS += -fsanitize=memory -fno-omit-frame-pointer
msan: LDFLAGS += -fsanitize=memory
msan: $(TARGET) $(MCP_TARGET)
	@echo ""
	@echo "Built with MemorySanitizer enabled (requires Clang)."

# Main executable
$(TARGET): $(LIB_OBJS) $(MAIN_OBJ) $(YYJSON_OBJ) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# MCP executable (stdio-based MCP server)
$(MCP_TARGET): $(LIB_OBJS) $(MCP_OBJ) $(YYJSON_OBJ) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(MCP_OBJ): $(SRC_DIR)/mcp.c | $(OBJ_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

# Object files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJ_DIR)/main.o: $(SRC_DIR)/main.c | $(OBJ_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

# Third party
$(YYJSON_OBJ): $(YYJSON_SRC) | $(OBJ_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Wno-unused-parameter -c -o $@ $<

# Create directories
$(BUILD_DIR) $(OBJ_DIR) $(BIN_DIR) $(COV_DIR):
	@mkdir -p $@

$(BIN_DIR)/unit $(BIN_DIR)/system $(BIN_DIR)/integration:
	@mkdir -p $@

# Unit tests
$(BIN_DIR)/unit/%: $(TEST_DIR)/unit/%.c $(LIB_OBJS) $(YYJSON_OBJ) | $(BIN_DIR)/unit
	$(CC) $(CFLAGS) -I$(TEST_DIR) -o $@ $< $(filter-out $(MAIN_OBJ),$(LIB_OBJS)) $(YYJSON_OBJ) $(LDFLAGS)

# System tests
$(BIN_DIR)/system/%: $(TEST_DIR)/system/%.c $(LIB_OBJS) $(YYJSON_OBJ) | $(BIN_DIR)/system
	$(CC) $(CFLAGS) -I$(TEST_DIR) -o $@ $< $(filter-out $(MAIN_OBJ),$(LIB_OBJS)) $(YYJSON_OBJ) $(LDFLAGS)

# Integration tests
$(BIN_DIR)/integration/%: $(TEST_DIR)/integration/%.c $(LIB_OBJS) $(YYJSON_OBJ) | $(BIN_DIR)/integration
	$(CC) $(CFLAGS) -I$(TEST_DIR) -o $@ $< $(filter-out $(MAIN_OBJ),$(LIB_OBJS)) $(YYJSON_OBJ) $(LDFLAGS)

# Run unit tests
.PHONY: test-unit
test-unit: CFLAGS += -O0 -g3 -DDEBUG
test-unit: $(UNIT_TEST_BINS)
	@echo ""
	@echo "======================================"
	@echo "Running Unit Tests"
	@echo "======================================"
	@failed=0; \
	for test in $(UNIT_TEST_BINS); do \
		echo ""; \
		echo ">>> Running $$test..."; \
		if $$test; then \
			echo ">>> $$test: PASSED"; \
		else \
			echo ">>> $$test: FAILED"; \
			failed=1; \
		fi; \
	done; \
	exit $$failed

# Run system tests
.PHONY: test-system
test-system: CFLAGS += -O0 -g3 -DDEBUG
test-system: $(SYSTEM_TEST_BINS)
	@echo ""
	@echo "======================================"
	@echo "Running System Tests"
	@echo "======================================"
	@failed=0; \
	for test in $(SYSTEM_TEST_BINS); do \
		echo ""; \
		echo ">>> Running $$test..."; \
		if $$test; then \
			echo ">>> $$test: PASSED"; \
		else \
			echo ">>> $$test: FAILED"; \
			failed=1; \
		fi; \
	done; \
	exit $$failed

# Run integration tests
.PHONY: test-integration
test-integration: CFLAGS += -O0 -g3 -DDEBUG
test-integration: $(INTEG_TEST_BINS)
	@echo ""
	@echo "======================================"
	@echo "Running Integration Tests"
	@echo "======================================"
	@failed=0; \
	for test in $(INTEG_TEST_BINS); do \
		echo ""; \
		echo ">>> Running $$test..."; \
		if $$test; then \
			echo ">>> $$test: PASSED"; \
		else \
			echo ">>> $$test: FAILED"; \
			failed=1; \
		fi; \
	done; \
	exit $$failed

# Run all tests
.PHONY: test
test: test-unit test-system

# Coverage build and report
.PHONY: coverage
coverage: CFLAGS += -O0 -g3 --coverage -fprofile-arcs -ftest-coverage
coverage: LDFLAGS += --coverage
coverage: clean $(UNIT_TEST_BINS)
	@mkdir -p $(COV_DIR)
	@echo ""
	@echo "======================================"
	@echo "Running Tests with Coverage"
	@echo "======================================"
	@for test in $(UNIT_TEST_BINS); do \
		$$test || true; \
	done
	@echo ""
	@echo "======================================"
	@echo "Coverage Report"
	@echo "======================================"
	@gcov -o $(OBJ_DIR)/core $(SRC_DIR)/core/*.c 2>/dev/null || true
	@gcov -o $(OBJ_DIR)/util $(SRC_DIR)/util/*.c 2>/dev/null || true
	@gcov -o $(OBJ_DIR)/storage $(SRC_DIR)/storage/*.c 2>/dev/null || true
	@mv *.gcov $(COV_DIR)/ 2>/dev/null || true
	@echo ""
	@echo "Coverage files in $(COV_DIR)/"
	@# Summary
	@echo ""
	@echo "Coverage Summary:"
	@for f in $(COV_DIR)/*.gcov; do \
		if [ -f "$$f" ]; then \
			name=$$(basename $$f .gcov); \
			total=$$(grep -c ":" $$f 2>/dev/null || echo 0); \
			covered=$$(grep -c "[0-9]*:" $$f 2>/dev/null | grep -v "0:" | wc -l || echo 0); \
			if [ $$total -gt 0 ]; then \
				pct=$$((covered * 100 / total)); \
				echo "  $$name: $$pct%"; \
			fi; \
		fi; \
	done

# Clean
.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)
	rm -f *.gcov *.gcda *.gcno

# Install dependencies (Debian/Ubuntu)
.PHONY: deps
deps:
	@echo "Installing dependencies..."
	apt-get update
	apt-get install -y build-essential pkg-config liblmdb-dev libmicrohttpd-dev
	@echo "Downloading yyjson..."
	@mkdir -p third_party/yyjson
	@curl -sL https://raw.githubusercontent.com/ibireme/yyjson/master/src/yyjson.c -o third_party/yyjson/yyjson.c
	@curl -sL https://raw.githubusercontent.com/ibireme/yyjson/master/src/yyjson.h -o third_party/yyjson/yyjson.h
	@echo "Dependencies installed."

# Download yyjson only
.PHONY: yyjson
yyjson:
	@mkdir -p third_party/yyjson
	@echo "Downloading yyjson..."
	@curl -sL https://raw.githubusercontent.com/ibireme/yyjson/master/src/yyjson.c -o third_party/yyjson/yyjson.c
	@curl -sL https://raw.githubusercontent.com/ibireme/yyjson/master/src/yyjson.h -o third_party/yyjson/yyjson.h
	@echo "yyjson downloaded."

# Help
.PHONY: help
help:
	@echo "Memory Service Build System"
	@echo ""
	@echo "Targets:"
	@echo "  make            - Build release version"
	@echo "  make debug      - Build with debug flags and sanitizers (ASAN+UBSAN)"
	@echo "  make asan       - Build with AddressSanitizer (optimized, for memory bugs)"
	@echo "  make tsan       - Build with ThreadSanitizer (for data race detection)"
	@echo "  make msan       - Build with MemorySanitizer (requires Clang)"
	@echo "  make test       - Run all tests"
	@echo "  make test-unit  - Run unit tests only"
	@echo "  make test-system - Run system tests"
	@echo "  make coverage   - Run tests with coverage report"
	@echo "  make clean      - Clean build artifacts"
	@echo "  make deps       - Install dependencies (requires sudo)"
	@echo "  make yyjson     - Download yyjson library"
	@echo "  make help       - Show this help"
	@echo ""
	@echo "Sanitizer Usage:"
	@echo "  ASAN_OPTIONS=detect_leaks=1 ./build/bin/memory-service"
	@echo "  TSAN_OPTIONS=halt_on_error=1 ./build/bin/memory-service"

# Print variables for debugging
.PHONY: vars
vars:
	@echo "=== Platform ==="
	@echo "PLATFORM = $(PLATFORM)"
	@echo "UNAME_S = $(UNAME_S)"
	@echo "UNAME_M = $(UNAME_M)"
	@echo ""
	@echo "=== Dependencies ==="
	@echo "ONNXRUNTIME_ROOT = $(ONNXRUNTIME_ROOT)"
	@echo "LMDB = $(if $(LMDB_LDFLAGS),found,not found)"
	@echo "MICROHTTPD = $(if $(HTTPD_LDFLAGS),found,not found)"
	@echo ""
	@echo "=== Compiler ==="
	@echo "CC = $(CC)"
	@echo "CFLAGS = $(CFLAGS)"
	@echo "LDFLAGS = $(LDFLAGS)"
	@echo ""
	@echo "=== Sources ==="
	@echo "PLATFORM_SRC = $(PLATFORM_SRC)"
