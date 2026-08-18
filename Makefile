# Makefile for PS1 Emulator and log splitter

# Compiler and flags
CC = gcc
CXX = g++

# Common includes and libs.
#
# SDL3 comes from pkg-config on both sides, not a bare -lSDL3: the library is
# built from source into /usr/local (Ubuntu 24.04 and its derivatives ship no
# libsdl3-dev), so the linker needs the -L and the -rpath that sdl3.pc carries.
INCLUDES = -Iinclude -Ithird_party/imgui -Ithird_party/imgui/backends -Ithird_party/lua

SDL_CFLAGS = $(shell pkg-config --cflags sdl3)
SDL_LIBS   = $(shell pkg-config --libs sdl3)

LIBS = $(SDL_LIBS) -lGL -lGLEW -lm -lpthread

# Build mode. Default is an optimised build — the emulator is an interpreter on
# the hot path, so an unoptimised (-O0) build ran ~3-5x slower than the machine,
# left no headroom over the frame budget and drifted the moment any debug
# instrumentation was on. `make DEBUG=1` restores an -O0 build for stepping in gdb.
ifdef DEBUG
  OPT = -O0 -g
else
  OPT = -O3 -g -march=native -DNDEBUG $(LTO)
endif

# Link-time optimisation.
#
# The interpreter's hot path crosses translation units constantly: every
# instruction calls debugger_check_breakpoint (debugger.c) and every load and
# store calls debugger_check_read/write_watchpoint, all of which return
# immediately when nothing is set — a perf profile of a 30 s run put those three
# at 2.92% of all samples doing nothing at all. cpu_reg (cpu_registers.c) and
# mask_region (bus.c) are single-expression functions that cost another 1.90%
# purely in call overhead. Without LTO the compiler cannot see across the .o
# boundary to inline any of them.
#
# Measured on a 30 s Monsters & Co. run, median of 3, ZS1_FRAME_PROFILE only:
#   gcc-14, no LTO   emu 3.710 ms
#   gcc-13, no LTO   emu 3.495 ms
#   gcc-13, LTO      emu 3.225 ms   (-7.7% from LTO, -13.1% from the baseline)
# CPI stayed at 1.618 in all three, which is the check that the emulated machine
# did not change: CPI is a guest property and no host optimisation may move it.
#
# LTO needs ONE toolchain for both languages. This machine has gcc 14.2 but g++
# 13.3, and lto-wrapper refuses the mismatch outright:
#   "bytecode stream in file 'src/main.o' generated with LTO version 14.0
#    instead of the expected 13.1"
# So enable it only when the two majors agree, and say so at build time rather
# than failing the link — a default that does not build is not a default.
#
# Force either way with `make LTO="-flto=auto"` or `make LTO=`. To get it on a
# mismatched box, pick a matching pair:
#   make clean && make CC=gcc-13 CXX=g++-13
CC_MAJOR  := $(shell $(CC) -dumpversion 2>/dev/null | cut -d. -f1)
CXX_MAJOR := $(shell $(CXX) -dumpversion 2>/dev/null | cut -d. -f1)
ifeq ($(CC_MAJOR),$(CXX_MAJOR))
  LTO ?= -flto=auto
else
  LTO ?=
  $(info [build] LTO off: $(CC) is $(CC_MAJOR) but $(CXX) is $(CXX_MAJOR) — they must match.)
  $(info [build]   retry with: make clean && make CC=gcc-$(CXX_MAJOR) CXX=g++-$(CXX_MAJOR))
endif

# Header dependency tracking. Without it, `make` after editing anything in
# include/ relinked stale objects: a struct whose layout changed in one
# translation unit and not another produces a binary that segfaults or
# misbehaves silently, and the only reliable answer was `make clean && make`
# every time. -MMD writes a .d file listing the headers each object really
# includes (project headers only — system ones do not change under us), and
# -MP emits a phony target for each so deleting a header does not wedge the
# build with "no rule to make target".
DEPFLAGS = -MMD -MP

