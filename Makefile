# Makefile for PS1 Emulator and log splitter

# Compiler and flags
CC = gcc
CXX = g++

# Common includes and libs
INCLUDES = -Iinclude -Ithird_party/imgui -Ithird_party/imgui/backends -Ithird_party/lua
LIBS = -lSDL2 -lGL -lGLEW -lm -lpthread

SDL_CFLAGS = $(shell pkg-config --cflags sdl2)

# Build mode. Default is an optimised build — the emulator is an interpreter on
# the hot path, so an unoptimised (-O0) build ran ~3-5x slower than the machine,
# left no headroom over the frame budget and drifted the moment any debug
# instrumentation was on. `make DEBUG=1` restores an -O0 build for stepping in gdb.
ifdef DEBUG
  OPT = -O0 -g
else
  OPT = -O3 -g -march=native -DNDEBUG
endif

CFLAGS = -std=c99 $(OPT) -Wall -Wextra $(INCLUDES) $(SDL_CFLAGS)
CXXFLAGS = -std=c++11 $(OPT) -Wall -Wextra $(INCLUDES) $(SDL_CFLAGS)

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
    src/spu/spu_mixing.c src/spu/spu_dma.c src/spu/spu_irq.c

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
               third_party/imgui/backends/imgui_impl_sdl2.cpp \
               third_party/imgui/backends/imgui_impl_opengl3.cpp

EMU_OBJS = $(EMU_C_SRCS:.c=.o) $(EMU_CXX_SRCS:.cpp=.o)
EMU_BIN = myps1_emu

# --- Test files ---
TEST_SRCS = tests/cpu_minimal_test.c \
    $(EMU_CPU_SRCS) src/core/interconnect.c src/core/bus.c src/core/bus_irq.c \
    src/core/ram.c src/core/dma.c src/core/timers.c src/core/bios.c \
    src/core/mdec.c src/core/debugger.c src/core/lua_debug.c $(EMU_LUA_SRCS) \
    src/gte/gte.c src/gte/gte_ops.c src/utils/log.c \
    src/gpu/gpu.c src/gpu/renderer.c src/gpu/vram.c \
    src/spu/spu.c src/utils/rxi_log.c src/core/event_scheduler.c
TEST_BIN = cpu_test

all: $(EMU_BIN)

$(EMU_BIN): $(EMU_OBJS)
	$(CXX) -o $@ $^ $(CXXFLAGS) $(LIBS)

%.o: %.c
	$(CC) -c $< -o $@ $(CFLAGS)

%.o: %.cpp
	$(CXX) -c $< -o $@ $(CXXFLAGS)

test: $(TEST_BIN)
	./$(TEST_BIN)

$(TEST_BIN): $(TEST_SRCS:.c=.o)
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

split_log: split_log.c
	$(CC) -o split_log split_log.c

clean:
	rm -f $(EMU_BIN) $(TEST_BIN) split_log \
	    src/*.o src/cpu/*.o src/core/*.o src/gpu/*.o src/gte/*.o \
	    src/cdrom/*.o src/spu/*.o src/utils/*.o \
	    third_party/imgui/*.o third_party/imgui/backends/*.o third_party/lua/*.o \
	    *.txt logs/*.txt logs/*_old.txt
