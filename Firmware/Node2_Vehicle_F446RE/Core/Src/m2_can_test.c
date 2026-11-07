#include "m2_can_test.h"

#if M2_TEST_ENABLED

#define M2_COMMAND_ID     0x100U
#define M2_REPLY_ID       0x200U
#define M2_PERIOD_MS      20U
#define M2_START_DELAY_MS 2000U
#define M2_DRAIN_MS       100U
#define M2_RX_TIMEOUT_MS  1000U
#define M2_MAX_RUN_MS     65000U

volatile M2_CAN_TestStats m2;
static CAN_HandleTypeDef *s_can;
static uint32_t s_rx_id;
static uint32_t s_rx_seq;
static uint32_t s_next_tx;
static uint32_t s_last_tx;
static volatile uint32_t s_first_rx;
static volatile uint32_t s_last_rx;
static volatile uint32_t s_rx_started;
static const uint32_t s_notifications = CAN_IT_TX_MAILBOX_EMPTY |
    CAN_IT_RX_FIFO0_MSG_PENDING | CAN_IT_RX_FIFO0_OVERRUN |
    CAN_IT_ERROR_WARNING | CAN_IT_ERROR_PASSIVE | CAN_IT_BUSOFF |
    CAN_IT_LAST_ERROR_CODE | CAN_IT_ERROR;

HAL_StatusTypeDef M2_CAN_Test_Start(CAN_HandleTypeDef *hcan)
{
    CAN_FilterTypeDef filter = {0};
    if ((hcan == NULL) || (s_can != NULL) || (hcan->Instance != CAN1) ||
        ((hcan->Init.Mode != CAN_MODE_LOOPBACK) &&
         (hcan->Init.Mode != CAN_MODE_NORMAL)) ||
        (HAL_RCC_GetPCLK1Freq() != 40000000U) ||
        (hcan->Init.Prescaler != 5U) ||
        (hcan->Init.TimeSeg1 != CAN_BS1_13TQ) ||
        (hcan->Init.TimeSeg2 != CAN_BS2_2TQ) ||
        (hcan->Init.SyncJumpWidth != CAN_SJW_2TQ)) {
        return HAL_ERROR;
    }

    m2 = (M2_CAN_TestStats){0};
    m2.loopback = (hcan->Init.Mode == CAN_MODE_LOOPBACK);
    s_rx_id = m2.loopback ? M2_REPLY_ID : M2_COMMAND_ID;
    s_rx_seq = 0U;
    s_rx_started = 0U;
    filter.FilterBank = 0;
    filter.FilterMode = CAN_FILTERMODE_IDMASK;
    filter.FilterScale = CAN_FILTERSCALE_32BIT;
    filter.FilterIdHigh = s_rx_id << 5;
    filter.FilterMaskIdHigh = 0x7FFU << 5;
    filter.FilterMaskIdLow = 0x0006U; /* Match standard data frames. */
    filter.FilterFIFOAssignment = CAN_RX_FIFO0;
    filter.FilterActivation = ENABLE;
    filter.SlaveStartFilterBank = 14;
    if (HAL_CAN_ConfigFilter(hcan, &filter) != HAL_OK) {
        return HAL_ERROR;
    }
    if (HAL_CAN_Start(hcan) != HAL_OK) {
        return HAL_ERROR;
    }
    s_can = hcan;
    if (HAL_CAN_ActivateNotification(hcan, s_notifications) != HAL_OK) {
        (void)HAL_CAN_Stop(hcan);
        s_can = NULL;
        return HAL_ERROR;
    }
    s_next_tx = HAL_GetTick() + M2_START_DELAY_MS;
    return HAL_OK;
}

/* Called only by main in loopback, and only by RX ISR in normal mode. */
static void M2_Send(uint8_t data[8])
{
    CAN_TxHeaderTypeDef tx = {0};
    uint32_t mailbox;
    if (m2.bus_off != 0U) {
        return;
    }
    m2.tx_attempted++;
    tx.StdId = M2_REPLY_ID;
    tx.IDE = CAN_ID_STD;
    tx.RTR = CAN_RTR_DATA;
    tx.DLC = 8;
    tx.TransmitGlobalTime = DISABLE;
    if ((HAL_CAN_GetTxMailboxesFreeLevel(s_can) == 0U) ||
        (HAL_CAN_AddTxMessage(s_can, &tx, data, &mailbox) != HAL_OK)) {
        m2.tx_fail++;
    } else {
        m2.tx_queued++;
    }
}

static void M2_Finish(void)
{
    if (HAL_CAN_DeactivateNotification(s_can, s_notifications) != HAL_OK) {
        m2.hal_error_events++;
        m2.error_mask |= HAL_CAN_GetError(s_can);
    }
    if (HAL_CAN_Stop(s_can) != HAL_OK) {
        m2.hal_error_events++;
        m2.error_mask |= HAL_CAN_GetError(s_can);
    }
    m2.passed = (m2.tx_attempted == M2_FRAME_COUNT) &&
                (m2.tx_queued == M2_FRAME_COUNT) &&
                (m2.tx_completed == M2_FRAME_COUNT) &&
                (m2.rx_total == M2_FRAME_COUNT) &&
                (m2.rx_ok == M2_FRAME_COUNT) &&
                (m2.bad_frames == 0U) && (m2.tx_fail == 0U) &&
                (m2.schedule_slips == 0U) && (m2.hal_error_events == 0U) &&
                (m2.bus_off == 0U) && (m2.rx_timeout == 0U);
    m2.done = 1U;
}

