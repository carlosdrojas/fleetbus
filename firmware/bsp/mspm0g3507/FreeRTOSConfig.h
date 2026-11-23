#pragma once
/* FreeRTOS for the MSPM0G3507 (Cortex-M0+, ARM_CM0 port). Tick from SysTick at the 80 MHz CPU clock.
 * Heap and stack sizes are estimates: the node firmware prints free stack and heap every second (P5). */
#define configUSE_PREEMPTION                     1
#define configCPU_CLOCK_HZ                       80000000u   /* MCLK from SYSPLL, set by bsp_clock_init() */
#define configTICK_RATE_HZ                       1000
#define configMAX_PRIORITIES                     6
#define configMINIMAL_STACK_SIZE                 128
#define configTOTAL_HEAP_SIZE                    (12 * 1024)   /* of 32 KB SRAM */
#define configMAX_TASK_NAME_LEN                  12
#define configTICK_TYPE_WIDTH_IN_BITS            TICK_TYPE_WIDTH_32_BITS
#define configUSE_MUTEXES                        1
#define configUSE_COUNTING_SEMAPHORES            0
#define configQUEUE_REGISTRY_SIZE                0
#define configUSE_IDLE_HOOK                      0
#define configUSE_TICK_HOOK                      0
#define configCHECK_FOR_STACK_OVERFLOW           2
#define configUSE_MALLOC_FAILED_HOOK             1
#define configSUPPORT_DYNAMIC_ALLOCATION         1
#define configSUPPORT_STATIC_ALLOCATION          0
#define configUSE_TIMERS                         0
#define configUSE_TRACE_FACILITY                 0
#define configENABLE_MPU                         0
#define configENABLE_FPU                         0
#define configENABLE_TRUSTZONE                   0

/* Cortex-M0+ has no BASEPRI: the ARM_CM0 port masks with PRIMASK. NVIC has 2 priority bits (4 levels). */
#define configKERNEL_INTERRUPT_PRIORITY          (3u << 6)

#define INCLUDE_vTaskDelay                       1
#define INCLUDE_vTaskDelayUntil                  1
#define INCLUDE_xTaskGetTickCount                1
#define INCLUDE_uxTaskGetStackHighWaterMark      1
#define INCLUDE_vTaskDelete                      0

/* TI startup (startup_mspm0g350x_gcc.c) names the exception vectors CMSIS-style. */
#define vPortSVCHandler     SVC_Handler
#define xPortPendSVHandler  PendSV_Handler
#define xPortSysTickHandler SysTick_Handler
