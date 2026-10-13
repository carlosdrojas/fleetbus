#pragma once
#include <stdint.h>
extern uint32_t SystemCoreClock;

#define configUSE_PREEMPTION                     1
#define configCPU_CLOCK_HZ                       (SystemCoreClock)
#define configTICK_RATE_HZ                       1000
#define configMAX_PRIORITIES                     6
#define configMINIMAL_STACK_SIZE                 128
#define configTOTAL_HEAP_SIZE                    (12 * 1024)   /* of 32 KB RAM; measured stack high-water marks in docs once on hardware */
#define configMAX_TASK_NAME_LEN                  12
#define configUSE_16_BIT_TICKS                   0
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
#define configENABLE_FPU                         1
#define configENABLE_MPU                         0
#define configENABLE_TRUSTZONE                   0

#define configPRIO_BITS                          4
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY  15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5
#define configKERNEL_INTERRUPT_PRIORITY          (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY     (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

#define INCLUDE_vTaskDelay                       1
#define INCLUDE_vTaskDelayUntil                  1
#define INCLUDE_xTaskGetTickCount                1
#define INCLUDE_uxTaskGetStackHighWaterMark      1
#define INCLUDE_vTaskDelete                      0

#define vPortSVCHandler     SVC_Handler
#define xPortPendSVHandler  PendSV_Handler
#define xPortSysTickHandler SysTick_Handler
