/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include "m2_can_test.h"

void M1_LCD_Test_Run(I2C_HandleTypeDef *hi2c, UART_HandleTypeDef *huart);
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;
DMA_HandleTypeDef hdma_adc1;

CAN_HandleTypeDef hcan1;

I2C_HandleTypeDef hi2c1;

UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */
#if !M2_TEST_ENABLED
#define ADC_CH_CNT   2
#define MA_LEN       8
#define BTN_DEBOUNCE_MS 25u

typedef enum { BTN_ARM = 0, BTN_MODE, BTN_ESTOP, BTN_COUNT } btn_id_t;

static volatile uint32_t s_btn_last[BTN_COUNT] = {0};
static volatile uint8_t s_btn_event[BTN_COUNT] = {0};
static volatile uint32_t s_irq_raw[BTN_COUNT] = {0}; /* For check: ISR entry count */
static volatile uint8_t s_btn_down[BTN_COUNT] = {0};

static const uint16_t BTN_PIN[BTN_COUNT] = {
		BTN_ARM_Pin, BTN_MODE_Pin, BTN_ESTOP_Pin
};
static GPIO_TypeDef * const BTN_PORT[BTN_COUNT] = {
		BTN_ARM_GPIO_Port, BTN_MODE_GPIO_Port, BTN_ESTOP_GPIO_Port
};

static volatile uint16_t g_adc_raw[ADC_CH_CNT];
#endif
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_ADC1_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_I2C1_Init(void);
static void MX_CAN1_Init(void);
/* USER CODE BEGIN PFP */
#if !M2_TEST_ENABLED
static uint8_t btn_take(btn_id_t id);
static void btn_release_scan(void);
#endif
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */
#if M2_TEST_ENABLED
  if (M2_CAN_Test_PrepareClock() != HAL_OK) {
    Error_Handler();
  }
#endif
  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_ADC1_Init();
  MX_USART2_UART_Init();
  MX_I2C1_Init();
  MX_CAN1_Init();
  /* USER CODE BEGIN 2 */
#if M2_TEST_ENABLED
  setvbuf(stdout, NULL, _IONBF, 0);
  printf("\r\n[M2] Node 1 CAN test: %s, 500kbps, 3000 frames\r\n",
         hcan1.Init.Mode == CAN_MODE_LOOPBACK ? "LOOPBACK" : "NORMAL");
  if (M2_CAN_Test_Start(&hcan1) != HAL_OK) {
    Error_Handler();
  }
#else
  HAL_UART_Transmit(&huart2, (uint8_t *)"UART-OK\r\n", 9, 100);
  M1_LCD_Test_Run(&hi2c1, &huart2);
  setvbuf(stdout, NULL, _IONBF, 0);
  /*Calibration: Before Start_DMA, it should be called when ADC off*/
  if (HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED) != HAL_OK) {
	  printf("ADC calibration FAILED\r\n");
	  Error_Handler();
  }
  printf("ADC calibration OK (factor=%lu)\r\n",
  HAL_ADCEx_Calibration_GetValue(&hadc1, ADC_SINGLE_ENDED));

  if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)(void *)g_adc_raw, ADC_CH_CNT) != HAL_OK) {
  	  printf("ADC DMA start Failed\r\n");
  	  Error_Handler();
  }
  HAL_Delay(50);
#endif
  /* USER CODE END 2 */

  /* Initialize leds */
  BSP_LED_Init(LED_GREEN);

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
#if !M2_TEST_ENABLED
  uint8_t mode = 0;
  uint8_t estop = 0;
  printf("\r\n=== BOOT ok, entering main loop ===\r\n");
