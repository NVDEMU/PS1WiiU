cmake_minimum_required(VERSION 3.13)
set(CMAKE_TOOLCHAIN_FILE $ENV{DEVKITPRO}/cmake/WiiU.cmake CACHE PATH "Wii U toolchain")

project(PS1WiiU C)

add_executable(PS1WiiU
    src/main.c
)

target_compile_options(PS1WiiU PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

# Produce both the RPX executable and an Aroma-compatible WUHB bundle.
wut_create_rpx(PS1WiiU)
wut_create_wuhb(PS1WiiU)
