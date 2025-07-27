# Core component CMakeLists.txt

set(CORE_SOURCES
    core.c
    core_config.c
    core_timing.c
    core_debug.c
)

# Create core library
add_library(zonistation_core STATIC ${CORE_SOURCES})

# Set include directories
target_include_directories(zonistation_core PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

# Link dependencies
target_link_libraries(zonistation_core
    zonistation_cpu
    zonistation_memory
    zonistation_hardware
    zonistation_bios
    zonistation_plugins
    zonistation_utils
) 