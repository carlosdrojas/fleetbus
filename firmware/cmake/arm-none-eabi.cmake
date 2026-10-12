# MOCK(M2): toolchain file for STM32G431RB (Cortex-M4F, hard float). Flags are unverified.
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_C_FLAGS_INIT "-mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb --specs=nano.specs")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
