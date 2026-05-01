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

# Emulator C sources
EMU_C_SRCS = src/main.c src/cpu/cpu_disasm.c src/cpu/cpu_init.c src/cpu/cpu_registers.c \
             src/cpu/cpu_bios.c src/cpu/cpu_exceptions.c src/cpu/cpu_icache.c \
             src/cpu/cpu_decode.c src/cpu/cpu_execution.c src/cpu/cpu_instructions.c \
             src/bios.c src/interconnect.c src/bus.c src/bus_irq.c src/ram.c src/dma.c \
             src/gpu.c src/gpu_helpers.c src/gpu_commands.c src/renderer.c src/vram.c \
             src/debugger.c src/timers.c src/cdrom.c src/cdrom_commands.c src/cdrom_disc.c \
             src/cdrom_audio.c src/gte.c src/log.c src/rxi_log.c src/event_scheduler.c \
             src/sio.c src/controller.c src/spu.c src/pcdrv.c

# Emulator C++ sources
EMU_CXX_SRCS = src/debug_ui.cpp \
               third_party/imgui/imgui.cpp \
               third_party/imgui/imgui_draw.cpp \
               third_party/imgui/imgui_widgets.cpp \
               third_party/imgui/imgui_tables.cpp \
               third_party/imgui/backends/imgui_impl_sdl2.cpp \
               third_party/imgui/backends/imgui_impl_opengl3.cpp

EMU_OBJS = $(EMU_C_SRCS:.c=.o) $(EMU_CXX_SRCS:.cpp=.o)
EMU_BIN = myps1_emu

# Test files
TEST_SRCS = tests/cpu_minimal_test.c src/cpu/cpu_disasm.c src/cpu/cpu_init.c src/cpu/cpu_registers.c \
            src/cpu/cpu_bios.c src/cpu/cpu_exceptions.c src/cpu/cpu_icache.c src/cpu/cpu_decode.c \
            src/cpu/cpu_execution.c src/cpu/cpu_instructions.c src/interconnect.c src/bus.c \
            src/bus_irq.c src/ram.c src/dma.c src/gpu.c src/timers.c src/cdrom.c \
            src/cdrom_commands.c src/cdrom_disc.c src/cdrom_audio.c src/bios.c src/gte.c \
            src/log.c src/event_scheduler.c src/renderer.c src/vram.c src/spu.c src/rxi_log.c
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
	rm -f $(EMU_BIN) $(TEST_BIN) split_log src/*.o src/cpu/*.o third_party/imgui/*.o third_party/imgui/backends/*.o *.txt \
	logs/*.txt logs/*_old.txt
