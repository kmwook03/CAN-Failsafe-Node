#include "m3_rtos_test.h"
#include "m3_can_test.h"
#include "main.h"
#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>

extern UART_HandleTypeDef huart2;
extern osThreadId_t ledTaskHandle;
extern osThreadId_t canRxTaskHandle;
extern osThreadId_t canTxTaskHandle;

volatile M3_RtosStats m3;

static void send_log(const char *data, uint16_t length)
{
    /* Only the monitor task writes UART during the RTOS test. */
    if (HAL_UART_Transmit(&huart2, (const uint8_t *)data, length, 100U) != HAL_OK) {
        m3.uart_errors++;
    }
}

static void wait_period(uint32_t *next, uint32_t period,
                        volatile uint32_t *misses)
{
    *next += period;
    if (osDelayUntil(*next) != osOK) {
        /* Report a late deadline; do not busy-loop trying to catch up. */
        (*misses)++;
        *next = osKernelGetTickCount() + period;
        if (osDelayUntil(*next) != osOK) {
            M3_RtosFatal(M3_FAULT_KERNEL);
        }
    }
}

static void send_formatted(char *line, size_t capacity, int length)
{
    if ((length > 0) && ((size_t)length < capacity)) {
        send_log(line, (uint16_t)length);
    } else {
        m3.uart_errors++;
    }
}

static void log_can_status(const M3_CAN_TestStats *can, char *line, size_t capacity)
{
    const char *state = can->done ? (can->passed ? "PASS" : "FAIL") :
        (can->processed ? "RUN" : "WAIT");
    int length = snprintf(line, capacity,
        "[M3-N1-CAN] %s: tx=%lu completed=%lu rx=%lu bad=%lu tx_fail=%lu "
        "slips=%lu errors=%lu mask=0x%08lX bus_off=%lu timeout=%lu initErr=%lu stopErr=%lu\r\n",
        state, (unsigned long)can->tx_queued, (unsigned long)can->tx_completed,
        (unsigned long)can->rx_ok, (unsigned long)can->bad_frames,
        (unsigned long)can->tx_fail, (unsigned long)can->schedule_slips,
        (unsigned long)can->hal_error_events,
        (unsigned long)can->error_mask, (unsigned long)can->bus_off,
        (unsigned long)can->rx_timeout, (unsigned long)can->start_errors,
        (unsigned long)can->stop_errors);
    send_formatted(line, capacity, length);
    length = snprintf(line, capacity,
        "[M3-N1-CAN] queue: received=%lu enqueued=%lu processed=%lu dropped=%lu "
        "readErr=%lu qErr=%lu peak=%lu maxWaitMs=%lu\r\n",
        (unsigned long)can->isr_received, (unsigned long)can->enqueued,
        (unsigned long)can->processed, (unsigned long)can->queue_dropped,
        (unsigned long)can->read_errors, (unsigned long)can->queue_errors,
        (unsigned long)can->queue_peak, (unsigned long)can->queue_max_wait_ms);
    send_formatted(line, capacity, length);
    length = snprintf(line, capacity,
        "[M3-N1-CAN] timing: periodMs=20 maxLateTicks=%lu deadlineErr=%lu txStackB=%lu rxStackB=%lu\r\n",
        (unsigned long)can->tx_max_late_ticks, (unsigned long)can->deadline_errors,
        (unsigned long)m3.tx_stack_free, (unsigned long)m3.rx_stack_free);
    send_formatted(line, capacity, length);
}

void M3_LedTask(void *argument)
{
    (void)argument;
    uint32_t next = osKernelGetTickCount();
    if (osKernelGetTickFreq() != 1000U) {
        M3_RtosFatal(M3_FAULT_KERNEL);
    }
    for (;;) {
        BSP_LED_Toggle(LED_GREEN);
        m3.led_toggles++;
        wait_period(&next, 500U, &m3.led_deadline_misses);
    }
}

