# Cross build for the LP-MSPM0G3507 LaunchPad (Cortex-M0+). Every image is linked at 0x0 and flashed
# with tools/flash_mspm0.sh. Vendor layer: TI MSPM0 SDK subset in third_party/mspm0_sdk (tools/vendor_mspm0_sdk.sh)
# and the FreeRTOS ARM_CM0 port.
add_link_options(--specs=nano.specs --specs=nosys.specs -Wl,--gc-sections -Wl,--print-memory-usage)
set(FB_VERSION 1 CACHE STRING "Firmware version sent in every heartbeat")
option(FB_LCD_GREENTAB "Adafruit 1.8in TFT is a green-tab panel (BGR + 1/2 px offsets); default black tab" OFF)
# Without the INA219 every current read NACKs, sensor_ok goes false and the battery node latches FAULT then SAFE.
option(FB_HAS_INA219 "Battery node has an INA219 current sensor on I2C1; OFF reports 0 mA and skips I2C" OFF)

set(SDK ${TP}/mspm0_sdk)
set(BSPDIR ${CMAKE_CURRENT_SOURCE_DIR}/../../bsp)
set(APPDIR ${CMAKE_CURRENT_SOURCE_DIR}/../../app)

file(GLOB DL_SRC ${SDK}/ti/driverlib/*.c ${SDK}/ti/driverlib/m0p/*.c ${SDK}/ti/driverlib/m0p/sysctl/*.c)
add_library(driverlib STATIC ${DL_SRC})
target_compile_definitions(driverlib PUBLIC __MSPM0G3507__)
target_include_directories(driverlib PUBLIC ${SDK} ${SDK}/CMSIS/Core/Include)
target_compile_options(driverlib PRIVATE -w)        # vendor code: not ours to warn about

set(RTOS ${TP}/freertos)
add_library(freertos STATIC ${RTOS}/tasks.c ${RTOS}/queue.c ${RTOS}/list.c
    ${RTOS}/portable/GCC/ARM_CM0/port.c ${RTOS}/portable/GCC/ARM_CM0/portasm.c ${RTOS}/portable/MemMang/heap_4.c)
target_include_directories(freertos PUBLIC ${RTOS}/include ${RTOS}/portable/GCC/ARM_CM0 ${BSPDIR}/mspm0g3507)
target_link_libraries(freertos PUBLIC driverlib)
target_compile_options(freertos PRIVATE -w)

set(STARTUP ${SDK}/ti/devices/msp/m0p/startup_mspm0g350x_gcc.c)
add_library(bsp STATIC ${BSPDIR}/bsp_log.c ${BSPDIR}/mspm0g3507/bsp_clock.c ${BSPDIR}/mspm0g3507/bsp_core.c
    ${BSPDIR}/mspm0g3507/bsp_can.c ${BSPDIR}/mspm0g3507/bsp_sensors.c ${BSPDIR}/mspm0g3507/bsp_display.c)
target_include_directories(bsp PUBLIC ${BSPDIR} ${BSPDIR}/mspm0g3507)
target_link_libraries(bsp PUBLIC driverlib freertos fb_common)
target_compile_options(bsp PRIVATE -Wall -Wextra)
if(FB_LCD_GREENTAB)
  target_compile_definitions(bsp PRIVATE FB_LCD_GREENTAB=1)
endif()

set(APP_ORIGIN 0x00000000)
set(APP_LENGTH "128K")
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/app.ld.in ${CMAKE_CURRENT_BINARY_DIR}/app.ld @ONLY)

function(fb_image name)
  add_executable(${name} ${ARGN} ${STARTUP})
  target_link_libraries(${name} PRIVATE bsp freertos)
  target_link_options(${name} PRIVATE -T${CMAKE_CURRENT_BINARY_DIR}/app.ld -L${CMAKE_CURRENT_SOURCE_DIR} -Wl,-Map=${name}.map)
  set_target_properties(${name} PROPERTIES SUFFIX ".elf" LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/common.ld)
  add_custom_command(TARGET ${name} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${name}> $<TARGET_FILE:${name}>.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${name}>
    COMMENT "${name}: .bin + size")
endfunction()

# ---- bench images (C, bsp.h only): P2 blink + log, P3 CAN self-test, P4 sensors and actuators
fb_image(bringup ${APPDIR}/bringup/bringup_main.c)
fb_image(can_selftest ${APPDIR}/bringup/can_selftest_main.c)
fb_image(io_selftest ${APPDIR}/bringup/io_selftest_main.c)

# ---- the two nodes (C++17). Flash: tools/flash_mspm0.sh node battery|body
foreach(role battery body)
  string(TOUPPER ${role} ROLE_UP)
  fb_image(node_${role} ${APPDIR}/app_main.cpp ${APPDIR}/${role}_hw.cpp)
  target_include_directories(node_${role} PRIVATE ${APPDIR})
  target_compile_definitions(node_${role} PRIVATE FB_ROLE_${ROLE_UP} FB_FW_VERSION=${FB_VERSION})
  target_compile_options(node_${role} PRIVATE -Wall -Wextra)
  if(FB_HAS_INA219)
    target_compile_definitions(node_${role} PRIVATE FB_HAS_INA219=1)
  endif()
endforeach()
target_compile_definitions(node_body PRIVATE FB_HAS_DISPLAY=1)    # ST7735R instrument cluster