CFLAGS = -std=c99 $(OPT) -Wall -Wextra $(DEPFLAGS) $(INCLUDES) $(SDL_CFLAGS)
CXXFLAGS = -std=c++11 $(OPT) -Wall -Wextra $(DEPFLAGS) $(INCLUDES) $(SDL_CFLAGS)

# Build every translation unit at once by default. The tree is ~90 objects and
# they are independent; an explicit -j on the command line still wins, because
# make appends command-line flags after MAKEFLAGS.
NPROC := $(shell nproc 2>/dev/null || echo 4)
MAKEFLAGS += -j$(NPROC)

# --- Core / CPU ---
EMU_CPU_SRCS = \
    src/cpu/cpu_disasm.c src/cpu/cpu_init.c src/cpu/cpu_registers.c \
    src/cpu/cpu_bios.c src/cpu/cpu_exceptions.c src/cpu/cpu_icache.c \
    src/cpu/cpu_decode.c src/cpu/cpu_execution.c src/cpu/cpu_instructions.c

# --- System Core ---
EMU_CORE_SRCS = \
    src/core/bios.c src/core/interconnect.c src/core/bus.c src/core/bus_irq.c \
    src/core/ram.c src/core/dma.c src/core/timers.c src/core/sio.c \
    src/core/mdec.c src/core/controller.c src/core/event_scheduler.c src/core/pcdrv.c \
    src/core/debugger.c src/core/lua_debug.c src/core/system.c \
    src/core/frame_events.c src/core/savestate.c

