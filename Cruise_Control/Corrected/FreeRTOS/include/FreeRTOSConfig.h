#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

// ─────────────────────────────────────────────
//  Core Configuration
// ─────────────────────────────────────────────
#define configUSE_PREEMPTION                    1
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configCPU_CLOCK_HZ                      ( ( unsigned long ) 16000000 )
#define configTICK_RATE_HZ                      ( ( TickType_t ) 1000 )     // 1ms tick
#define configMAX_PRIORITIES                    5
#define configMINIMAL_STACK_SIZE                ( ( unsigned short ) 128 )
#define configTOTAL_HEAP_SIZE                   ( ( size_t ) ( 8192 ) )     // 8KB heap
#define configMAX_TASK_NAME_LEN                 16
#define configUSE_TRACE_FACILITY                0
#define configUSE_16_BIT_TICKS                  0
#define configIDLE_SHOULD_YIELD                 1
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             0
#define configUSE_COUNTING_SEMAPHORES           0
#define configQUEUE_REGISTRY_SIZE               0
#define configUSE_APPLICATION_TASK_TAG          0

// ─────────────────────────────────────────────
//  Memory Allocation
// ─────────────────────────────────────────────
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configSUPPORT_STATIC_ALLOCATION         0

// ─────────────────────────────────────────────
//  Software Timers (not used, keep disabled)
// ─────────────────────────────────────────────
#define configUSE_TIMERS                        0
#define configTIMER_TASK_PRIORITY               0
#define configTIMER_QUEUE_LENGTH                0
#define configTIMER_TASK_STACK_DEPTH            0

// ─────────────────────────────────────────────
//  Cortex-M4 Interrupt Priority Settings
//  MUST be configured correctly or FreeRTOS
//  will crash on context switches
// ─────────────────────────────────────────────
#ifdef __NVIC_PRIO_BITS
    #define configPRIO_BITS                     __NVIC_PRIO_BITS
#else
    #define configPRIO_BITS                     3   // TM4C123 has 3 priority bits = 8 levels
#endif

// Lowest interrupt priority FreeRTOS will manage
// Must be set to the lowest priority (highest number)
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY         0x07

// Highest interrupt priority that calls FreeRTOS API
// Any ISR that calls FreeRTOS functions MUST have
// priority >= this value (numerically)
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY    0x04

// Do not change these — they shift the values into
// the correct bit positions for the NVIC
#define configKERNEL_INTERRUPT_PRIORITY     \
    ( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )

#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
    ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )

// ─────────────────────────────────────────────
//  API Functions to Include
// ─────────────────────────────────────────────
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_vTaskDelayUntil                 1
#define INCLUDE_vTaskDelete                     0
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_xTaskGetTickCount               1
#define INCLUDE_uxTaskGetStackHighWaterMark     1   // useful for debugging stack overflow
#define INCLUDE_xTaskGetCurrentTaskHandle       0

// ─────────────────────────────────────────────
//  Stack Overflow Detection
//  Method 2 = fills stack with pattern and checks
//  Define vApplicationStackOverflowHook in main.c
// ─────────────────────────────────────────────
#define configCHECK_FOR_STACK_OVERFLOW          2

// ─────────────────────────────────────────────
//  Assert — halts on FreeRTOS internal errors
// ─────────────────────────────────────────────
#define configASSERT( x )   if( ( x ) == 0 ) { taskDISABLE_INTERRUPTS(); while(1); }

// ─────────────────────────────────────────────
//  Map FreeRTOS port interrupt handlers to
//  the names used in startup.c vector table
// ─────────────────────────────────────────────
#define vPortSVCHandler     SVC_Handler
#define xPortPendSVHandler  PendSV_Handler
#define xPortSysTickHandler SysTick_Handler

#endif /* FREERTOS_CONFIG_H */