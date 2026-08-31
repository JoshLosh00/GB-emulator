# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.0)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()

set(sdkVersion 2.3.0)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)

if(EXISTS ${picoVscode})
    include(${picoVscode})
endif()

set(PICO_BOARD pico2 CACHE STRING "Board type")

include(pico_sdk_import.cmake)

project(pico-emulator C CXX ASM)

pico_sdk_init()

add_executable(pico-emulator
    main.c
    #test.c

    src/emulator.c
    src/cpu.c
    src/ppu.c
    src/memory.c
    src/apu.c

    lib/Config/DEV_Config.c
    lib/LCD/LCD_1in3.c
    lib/audio_pio/audio_pio.c
    lib/audio_data/audio_data.c
    lib/infrared/infrared.c
)

pico_generate_pio_header(pico-emulator ${CMAKE_CURRENT_LIST_DIR}/audio_pio.pio)

pico_set_program_name(pico-emulator "pico-emulator")
pico_set_program_version(pico-emulator "0.1")

pico_enable_stdio_uart(pico-emulator 0)
pico_enable_stdio_usb(pico-emulator 1)

target_include_directories(pico-emulator PRIVATE
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/lib/Config
    ${PROJECT_SOURCE_DIR}/lib/LCD
    ${PROJECT_SOURCE_DIR}/lib/audio_data
    ${PROJECT_SOURCE_DIR}/lib/audio_pio
    ${PROJECT_SOURCE_DIR}/lib/infrared
)

target_link_libraries(pico-emulator
    pico_stdlib
    hardware_spi
    hardware_dma
    hardware_i2c
    hardware_adc
    hardware_pwm
    hardware_pio
)

pico_add_extra_outputs(pico-emulator)