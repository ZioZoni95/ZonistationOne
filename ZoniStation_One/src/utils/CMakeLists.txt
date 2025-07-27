# Utils component CMakeLists.txt

set(UTILS_SOURCES
    utils.c
    logging.c
    config.c
)

# Create utils library
add_library(zonistation_utils STATIC ${UTILS_SOURCES})

# Set include directories
target_include_directories(zonistation_utils PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
) 