# --- Lua 5.4 (vendored source, see third_party/lua/) ---
# lua.c/luac.c both define main() (would collide with src/main.c); loadlib.c
# implements dynamic C-module loading (package/require/dlopen) which a debug
# script has no legitimate use for — excluding it drops the -ldl requirement
# too. See lua_debug.c's lua_debug_init for the luaL_requiref() calls that
# replace luaL_openlibs() (which needs loadlib.c's luaopen_package).
EMU_LUA_SRCS = $(filter-out third_party/lua/lua.c third_party/lua/luac.c \
                 third_party/lua/loadlib.c third_party/lua/linit.c, \
                 $(wildcard third_party/lua/*.c))

# --- GPU ---
EMU_GPU_SRCS = \
    src/gpu/gpu.c src/gpu/gpu_helpers.c src/gpu/gpu_commands.c \
    src/gpu/renderer.c src/gpu/vram.c

# --- GTE ---
EMU_GTE_SRCS = \
    src/gte/gte.c src/gte/gte_ops.c

# --- CDROM ---
EMU_CDROM_SRCS = \
    src/cdrom/cdrom.c src/cdrom/cdrom_commands.c \
    src/cdrom/cdrom_disc.c src/cdrom/cdrom_audio.c

# --- SPU ---
EMU_SPU_SRCS = \
    src/spu/spu.c src/spu/spu_voice.c src/spu/spu_adsr.c \
    src/spu/spu_mixing.c src/spu/spu_dma.c src/spu/spu_irq.c \
    src/spu/spu_stretch.c

# --- Utils ---
EMU_UTIL_SRCS = \
    src/utils/log.c src/utils/rxi_log.c

# --- Emulator C sources (aggregated) ---
EMU_C_SRCS = src/main.c \
    $(EMU_CPU_SRCS) $(EMU_CORE_SRCS) $(EMU_GPU_SRCS) \
    $(EMU_GTE_SRCS) $(EMU_CDROM_SRCS) $(EMU_SPU_SRCS) $(EMU_UTIL_SRCS) $(EMU_LUA_SRCS)

# --- Emulator C++ sources ---
EMU_CXX_SRCS = src/debug_ui.cpp \
               third_party/imgui/imgui.cpp \
               third_party/imgui/imgui_draw.cpp \
               third_party/imgui/imgui_widgets.cpp \
               third_party/imgui/imgui_tables.cpp \
               third_party/imgui/backends/imgui_impl_sdl3.cpp \
               third_party/imgui/backends/imgui_impl_opengl3.cpp

EMU_OBJS = $(EMU_C_SRCS:.c=.o) $(EMU_CXX_SRCS:.cpp=.o)
EMU_BIN = ZoniStation_One

# --- Test files ---
TEST_SRCS = tests/cpu_minimal_test.c \
    $(EMU_CPU_SRCS) src/core/interconnect.c src/core/bus.c src/core/bus_irq.c \
    src/core/ram.c src/core/dma.c src/core/timers.c src/core/bios.c \
    src/core/mdec.c src/core/debugger.c src/core/lua_debug.c $(EMU_LUA_SRCS) \
    src/gte/gte.c src/gte/gte_ops.c src/utils/log.c \
    src/gpu/gpu.c src/gpu/renderer.c src/gpu/vram.c \
    src/spu/spu.c src/utils/rxi_log.c src/core/event_scheduler.c
TEST_BIN = cpu_test

TEST_OBJS = $(TEST_SRCS:.c=.o)

# Every object either target can build, so the .d files are picked up whichever
# one was made last.
ALL_OBJS = $(sort $(EMU_OBJS) $(TEST_OBJS))
DEPS = $(ALL_OBJS:.o=.d)

.PHONY: all test clean compile_commands

# compile_commands.json — what a language server needs to parse this tree.
#
# Without it clangd guesses the include path, fails to find "interconnect.h" and
# the SDL3 headers, and then reports nonsense downstream: include/cpu.h lights up
# with "identifier uint32_t is undefined" on dozens of lines even though the
# header is self-contained and `gcc -fsyntax-only include/cpu.h` returns 0. The
# errors are the editor's, not the code's, and they bury the real ones.
#
# Generated from the same source lists the build uses, so it cannot drift from
# what actually gets compiled, and with no extra tooling to install.
compile_commands:
	@printf '[\n' > compile_commands.json
	@first=1; for f in $(EMU_C_SRCS); do \
	   [ $$first -eq 1 ] || printf ',\n' >> compile_commands.json; first=0; \
	   printf '  {"directory": "%s", "file": "%s", "command": "%s -c %s -o %s"}' \
	     "$(CURDIR)" "$$f" "$(CC) $(CFLAGS)" "$$f" "$${f%.c}.o" >> compile_commands.json; \
	 done; \
	 for f in $(EMU_CXX_SRCS); do \
	   printf ',\n  {"directory": "%s", "file": "%s", "command": "%s -c %s -o %s"}' \
	     "$(CURDIR)" "$$f" "$(CXX) $(CXXFLAGS)" "$$f" "$${f%.cpp}.o" >> compile_commands.json; \
	 done
	@printf '\n]\n' >> compile_commands.json
	@echo "compile_commands.json: $(words $(EMU_C_SRCS) $(EMU_CXX_SRCS)) entries"

all: $(EMU_BIN)

$(EMU_BIN): $(EMU_OBJS)
	$(CXX) -o $@ $^ $(CXXFLAGS) $(LIBS)

%.o: %.c
	$(CC) -c $< -o $@ $(CFLAGS)

%.o: %.cpp
	$(CXX) -c $< -o $@ $(CXXFLAGS)

test: $(TEST_BIN)
	./$(TEST_BIN)

$(TEST_BIN): $(TEST_OBJS)
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

split_log: split_log.c
	$(CC) -o split_log split_log.c

clean:
	rm -f $(EMU_BIN) $(TEST_BIN) split_log \
	    src/*.[od] src/cpu/*.[od] src/core/*.[od] src/gpu/*.[od] src/gte/*.[od] \
	    src/cdrom/*.[od] src/spu/*.[od] src/utils/*.[od] tests/*.[od] \
	    third_party/imgui/*.[od] third_party/imgui/backends/*.[od] third_party/lua/*.[od] \
	    *.txt logs/*.txt logs/*_old.txt

# Last: the .d files are generated, so a build that has never run simply has
# none and make carries on.
-include $(DEPS)
