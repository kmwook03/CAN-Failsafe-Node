/**
 * @file  motor_test.c
 * @brief M1 ④ TIM3 PWM 모터 단독 회전 검증 시퀀스 (베어메탈)
 *
 * @note  이 파일은 M1 전용이다. M3(RTOS 통합)로 넘어가면 HAL_Delay 는 전부 금지되므로
 *        이 파일은 프로젝트에서 제외하고 MotorTask 로 대체한다.
 */

#include "main.h"
#include "motor.h"
#include "motor_test.h"
#include <stdio.h>

/* Nucleo-64 온보드 파란 버튼 B1 = PC13, 눌림 = LOW */
#ifndef B1_Pin
  #define B1_GPIO_Port  GPIOC
  #define B1_Pin        GPIO_PIN_13
#endif
/* 온보드 LD2 = PA5 */
#ifndef LD2_Pin
  #define LD2_GPIO_Port GPIOA
  #define LD2_Pin       GPIO_PIN_5
#endif

bool MotorTest_AbortRequested(void)
{
    return (HAL_GPIO_ReadPin(B1_GPIO_Port, B1_Pin) == GPIO_PIN_RESET);
}

/** @brief 중단 가능한 지연. 10ms 마다 B1 을 확인한다. */
static bool wait_ms(uint32_t ms)
{
    uint32_t t0 = HAL_GetTick();
    while ((HAL_GetTick() - t0) < ms) {
        if (MotorTest_AbortRequested()) {
            Motor_Stop(MOTOR_STOP_BRAKE);
            printf("\r\n[ABORT] B1 pressed -> BRAKE\r\n");
            return false;
        }
        HAL_Delay(10);
    }
    return true;
}

/** @brief 한 모터를 0 -> peak -> 0 으로 램프. step_ms 마다 5%씩. */
static bool ramp(motor_id_t id, int8_t peak, uint32_t step_ms)
{
    int8_t step = (peak >= 0) ? 5 : -5;
    for (int8_t p = 0; (step > 0) ? (p <= peak) : (p >= peak); p += step) {
        Motor_Set(id, p);
        printf("  %s cmd=%4d%%  duty=%3u%%\r\n",
               (id == MOTOR_LEFT) ? "L" : "R", p, Motor_GetDuty(id));
        if (!wait_ms(step_ms)) return false;
    }
    for (int8_t p = peak; (step > 0) ? (p >= 0) : (p <= 0); p -= step) {
        Motor_Set(id, p);
        if (!wait_ms(step_ms)) return false;
    }
    Motor_Set(id, 0);
    return wait_ms(300);
}

/* ══════════════════════════════════════════════════════════ */

