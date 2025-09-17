# PlayStation 1 Emulator - Fresh Start Makefile
# Following guide.tex structure with PSX-SPX compliance

CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -O2 -g
INCLUDES = -Iinclude
LIBS = 
TARGET = myps1_emu

# Source files
SRCDIR = src
SOURCES = $(wildcard $(SRCDIR)/*.c) main.c

# Object files
OBJDIR = obj
OBJECTS = $(SOURCES:%.c=$(OBJDIR)/%.o)

# Create directories
$(shell mkdir -p $(OBJDIR)/$(SRCDIR))

# Default target
all: $(TARGET)

# Link executable
$(TARGET): $(OBJECTS)
	@echo "Linking $@..."
	$(CC) $(OBJECTS) -o $@ $(LIBS)
	@echo "Build complete: $@"

# Compile source files
$(OBJDIR)/%.o: %.c
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Build target for VS Code task
build-debug: $(TARGET)
	@echo "Debug build complete"

# Clean
clean:
	@echo "Cleaning..."
	rm -rf $(OBJDIR)
	rm -f $(TARGET)
	@echo "Clean complete"

# Debug run
debug: $(TARGET)
	./$(TARGET) --debug --bios roms/SCPH1001.BIN

# Normal run  
run: $(TARGET)
	./$(TARGET) --bios roms/SCPH1001.BIN

# Test basic functionality
test: $(TARGET)
	@echo "Running basic tests..."
	./$(TARGET) --bios roms/SCPH1001.BIN 2>&1 | head -50

# Show file structure
structure:
	@echo "Project structure:"
	@find . -type f -name "*.c" -o -name "*.h" | sort

# Dependencies
.PHONY: all clean debug run test structure build-debug

# Automatic dependency generation
-include $(OBJECTS:.o=.d)

$(OBJDIR)/%.d: %.c
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) $(INCLUDES) -MM -MT $(@:.d=.o) $< > $@