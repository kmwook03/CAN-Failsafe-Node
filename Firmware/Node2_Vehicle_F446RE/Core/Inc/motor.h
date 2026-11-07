/**
 * @file    motor.h
 * @brief   L298N + TIM3 PWM 모터 드라이버 (Node 2 / NUCLEO-F446RE)
 * @note    M1 단계에서는 베어메탈로 검증하고, M4 주행 통합 / M5 Fail-safe 에서
 *          MotorTask 가 그대로 이 API 만 호출하도록 설계되어 있다.
 *          PWM 에 쓰는 주체는 언제나 하나여야 한다 (계획서 §2-1 Single Ownership).
 */

#ifndef __MOTOR_H
#define __MOTOR_H

#include <stdint.h>
#include <stdbool.h>

/* ── 하드웨어 상수 ─────────────────────────────────────────────
 * TIM3 clk = 80 MHz (APB1 40MHz, prescaler != 1 이므로 x2)
 * PWM freq = 80MHz / ((PSC+1) * (ARR+1)) = 80MHz / 4000 = 20 kHz
 * 분해능    = 4000 step (0.025 % / step)
 */
#define MOTOR_TIM_PSC          0U
#define MOTOR_TIM_ARR          7999U
#define MOTOR_PWM_STEPS        (MOTOR_TIM_ARR + 1U)   /* 4000 */

/* ── 튜닝 파라미터 ─────────────────────────────────────────────
 * MOTOR_DEADZONE_PCT : 이 값 이하 명령은 0 으로 간주 (M4 데드존 ±8% 의 하드웨어측 하한)
 * MOTOR_MIN_MOVE_PCT : 모터가 실제로 기동하는 최소 듀티.
 *                      SelfTest Phase 5 로 실측한 뒤 이 값을 고칠 것. (초기값은 보수적)
 * MOTOR_MAX_PCT      : M1 검증 단계 출력 상한. 책상에서 테스트할 때 차가 튀어나가지 않도록.
 */
#define MOTOR_DEADZONE_PCT     3
#define MOTOR_MIN_MOVE_PCT     25
#define MOTOR_MAX_PCT          100

typedef enum {
    MOTOR_LEFT  = 0,
    MOTOR_RIGHT = 1,
    MOTOR_COUNT = 2
} motor_id_t;

typedef enum {
    MOTOR_STOP_COAST = 0,   /* IN1=IN2=0 : 관성 주행 (프리런) */
    MOTOR_STOP_BRAKE = 1    /* IN1=IN2=1 : 전기 제동 (급정지) */
} motor_stop_mode_t;

/**
 * @brief  PWM 채널 시작 + 방향핀 초기화. 반드시 MX_TIM3_Init() 이후에 호출.
 *         호출 직후 상태는 양쪽 모터 COAST(정지).
 */
void    Motor_Init(void);

/**
 * @brief  모터 1개 출력 설정
 * @param  id  MOTOR_LEFT / MOTOR_RIGHT
 * @param  pct -100 ~ +100. 양수=전진, 음수=후진, 0=COAST
 * @note   데드존 이하 -> 0, |pct| > MOTOR_MAX_PCT -> 클리핑.
 *         방향 반전 시 내부적으로 1 tick 동안 듀티 0 을 거치지 않으므로,
 *         급반전이 필요한 상위 로직(M4 슬루레이트)에서 완충할 것.
 */
void    Motor_Set(motor_id_t id, int8_t pct);

/** @brief  좌/우 동시 설정 (차동 구동용) */
void    Motor_SetBoth(int8_t left_pct, int8_t right_pct);

/** @brief  양쪽 모터 정지. FAULT / LINK_LOST / E-BRAKE 경로가 모두 여기로 수렴한다. */
void    Motor_Stop(motor_stop_mode_t mode);

/** @brief  현재 적용된 듀티 절대값(%) — 텔레메트리(0x200 byte2/3)용 */
uint8_t Motor_GetDuty(motor_id_t id);

/** @brief  현재 적용된 명령값(-100~100) — 디버그용 */
int8_t  Motor_GetCmd(motor_id_t id);

#endif /* __MOTOR_H */
