/**
 * @file  ultrasonic_test.h
 * @brief M1 ⑤ HC-SR04 Input Capture standalone test (bare metal)
 */
#ifndef __ULTRASONIC_TEST_H
#define __ULTRASONIC_TEST_H

#include "stm32f4xx_hal.h"

/**
 * @brief Start TIM4 and prepare the HC-SR04 test state machine.
 * @param htim TIM4 handle configured for a 1 MHz CH1 input capture.
 */
void UltrasonicTest_Init(TIM_HandleTypeDef *htim);

/**
 * @brief Run the non-blocking test state machine.
 *
 * Call continuously from the bare-metal main loop. A new measurement starts
 * every 60 ms. Echo edges are measured only by the TIM4 capture interrupt;
 * this function handles result printing and the 30 ms no-echo timeout.
 */
void UltrasonicTest_Process(void);

#endif /* __ULTRASONIC_TEST_H */
