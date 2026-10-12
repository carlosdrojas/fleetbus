cmake_minimum_required(VERSION 3.25)
# Host build: pure-C common library + Unity tests. The cross build lives in CMakeLists.arm.cmake (M2),
# selected with -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake.
project(fleetbus_firmware C)
set(CMAKE_C_STANDARD 11)
add_compile_options(-Wall -Wextra -Wshadow -Wconversion -Wno-sign-conversion)

set(TP ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
file(GLOB COMMON_SRC common/*.c)
add_library(fb_common STATIC ${COMMON_SRC})
target_include_directories(fb_common PUBLIC common ${TP}/monocypher/src ${TP}/monocypher/src/optional)

if(NOT CMAKE_CROSSCOMPILING)
  enable_testing()
  add_subdirectory(tests)
endif()
