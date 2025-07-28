# ZoniStationOne Makefile

# Include configuration
-include config.mak

# Configuration
TARGET = zonistationone
VERSION = 0.1.0

# Default compiler settings (can be overridden by config.mak)
CC ?= gcc
CFLAGS ?= -Wall -Wextra -std=c99 -O2 -g
LDFLAGS ?= 
LIBS ?= -lm

# Directories
SRCDIR = src
INCDIR = src/include
OBJDIR = obj
BINDIR = bin

# Source files
CORE_SOURCES = $(wildcard $(SRCDIR)/core/*.c)
PLUGIN_SOURCES = $(wildcard $(SRCDIR)/plugins/*.c)
FRONTEND_SOURCES = $(wildcard $(SRCDIR)/frontend/*.c)
TOOLS_SOURCES = $(wildcard $(SRCDIR)/tools/*.c)

# Object files
CORE_OBJECTS = $(CORE_SOURCES:$(SRCDIR)/%.c=$(OBJDIR)/%.o)
PLUGIN_OBJECTS = $(PLUGIN_SOURCES:$(SRCDIR)/%.c=$(OBJDIR)/%.o)
FRONTEND_OBJECTS = $(FRONTEND_SOURCES:$(SRCDIR)/%.c=$(OBJDIR)/%.o)
TOOLS_OBJECTS = $(TOOLS_SOURCES:$(SRCDIR)/%.c=$(OBJDIR)/%.o)

# All object files
OBJECTS = $(CORE_OBJECTS) $(PLUGIN_OBJECTS) $(FRONTEND_OBJECTS)

# Default target
all: config.mak $(BINDIR)/$(TARGET)

# Configuration target
config.mak:
	@echo "Please run ./configure first"
	@exit 1

# Create directories
$(OBJDIR):
	mkdir -p $(OBJDIR)/core $(OBJDIR)/plugins $(OBJDIR)/frontend $(OBJDIR)/tools

$(BINDIR):
	mkdir -p $(BINDIR)

# Main executable
$(BINDIR)/$(TARGET): $(CORE_OBJECTS) $(PLUGIN_OBJECTS) $(FRONTEND_OBJECTS) | $(BINDIR)
	$(CC) $(CORE_OBJECTS) $(PLUGIN_OBJECTS) $(FRONTEND_OBJECTS) -o $@ $(LDFLAGS) $(LIBS)

# BIOS Disassembler tool
$(BINDIR)/bios_disassembler: $(CORE_OBJECTS) $(TOOLS_OBJECTS) | $(BINDIR)
	$(CC) $(CORE_OBJECTS) $(TOOLS_OBJECTS) -o $@ $(LDFLAGS) $(LIBS)

# Compile object files
$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -I$(INCDIR) $(SDL2_CFLAGS) -c $< -o $@

# Clean
clean:
	rm -rf $(OBJDIR) $(BINDIR)

# Install
install: $(BINDIR)/$(TARGET)
	install -D $(BINDIR)/$(TARGET) /usr/local/bin/$(TARGET)

# Uninstall
uninstall:
	rm -f /usr/local/bin/$(TARGET)

# Debug build
debug: CFLAGS += -DDEBUG -O0
debug: $(BINDIR)/$(TARGET)

# Release build
release: CFLAGS += -DNDEBUG -O3
release: clean $(BINDIR)/$(TARGET)

# Test target
test: $(BINDIR)/$(TARGET)
	@echo "Running tests..."
	# TODO: Add test execution

# Tools target
tools: $(BINDIR)/bios_disassembler
	@echo "Built BIOS disassembler tool"

# Help
help:
	@echo "Available targets:"
	@echo "  all      - Build the emulator (default)"
	@echo "  debug    - Build with debug symbols"
	@echo "  release  - Build optimized release version"
	@echo "  clean    - Remove build artifacts"
	@echo "  install  - Install to system"
	@echo "  uninstall- Remove from system"
	@echo "  test     - Run tests"
	@echo "  tools    - Build BIOS disassembler tool"
	@echo "  help     - Show this help"

.PHONY: all clean install uninstall debug release test tools help 