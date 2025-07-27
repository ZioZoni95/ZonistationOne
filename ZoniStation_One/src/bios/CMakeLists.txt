# BIOS component CMakeLists.txt

set(BIOS_SOURCES
    bios.c
)

# Create BIOS library
add_library(zonistation_bios STATIC ${BIOS_SOURCES})

# Set include directories
target_include_directories(zonistation_bios PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

# Link dependencies
target_link_libraries(zonistation_bios
    zonistation_utils
) 