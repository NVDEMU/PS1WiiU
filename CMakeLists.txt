cmake_minimum_required(VERSION 3.13)
set(CMAKE_TOOLCHAIN_FILE $ENV{DEVKITPRO}/cmake/WiiU.cmake CACHE PATH "Wii U toolchain")

project(PS1WiiU C)

set(PCSX_REARMED_DIR "${CMAKE_SOURCE_DIR}/vendor/pcsx_rearmed")
set(PCSX_REARMED_CORE "${PCSX_REARMED_DIR}/pcsx_rearmed_libretro_wiiu.a")

if(NOT EXISTS "${PCSX_REARMED_CORE}")
    message(FATAL_ERROR
        "PCSX-ReARMed core not built. Run: make -C vendor/pcsx_rearmed -f Makefile.libretro platform=wiiu")
endif()

add_executable(PS1WiiU
    src/main.c
)

target_include_directories(PS1WiiU PRIVATE
    "${PCSX_REARMED_DIR}/deps/libretro-common/include"
)

target_compile_options(PS1WiiU PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wno-unused-parameter
)

# PCSX-ReARMed is built by its upstream Makefile.libretro target for Wii U.
# WUT supplies the native runtime; libm supplies shared math routines.
target_link_libraries(PS1WiiU PRIVATE
    "${PCSX_REARMED_CORE}"
    m
)

wut_create_rpx(PS1WiiU)
wut_create_wuhb(PS1WiiU)
