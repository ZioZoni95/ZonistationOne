# Makefile for ZonistationOne PlayStation 1 Emulator
# Based on PCSX-Redux reference implementation

PROJECT_NAME = myps1_emu
VERSION = 0.1.0

SRC_DIR = src
INC_DIR = include  
BUILD_DIR = build
ROMS_DIR = roms

CC = gcc
BASE_CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wformat=2 -Wcast-align \
              -Wconversion -Wsign-conversion -Wnull-dereference -I$(INC_DIR)
LDFLAGS = -lm

SOURCES = $(wildcard $(SRC_DIR)/*.c)

.PHONY: all debug release clean help run-debug run

all: debug

debug: | $(BUILD_DIR)
	$(CC) $(BASE_CFLAGS) -g -O0 -DDEBUG $(SOURCES) -o $(BUILD_DIR)/$(PROJECT_NAME)_debug $(LDFLAGS) -fsanitize=address -fsanitize=undefined
	@echo "Debug build complete: $(BUILD_DIR)/$(PROJECT_NAME)_debug"

release: | $(BUILD_DIR)
	$(CC) $(BASE_CFLAGS) -O3 -DNDEBUG -march=native $(SOURCES) -o $(BUILD_DIR)/$(PROJECT_NAME) $(LDFLAGS)
	@echo "Release build complete: $(BUILD_DIR)/$(PROJECT_NAME)"

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

run-debug: debug
	./$(BUILD_DIR)/$(PROJECT_NAME)_debug --debug

run: release
	./$(BUILD_DIR)/$(PROJECT_NAME)

clean:
	rm -f $(BUILD_DIR)/*
	@echo "Build artifacts cleaned"

help:
	@echo "Available targets:"
	@echo "  all         - Build debug version (default)"
	@echo "  debug       - Build debug version with sanitizers"
	@echo "  release     - Build optimized release version" 
	@echo "  run-debug   - Build and run debug version"
	@echo "  run         - Build and run release version"
	@echo "  clean       - Remove build artifacts"
	@echo "  help        - Show this help message"