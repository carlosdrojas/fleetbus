cmake_minimum_required(VERSION 3.25)
# MOCK(M2): the cross build (HAL, FreeRTOS, per-node targets, size report) is added in M2.
# Today only the host-testable common/ library and the Unity tests are described here.
project(fleetbus_firmware C)

add_library(fb_common STATIC common/crc32.c common/fault_fsm.c common/hb_monitor.c common/can_msgs.c)
target_include_directories(fb_common PUBLIC common)

enable_testing()
add_subdirectory(tests)
