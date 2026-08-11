/**
 * @file    lcd1602_i2c.h
 * @brief   1602 Character LCD (HD44780) driver via PCF8574 I2C backpack
 * @target  STM32L432KC (Node 1 : REMOTE)  /  I2C1 = PB6(SCL) / PB7(SDA)
 *
 *
 *  PCF8574 → HD44780 :
 *    P0 = RS,  P1 = RW,  P2 = E,  P3 = Backlight,  P4..P7 = D4..D7
 */

#ifndef LCD1602_I2C_H
#define LCD1602_I2C_H

#include <stdint.h>
#include <stdbool.h>

#include "stm32l4xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif


/**
 * LCD1602_STRICT_TIMING
 *   1 = HD44780 데이터시트 준수 3-페이즈 시퀀스 (nibble당 3바이트)
 *       [RS/DATA, E=0] → [E=1] → [E=0]
 *       주소 셋업 시간 tAS(min 60ns)를 실제로 보장한다.
 *   0 = 통용되는 2-페이즈 시퀀스 (nibble당 2바이트, 전송량 33% 절감)
 *       RS와 E가 같은 바이트에서 동시에 올라가므로 tAS를 위반한다.
 *       대부분의 모듈에서 동작하지만 규격 위반이다.
 *
 *  M1 검증 단계에서는 1을 권장. M3에서 전송량이 문제가 되면 0으로 바꾸고
 *  파형을 실측해 근거를 남길 것.
 */
#ifndef LCD1602_STRICT_TIMING
#define LCD1602_STRICT_TIMING   1
#endif

/** I2C 전송 타임아웃 (ms). NACK/버스 홀드 시 여기서 빠져나온다. */
#ifndef LCD1602_I2C_TIMEOUT_MS
#define LCD1602_I2C_TIMEOUT_MS  50U
#endif

#define LCD1602_COLS            16U
#define LCD1602_ROWS            2U

/* PCF8574 흔한 주소 (7-bit) */
#define LCD1602_ADDR_PCF8574    0x27U   /* PCF8574  : 0x20 + A2A1A0(=111) */
#define LCD1602_ADDR_PCF8574A   0x3FU   /* PCF8574A : 0x38 + A2A1A0(=111) */

/* ── 핸들 ──────────────────────────────────────────────────────────── */

typedef struct {
    I2C_HandleTypeDef *hi2c;      /**< 사용할 I2C 핸들 */
    uint8_t  addr7;               /**< 7-bit 슬레이브 주소 (HAL에 넘길 때 <<1) */
    uint8_t  backlight;           /**< 0 = off, 1 = on */
    uint8_t  initialized;         /**< Init 성공 여부 */
    uint16_t err_count;           /**< 누적 I2C 전송 실패 횟수 (텔레메트리용) */
} LCD1602_t;

/* ── API ───────────────────────────────────────────────────────────── */

/**
 * @brief  DWT 사이클 카운터 기반 마이크로초 지연 초기화.
 *         LCD1602_Init() 내부에서 자동 호출되므로 별도 호출은 불필요하나,
 *         다른 드라이버와 공유하려면 main 초반에 한 번 불러도 무해하다.
 * @retval true  DWT 사용 가능
 * @retval false DWT 미지원 (이 경우 지연은 busy-loop 근사로 대체됨)
 */
bool LCD1602_DelayInit(void);

/**
 * @brief  I2C 버스 스캔. 0x08~0x77 범위를 훑어 응답하는 주소를 채워 넣는다.
 * @param  hi2c   대상 I2C 핸들
 * @param  found  결과를 담을 배열 (7-bit 주소)
 * @param  max    found 배열 크기
 * @retval 발견된 장치 수
 */
uint8_t LCD1602_ScanBus(I2C_HandleTypeDef *hi2c, uint8_t *found, uint8_t max);

/**
 * @brief  LCD 초기화. HD44780 4-bit 진입 시퀀스를 수행한다.
 * @param  addr7  7-bit 주소. 0 을 넘기면 0x27 → 0x3F 순으로 자동 탐색한다.
 * @retval HAL_OK / HAL_ERROR (장치 미응답)
 */
HAL_StatusTypeDef LCD1602_Init(LCD1602_t *lcd, I2C_HandleTypeDef *hi2c, uint8_t addr7);

void LCD1602_Clear(LCD1602_t *lcd);                                  /* 1.52ms 소요 */
void LCD1602_Home(LCD1602_t *lcd);                                   /* 1.52ms 소요 */
void LCD1602_SetCursor(LCD1602_t *lcd, uint8_t col, uint8_t row);
void LCD1602_Backlight(LCD1602_t *lcd, uint8_t on);
void LCD1602_DisplayOn(LCD1602_t *lcd, uint8_t on);

void LCD1602_PutChar(LCD1602_t *lcd, char c);
void LCD1602_Print(LCD1602_t *lcd, const char *s);
void LCD1602_PrintAt(LCD1602_t *lcd, uint8_t col, uint8_t row, const char *s);

/** 한 줄을 16칸으로 공백 패딩해 덮어쓴다. Clear 없이 깜빡임 없는 갱신용. */
void LCD1602_WriteLine(LCD1602_t *lcd, uint8_t row, const char *s);

/** printf 서식 지원. 내부 버퍼 17바이트로 잘림. 부동소수점은 링커 옵션 주의.
 *  @warning 패딩하지 않는다. 이전 프레임이 더 길었으면 잔상이 남는다.
 *           대시보드처럼 값이 계속 바뀌는 곳에는 LCD1602_PrintfLine() 을 쓸 것. */
void LCD1602_Printf(LCD1602_t *lcd, uint8_t col, uint8_t row, const char *fmt, ...);

/** printf + 16칸 공백 패딩. 잔상 없이 한 줄을 통째로 갱신한다.
 *  M4 대시보드/M5 경고 표시의 기본 출력 함수로 쓸 것. */
void LCD1602_PrintfLine(LCD1602_t *lcd, uint8_t row, const char *fmt, ...);

/**
 * @brief  CGRAM 커스텀 문자 등록 (0~7번 슬롯).
 * @param  pattern  5x8 비트맵 8바이트. 각 바이트 하위 5비트가 한 행.
 * @note   호출 후 DDRAM 주소가 초기화되므로 SetCursor를 다시 할 것.
 */
void LCD1602_CreateChar(LCD1602_t *lcd, uint8_t slot, const uint8_t pattern[8]);

#ifdef __cplusplus
}
#endif
#endif /* LCD1602_I2C_H */
