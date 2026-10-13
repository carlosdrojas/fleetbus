cmake_minimum_required(VERSION 3.25)
# Two builds from one tree:
#   host:  cmake -S firmware -B build/host -G Ninja [-DFB_BOARD=mspm0g3507]   (Unity tests + host simulator)
#   cross: cmake -S firmware -B build/fw -G Ninja -DCMAKE_TOOLCHAIN_FILE=firmware/cmake/arm-none-eabi.cmake [-DFB_BOARD=...]
# FB_BOARD picks firmware/boards/<board>/fb_board.h (flash map, page size, image hw id) and, when cross-compiling,
# the CPU flags and firmware/arm/<board>/. Host tests run once per board (Makefile host-test).
set(FB_BOARD stm32g4 CACHE STRING "Target board: stm32g4 or mspm0g3507")
set_property(CACHE FB_BOARD PROPERTY STRINGS stm32g4 mspm0g3507)
project(fleetbus_firmware C ASM)
set(CMAKE_C_STANDARD 11)
set(TP ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/boards/${FB_BOARD}/fb_board.h)
  message(FATAL_ERROR "Unknown FB_BOARD '${FB_BOARD}' (expected stm32g4 or mspm0g3507)")
endif()
if(CMAKE_CROSSCOMPILING)
  # CMake's GNU defaults append "-O3 -DNDEBUG" after the toolchain's Release flags, so -Os never applied (found in P0).
  set(CMAKE_C_FLAGS_RELEASE "-Os -g3 -DNDEBUG")
else()
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)   # host only: the simulator is a shared library loaded by Python
endif()

add_library(monocypher STATIC ${TP}/monocypher/src/monocypher.c ${TP}/monocypher/src/optional/monocypher-ed25519.c)
target_include_directories(monocypher PUBLIC ${TP}/monocypher/src ${TP}/monocypher/src/optional)
target_compile_options(monocypher PRIVATE -w)       # third-party code: not ours to warn about

file(GLOB COMMON_SRC common/*.c)
add_library(fb_common STATIC ${COMMON_SRC})
target_include_directories(fb_common PUBLIC common boards/${FB_BOARD})
target_link_libraries(fb_common PUBLIC monocypher)
target_compile_options(fb_common PRIVATE -Wall -Wextra -Wshadow)

if(CMAKE_CROSSCOMPILING)
  add_subdirectory(arm/${FB_BOARD})
else()
  enable_testing()
  add_subdirectory(tests)
  add_subdirectory(host)
endif()
