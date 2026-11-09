#include "m3_can_test.h"
#include "m3_rtos_test.h"
#include "m2_can_test.h"
#include "cmsis_os2.h"

#if M2_TEST_ENABLED
#error "M2 and M3 CAN callbacks must not be enabled together"
#endif

#define COMMAND_ID       0x100U
#define REPLY_ID         0x200U
#define DRAIN_MS         1000U
#define TX_PERIOD_TICKS  20U
#define RX_READY_FLAG    1U
#define RX_TIMEOUT_MS    1000U
#define MAX_RUN_MS       65000U

typedef struct {
    CAN_RxHeaderTypeDef header;
    uint32_t received_ms;
    uint8_t data[8];
} CanRxFrame;

volatile M3_CAN_TestStats m3_can;
static CAN_HandleTypeDef *s_can;
static osMessageQueueId_t s_rx_queue;
static volatile uint32_t s_callbacks_active;
extern osThreadId_t canRxTaskHandle;
/* RX owns sequence validation; TX owns CAN start, transmit, stop and verdict. */
static uint32_t s_expected_seq;

static const uint32_t notifications = CAN_IT_TX_MAILBOX_EMPTY |
    CAN_IT_RX_FIFO0_MSG_PENDING | CAN_IT_RX_FIFO0_OVERRUN |
    CAN_IT_ERROR_WARNING | CAN_IT_ERROR_PASSIVE | CAN_IT_BUSOFF |
    CAN_IT_LAST_ERROR_CODE | CAN_IT_ERROR;

void M3_CAN_Test_Init(CAN_HandleTypeDef *hcan)
{
    const osMessageQueueAttr_t attributes = {.name = "canRxQueue"};
    if ((hcan == NULL) || (s_can != NULL) ||
        (osKernelGetState() != osKernelReady)) {
        M3_RtosFatal(M3_FAULT_KERNEL);
    }
    s_can = hcan;
    s_rx_queue = osMessageQueueNew(M3_CAN_QUEUE_DEPTH, sizeof(CanRxFrame), &attributes);
    if (s_rx_queue == NULL) {
        M3_RtosFatal(M3_FAULT_ALLOCATION);
    }
}

static HAL_StatusTypeDef start_can(void)
{
    CAN_FilterTypeDef filter = {0};
    if ((s_can == NULL) || (s_rx_queue == NULL) ||
        (osKernelGetState() != osKernelRunning) || (osKernelGetTickFreq() != 1000U) ||
        (s_can->Instance != CAN1) || (s_can->Init.Mode != CAN_MODE_NORMAL) ||
        (HAL_RCC_GetPCLK1Freq() != 48000000U) || (s_can->Init.Prescaler != 6U) ||
        (s_can->Init.TimeSeg1 != CAN_BS1_13TQ) ||
        (s_can->Init.TimeSeg2 != CAN_BS2_2TQ) ||
        (s_can->Init.SyncJumpWidth != CAN_SJW_2TQ)) {
        return HAL_ERROR;
    }
    filter.FilterBank = 0;
    filter.FilterMode = CAN_FILTERMODE_IDMASK;
    filter.FilterScale = CAN_FILTERSCALE_32BIT;
    filter.FilterIdHigh = REPLY_ID << 5;
    filter.FilterMaskIdHigh = 0x7FFU << 5;
    filter.FilterMaskIdLow = 0x0006U; /* Standard data frames only. */
    filter.FilterFIFOAssignment = CAN_RX_FIFO0;
    filter.FilterActivation = ENABLE;
    filter.SlaveStartFilterBank = 14;
    if ((HAL_CAN_ConfigFilter(s_can, &filter) != HAL_OK) ||
        (HAL_CAN_Start(s_can) != HAL_OK)) {
        return HAL_ERROR;
    }
    s_callbacks_active = 1U;
    if (HAL_CAN_ActivateNotification(s_can, notifications) != HAL_OK) {
        s_callbacks_active = 0U;
        if (HAL_CAN_Stop(s_can) != HAL_OK) {
            m3_can.stop_errors++;
        }
        return HAL_ERROR;
    }
    m3_can.ready = 1U;
    return HAL_OK;
}

HAL_StatusTypeDef M3_CAN_Test_PrepareClock(void)
{
    RCC_OscInitTypeDef osc = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();
    osc.OscillatorType = RCC_OSCILLATORTYPE_LSE;
    osc.LSEState = RCC_LSE_ON;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        return HAL_ERROR;
    }
    HAL_RCCEx_EnableMSIPLLMode();

    return ((__HAL_RCC_GET_FLAG(RCC_FLAG_LSERDY) != RESET) &&
            ((RCC->CR & RCC_CR_MSIPLLEN) != 0U)) ? HAL_OK : HAL_ERROR;
}

