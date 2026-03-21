# fandex - Makefile

.PHONY: all help clean install fmt
.DEFAULT_GOAL := all

# Compiler
CC = gcc

# Build directory
BUILDDIR ?= build

# Installation paths (always ~/.local, XDG-aware)
HOME_DIR := $(shell echo $$HOME)
bindir    = $(HOME_DIR)/.local/bin

# Warning flags
WARNING_FLAGS = -Wall -Wextra -Wshadow \
  -Wstrict-prototypes -Wmissing-prototypes -Wwrite-strings \
  -Wformat=2 -Wconversion -Wcast-qual -Wundef \
  -Wdate-time -Winit-self -Wstrict-overflow=2 \
  -Wimplicit-fallthrough -Walloca -Wvla \
  -Wnull-dereference -Wdouble-promotion -Werror

# Security hardening
SECURITY_FLAGS = -fstack-protector-strong

# Dependency generation
DEP_FLAGS = -MMD -MP

# Build mode flags
DEBUG_FLAGS = -O0 -g3 -fno-omit-frame-pointer -DDEBUG
RELEASE_FLAGS = -O2 -g -DNDEBUG -D_FORTIFY_SOURCE=2
SANITIZE_FLAGS = -fsanitize=address,undefined
TSAN_FLAGS = -fsanitize=thread
VALGRIND_FLAGS = -O0 -g3 -fno-omit-frame-pointer -DDEBUG
COVERAGE_FLAGS = -O0 -g3 -fprofile-arcs -ftest-coverage

# Base flags
BASE_FLAGS = -std=c17 -fPIC -D_GNU_SOURCE -Isrc -Ivendor

# Build type selection (debug, release, sanitize, tsan, or valgrind)
BUILD ?= debug

ifeq ($(BUILD),release)
  MODE_FLAGS = $(RELEASE_FLAGS)
else ifeq ($(BUILD),sanitize)
  MODE_FLAGS = $(DEBUG_FLAGS) $(SANITIZE_FLAGS)
else ifeq ($(BUILD),tsan)
  MODE_FLAGS = $(DEBUG_FLAGS) $(TSAN_FLAGS)
else ifeq ($(BUILD),valgrind)
  MODE_FLAGS = $(VALGRIND_FLAGS)
else ifeq ($(BUILD),coverage)
  MODE_FLAGS = $(DEBUG_FLAGS) $(COVERAGE_FLAGS)
else
  MODE_FLAGS = $(DEBUG_FLAGS)
endif

# Diagnostic flags for cleaner error output
DIAG_FLAGS = -fmax-errors=1 -fno-diagnostics-show-caret

# Linker flags (varies by BUILD mode)
ifeq ($(BUILD),sanitize)
  LDFLAGS = -fsanitize=address,undefined -Wl,--gc-sections
else ifeq ($(BUILD),tsan)
  LDFLAGS = -fsanitize=thread -Wl,--gc-sections
else
  LDFLAGS = -Wl,--gc-sections
endif

# Linker libraries
LDLIBS = -ltalloc

# Function/data sections for gc-sections
SECTION_FLAGS = -ffunction-sections -fdata-sections

# Combined compiler flags
CFLAGS = $(BASE_FLAGS) $(WARNING_FLAGS) $(SECURITY_FLAGS) $(MODE_FLAGS) $(DEP_FLAGS) $(DIAG_FLAGS) $(SECTION_FLAGS)

# Relaxed flags for vendor files
VENDOR_CFLAGS = $(BASE_FLAGS) $(SECURITY_FLAGS) $(MODE_FLAGS) $(DEP_FLAGS) $(DIAG_FLAGS) -Wno-conversion

# Discover all source files
SRC_FILES = $(shell find src -name '*.c' 2>/dev/null)
TEST_FILES = $(shell find tests -name '*.c' 2>/dev/null)
VENDOR_FILES = $(shell find vendor -name '*.c' 2>/dev/null)

# Convert to object files
SRC_OBJECTS = $(patsubst src/%.c,$(BUILDDIR)/src/%.o,$(SRC_FILES))
TEST_OBJECTS = $(patsubst tests/%.c,$(BUILDDIR)/tests/%.o,$(TEST_FILES))
VENDOR_OBJECTS = $(patsubst vendor/%.c,$(BUILDDIR)/vendor/%.o,$(VENDOR_FILES))

# Module objects for tests (all src objects EXCEPT main.o + vendor)
MODULE_OBJ = $(filter-out $(BUILDDIR)/src/main.o,$(SRC_OBJECTS)) $(VENDOR_OBJECTS)

# Discover test binaries
UNIT_TEST_BINARIES = $(patsubst tests/%.c,$(BUILDDIR)/tests/%,$(shell find tests/unit -name '*_test.c' 2>/dev/null))

# All objects
ALL_OBJECTS = $(SRC_OBJECTS) $(TEST_OBJECTS) $(VENDOR_OBJECTS)

# Parallel execution settings
MAKE_JOBS ?= $(shell nproc=$(shell nproc); echo $$((nproc / 2)))
MAKEFLAGS += --output-sync=line
MAKEFLAGS += --no-print-directory

# =============================================================================
# Pattern rules
# =============================================================================

