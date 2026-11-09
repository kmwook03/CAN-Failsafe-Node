#ifndef M3_RTOS_TEST_H
#define M3_RTOS_TEST_H

#include <stdint.h>

typedef enum {
    M3_FAULT_NONE = 0,
    M3_FAULT_ASSERT,
    M3_FAULT_STACK_OVERFLOW,
    M3_FAULT_ALLOCATION,
    M3_FAULT_KERNEL,
    M3_FAULT_HAL
} M3_Fault;

/* Read in the debugger; stack and heap measurements are bytes. */
typedef struct {
    uint32_t led_toggles;
    uint32_t samples;
    uint32_t rtos_tick;
    uint32_t hal_ms;
    uint32_t rtos_delta;
    uint32_t hal_delta;
    uint32_t led_stack_free;
    uint32_t monitor_stack_free;
    uint32_t can_stack_free;
    uint32_t heap_min_free;
    uint32_t tick_mismatches;
    uint32_t led_deadline_misses;
    uint32_t monitor_deadline_misses;
    uint32_t uart_errors;
    uint32_t fatal_reason;
} M3_RtosStats;

extern volatile M3_RtosStats m3;

void M3_LedTask(void *argument);
void M3_MonitorTask(void *argument);
void M3_RtosFatal(M3_Fault reason) __attribute__((noreturn));

#endif
