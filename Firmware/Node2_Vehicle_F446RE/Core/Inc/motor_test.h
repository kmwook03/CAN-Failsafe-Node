/**
 * @file  motor_test.h
 * @brief M1 DoD ④ "TIM3 PWM 모터 단독 회전" 검증 시퀀스
 */
#ifndef __MOTOR_TEST_H
#define __MOTOR_TEST_H

#include <stdbool.h>

/**
 * @brief 검증 시퀀스 1회 실행.
 * @return true = 끝까지 완주, false = B1(파란 버튼)으로 중단됨
 *
 * Phase 0 : 모터 미연결 상태에서 PWM 파형/듀티 확인 (멀티미터·오실로스코프)
 * Phase 1 : 좌측 모터 전진 램프
 * Phase 2 : 좌측 모터 후진 램프
 * Phase 3 : 우측 모터 전진/후진
 * Phase 4 : 양쪽 동시 + 제자리 회전 + COAST/BRAKE 비교
 * Phase 5 : 최소 기동 듀티 실측 (MOTOR_MIN_MOVE_PCT 결정용)
 */
bool MotorTest_Run(void);

/** @brief 언제든 즉시 정지시키는 비상 정지 (B1 눌림 감지 시 내부 호출) */
bool MotorTest_AbortRequested(void);

#endif /* __MOTOR_TEST_H */
