# Memory component CMakeLists.txt

set(MEMORY_SOURCES
    memory.c
    memory_mapping.c
)

# Create memory library
add_library(zonistation_memory STATIC ${MEMORY_SOURCES})

# Set include directories
target_include_directories(zonistation_memory PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

# Link dependencies
target_link_libraries(zonistation_memory
    zonistation_utils
) 