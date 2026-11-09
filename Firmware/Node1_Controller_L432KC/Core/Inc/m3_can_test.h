#ifndef M3_CAN_TEST_H
#define M3_CAN_TEST_H

#include "main.h"

#define M3_CAN_FRAME_COUNT 3000U
#define M3_CAN_QUEUE_DEPTH 16U

typedef struct {
    uint32_t ready;
    uint32_t done;
    uint32_t passed;
    uint32_t isr_received;
    uint32_t enqueued;
    uint32_t processed;
    uint32_t rx_ok;
    uint32_t bad_frames;
    uint32_t tx_attempted;
    uint32_t tx_queued;
    uint32_t tx_completed;
    uint32_t tx_fail;
    uint32_t queue_dropped;
    uint32_t queue_peak;
    uint32_t queue_max_wait_ms;
    uint32_t queue_errors;
    uint32_t read_errors;
    uint32_t hal_error_events;
    uint32_t error_mask;
    uint32_t bus_off;
    uint32_t rx_timeout;
    uint32_t start_errors;
    uint32_t stop_errors;
    uint32_t schedule_slips;
    uint32_t tx_max_late_ticks;
    uint32_t deadline_errors;
} M3_CAN_TestStats;

extern volatile M3_CAN_TestStats m3_can;

/* After osKernelInitialize(), before osKernelStart(): allocate the RX queue. */
void M3_CAN_Test_Init(CAN_HandleTypeDef *hcan);
/* LSE-referenced MSI calibration, retained from the passing M2 baseline. */
HAL_StatusTypeDef M3_CAN_Test_PrepareClock(void);
/* TX owns CAN start/stop; RX validates copied echo frames. */
void M3_CAN_Test_TxTask(void *argument);
void M3_CAN_Test_RxTask(void *argument);
/* Task-context coherent snapshot; UART output belongs to monitorTask only. */
void M3_CAN_Test_GetStats(M3_CAN_TestStats *stats);

#endif
