# ZonistationOne PlayStation 1 Emulator Makefile
# Based on modern C++ standards with reference to PCSX-Redux architecture

# Compiler settings
CXX = g++
CXXFLAGS = -std=c++20 -Wall -Wextra -O2 -g
LDFLAGS = 

# Directories
SRCDIR = src
INCDIR = include
BUILDDIR = build
OBJDIR = $(BUILDDIR)/obj

# Include paths
INCLUDES = -I$(INCDIR) -I$(SRCDIR)

# Target executable
TARGET = $(BUILDDIR)/zonistation-one
TEST_DEBUGGER = $(BUILDDIR)/test-debugger
TEST_DEBUGGER_BIOS = $(BUILDDIR)/test-debugger-bios

# Source files (automatically find all .cpp files, excluding tests)
SOURCES = $(shell find $(SRCDIR) -name "*.cpp" ! -name "test_*.cpp")
OBJECTS = $(SOURCES:$(SRCDIR)/%.cpp=$(OBJDIR)/%.o)

# Test sources
TEST_DEBUGGER_SOURCES = $(SRCDIR)/test_debugger.cpp $(filter-out $(SRCDIR)/main.cpp, $(SOURCES))
TEST_DEBUGGER_OBJECTS = $(TEST_DEBUGGER_SOURCES:$(SRCDIR)/%.cpp=$(OBJDIR)/%.o)

TEST_DEBUGGER_BIOS_SOURCES = $(SRCDIR)/test_debugger_bios.cpp $(filter-out $(SRCDIR)/main.cpp, $(SOURCES))
TEST_DEBUGGER_BIOS_OBJECTS = $(TEST_DEBUGGER_BIOS_SOURCES:$(SRCDIR)/%.cpp=$(OBJDIR)/%.o)

# Dependencies for automatic header dependency tracking
DEPENDS = $(OBJECTS:.o=.d)

# Default target
.PHONY: all clean debug release run test-debugger

all: $(TARGET)

# Build target
$(TARGET): $(OBJECTS) | $(BUILDDIR)
	@echo "Linking $(TARGET)..."
	$(CXX) $(OBJECTS) -o $@ $(LDFLAGS)
	@echo "Build complete!"

# Build debugger test
$(TEST_DEBUGGER): $(TEST_DEBUGGER_OBJECTS) | $(BUILDDIR)
	@echo "Linking $(TEST_DEBUGGER)..."
	$(CXX) $(TEST_DEBUGGER_OBJECTS) -o $@ $(LDFLAGS)
	@echo "Debugger test build complete!"

# Build enhanced BIOS debugger test  
$(TEST_DEBUGGER_BIOS): $(TEST_DEBUGGER_BIOS_OBJECTS) | $(BUILDDIR)
	@echo "Linking $(TEST_DEBUGGER_BIOS)..."
	$(CXX) $(TEST_DEBUGGER_BIOS_OBJECTS) -o $@ $(LDFLAGS)
	@echo "Enhanced BIOS debugger test build complete!"

# Test debugger
test-debugger: $(TEST_DEBUGGER)
	@echo "Running debugger test..."
	./$(TEST_DEBUGGER)

# Test debugger with BIOS
test-debugger-bios: $(TEST_DEBUGGER_BIOS)
	@echo "Running enhanced BIOS debugger test..."
	./$(TEST_DEBUGGER_BIOS)

# Compile source files
$(OBJDIR)/%.o: $(SRCDIR)/%.cpp | $(OBJDIR)
	@echo "Compiling $<..."
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -MMD -c $< -o $@

# Create directories
$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(OBJDIR):
	mkdir -p $(OBJDIR)

# Debug build
debug: CXXFLAGS += -DDEBUG -g3 -O0
debug: $(TARGET)

# Release build  
release: CXXFLAGS += -DNDEBUG -O3
release: $(TARGET)

# Run the emulator
run: $(TARGET)
	./$(TARGET)

# Run with BIOS
run-bios: $(TARGET)
	./$(TARGET) bios_files/SCPH1001.BIN

# Clean build files
clean:
	@echo "Cleaning build files..."
	rm -rf $(BUILDDIR)

# Install dependencies (Ubuntu/Debian)
install-deps:
	@echo "Installing development dependencies..."
	sudo apt-get update
	sudo apt-get install -y build-essential g++ cmake pkg-config \
		libgl1-mesa-dev libglu1-mesa-dev \
		libasound2-dev libpulse-dev \
		libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev

# Print build info
info:
	@echo "ZonistationOne PS1 Emulator Build Configuration"
	@echo "=============================================="
	@echo "Compiler: $(CXX)"
	@echo "Flags: $(CXXFLAGS)"
	@echo "Sources: $(words $(SOURCES)) files"
	@echo "Target: $(TARGET)"

# Include dependency files
-include $(DEPENDS)

# Help target
help:
	@echo "ZonistationOne PS1 Emulator Makefile"
	@echo "===================================="
	@echo "Available targets:"
	@echo "  all         - Build the emulator (default)"
	@echo "  debug       - Build with debug information"
	@echo "  release     - Build optimized release version"
	@echo "  test-debugger - Build and run debugger functionality test"
	@echo "  clean       - Remove all build files"
	@echo "  run         - Build and run the emulator"
	@echo "  run-bios    - Build and run with BIOS file"
	@echo "  install-deps- Install build dependencies (Ubuntu/Debian)"
	@echo "  info        - Show build configuration"
	@echo "  help        - Show this help message"