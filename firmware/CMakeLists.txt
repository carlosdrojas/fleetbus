cmake_minimum_required(VERSION 3.25)
# Two builds from one tree:
#   host:  cmake -S firmware -B build/host -G Ninja                    (Unity tests + host simulator)
#   cross: cmake -S firmware -B build/fw-mspm0 -G Ninja -DCMAKE_TOOLCHAIN_FILE=firmware/cmake/arm-none-eabi.cmake
# Target: TI LP-MSPM0G3507 LaunchPad (Cortex-M0+, 80 MHz, 128 KB flash, 32 KB SRAM).
project(fleetbus_firmware C CXX ASM)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(TP ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
if(CMAKE_CROSSCOMPILING)
  # CMake's GNU defaults append "-O3 -DNDEBUG" after the toolchain's Release flags, so -Os never applied (found in P0).
  set(CMAKE_C_FLAGS_RELEASE "-Os -g3 -DNDEBUG")
  set(CMAKE_CXX_FLAGS_RELEASE "-Os -g3 -DNDEBUG")
else()
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)   # host only: the simulator is a shared library loaded by Python
endif()

# The portable node logic (C++17) plus the generated CAN codec (C). Same sources on the board and on the host.
# Embedded C++ subset: no exceptions, no RTTI, no thread-safe statics (no guard locks), no heap in this library.
file(GLOB COMMON_SRC CONFIGURE_DEPENDS common/*.c common/*.cpp)
add_library(fb_common STATIC ${COMMON_SRC})
target_include_directories(fb_common PUBLIC common)
target_compile_options(fb_common PRIVATE -Wall -Wextra -Wshadow
  $<$<COMPILE_LANGUAGE:CXX>:-Wconversion;-Wno-sign-conversion;-Wnon-virtual-dtor>)
target_compile_options(fb_common PUBLIC
  $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions;-fno-rtti;-fno-threadsafe-statics>)

if(CMAKE_CROSSCOMPILING)
  add_subdirectory(arm/mspm0g3507)
else()
  enable_testing()
  add_subdirectory(tests)
  add_subdirectory(host)
endif()
