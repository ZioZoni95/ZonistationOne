# Makefile for PS1 Emulator and log splitter

# Source files for the emulator
# NOTE: Now using modular architecture (DuckStation-inspired)
#       - CPU: Modular cpu/cpu_*.c modules
#       - GPU: Modular GPU architecture (Phase 1: Commands module)
#             * gpu/gpu_core.c - Core state and init
#             * gpu/gpu_commands.c - GP0/GP1 dispatch (NEW)
#             * gpu.c - Legacy handlers (being migrated)
#       - IRQ: Modular interrupt system (irq/irq_core.c)
#       - CDROM: Modular CDROM controller (cdrom/cdrom_core.c)
#       - BIOS: Modular BIOS system (bios/bios_core.c)
#       - TIMERS: Modular timer system (timers/timer_core.c)
EMU_SRCS = src/main.c \
           src/cpu/cpu_types.c \
           src/cpu/cpu_cache.c \
           src/cpu/cpu_exceptions.c \
           src/cpu/cpu_instructions.c \
           src/cpu/cpu_core.c \
           src/cpu/cpu_disasm.c \
           src/cpu/cpu_debugger.c \
           src/irq/irq_core.c \
           src/cdrom/cdrom_core.c \
           src/cdrom/cdrom_commands.c \
           src/bios/bios_core.c \
           src/timers/timer_core.c \
           src/interconnect.c \
           src/ram.c \
           src/dma.c \
           src/gpu/gpu_core.c \
           src/gpu/gpu_commands.c \
           src/gpu/gpu_rendering.c \
           src/gpu/gpu_vram.c \
           src/gpu/gpu_display.c \
           src/renderer.c \
           src/vram.c \
           src/gte.c \
           src/log.c \
           src/sio.c \
           src/spu.c \
           src/controller.c \
           src/threading.c \
           src/gpu/gpu_thread.c 

EMU_OBJS = $(EMU_SRCS:.c=.o)
EMU_BIN = myps1_emu

# Test files
TEST_SRCS = tests/cpu_minimal_test.c src/cpu.c src/interconnect.c src/ram.c src/dma.c src/gpu.c src/timers.c src/cdrom.c src/bios.c src/gte.c src/log.c src/event_scheduler.c src/renderer.c src/vram.c src/spu.c src/threading.c
TEST_BIN = cpu_test

# Compiler and flags
CC = gcc
CFLAGS = -std=c11 -g -Wall -Wextra \
	-Iinclude \
	-pthread \
	-lSDL2 -lGL -lGLEW -lm -lrt

# Log level is now set at runtime via log_set_level()

# Default target: build the emulator
all: $(EMU_BIN)

$(EMU_BIN): $(EMU_SRCS)
	$(CC) $(EMU_SRCS) -o $(EMU_BIN) $(CFLAGS)

# Build and run the CPU test
test: $(TEST_BIN)
	./$(TEST_BIN)

$(TEST_BIN): $(TEST_SRCS)
	$(CC) $(TEST_SRCS) -o $(TEST_BIN) $(CFLAGS)

# Build the log splitter utility
split_log: split_log.c
	$(CC) -o split_log split_log.c

# Clean build artifacts
clean:
	rm -f $(EMU_BIN) $(TEST_BIN) split_log *.o *.txt \
	logs/*.txt logs/*_old.txt 