cmake_minimum_required(VERSION 3.25)
# Two builds from one tree:
#   host:  cmake -S firmware -B build/host -G Ninja                       (Unity tests + host simulator)
#   cross: cmake -S firmware -B build/fw -G Ninja -DCMAKE_TOOLCHAIN_FILE=firmware/cmake/arm-none-eabi.cmake
project(fleetbus_firmware C ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(TP ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

add_library(monocypher STATIC ${TP}/monocypher/src/monocypher.c ${TP}/monocypher/src/optional/monocypher-ed25519.c)
target_include_directories(monocypher PUBLIC ${TP}/monocypher/src ${TP}/monocypher/src/optional)
target_compile_options(monocypher PRIVATE -w)       # third-party code: not ours to warn about

file(GLOB COMMON_SRC common/*.c)
add_library(fb_common STATIC ${COMMON_SRC})
target_include_directories(fb_common PUBLIC common)
target_link_libraries(fb_common PUBLIC monocypher)
target_compile_options(fb_common PRIVATE -Wall -Wextra -Wshadow)

if(CMAKE_CROSSCOMPILING)
  add_subdirectory(arm)
else()
  enable_testing()
  add_subdirectory(tests)
  add_subdirectory(host)
endif()