static void send_command(uint32_t seq)
{
    CAN_TxHeaderTypeDef tx = {0};
    const uint8_t data[8] = {(uint8_t)seq, (uint8_t)(seq >> 8),
        (uint8_t)(seq >> 16), (uint8_t)(seq >> 24), 0x55U, 0xAAU, 0x12U, 0x34U};
    uint32_t mailbox;
    /* Publish before submission: a fast echo may arrive immediately. */
    m3_can.tx_attempted++;
    tx.StdId = COMMAND_ID;
    tx.IDE = CAN_ID_STD;
    tx.RTR = CAN_RTR_DATA;
    tx.DLC = 8U;
    tx.TransmitGlobalTime = DISABLE;
    if ((HAL_CAN_GetTxMailboxesFreeLevel(s_can) == 0U) ||
        (HAL_CAN_AddTxMessage(s_can, &tx, data, &mailbox) != HAL_OK)) {
        m3_can.tx_fail++;
    } else {
        m3_can.tx_queued++;
    }
}

static void consume_frame(const CanRxFrame *frame)
{
    const uint32_t wait_ms = HAL_GetTick() - frame->received_ms;
    if (wait_ms > m3_can.queue_max_wait_ms) {
        m3_can.queue_max_wait_ms = wait_ms;
    }
    m3_can.processed++;
    if (wait_ms >= RX_TIMEOUT_MS) {
        m3_can.rx_timeout = 1U;
        return;
    }
    if ((frame->header.IDE != CAN_ID_STD) || (frame->header.RTR != CAN_RTR_DATA) ||
        (frame->header.StdId != REPLY_ID) || (frame->header.DLC != 8U)) {
        m3_can.bad_frames++;
        return;
    }
    const uint8_t *data = frame->data;
    const uint32_t seq = (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
        ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
    if ((seq != s_expected_seq) || (seq >= m3_can.tx_attempted) ||
        (seq >= M3_CAN_FRAME_COUNT) || (data[4] != 0x55U) || (data[5] != 0xAAU) ||
        (data[6] != 0x12U) || (data[7] != 0x34U)) {
        m3_can.bad_frames++;
    } else {
        m3_can.rx_ok++;
    }
    s_expected_seq = seq + 1U;
}

static uint32_t transfer_failed(void)
{
    return (m3_can.bus_off != 0U) || (m3_can.queue_dropped != 0U) ||
        (m3_can.queue_errors != 0U) || (m3_can.read_errors != 0U) ||
        (m3_can.rx_timeout != 0U) || (m3_can.tx_fail != 0U);
}

static void finish_test(void)
{
    if (m3_can.ready != 0U) {
        if (HAL_CAN_DeactivateNotification(s_can, notifications) != HAL_OK) {
            m3_can.stop_errors++;
        }
        HAL_NVIC_DisableIRQ(CAN1_RX0_IRQn);
        HAL_NVIC_DisableIRQ(CAN1_TX_IRQn);
        HAL_NVIC_DisableIRQ(CAN1_SCE_IRQn);
        s_callbacks_active = 0U;
        if (HAL_CAN_Stop(s_can) != HAL_OK) {
            m3_can.stop_errors++;
        }
    }
    /* TX has higher priority than RX. Freeze RX before publishing final counts.
       No task is deleted, so high-water marks remain available after the test. */
    if (osThreadSuspend(canRxTaskHandle) != osOK) {
        M3_RtosFatal(M3_FAULT_KERNEL);
    }
    m3_can.passed = (m3_can.ready != 0U) &&
        (m3_can.isr_received == M3_CAN_FRAME_COUNT) &&
        (m3_can.enqueued == M3_CAN_FRAME_COUNT) &&
        (m3_can.processed == M3_CAN_FRAME_COUNT) &&
        (m3_can.rx_ok == M3_CAN_FRAME_COUNT) &&
        (m3_can.tx_attempted == M3_CAN_FRAME_COUNT) &&
        (m3_can.tx_queued == M3_CAN_FRAME_COUNT) &&
        (m3_can.tx_completed == M3_CAN_FRAME_COUNT) &&
        (m3_can.bad_frames == 0U) && (m3_can.tx_fail == 0U) &&
        (m3_can.queue_dropped == 0U) && (m3_can.queue_errors == 0U) &&
        (m3_can.read_errors == 0U) && (m3_can.hal_error_events == 0U) &&
        (m3_can.bus_off == 0U) && (m3_can.rx_timeout == 0U) &&
        (m3_can.start_errors == 0U) && (m3_can.stop_errors == 0U) &&
        (m3_can.schedule_slips == 0U) && (m3_can.deadline_errors == 0U) &&
        (osMessageQueueGetCount(s_rx_queue) == 0U);
    m3_can.done = 1U;
}

/* Absolute tick deadlines survive uint32 wrap. No catch-up transmission burst. */
static uint32_t wait_until(uint32_t deadline)
{
    const uint32_t remaining = deadline - osKernelGetTickCount();
    if ((remaining != 0U) && (remaining < 0x80000000U)) {
        const osStatus_t status = osDelayUntil(deadline);
        if (status != osOK) {
            /* The tick can reach the deadline between our check and the API. */
            const uint32_t late = osKernelGetTickCount() - deadline;
            if ((status != osErrorParameter) || (late >= 0x80000000U)) {
                m3_can.deadline_errors++;
                return 0U;
            }
        }
    }
    return 1U;
}

void M3_CAN_Test_TxTask(void *argument)
{
    (void)argument;
    if (start_can() != HAL_OK) {
        m3_can.start_errors++;
    } else if ((osThreadFlagsSet(canRxTaskHandle, RX_READY_FLAG) & osFlagsError) != 0U) {
        m3_can.start_errors++;
    } else {
        uint32_t deadline = osKernelGetTickCount() + 2000U;
        const uint32_t first_deadline = deadline;
        for (uint32_t seq = 0U; seq < M3_CAN_FRAME_COUNT; ++seq) {
            if (!wait_until(deadline)) {
                break;
            }
            const uint32_t now = osKernelGetTickCount();
            const uint32_t late = now - deadline;
            if (late > m3_can.tx_max_late_ticks) {
                m3_can.tx_max_late_ticks = late;
            }
            if (late >= TX_PERIOD_TICKS) {
                m3_can.schedule_slips++;
            }
            if ((uint32_t)(now - first_deadline) >= MAX_RUN_MS) {
                m3_can.rx_timeout = 1U;
                break;
            }
            if (transfer_failed()) {
                break;
            }
            send_command(seq);
            /* Rebase after a missed slot, otherwise keep the absolute cadence. */
            deadline = ((late >= TX_PERIOD_TICKS) ? now : deadline) + TX_PERIOD_TICKS;
        }
        const uint32_t drain_start = osKernelGetTickCount();
        while (!transfer_failed() &&
               ((uint32_t)(osKernelGetTickCount() - drain_start) < DRAIN_MS)) {
            if (osDelay(10U) != osOK) {
                M3_RtosFatal(M3_FAULT_KERNEL);
            }
        }
        if (m3_can.rx_ok != M3_CAN_FRAME_COUNT) {
            m3_can.rx_timeout = 1U;
        }
    }
    finish_test();
    (void)osThreadSuspend(osThreadGetId());
    M3_RtosFatal(M3_FAULT_KERNEL);
}

void M3_CAN_Test_RxTask(void *argument)
{
    (void)argument;
    if ((osThreadFlagsWait(RX_READY_FLAG, osFlagsWaitAny, osWaitForever) & osFlagsError) != 0U) {
        M3_RtosFatal(M3_FAULT_KERNEL);
    }
    for (;;) {
        CanRxFrame frame;
        if (osMessageQueueGet(s_rx_queue, &frame, NULL, osWaitForever) != osOK) {
            m3_can.queue_errors++;
            (void)osThreadSuspend(osThreadGetId());
            M3_RtosFatal(M3_FAULT_KERNEL);
        }
        consume_frame(&frame);
    }
}

void M3_CAN_Test_GetStats(M3_CAN_TestStats *stats)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    *stats = m3_can;
    __set_PRIMASK(primask);
}

