#pragma once

 /* ====== Core kernel features ====== */
 #define configUSE_PREEMPTION                    1
 #define configUSE_TIME_SLICING                  1
 #define configUSE_IDLE_HOOK                     0
 #define configUSE_TICK_HOOK                     0
 #define configCPU_CLOCK_HZ                      ( 125000000UL )   /* default Pico clock */
 #define configTICK_RATE_HZ                      ( 1000 )
 #define configMAX_PRIORITIES                    ( 7 )
 #define configMINIMAL_STACK_SIZE                ( 256 )
 #define configTOTAL_HEAP_SIZE                   ( ( size_t ) ( 64 * 1024 ) )  /* heap_4 */

 /* ====== SMP on RP2040 (2 cores) ====== */
 #define configNUMBER_OF_CORES                   2
 #define configRUN_MULTIPLE_PRIORITIES           1

 /* ====== Queues/semaphores/timers ====== */
 #define configUSE_16_BIT_TICKS                  0
 #define configUSE_MUTEXES                       1
 #define configUSE_RECURSIVE_MUTEXES             1
 #define configUSE_COUNTING_SEMAPHORES           1
 #define configUSE_TIMERS                        1
 #define configTIMER_TASK_PRIORITY               ( configMAX_PRIORITIES - 1 )
 #define configTIMER_QUEUE_LENGTH                10
 #define configTIMER_TASK_STACK_DEPTH            ( 1024 )

 /* ====== API functions we want ====== */
 #define INCLUDE_vTaskDelay                      1
 #define INCLUDE_vTaskDelayUntil                 1
 #define INCLUDE_vTaskSuspend                    1
 #define INCLUDE_vTaskDelete                     1
 #define INCLUDE_xTaskGetCurrentTaskHandle       1
 #define INCLUDE_xTaskGetIdleTaskHandle          1
 #define INCLUDE_uxTaskPriorityGet               1
 #define INCLUDE_xTimerPendFunctionCall          1

 /* ====== Interrupt priorities (Cortex-M0+) ====== */
 #define configPRIO_BITS                         2
 #define configLIBRARY_LOWEST_INTERRUPT_PRIORITY 3
 #define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 1
 #define configKERNEL_INTERRUPT_PRIORITY         ( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )
 #define configMAX_SYSCALL_INTERRUPT_PRIORITY    ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )

 /* ====== Assert hook to help debugging ====== */
#ifdef __cplusplus
 extern "C" {
#endif
 void vAssertCalled( const char * file, int line );
#ifdef __cplusplus
 }
#endif
 #define configASSERT( x ) if( ( x ) == 0 ) vAssertCalled(__FILE__, __LINE__)

 #define configUSE_PASSIVE_IDLE_HOOK 0
 #define configUSE_CORE_AFFINITY 1