#endif
  while (1) {
#if M2_TEST_ENABLED
      static uint8_t reported = 0;
      M2_CAN_Test_Process();
      if ((m2.done != 0U) && (reported == 0U)) {
          reported = 1;
          printf("[M2] %s: tx=%lu rx=%lu bad=%lu tx_fail=%lu "
                 "slips=%lu errors=%lu mask=0x%08lX bus_off=%lu\r\n",
                 m2.passed ? "PASS" : "FAIL",
                 (unsigned long)m2.tx_queued, (unsigned long)m2.rx_ok,
                 (unsigned long)m2.bad_frames, (unsigned long)m2.tx_fail,
                 (unsigned long)m2.schedule_slips,
                 (unsigned long)m2.hal_error_events,
                 (unsigned long)m2.error_mask, (unsigned long)m2.bus_off);
      }
#else
	  btn_release_scan();
	  static uint32_t t_last = 0;
	  if ((uint32_t)(HAL_GetTick() - t_last) >= 1000) {
	      t_last = HAL_GetTick();
	      printf("raw = %lu / %lu / %lu\r\n", s_irq_raw[0], s_irq_raw[1], s_irq_raw[2]);
	  }
	  if (btn_take(BTN_ARM)) {
		  printf("[BTN] ARM        (raw=%lu)\r\n", s_irq_raw[BTN_ARM]);
	  }
	  if (btn_take(BTN_MODE)) {
		  mode = (mode + 1) % 3;
		  printf("[BTN] MODE -> %u  (raw=%lu)\r\n", mode, s_irq_raw[BTN_MODE]);
	  }
	  if (btn_take(BTN_ESTOP)) {
		  estop = !estop;
		  HAL_GPIO_WritePin(LED3_GPIO_PORT, LED3_PIN, estop ? GPIO_PIN_SET : GPIO_PIN_RESET);
		  printf("[BTN] E-STOP %s\r\n", estop ? "ON" : "OFF");
	  }
	  HAL_Delay(1);
#endif
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI|RCC_OSCILLATORTYPE_MSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.MSIState = RCC_MSI_ON;
  RCC_OscInitStruct.MSICalibrationValue = 0;
  RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_11;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_MSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Common config
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.ScanConvMode = ADC_SCAN_ENABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SEQ_CONV;
  hadc1.Init.LowPowerAutoWait = DISABLE;
  hadc1.Init.ContinuousConvMode = ENABLE;
  hadc1.Init.NbrOfConversion = 2;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.DMAContinuousRequests = ENABLE;
  hadc1.Init.Overrun = ADC_OVR_DATA_OVERWRITTEN;
  hadc1.Init.OversamplingMode = DISABLE;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_5;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_247CYCLES_5;
  sConfig.SingleDiff = ADC_SINGLE_ENDED;
  sConfig.OffsetNumber = ADC_OFFSET_NONE;
  sConfig.Offset = 0;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_6;
  sConfig.Rank = ADC_REGULAR_RANK_2;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief CAN1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_CAN1_Init(void)
{

  /* USER CODE BEGIN CAN1_Init 0 */

  /* USER CODE END CAN1_Init 0 */

  /* USER CODE BEGIN CAN1_Init 1 */

  /* USER CODE END CAN1_Init 1 */
  hcan1.Instance = CAN1;
  hcan1.Init.Prescaler = 6;
  hcan1.Init.Mode = CAN_MODE_NORMAL;
  hcan1.Init.SyncJumpWidth = CAN_SJW_2TQ;
  hcan1.Init.TimeSeg1 = CAN_BS1_13TQ;
  hcan1.Init.TimeSeg2 = CAN_BS2_2TQ;
  hcan1.Init.TimeTriggeredMode = DISABLE;
  hcan1.Init.AutoBusOff = DISABLE;
  hcan1.Init.AutoWakeUp = DISABLE;
  hcan1.Init.AutoRetransmission = ENABLE;
  hcan1.Init.ReceiveFifoLocked = DISABLE;
  hcan1.Init.TransmitFifoPriority = DISABLE;
  if (HAL_CAN_Init(&hcan1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN CAN1_Init 2 */

  /* USER CODE END CAN1_Init 2 */

}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.Timing = 0x00503D58;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Analogue filter
  */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
  */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Channel1_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel1_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pins : BTN_ARM_Pin BTN_MODE_Pin */
  GPIO_InitStruct.Pin = BTN_ARM_Pin|BTN_MODE_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : BTN_ESTOP_Pin */
  GPIO_InitStruct.Pin = BTN_ESTOP_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(BTN_ESTOP_GPIO_Port, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI0_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(EXTI0_IRQn);

  HAL_NVIC_SetPriority(EXTI1_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(EXTI1_IRQn);

  HAL_NVIC_SetPriority(EXTI9_5_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
int __io_putchar(int ch)
{
  HAL_UART_Transmit(&huart2, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
  return ch;
}

#if !M2_TEST_ENABLED
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  btn_id_t id;

  switch (GPIO_Pin) {
    case BTN_ARM_Pin: id = BTN_ARM; break;
    case BTN_MODE_Pin: id = BTN_MODE; break;
    case BTN_ESTOP_Pin: id = BTN_ESTOP; break;
    default: return;
  }

  s_irq_raw[id]++;

  if (s_btn_down[id]) return;
  /* time filter: unsigned subtraction -> safe from tick overflow */
  uint32_t now = HAL_GetTick();
  if ((uint32_t)(now - s_btn_last[id]) < BTN_DEBOUNCE_MS) return;

  s_btn_last[id] = now;
  s_btn_down[id] = 1;
  s_btn_event[id] = 1;
}

static uint8_t btn_take(btn_id_t id)
{
  uint8_t ev;
  __disable_irq();
  ev = s_btn_event[id];
  s_btn_event[id] = 0;
  __enable_irq();
  return ev;
}

static void btn_release_scan(void)
{
  uint32_t now = HAL_GetTick();

  for (int i = 0; i < BTN_COUNT; i++) {
	  if (!s_btn_down[i]) continue;

	  if (HAL_GPIO_ReadPin(BTN_PORT[i], BTN_PIN[i]) == GPIO_PIN_SET) {
		  if ((uint32_t)(now - s_btn_last[i]) >= BTN_DEBOUNCE_MS) {
			  s_btn_down[i] = 0;
		  }
	  } else {
		  s_btn_last[i] = now;
	  }
  }
}
#endif /* !M2_TEST_ENABLED */
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