void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
    if ((hcan != s_can) || (s_callbacks_active == 0U)) {
        return;
    }
    /* bxCAN FIFO holds three frames. Bound each ISR visit even under a flood. */
    for (uint32_t budget = 0U; budget < 3U; ++budget) {
        if (HAL_CAN_GetRxFifoFillLevel(hcan, CAN_RX_FIFO0) == 0U) {
            break;
        }
        CanRxFrame frame;
        if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &frame.header, frame.data) != HAL_OK) {
            m3_can.read_errors++;
            break;
        }
        frame.received_ms = HAL_GetTick();
        m3_can.isr_received++;
        /* Copy the complete frame by value, never a pointer into the ISR stack. */
        if (osMessageQueuePut(s_rx_queue, &frame, 0U, 0U) != osOK) {
            m3_can.queue_dropped++;
        } else {
            m3_can.enqueued++;
            const uint32_t count = osMessageQueueGetCount(s_rx_queue);
            if (count > m3_can.queue_peak) {
                m3_can.queue_peak = count;
            }
        }
    }
}

void HAL_CAN_TxMailbox0CompleteCallback(CAN_HandleTypeDef *hcan)
{
    if ((hcan == s_can) && (s_callbacks_active != 0U)) {
        m3_can.tx_completed++;
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
    if ((hcan == s_can) && (s_callbacks_active != 0U)) {
        const uint32_t error = HAL_CAN_GetError(hcan);
        m3_can.hal_error_events++;
        m3_can.error_mask |= error;
        if ((error & HAL_CAN_ERROR_BOF) != 0U) {
            m3_can.bus_off = 1U;
        }
    }
}
