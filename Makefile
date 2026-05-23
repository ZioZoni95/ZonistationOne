# Makefile for PS1 Emulator and log splitter

# Compiler and flags
CC = gcc
CXX = g++

# Common includes and libs
INCLUDES = -Iinclude -Ithird_party/imgui -Ithird_party/imgui/backends
LIBS = -lSDL2 -lGL -lGLEW -lm -lpthread

SDL_CFLAGS = $(shell pkg-config --cflags sdl2)

CFLAGS = -std=c99 -g -Wall -Wextra $(INCLUDES) $(SDL_CFLAGS)
CXXFLAGS = -std=c++11 -g -Wall -Wextra $(INCLUDES) $(SDL_CFLAGS)

# --- Core / CPU ---
EMU_CPU_SRCS = \
    src/cpu/cpu_disasm.c src/cpu/cpu_init.c src/cpu/cpu_registers.c \
    src/cpu/cpu_bios.c src/cpu/cpu_exceptions.c src/cpu/cpu_icache.c \
    src/cpu/cpu_decode.c src/cpu/cpu_execution.c src/cpu/cpu_instructions.c

# --- System Core ---
EMU_CORE_SRCS = \
    src/core/bios.c src/core/interconnect.c src/core/bus.c src/core/bus_irq.c \
    src/core/ram.c src/core/dma.c src/core/timers.c src/core/sio.c \
    src/core/mdec.c src/core/controller.c src/core/event_scheduler.c src/core/pcdrv.c

# --- GPU ---
EMU_GPU_SRCS = \
    src/gpu/gpu.c src/gpu/gpu_helpers.c src/gpu/gpu_commands.c \
    src/gpu/renderer.c src/gpu/vram.c src/gpu/debugger.c

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
    $(EMU_GTE_SRCS) $(EMU_CDROM_SRCS) $(EMU_SPU_SRCS) $(EMU_UTIL_SRCS)

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
	    third_party/imgui/*.o third_party/imgui/backends/*.o \
	    *.txt logs/*.txt logs/*_old.txt
