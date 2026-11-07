/**
 * @file    motor.c
 * @brief   L298N + TIM3 PWM 모터 드라이버 구현
 *
 *  핀 매핑 (계획서 §4.2 / 회로도 Rev 0.1 기준)
 *  ┌──────────┬───────┬──────────┬──────────────────────┐
 *  │ L298N    │ MCU   │ 보드라벨 │ 기능                 │
 *  ├──────────┼───────┼──────────┼──────────────────────┤
 *  │ ENA      │ PA6   │ D12      │ TIM3_CH1 PWM 20kHz   │
 *  │ IN1      │ PB12  │ CN10-16  │ GPIO OUT (좌 방향)   │
 *  │ IN2      │ PB13  │ CN10-30  │ GPIO OUT (좌 방향)   │
 *  │ IN3      │ PB14  │ CN10-28  │ GPIO OUT (우 방향)   │
 *  │ IN4      │ PB15  │ CN10-26  │ GPIO OUT (우 방향)   │
 *  │ ENB      │ PA7   │ D11      │ TIM3_CH2 PWM 20kHz   │
 *  └──────────┴───────┴──────────┴──────────────────────┘
 *
 *  L298N 진리표 (ENx = PWM)
 *   INx  INy  | 동작
 *    1    0   | 정회전
 *    0    1   | 역회전
 *    0    0   | COAST (프리런)
 *    1    1   | BRAKE (전기 제동)
 */

#include "main.h"
#include "motor.h"

extern TIM_HandleTypeDef htim3;

/* 방향핀이 CubeMX 에서 MOT_IN1_Pin 등으로 라벨링되지 않았을 경우를 위한 폴백 */
#ifndef MOT_IN1_Pin
  #define MOT_IN1_GPIO_Port  GPIOB
  #define MOT_IN1_Pin        GPIO_PIN_12
  #define MOT_IN2_GPIO_Port  GPIOB
  #define MOT_IN2_Pin        GPIO_PIN_13
  #define MOT_IN3_GPIO_Port  GPIOB
  #define MOT_IN3_Pin        GPIO_PIN_14
  #define MOT_IN4_GPIO_Port  GPIOB
  #define MOT_IN4_Pin        GPIO_PIN_15
#endif

typedef struct {
    GPIO_TypeDef *inA_port;  uint16_t inA_pin;   /* IN1 / IN3 */
    GPIO_TypeDef *inB_port;  uint16_t inB_pin;   /* IN2 / IN4 */
    uint32_t      tim_channel;
} motor_hw_t;

static const motor_hw_t s_hw[MOTOR_COUNT] = {
    [MOTOR_LEFT]  = { MOT_IN1_GPIO_Port, MOT_IN1_Pin,
                      MOT_IN2_GPIO_Port, MOT_IN2_Pin, TIM_CHANNEL_1 },
    [MOTOR_RIGHT] = { MOT_IN3_GPIO_Port, MOT_IN3_Pin,
                      MOT_IN4_GPIO_Port, MOT_IN4_Pin, TIM_CHANNEL_2 },
};

static int8_t  s_cmd[MOTOR_COUNT]  = { 0, 0 };
static uint8_t s_duty[MOTOR_COUNT] = { 0, 0 };

/* ─────────────────────────────────────────────────────────── */

static inline void motor_write_duty(motor_id_t id, uint8_t duty_pct)
{
    /* CCR = 4000 * duty / 100.  duty=100 -> CCR=4000 > ARR(3999) -> 항상 High = 100% */
    uint32_t ccr = ((uint32_t)MOTOR_PWM_STEPS * duty_pct) / 100U;
    __HAL_TIM_SET_COMPARE(&htim3, s_hw[id].tim_channel, ccr);
    s_duty[id] = duty_pct;
}

static inline void motor_write_dir(motor_id_t id, GPIO_PinState a, GPIO_PinState b)
{
    HAL_GPIO_WritePin(s_hw[id].inA_port, s_hw[id].inA_pin, a);
    HAL_GPIO_WritePin(s_hw[id].inB_port, s_hw[id].inB_pin, b);
}

/* ─────────────────────────────────────────────────────────── */

void Motor_Init(void)
{
    /* 방향핀을 먼저 안전 상태로 내리고 PWM 을 켠다 (순서 반대면 부팅 순간 튄다) */
    motor_write_dir(MOTOR_LEFT,  GPIO_PIN_RESET, GPIO_PIN_RESET);
    motor_write_dir(MOTOR_RIGHT, GPIO_PIN_RESET, GPIO_PIN_RESET);

    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, 0);
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, 0);

    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_2);

    s_cmd[MOTOR_LEFT] = s_cmd[MOTOR_RIGHT] = 0;
    s_duty[MOTOR_LEFT] = s_duty[MOTOR_RIGHT] = 0;
}

void Motor_Set(motor_id_t id, int8_t pct)
{
    if (id >= MOTOR_COUNT) return;

    /* 클리핑 */
    if (pct >  MOTOR_MAX_PCT) pct =  MOTOR_MAX_PCT;
    if (pct < -MOTOR_MAX_PCT) pct = -MOTOR_MAX_PCT;

    int16_t mag = (pct < 0) ? -(int16_t)pct : (int16_t)pct;

    /* 데드존: 미세 입력으로 모터가 '으---' 하고 떠는 것을 막는다 (검증 N2) */
    if (mag <= MOTOR_DEADZONE_PCT) {
        motor_write_duty(id, 0);
        motor_write_dir(id, GPIO_PIN_RESET, GPIO_PIN_RESET);   /* COAST */
        s_cmd[id] = 0;
        return;
    }

    if (pct > 0) motor_write_dir(id, GPIO_PIN_SET,   GPIO_PIN_RESET); /* 전진 */
    else         motor_write_dir(id, GPIO_PIN_RESET, GPIO_PIN_SET);   /* 후진 */

    motor_write_duty(id, (uint8_t)mag);
    s_cmd[id] = pct;
}

void Motor_SetBoth(int8_t left_pct, int8_t right_pct)
{
    Motor_Set(MOTOR_LEFT,  left_pct);
    Motor_Set(MOTOR_RIGHT, right_pct);
}

void Motor_Stop(motor_stop_mode_t mode)
{
    for (motor_id_t id = MOTOR_LEFT; id < MOTOR_COUNT; id++) {
        if (mode == MOTOR_STOP_BRAKE) {
            /* 전기 제동: 양쪽 IN 을 High 로 놓고 ENx 를 100% 로 물린다 */
            motor_write_dir(id, GPIO_PIN_SET, GPIO_PIN_SET);
            motor_write_duty(id, 100);
        } else {
            motor_write_duty(id, 0);
            motor_write_dir(id, GPIO_PIN_RESET, GPIO_PIN_RESET);
        }
        s_cmd[id] = 0;
    }
    if (mode == MOTOR_STOP_BRAKE) {
        /* 제동 듀티는 텔레메트리에 '출력 100%' 로 잡히면 안 되므로 0 으로 보고 */
        s_duty[MOTOR_LEFT] = s_duty[MOTOR_RIGHT] = 0;
    }
}

uint8_t Motor_GetDuty(motor_id_t id)
{
    return (id < MOTOR_COUNT) ? s_duty[id] : 0;
}

int8_t Motor_GetCmd(motor_id_t id)
{
    return (id < MOTOR_COUNT) ? s_cmd[id] : 0;
}
