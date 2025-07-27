# CPU component CMakeLists.txt

set(CPU_SOURCES
    cpu.c
    cpu_interpreter.c
    cpu_instructions.c
    cpu_exceptions.c
    cpu_cache.c
)

# Create CPU library
add_library(zonistation_cpu STATIC ${CPU_SOURCES})

# Set include directories
target_include_directories(zonistation_cpu PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

# Link dependencies
target_link_libraries(zonistation_cpu
    zonistation_memory
    zonistation_utils
) 