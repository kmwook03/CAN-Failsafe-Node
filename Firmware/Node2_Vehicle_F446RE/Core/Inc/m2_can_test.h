#ifndef M2_CAN_TEST_H
#define M2_CAN_TEST_H

#include "main.h"

/* Historical M2 module; restore the m2-baseline-before-m3 tag to run M2. */
#ifndef M2_TEST_ENABLED
#define M2_TEST_ENABLED 0
#endif

#define M2_FRAME_COUNT 3000U

typedef struct {
    uint32_t loopback;
    uint32_t tx_attempted;
    uint32_t tx_queued;
    uint32_t tx_completed;
    uint32_t rx_total;
    uint32_t rx_ok;
    uint32_t bad_frames;
    uint32_t tx_fail;
    uint32_t schedule_slips;
    uint32_t hal_error_events;
    uint32_t error_mask;
    uint32_t last_esr;
    uint32_t bus_off;
    uint32_t rx_timeout;
    uint32_t done;
    uint32_t passed;
} M2_CAN_TestStats;

extern volatile M2_CAN_TestStats m2;

/* Call once after MX_CAN1_Init. Mode follows the CubeMX configuration. */
HAL_StatusTypeDef M2_CAN_Test_Start(CAN_HandleTypeDef *hcan);
void M2_CAN_Test_Process(void);

#endif
