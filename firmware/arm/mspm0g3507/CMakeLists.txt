# Cross build for FB_BOARD=mspm0g3507 (LP-MSPM0G3507 LaunchPad, Cortex-M0+). UNVERIFIED ON HARDWARE: everything here
# compiles and links; nothing has run on a board. Vendor layer: TI MSPM0 SDK subset in third_party/mspm0_sdk
# (tools/vendor_mspm0_sdk.sh), FreeRTOS ARM_CM0 port.
add_link_options(--specs=nano.specs --specs=nosys.specs -Wl,--gc-sections -Wl,--print-memory-usage)
set(FB_VERSION 1 CACHE STRING "Firmware image version stamped into signed images")

set(SDK ${TP}/mspm0_sdk)
set(BSPDIR ${CMAKE_CURRENT_SOURCE_DIR}/../../bsp)
file(GLOB DL_SRC ${SDK}/ti/driverlib/*.c ${SDK}/ti/driverlib/m0p/*.c ${SDK}/ti/driverlib/m0p/sysctl/*.c)
add_library(driverlib STATIC ${DL_SRC})
target_compile_definitions(driverlib PUBLIC __MSPM0G3507__)
target_include_directories(driverlib PUBLIC ${SDK} ${SDK}/CMSIS/Core/Include)
target_compile_options(driverlib PRIVATE -w)

set(RTOS ${TP}/freertos)
add_library(freertos STATIC ${RTOS}/tasks.c ${RTOS}/queue.c ${RTOS}/list.c
    ${RTOS}/portable/GCC/ARM_CM0/port.c ${RTOS}/portable/GCC/ARM_CM0/portasm.c ${RTOS}/portable/MemMang/heap_4.c)
target_include_directories(freertos PUBLIC ${RTOS}/include ${RTOS}/portable/GCC/ARM_CM0 ${BSPDIR}/mspm0g3507)
target_link_libraries(freertos PUBLIC driverlib)
target_compile_options(freertos PRIVATE -w)

set(STARTUP ${SDK}/ti/devices/msp/m0p/startup_mspm0g350x_gcc.c)
# bsp_boot: what the bootloader may use (clock, GPIO, watchdog, flash). bsp: the full set for applications.
add_library(bsp_boot STATIC ${BSPDIR}/mspm0g3507/bsp_clock.c)
target_link_libraries(bsp_boot PUBLIC driverlib fb_common)
target_include_directories(bsp_boot PUBLIC ${BSPDIR} ${BSPDIR}/mspm0g3507)
target_compile_options(bsp_boot PRIVATE -Wall -Wextra)
add_library(bsp STATIC ${BSPDIR}/bsp_log.c ${BSPDIR}/mspm0g3507/bsp_core.c ${BSPDIR}/mspm0g3507/bsp_can.c ${BSPDIR}/mspm0g3507/bsp_sensors.c)
target_link_libraries(bsp PUBLIC bsp_boot freertos)
target_compile_options(bsp PRIVATE -Wall -Wextra)

function(fb_finish tgt)
  add_custom_command(TARGET ${tgt} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${tgt}> $<TARGET_FILE:${tgt}>.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${tgt}>
    COMMENT "${tgt}: .bin + size")
endfunction()

# ---- bench images, linked at 0x0 with no bootloader: P2 blink + log, P3 CAN self-test
set(APP_ORIGIN 0x00000000)
set(APP_LENGTH "128K")
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/app.ld.in ${CMAKE_CURRENT_BINARY_DIR}/bringup.ld @ONLY)
function(add_bench_image name src)
  add_executable(${name} ${src} ${STARTUP})
  target_link_libraries(${name} bsp freertos)
  target_link_options(${name} PRIVATE -T${CMAKE_CURRENT_BINARY_DIR}/bringup.ld -L${CMAKE_CURRENT_SOURCE_DIR} -Wl,-Map=${name}.map)
  set_target_properties(${name} PROPERTIES SUFFIX ".elf" LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/common.ld)
  fb_finish(${name})
endfunction()
add_bench_image(bringup ../../app/bringup/bringup_main.c)
add_bench_image(can_selftest ../../app/bringup/can_selftest_main.c)   # P3: BENCH ONLY, never on the car bus
add_bench_image(io_selftest ../../app/bringup/io_selftest_main.c)     # P4: sensors and actuators
