# Makefile for PS1 Emulator and log splitter

# Source files for the emulator
EMU_SRCS = main.c cpu.c bios.c interconnect.c ram.c dma.c gpu.c renderer.c vram.c debugger.c timers.c cdrom.c gte.c
EMU_OBJS = $(EMU_SRCS:.c=.o)
EMU_BIN = myps1_emu

# Compiler and flags
CC = gcc
CFLAGS = -std=c99 -g -Wall -Wextra -DLOG_LEVEL=$(LOG_LEVEL) \
	-lSDL2 -lGL -lGLEW -lm

# Default log level
LOG_LEVEL ?= LOG_LEVEL_INFO

# Default target: build the emulator
all: $(EMU_BIN)

$(EMU_BIN): $(EMU_SRCS)
	$(CC) $(EMU_SRCS) -o $(EMU_BIN) $(CFLAGS)

# Build the log splitter utility
split_log: split_log.c
	$(CC) -o split_log split_log.c

# Clean build artifacts
clean:
	rm -f $(EMU_BIN) split_log *.o *.txt 