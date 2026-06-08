cmake_minimum_required(VERSION 3.10)
project(emulator C)

set(CMAKE_C_STANDARD 11)

add_executable(emulator
    src/emulator.c
    src/CPU.c
    src/memory.c
    src/PPU.c
    src/optable.c
    src/debugger.c
)

target_include_directories(emulator PRIVATE ${CMAKE_SOURCE_DIR}/src)

# Use pkg-config to find SDL2 (same as: pkg-config --cflags --libs sdl2)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)

target_include_directories(emulator PRIVATE ${SDL2_INCLUDE_DIRS})
target_link_libraries(emulator PRIVATE ${SDL2_LIBRARIES}) 