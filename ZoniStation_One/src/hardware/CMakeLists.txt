# Hardware component CMakeLists.txt

set(HARDWARE_SOURCES
    hardware.c
)

# Create hardware library
add_library(zonistation_hardware STATIC ${HARDWARE_SOURCES})

# Set include directories
target_include_directories(zonistation_hardware PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

# Link dependencies
target_link_libraries(zonistation_hardware
    zonistation_utils
) 