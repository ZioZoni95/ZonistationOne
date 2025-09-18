# ZonistationOne - PlayStation One Emulator
# Makefile

# Project settings
PROJECT_NAME := zonistation-one
VERSION := 1.0.0

# Directories
SRC_DIR := src
BUILD_DIR := build
CORE_DIR := $(SRC_DIR)/core

# Compiler settings
CC := gcc
CFLAGS := -Wall -Wextra -std=c99 -O2 -g
CPPFLAGS := -I$(SRC_DIR) -DVERSION=\"$(VERSION)\"
LDFLAGS := -lm

# Debug build settings
DEBUG_CFLAGS := -Wall -Wextra -std=c99 -O0 -g -DDEBUG
DEBUG_LDFLAGS := -lm

# Source files
CORE_SOURCES := $(wildcard $(CORE_DIR)/*.c)
MAIN_SOURCES := $(SRC_DIR)/main.c
ALL_SOURCES := $(CORE_SOURCES) $(MAIN_SOURCES)

# Object files
CORE_OBJECTS := $(CORE_SOURCES:$(CORE_DIR)/%.c=$(BUILD_DIR)/core/%.o)
MAIN_OBJECTS := $(MAIN_SOURCES:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)
ALL_OBJECTS := $(CORE_OBJECTS) $(MAIN_OBJECTS)

# Targets
TARGET := $(BUILD_DIR)/$(PROJECT_NAME)
DEBUG_TARGET := $(BUILD_DIR)/$(PROJECT_NAME)-debug

# Default target
all: $(TARGET)

# Debug target
debug: CFLAGS = $(DEBUG_CFLAGS)
debug: LDFLAGS = $(DEBUG_LDFLAGS)
debug: $(DEBUG_TARGET)

# Create build directories
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)
	mkdir -p $(BUILD_DIR)/core

# Build main target
$(TARGET): $(BUILD_DIR) $(ALL_OBJECTS)
	@echo "Linking $(TARGET)..."
	$(CC) $(ALL_OBJECTS) -o $@ $(LDFLAGS)
	@echo "Build complete: $(TARGET)"

# Build debug target
$(DEBUG_TARGET): $(BUILD_DIR) $(ALL_OBJECTS)
	@echo "Linking debug target $(DEBUG_TARGET)..."
	$(CC) $(ALL_OBJECTS) -o $@ $(LDFLAGS)
	@echo "Debug build complete: $(DEBUG_TARGET)"

# Compile core source files
$(BUILD_DIR)/core/%.o: $(CORE_DIR)/%.c
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

# Compile main source files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

# Clean build files
clean:
	@echo "Cleaning build directory..."
	rm -rf $(BUILD_DIR)

# Install target (optional)
install: $(TARGET)
	@echo "Installing $(PROJECT_NAME)..."
	sudo cp $(TARGET) /usr/local/bin/$(PROJECT_NAME)
	@echo "Installation complete"

# Uninstall target (optional)
uninstall:
	@echo "Uninstalling $(PROJECT_NAME)..."
	sudo rm -f /usr/local/bin/$(PROJECT_NAME)
	@echo "Uninstall complete"

# Run the emulator
run: $(TARGET)
	@echo "Running $(PROJECT_NAME)..."
	./$(TARGET)

# Run with debug
run-debug: $(DEBUG_TARGET)
	@echo "Running $(PROJECT_NAME) in debug mode..."
	./$(DEBUG_TARGET) -v -d

# Run with BIOS
run-bios: $(TARGET)
	@echo "Running $(PROJECT_NAME) with BIOS..."
	./$(TARGET) -b bios_files/SCPH1001.BIN -v

# Show help
help:
	@echo "ZonistationOne - PlayStation One Emulator"
	@echo "Available targets:"
	@echo "  all          - Build release version (default)"
	@echo "  debug        - Build debug version"
	@echo "  clean        - Clean build files"
	@echo "  install      - Install to system"
	@echo "  uninstall    - Remove from system"
	@echo "  run          - Build and run emulator"
	@echo "  run-debug    - Build and run debug version"
	@echo "  run-bios     - Build and run with BIOS file"
	@echo "  help         - Show this help message"
	@echo ""
	@echo "Build variables:"
	@echo "  CC=$(CC)"
	@echo "  CFLAGS=$(CFLAGS)"
	@echo "  VERSION=$(VERSION)"

# Print project information
info:
	@echo "Project: $(PROJECT_NAME) v$(VERSION)"
	@echo "Source files: $(words $(ALL_SOURCES))"
	@echo "Object files: $(words $(ALL_OBJECTS))"
	@echo "Build directory: $(BUILD_DIR)"
	@echo "Target: $(TARGET)"

# Show dependencies
deps:
	@echo "Core dependencies:"
	@echo "  - Standard C library"
	@echo "  - Math library (libm)"
	@echo ""
	@echo "Optional dependencies:"
	@echo "  - BIOS file (SCPH1001.BIN or compatible)"

# Phony targets
.PHONY: all debug clean install uninstall run run-debug run-bios help info deps

# Build rule dependencies
$(CORE_OBJECTS): $(CORE_DIR)/system.h $(CORE_DIR)/logger.h $(CORE_DIR)/memory.h $(CORE_DIR)/emulator.h
$(MAIN_OBJECTS): $(CORE_DIR)/system.h $(CORE_DIR)/emulator.h $(CORE_DIR)/logger.h