void M2_CAN_Test_Process(void)
{
    if ((s_can == NULL) || (m2.done != 0U)) {
        return;
    }
    if (m2.bus_off != 0U) {
        M2_Finish();
        return;
    }

    if (m2.loopback == 0U) {
        /* Snapshot ISR-owned timestamps before sampling time. */
        uint32_t primask = __get_PRIMASK();
        __disable_irq();
        uint32_t started = s_rx_started;
        uint32_t last_rx = s_last_rx;
        uint32_t first_rx = s_first_rx;
        uint32_t rx_total = m2.rx_total;
        uint32_t now = HAL_GetTick();
        __set_PRIMASK(primask);

        /* Wait indefinitely for Node 1 to start; then bound the test. */
        if (started == 0U) {
            return;
        }
        if ((rx_total >= M2_FRAME_COUNT) &&
            ((uint32_t)(now - last_rx) >= M2_DRAIN_MS)) {
            M2_Finish();
        } else if (((uint32_t)(now - last_rx) >= M2_RX_TIMEOUT_MS) ||
                   ((uint32_t)(now - first_rx) >= M2_MAX_RUN_MS)) {
            m2.rx_timeout = 1U;
            M2_Finish();
        }
        return;
    }

    uint32_t now = HAL_GetTick();
    if (m2.tx_attempted >= M2_FRAME_COUNT) {
        if ((uint32_t)(now - s_last_tx) >= M2_DRAIN_MS) {
            M2_Finish();
        }
        return;
    }
    if ((int32_t)(now - s_next_tx) < 0) {
        return;
    }
    if ((uint32_t)(now - s_next_tx) >= M2_PERIOD_MS) {
        m2.schedule_slips++;
        s_next_tx = now + M2_PERIOD_MS;
    } else {
        s_next_tx += M2_PERIOD_MS;
    }
    s_last_tx = now;
    uint32_t seq = m2.tx_attempted;
    uint8_t data[8] = {
        (uint8_t)seq, (uint8_t)(seq >> 8),
        (uint8_t)(seq >> 16), (uint8_t)(seq >> 24),
        0x55, 0xAA, 0x12, 0x34
    };
    M2_Send(data);
}

void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
    if ((hcan != s_can) || (m2.done != 0U)) {
        return;
    }
    while (HAL_CAN_GetRxFifoFillLevel(hcan, CAN_RX_FIFO0) != 0U) {
        CAN_RxHeaderTypeDef rx;
        uint8_t data[8];
        if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &rx, data) != HAL_OK) {
            m2.bad_frames++;
            break;
        }
        s_last_rx = HAL_GetTick();
        if (s_rx_started == 0U) {
            s_first_rx = s_last_rx;
            s_rx_started = 1U;
        }
        m2.rx_total++;
        if ((rx.IDE != CAN_ID_STD) || (rx.RTR != CAN_RTR_DATA) ||
            (rx.StdId != s_rx_id) || (rx.DLC != 8U)) {
            m2.bad_frames++;
            continue;
        }
        uint32_t seq = (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
            ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
        if ((seq != s_rx_seq) || (seq >= M2_FRAME_COUNT) ||
            ((m2.loopback != 0U) && (seq >= m2.tx_attempted)) ||
            (data[4] != 0x55) || (data[5] != 0xAA) ||
            (data[6] != 0x12) || (data[7] != 0x34)) {
            m2.bad_frames++;
        } else {
            m2.rx_ok++;
        }
        s_rx_seq = seq + 1U;
        if (m2.loopback == 0U) {
            /* Echo exact bytes, including bad payloads, for Node 1 checking. */
            M2_Send(data);
        }
    }
}

void HAL_CAN_TxMailbox0CompleteCallback(CAN_HandleTypeDef *hcan)
{
    if ((hcan == s_can) && (m2.done == 0U)) {
        m2.tx_completed++;
    }
}

void HAL_CAN_TxMailbox1CompleteCallback(CAN_HandleTypeDef *hcan)
{
    HAL_CAN_TxMailbox0CompleteCallback(hcan);
}

void HAL_CAN_TxMailbox2CompleteCallback(CAN_HandleTypeDef *hcan)
{
    HAL_CAN_TxMailbox0CompleteCallback(hcan);
}

void HAL_CAN_ErrorCallback(CAN_HandleTypeDef *hcan)
{
    if ((hcan != s_can) || (m2.done != 0U)) {
        return;
    }
    uint32_t error = HAL_CAN_GetError(hcan);
    m2.hal_error_events++;
    m2.error_mask |= error;
    m2.last_esr = hcan->Instance->ESR;
    if ((error & HAL_CAN_ERROR_BOF) != 0U) {
        m2.bus_off = 1U;
    }
}

#endif /* M2_TEST_ENABLED */