# Compile source files from src/
$(BUILDDIR)/src/%.o: src/%.c
	@mkdir -p $(dir $@)
	@if $(CC) $(CFLAGS) -c $< -o $@ 2>&1; then \
		echo "🟢 $<"; \
	else \
		echo "🔴 $<"; \
		exit 1; \
	fi

# Compile test files from tests/
$(BUILDDIR)/tests/%.o: tests/%.c
	@mkdir -p $(dir $@)
	@if $(CC) $(CFLAGS) -c $< -o $@ 2>&1; then \
		echo "🟢 $<"; \
	else \
		echo "🔴 $<"; \
		exit 1; \
	fi

# Compile vendor files with relaxed warnings
$(BUILDDIR)/vendor/%.o: vendor/%.c
	@mkdir -p $(dir $@)
	@if $(CC) $(VENDOR_CFLAGS) -c $< -o $@ 2>&1; then \
		echo "🟢 $<"; \
	else \
		echo "🔴 $<"; \
		exit 1; \
	fi

# Include dependency files
-include $(ALL_OBJECTS:.o=.d)

# Include check targets
include .make/check-compile.mk
include .make/check-link.mk
include .make/check-unit.mk
include .make/check-filesize.mk
include .make/check-complexity.mk
include .make/check-sanitize.mk
include .make/check-tsan.mk
include .make/check-valgrind.mk
include .make/check-helgrind.mk

# =============================================================================
# Main binary
# =============================================================================

bin/fandex: $(SRC_OBJECTS) $(VENDOR_OBJECTS)
	@mkdir -p $(dir $@)
	@if $(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS) 2>&1; then \
		echo "🟢 $@"; \
	else \
		echo "🔴 $@"; \
		exit 1; \
	fi

# =============================================================================
# Test binaries
# =============================================================================

$(BUILDDIR)/tests/unit/%_test: $(BUILDDIR)/tests/unit/%_test.o $(MODULE_OBJ)
	@mkdir -p $(dir $@)
	@if $(CC) $(LDFLAGS) -o $@ $< $(MODULE_OBJ) -lcheck -lm -lsubunit $(LDLIBS) 2>&1; then \
		echo "🟢 $@"; \
	else \
		echo "🔴 $@"; \
		exit 1; \
	fi

# =============================================================================
# Targets
# =============================================================================

# all: Build main binary
all:
	@$(MAKE) -k -j$(MAKE_JOBS) $(SRC_OBJECTS) $(VENDOR_OBJECTS) 2>&1 | grep -E "^(🟢|🔴)" || true
	@$(MAKE) -k -j$(MAKE_JOBS) bin/fandex 2>&1 | grep -E "^(🟢|🔴)" || true
	@if [ -f bin/fandex ]; then \
		echo "✅ Build complete"; \
	else \
		echo "❌ Build failed"; \
		exit 1; \
	fi

# fmt: Format all source files with uncrustify (excludes vendor/)
fmt:
	@find src/ tests/ -name '*.c' -o -name '*.h' | \
		xargs uncrustify -c .uncrustify.cfg --no-backup -q
	@echo "✨ Formatted"

# clean: Remove build artifacts
clean:
	@rm -rf $(BUILDDIR) build-sanitize build-tsan build-valgrind build-helgrind build-coverage reports/ bin/
	@find src/ tests/ vendor/ -name '*.d' -delete 2>/dev/null || true
	@echo "✨ Cleaned"

# install: Install binary to ~/.local/bin
install: all
	install -d $(bindir)
	install -m 755 bin/fandex $(bindir)/fandex
	sudo setcap cap_sys_admin+ep $(bindir)/fandex
	@echo "Installed to $(bindir)/fandex"

# help: Show available targets
help:
	@echo "Available targets:"
	@echo "  all              - Build main binary (default)"
	@echo "  check-compile    - Compile all source files to .o files"
	@echo "  check-link       - Link all binaries (main + tests)"
	@echo "  check-unit       - Run unit tests"
	@echo "  check-filesize   - Verify source files under 16KB"
	@echo "  check-complexity - Verify cyclomatic complexity under threshold"
	@echo "  fmt              - Format source files with uncrustify"
	@echo "  install          - Install to ~/.local/bin"
	@echo "  clean            - Remove build artifacts"
	@echo "  help             - Show this help"
	@echo ""
	@echo ""
	@echo "Extended quality checks:"
	@echo "  check-sanitize   - Run tests with AddressSanitizer/UBSan"
	@echo "  check-tsan       - Run tests with ThreadSanitizer"
	@echo "  check-valgrind   - Run tests under Valgrind Memcheck"
	@echo "  check-helgrind   - Run tests under Valgrind Helgrind"
	@echo ""
	@echo "Build modes (BUILD=<mode>):"
	@echo "  debug            - Debug build with symbols (default)"
	@echo "  release          - Optimized release build"
	@echo "  sanitize         - Debug build with address/undefined sanitizers"
	@echo "  tsan             - Debug build with thread sanitizer"
	@echo "  valgrind         - Debug build optimized for Valgrind"
	@echo "  coverage         - Debug build with coverage instrumentation"
	@echo ""
	@echo "Examples:"
	@echo "  make                                         - Build in debug mode"
	@echo "  make BUILD=release                           - Build in release mode"
	@echo "  make check-unit                              - Run unit tests"
	@echo "  make check-unit FILE=build/tests/unit/hello_test  - Run single test"
