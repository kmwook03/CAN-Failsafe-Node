#ifndef M2_CAN_TEST_H
#define M2_CAN_TEST_H

#include "main.h"

/* Archived M2 module. Restore the M2 baseline to run the bare-metal test. */
#ifndef M2_TEST_ENABLED
#define M2_TEST_ENABLED 0
#endif

#define M2_FRAME_COUNT 3000U

typedef struct {
    uint32_t loopback;
    uint32_t tx_attempted;
    uint32_t tx_queued;
    uint32_t rx_ok;
    uint32_t bad_frames;
    uint32_t tx_fail;
    uint32_t schedule_slips;
    uint32_t hal_error_events;
    uint32_t error_mask;
    uint32_t last_esr;
    uint32_t bus_off;
    uint32_t done;
    uint32_t passed;
} M2_CAN_TestStats;

/* Watch "m2" in the debugger. tx_queued alone does not prove delivery. */
extern volatile M2_CAN_TestStats m2;

HAL_StatusTypeDef M2_CAN_Test_PrepareClock(void);
/* Call once after MX_CAN1_Init. Mode is read from the CubeMX configuration. */
HAL_StatusTypeDef M2_CAN_Test_Start(CAN_HandleTypeDef *hcan);
void M2_CAN_Test_Process(void);

#endif
