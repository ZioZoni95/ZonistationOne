#!/bin/bash

# ZoniStation One Build Script

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to print colored output
print_status() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Default values
BUILD_TYPE="Release"
CLEAN_BUILD=false
INSTALL=false
JOBS=$(nproc)

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -d|--debug)
            BUILD_TYPE="Debug"
            shift
            ;;
        -c|--clean)
            CLEAN_BUILD=true
            shift
            ;;
        -i|--install)
            INSTALL=true
            shift
            ;;
        -j|--jobs)
            JOBS="$2"
            shift 2
            ;;
        -h|--help)
            echo "Usage: $0 [OPTIONS]"
            echo "Options:"
            echo "  -d, --debug     Build in debug mode"
            echo "  -c, --clean     Clean build directory before building"
            echo "  -i, --install   Install after building"
            echo "  -j, --jobs N    Use N jobs for parallel compilation (default: $(nproc))"
            echo "  -h, --help      Show this help message"
            exit 0
            ;;
        *)
            print_error "Unknown option: $1"
            exit 1
            ;;
    esac
done

# Check if we're in the right directory
if [[ ! -f "CMakeLists.txt" ]]; then
    print_error "CMakeLists.txt not found. Please run this script from the ZoniStation_One directory."
    exit 1
fi

print_status "Building ZoniStation One in $BUILD_TYPE mode"

# Check for required dependencies
print_status "Checking dependencies..."

# Check for CMake
if ! command -v cmake &> /dev/null; then
    print_error "CMake not found. Please install CMake 3.15 or higher."
    exit 1
fi

# Check for compiler
if ! command -v gcc &> /dev/null && ! command -v clang &> /dev/null; then
    print_error "No C compiler found. Please install GCC or Clang."
    exit 1
fi

# Check for SDL2
if ! pkg-config --exists sdl2; then
    print_warning "SDL2 not found via pkg-config. Make sure SDL2 development libraries are installed."
fi

print_success "Dependencies check completed"

# Create build directory
BUILD_DIR="build"
if [[ "$CLEAN_BUILD" == true ]]; then
    print_status "Cleaning build directory..."
    rm -rf "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure with CMake
print_status "Configuring with CMake..."
cmake .. -DCMAKE_BUILD_TYPE="$BUILD_TYPE"

# Build
print_status "Building with $JOBS jobs..."
make -j"$JOBS"

print_success "Build completed successfully!"

# Install if requested
if [[ "$INSTALL" == true ]]; then
    print_status "Installing..."
    make install
    print_success "Installation completed!"
fi

print_success "ZoniStation One is ready!"
print_status "You can run the emulator with: ./bin/zonistation_one --help" 