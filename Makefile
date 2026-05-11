# Kaleidoscope — LLVM tutorial frontend
# Requires LLVM with llvm-config on PATH (e.g. brew install llvm)

LLVM_CONFIG ?= llvm-config

# Fail fast if LLVM_CONFIG is wrong (avoid silent empty CXXFLAGS / stale “Nothing to be done”).
ifneq ($(shell $(LLVM_CONFIG) --version >/dev/null 2>&1 && echo ok),ok)
$(error LLVM_CONFIG "$(LLVM_CONFIG)" not found or not runnable. Example: LLVM_CONFIG="$$(brew --prefix llvm)/bin/llvm-config" or put llvm-config on PATH and run make without LLVM_CONFIG=)
endif

BUILD_DIR := build
EXE       := kaleidoscope
TARGET    := $(BUILD_DIR)/$(EXE)
KAL_FILE  ?= examples/aot_entry.kal
AOT_RUNNER := $(BUILD_DIR)/aot_runner

CXX      := $(shell $(LLVM_CONFIG) --bindir)/clang++
CC       := $(shell $(LLVM_CONFIG) --bindir)/clang
CXXFLAGS := -std=c++20 -O1 -Wall -Wextra -Wno-unused-parameter -Isrc
SRCS := src/Lexer.cpp src/Parser.cpp src/Codegen.cpp \
	src/passes/AlgebraicSimplifyPass.cpp src/main.cpp

LLVM_CXXFLAGS := $(shell $(LLVM_CONFIG) --cxxflags)
LLVM_LDFLAGS  := $(shell $(LLVM_CONFIG) --ldflags --system-libs)
LLVM_LIBS     := $(shell $(LLVM_CONFIG) --libs core mcjit native orcjit)

.PHONY: all clean check-llvm run-aot aot-link

all: $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TARGET): $(SRCS) | $(BUILD_DIR)
	$(CXX) $(LLVM_CXXFLAGS) $(CXXFLAGS) $(SRCS) $(LLVM_LDFLAGS) $(LLVM_LIBS) -o $(TARGET)

clean:
	rm -rf $(BUILD_DIR)
	rm -f toy kaleidoscope output.o

check-llvm:
	@$(LLVM_CONFIG) --version && echo "LLVM OK ($(LLVM_CONFIG))"


aot-link: $(TARGET) | $(BUILD_DIR)
	./$(TARGET) < $(KAL_FILE)
	$(CC) $(BUILD_DIR)/output.o src/runtime.c src/aot_runner.c -o $(AOT_RUNNER)

run-aot: aot-link
	./$(AOT_RUNNER)