void M3_MonitorTask(void *argument)
{
    (void)argument;
    const char banner[] = "\r\n[M3-N1] Node 1 controller RTOS: FreeRTOS "
        tskKERNEL_VERSION_NUMBER ", CMSIS-RTOS2, SysTick=1kHz, HAL=TIM6\r\n";
    send_log(banner, (uint16_t)(sizeof(banner) - 1U));
    uint32_t next = osKernelGetTickCount();
    uint32_t previous_tick = next;
    uint32_t previous_hal = HAL_GetTick();
    uint32_t can_announced = 0U;
    uint32_t can_reported = 0U;

    for (;;) {
        wait_period(&next, 1000U, &m3.monitor_deadline_misses);
        const uint32_t now_tick = osKernelGetTickCount();
        const uint32_t now_hal = HAL_GetTick();
        m3.rtos_tick = now_tick;
        m3.hal_ms = now_hal;
        m3.rtos_delta = now_tick - previous_tick;
        m3.hal_delta = now_hal - previous_hal;
        previous_tick = now_tick;
        previous_hal = now_hal;
        /* Offsets differ at boot; compare elapsed time, not absolute ticks. */
        const uint32_t difference = (m3.rtos_delta > m3.hal_delta)
            ? m3.rtos_delta - m3.hal_delta : m3.hal_delta - m3.rtos_delta;
        if (difference > 5U) {
            m3.tick_mismatches++;
        }
        m3.led_stack_free = osThreadGetStackSpace(ledTaskHandle);
        m3.monitor_stack_free = osThreadGetStackSpace(osThreadGetId());
        m3.tx_stack_free = osThreadGetStackSpace(canTxTaskHandle);
        m3.rx_stack_free = osThreadGetStackSpace(canRxTaskHandle);
        m3.heap_min_free = (uint32_t)xPortGetMinimumEverFreeHeapSize();
        m3.samples++;

        char line[320];
        const int length = snprintf(line, sizeof(line),
            "[M3-N1] n=%lu led=%lu dt=%lu/%lu stackB=%lu/%lu heapMinB=%lu "
            "tickErr=%lu late=%lu/%lu uartErr=%lu txStackB=%lu rxStackB=%lu\r\n",
            (unsigned long)m3.samples, (unsigned long)m3.led_toggles,
            (unsigned long)m3.rtos_delta, (unsigned long)m3.hal_delta,
            (unsigned long)m3.led_stack_free, (unsigned long)m3.monitor_stack_free,
            (unsigned long)m3.heap_min_free, (unsigned long)m3.tick_mismatches,
            (unsigned long)m3.led_deadline_misses,
            (unsigned long)m3.monitor_deadline_misses, (unsigned long)m3.uart_errors,
            (unsigned long)m3.tx_stack_free, (unsigned long)m3.rx_stack_free);
        send_formatted(line, sizeof(line), length);

        M3_CAN_TestStats can;
        M3_CAN_Test_GetStats(&can);
        if ((can.ready != 0U) && (can.done == 0U) && (can_announced == 0U)) {
            const char ready[] = "[M3-N1-CAN] READY: NORMAL TX/RX, 500kbps, 3000 frames, period=20ms\r\n";
            send_log(ready, (uint16_t)(sizeof(ready) - 1U));
            can_announced = 1U;
        }
        if (((can.done != 0U) && (can_reported == 0U)) ||
            ((can.done == 0U) && ((m3.samples % 5U) == 0U))) {
            log_can_status(&can, line, sizeof(line));
            if (can.done != 0U) {
                can_reported = 1U;
            }
        }
    }
}

void M3_RtosFatal(M3_Fault reason)
{
    /* No printf, allocation or blocking API from assertion/overflow hooks. */
    __disable_irq();
    m3.fatal_reason = (uint32_t)reason;
    for (;;) {
        __NOP();
    }
}
