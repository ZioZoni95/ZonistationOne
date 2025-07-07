# Makefile for PS1 Emulator and log splitter

# Source files for the emulator
EMU_SRCS = src/main.c src/cpu.c src/bios.c src/interconnect.c src/ram.c src/dma.c src/gpu.c src/renderer.c src/vram.c src/debugger.c src/timers.c src/cdrom.c src/gte.c src/log.c src/event_scheduler.c
EMU_OBJS = $(EMU_SRCS:.c=.o)
EMU_BIN = myps1_emu

# Test files
TEST_SRCS = tests/cpu_minimal_test.c src/cpu.c src/interconnect.c src/ram.c src/dma.c src/gpu.c src/timers.c src/cdrom.c src/bios.c src/gte.c src/log.c src/event_scheduler.c src/renderer.c src/vram.c
TEST_BIN = cpu_test

# Compiler and flags
CC = gcc
CFLAGS = -std=c99 -g -Wall -Wextra \
	-Iinclude \
	-lSDL2 -lGL -lGLEW -lm

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