bool MotorTest_Run(void)
{
    printf("\r\n");
    printf("===========================================\r\n");
    printf(" M1-4  TIM3 PWM Motor Standalone Test\r\n");
    printf("===========================================\r\n");
    printf(" SYSCLK  : %lu Hz\r\n", (unsigned long)HAL_RCC_GetSysClockFreq());
    printf(" HCLK    : %lu Hz\r\n", (unsigned long)HAL_RCC_GetHCLKFreq());
    printf(" PCLK1   : %lu Hz  (TIM3 clk = x2)\r\n", (unsigned long)HAL_RCC_GetPCLK1Freq());
    printf(" TIM3    : PSC=%u ARR=%u -> %lu Hz\r\n",
           (unsigned)MOTOR_TIM_PSC, (unsigned)MOTOR_TIM_ARR,
           (unsigned long)(((unsigned long)HAL_RCC_GetPCLK1Freq() * 2UL)
                           / ((MOTOR_TIM_PSC + 1UL) * (MOTOR_TIM_ARR + 1UL))));
    printf(" Abort   : press B1 (blue button) anytime\r\n");
    printf("-------------------------------------------\r\n");

    /* ── Phase 0 : 모터 미연결 파형 검증 ──────────────────────
     * L298N 을 아직 연결하지 않은 상태에서 PA6 / GND 사이를 멀티미터 DC 로 측정.
     * 20kHz PWM 의 평균값이 읽히므로 듀티 x 3.3V 가 나와야 한다.
     */
    printf("\r\n[Phase 0] PWM waveform check (motor DISCONNECTED)\r\n");
    printf("  Measure PA6(D12) & PA7(D11) vs GND with a DC multimeter.\r\n");
    const uint8_t duty_steps[] = { 0, 25, 50, 75, 100 };
    for (unsigned i = 0; i < sizeof(duty_steps); i++) {
        uint8_t d = duty_steps[i];
        Motor_SetBoth((int8_t)d, (int8_t)d);
        /* float printf 링커 옵션(-u _printf_float) 없이 쓰려고 mV 정수로 출력 */
        unsigned mv = (3300U * d) / 100U;
        printf("  duty %3u%%  -> expect %u.%03u V avg,  CCR=%lu\r\n",
               d, mv / 1000U, mv % 1000U, (unsigned long)((4000UL * d) / 100UL));
        if (!wait_ms(4000)) return false;
    }
    Motor_Stop(MOTOR_STOP_COAST);
    if (!wait_ms(1000)) return false;

    /* 여기서부터는 L298N + 모터 + 18650 이 연결되어 있어야 한다.
     * 차체는 바닥에서 띄워 놓을 것 (바퀴가 공중에 뜨도록).
     */
    printf("\r\n>>> Connect L298N + motors now. Wheels OFF the ground! <<<\r\n");
    printf(">>> Starting in 5 s ...\r\n");
    if (!wait_ms(5000)) return false;

    /* ── Phase 1 : 좌측 전진 ─────────────────────────────── */
    printf("\r\n[Phase 1] LEFT motor FORWARD ramp 0->60->0\r\n");
    if (!ramp(MOTOR_LEFT, 60, 400)) return false;

    /* ── Phase 2 : 좌측 후진 ─────────────────────────────── */
    printf("\r\n[Phase 2] LEFT motor REVERSE ramp 0->-60->0\r\n");
    if (!ramp(MOTOR_LEFT, -60, 400)) return false;

    /* ── Phase 3 : 우측 ─────────────────────────────────── */
    printf("\r\n[Phase 3] RIGHT motor FORWARD ramp 0->60->0\r\n");
    if (!ramp(MOTOR_RIGHT, 60, 400)) return false;
    printf("\r\n[Phase 3] RIGHT motor REVERSE ramp 0->-60->0\r\n");
    if (!ramp(MOTOR_RIGHT, -60, 400)) return false;

    /* ── Phase 4 : 동시 구동 / 제자리 회전 / 정지 방식 비교 ── */
    printf("\r\n[Phase 4] BOTH forward 50%% (3 s)\r\n");
    Motor_SetBoth(50, 50);
    if (!wait_ms(3000)) return false;

    printf("[Phase 4] COAST stop -> watch how long wheels keep spinning\r\n");
    Motor_Stop(MOTOR_STOP_COAST);
    if (!wait_ms(3000)) return false;

    printf("[Phase 4] BOTH forward 50%% again (3 s)\r\n");
    Motor_SetBoth(50, 50);
    if (!wait_ms(3000)) return false;

    printf("[Phase 4] BRAKE stop -> should stop noticeably faster\r\n");
    Motor_Stop(MOTOR_STOP_BRAKE);
    if (!wait_ms(2000)) return false;
    Motor_Stop(MOTOR_STOP_COAST);
    if (!wait_ms(1000)) return false;

    printf("[Phase 4] Spin in place: L=+50, R=-50 (2 s)\r\n");
    Motor_SetBoth(50, -50);
    if (!wait_ms(2000)) return false;
    printf("[Phase 4] Spin the other way: L=-50, R=+50 (2 s)\r\n");
    Motor_SetBoth(-50, 50);
    if (!wait_ms(2000)) return false;
    Motor_Stop(MOTOR_STOP_COAST);
    if (!wait_ms(1000)) return false;

    /* ── Phase 5 : 최소 기동 듀티 실측 ──────────────────────
     * 1%씩 올리며 바퀴가 "실제로 돌기 시작하는" 듀티를 눈으로 확인한다.
     * 이 값이 M4 데드존 / 슬루레이트 설계의 근거가 된다.
     */
    printf("\r\n[Phase 5] Minimum start duty sweep (LEFT then RIGHT)\r\n");
    printf("  Watch the wheel. Note the %% at which it first turns.\r\n");
    for (motor_id_t id = MOTOR_LEFT; id < MOTOR_COUNT; id++) {
        printf("  -- %s motor --\r\n", (id == MOTOR_LEFT) ? "LEFT" : "RIGHT");
        for (int8_t p = 5; p <= 60; p += 1) {
            Motor_Set(id, p);
            printf("    duty %3d%%\r\n", p);
            if (!wait_ms(700)) return false;
        }
        Motor_Set(id, 0);
        if (!wait_ms(800)) return false;
    }

    Motor_Stop(MOTOR_STOP_COAST);
    printf("\r\n===========================================\r\n");
    printf(" TEST COMPLETE. Motors stopped (COAST).\r\n");
    printf(" -> Update MOTOR_MIN_MOVE_PCT in motor.h\r\n");
    printf("===========================================\r\n");
    return true;
}
