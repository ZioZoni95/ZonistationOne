# Plugins component CMakeLists.txt

set(PLUGINS_SOURCES
    plugins.c
)

# Create plugins library
add_library(zonistation_plugins STATIC ${PLUGINS_SOURCES})

# Set include directories
target_include_directories(zonistation_plugins PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

# Link dependencies
target_link_libraries(zonistation_plugins
    zonistation_utils
) 