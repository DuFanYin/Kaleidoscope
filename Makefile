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
RT_OBJ    := $(BUILD_DIR)/host_runtime.o
KAL_FILE  ?= examples/pricing.kal
AOT_RUNNER := $(BUILD_DIR)/aot_runner
EMBED_SMOKE := $(BUILD_DIR)/embed_smoke

CXX      := $(shell $(LLVM_CONFIG) --bindir)/clang++
CC       := $(shell $(LLVM_CONFIG) --bindir)/clang
CXXFLAGS := -std=c++20 -O1 -Wall -Wextra -Wno-unused-parameter -Isrc -Iinclude

CORE_SRCS := src/frontend/Lexer.cpp src/frontend/Parser.cpp src/backend/Codegen.cpp \
	src/backend/passes/AlgebraicSimplifyPass.cpp src/driver/Driver.cpp src/host/HostABI.cpp \
	src/embed/Embed.cpp
MAIN_SRC  := src/main.cpp
SRCS      := $(CORE_SRCS) $(MAIN_SRC)
TOOLS_EMBED := tools/embed_smoke.cpp

LLVM_CXXFLAGS := $(shell $(LLVM_CONFIG) --cxxflags)
LLVM_LDFLAGS  := $(shell $(LLVM_CONFIG) --ldflags --system-libs)
LLVM_LIBS     := $(shell $(LLVM_CONFIG) --libs core mcjit native orcjit)

.PHONY: all clean check-llvm run-aot aot-link embed-smoke

all: $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(RT_OBJ): src/host/runtime.c | $(BUILD_DIR)
	$(CC) -Wall -Wextra -Iinclude -Isrc -c src/host/runtime.c -o $(RT_OBJ)

$(TARGET): $(SRCS) $(RT_OBJ) | $(BUILD_DIR)
	$(CXX) $(LLVM_CXXFLAGS) $(CXXFLAGS) $(SRCS) $(RT_OBJ) $(LLVM_LDFLAGS) $(LLVM_LIBS) -o $(TARGET)

$(EMBED_SMOKE): $(TOOLS_EMBED) $(CORE_SRCS) $(RT_OBJ) | $(BUILD_DIR)
	$(CXX) $(LLVM_CXXFLAGS) $(CXXFLAGS) $(TOOLS_EMBED) $(CORE_SRCS) $(RT_OBJ) $(LLVM_LDFLAGS) $(LLVM_LIBS) -o $(EMBED_SMOKE)

embed-smoke: $(EMBED_SMOKE)

clean:
	rm -rf $(BUILD_DIR)
	rm -f toy kaleidoscope output.o

check-llvm:
	@$(LLVM_CONFIG) --version && echo "LLVM OK ($(LLVM_CONFIG))"


aot-link: $(TARGET) | $(BUILD_DIR)
	./$(TARGET) < $(KAL_FILE)
	$(CC) -Iinclude -Isrc $(BUILD_DIR)/output.o src/host/runtime.c src/host/aot_runner.c -o $(AOT_RUNNER)

run-aot: aot-link
	./$(AOT_RUNNER)
