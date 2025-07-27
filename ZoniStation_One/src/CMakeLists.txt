# Source directory CMakeLists.txt

# Add subdirectories for each component
add_subdirectory(core)
add_subdirectory(cpu)
add_subdirectory(memory)
add_subdirectory(hardware)
add_subdirectory(bios)
add_subdirectory(plugins)
add_subdirectory(utils)

# Main executable
set(MAIN_SOURCES
    main.c
)

# Create the main executable
add_executable(zonistation_one ${MAIN_SOURCES})

# Link against all components
target_link_libraries(zonistation_one
    zonistation_core
    zonistation_cpu
    zonistation_memory
    zonistation_hardware
    zonistation_bios
    zonistation_plugins
    zonistation_utils
    SDL2::SDL2
    OpenGL::GL
)

# Platform-specific libraries
if(WIN32)
    target_link_libraries(zonistation_one
        ws2_32
        winmm
        dinput8
        dxguid
    )
elseif(UNIX AND NOT APPLE)
    target_link_libraries(zonistation_one
        pthread
        dl
        m
    )
elseif(APPLE)
    target_link_libraries(zonistation_one
        "-framework Cocoa"
        "-framework IOKit"
        "-framework CoreAudio"
        "-framework AudioToolbox"
        "-framework CoreVideo"
        "-framework OpenGL"
    )
endif()

# Set compile definitions
target_compile_definitions(zonistation_one PRIVATE
    ZONISTATION_VERSION="${PROJECT_VERSION}"
    ZONISTATION_BUILD_DATE="${CMAKE_BUILD_DATE}"
)

# Set include directories
target_include_directories(zonistation_one PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
) 