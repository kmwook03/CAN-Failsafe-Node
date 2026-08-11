/**
 * @file    lcd1602_i2c.c
 * @brief   1602 Character LCD (HD44780) driver via PCF8574 I2C backpack
 * @target  STM32L432KC (Node 1 : REMOTE)
 */

#include "lcd1602_i2c.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* ── PCF8574 비트 매핑 ─────────────────────────────────────────────── */
#define PIN_RS      0x01U   /* P0 */
#define PIN_RW      0x02U   /* P1 : 항상 0 (Write only) */
#define PIN_EN      0x04U   /* P2 */
#define PIN_BL      0x08U   /* P3 */

/* ── HD44780 명령 ──────────────────────────────────────────────────── */
#define CMD_CLEAR           0x01U
#define CMD_HOME            0x02U
#define CMD_ENTRY_MODE      0x04U   /* |0x02 = 증가, |0x01 = 화면 시프트 */
#define CMD_DISPLAY_CTRL    0x08U   /* |0x04 = Display, |0x02 = Cursor, |0x01 = Blink */
#define CMD_FUNCTION_SET    0x20U   /* |0x10 = 8bit, |0x08 = 2line, |0x04 = 5x10 */
#define CMD_SET_CGRAM       0x40U
#define CMD_SET_DDRAM       0x80U

static const uint8_t s_row_offset[LCD1602_ROWS] = { 0x00U, 0x40U };

/* ══════════════════════════════════════════════════════════════════════
 *  마이크로초 지연 (DWT CYCCNT)
 *  HAL_Delay()는 1ms 해상도라 HD44780의 40µs / 100µs 요구를 표현할 수 없다.
 *  Cortex-M4의 DWT 사이클 카운터를 쓰면 CPU 클럭 정확도로 지연이 가능하다.
 *  ※ M2에서 SYSCLK을 4MHz → 48MHz로 바꿔도 HAL_RCC_GetHCLKFreq()를 매번
 *    읽으므로 재조정이 필요 없다.
 * ══════════════════════════════════════════════════════════════════════ */

static bool s_dwt_ok = false;

bool LCD1602_DelayInit(void)
{
#if defined(DWT) && defined(CoreDebug)
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;

    /* 실제로 증가하는지 확인 (일부 디버그 설정에서 잠겨 있을 수 있다) */
    uint32_t t0 = DWT->CYCCNT;
    for (volatile int i = 0; i < 10; i++) { __NOP(); }
    s_dwt_ok = (DWT->CYCCNT != t0);
#else
    s_dwt_ok = false;
#endif
    return s_dwt_ok;
}

static void delay_us(uint32_t us)
{
#if defined(DWT)
    if (s_dwt_ok) {
        uint32_t cycles = (HAL_RCC_GetHCLKFreq() / 1000000U) * us;
        uint32_t start  = DWT->CYCCNT;
        while ((DWT->CYCCNT - start) < cycles) { /* wrap-safe (uint32 뺄셈) */ }
        return;
    }
#endif
    /* 폴백: 매우 보수적인 busy-loop. 정확하지 않으나 "너무 짧지는" 않다. */
    uint32_t loops = (HAL_RCC_GetHCLKFreq() / 4000000U) * us + 1U;
    while (loops--) { __NOP(); }
}

/* ══════════════════════════════════════════════════════════════════════
 *  전송 계층
 * ══════════════════════════════════════════════════════════════════════ */

static HAL_StatusTypeDef bus_tx(LCD1602_t *lcd, uint8_t *buf, uint16_t len)
{
    HAL_StatusTypeDef st = HAL_I2C_Master_Transmit(lcd->hi2c,
                                                  (uint16_t)(lcd->addr7 << 1),
                                                  buf, len,
                                                  LCD1602_I2C_TIMEOUT_MS);
    if (st != HAL_OK) {
        if (lcd->err_count < 0xFFFFU) { lcd->err_count++; }
    }
    return st;
}

/**
 * @brief  4비트 nibble 하나를 E 펄스와 함께 밀어 넣는다.
 * @param  nibble  하위 4비트만 유효
 * @param  ctrl    PIN_RS 포함 여부
 */
static void write_nibble(LCD1602_t *lcd, uint8_t nibble, uint8_t ctrl)
{
    uint8_t bl   = lcd->backlight ? PIN_BL : 0U;
    uint8_t base = (uint8_t)((nibble << 4) | ctrl | bl);   /* RW는 항상 0 */

#if LCD1602_STRICT_TIMING
    uint8_t buf[3];
    buf[0] = base;                       /* RS/DATA 확정, E=0 → tAS 확보 */
    buf[1] = (uint8_t)(base | PIN_EN);   /* E 상승 */
    buf[2] = base;                       /* E 하강 → 이 시점에 래치 */
    (void)bus_tx(lcd, buf, 3);
#else
    uint8_t buf[2];
    buf[0] = (uint8_t)(base | PIN_EN);
    buf[1] = base;
    (void)bus_tx(lcd, buf, 2);
#endif
}

/** 8비트 = 상위 nibble → 하위 nibble */
static void write_byte(LCD1602_t *lcd, uint8_t value, uint8_t ctrl)
{
    write_nibble(lcd, (uint8_t)(value >> 4),   ctrl);
    write_nibble(lcd, (uint8_t)(value & 0x0FU), ctrl);
    /*
     * 명령 실행 시간 37µs. I2C 100kHz에서 nibble 하나가 이미 270µs 이상
     * 걸리므로 실질적으로 충족되지만, 400kHz나 DMA 전환 시를 대비해 명시한다.
     * Clear/Home(1.52ms)은 각 호출부에서 추가로 대기한다.
     */
    delay_us(50U);
}

static inline void send_cmd(LCD1602_t *lcd, uint8_t cmd)
{
    write_byte(lcd, cmd, 0U);
}

static inline void send_data(LCD1602_t *lcd, uint8_t data)
{
    write_byte(lcd, data, PIN_RS);
}

/* ══════════════════════════════════════════════════════════════════════
 *  초기화
 * ══════════════════════════════════════════════════════════════════════ */

uint8_t LCD1602_ScanBus(I2C_HandleTypeDef *hi2c, uint8_t *found, uint8_t max)
{
    uint8_t n = 0U;
    if ((hi2c == NULL) || (found == NULL) || (max == 0U)) { return 0U; }

    for (uint8_t a = 0x08U; a <= 0x77U; a++) {
        if (HAL_I2C_IsDeviceReady(hi2c, (uint16_t)(a << 1), 2U, 5U) == HAL_OK) {
            found[n++] = a;
            if (n >= max) { break; }
        }
    }
    return n;
}

HAL_StatusTypeDef LCD1602_Init(LCD1602_t *lcd, I2C_HandleTypeDef *hi2c, uint8_t addr7)
{
    if ((lcd == NULL) || (hi2c == NULL)) { return HAL_ERROR; }

    memset(lcd, 0, sizeof(*lcd));
    lcd->hi2c      = hi2c;
    lcd->backlight = 1U;

    LCD1602_DelayInit();

    /* ── 주소 결정 ── */
    if (addr7 != 0U) {
        if (HAL_I2C_IsDeviceReady(hi2c, (uint16_t)(addr7 << 1), 3U, 10U) != HAL_OK) {
            return HAL_ERROR;
        }
        lcd->addr7 = addr7;
    } else {
        static const uint8_t cand[] = { LCD1602_ADDR_PCF8574, LCD1602_ADDR_PCF8574A };
        lcd->addr7 = 0U;
        for (uint8_t i = 0U; i < (uint8_t)(sizeof(cand)); i++) {
            if (HAL_I2C_IsDeviceReady(hi2c, (uint16_t)(cand[i] << 1), 3U, 10U) == HAL_OK) {
                lcd->addr7 = cand[i];
                break;
            }
        }
        if (lcd->addr7 == 0U) { return HAL_ERROR; }
    }

    /*
     * HD44780 전원 투입 후 안정화.
     * 데이터시트: VCC가 4.5V를 넘은 뒤 40ms 이상. 여유를 두어 50ms.
     * (MCU가 부팅 직후 여기 도달하면 LCD 전원이 아직 안 올라와 있을 수 있다)
     */
    HAL_Delay(50U);

    /* ── 8비트 → 4비트 진입 시퀀스 (nibble 단독 전송) ── */
    write_nibble(lcd, 0x03U, 0U);  HAL_Delay(5U);      /* ≥4.1ms */
    write_nibble(lcd, 0x03U, 0U);  delay_us(150U);     /* ≥100µs */
    write_nibble(lcd, 0x03U, 0U);  delay_us(150U);
    write_nibble(lcd, 0x02U, 0U);  delay_us(150U);     /* 4비트 모드 확정 */

    /* ── 이후부터는 8비트(=2 nibble) 전송 가능 ── */
    send_cmd(lcd, CMD_FUNCTION_SET | 0x08U);           /* 4bit, 2line, 5x8 */
    send_cmd(lcd, CMD_DISPLAY_CTRL);                   /* Display OFF */
    send_cmd(lcd, CMD_CLEAR);          HAL_Delay(2U);  /* ≥1.52ms */
    send_cmd(lcd, CMD_ENTRY_MODE | 0x02U);             /* 좌→우, 시프트 없음 */
    send_cmd(lcd, CMD_DISPLAY_CTRL | 0x04U);           /* Display ON, 커서 off */

    lcd->initialized = 1U;
    return (lcd->err_count == 0U) ? HAL_OK : HAL_ERROR;
}

/* ══════════════════════════════════════════════════════════════════════
 *  출력 API
 * ══════════════════════════════════════════════════════════════════════ */

void LCD1602_Clear(LCD1602_t *lcd)
{
    send_cmd(lcd, CMD_CLEAR);
    HAL_Delay(2U);
}

void LCD1602_Home(LCD1602_t *lcd)
{
    send_cmd(lcd, CMD_HOME);
    HAL_Delay(2U);
}

void LCD1602_SetCursor(LCD1602_t *lcd, uint8_t col, uint8_t row)
{
    if (row >= LCD1602_ROWS) { row = (uint8_t)(LCD1602_ROWS - 1U); }
    if (col >= LCD1602_COLS) { col = (uint8_t)(LCD1602_COLS - 1U); }
    send_cmd(lcd, (uint8_t)(CMD_SET_DDRAM | (s_row_offset[row] + col)));
}

void LCD1602_Backlight(LCD1602_t *lcd, uint8_t on)
{
    lcd->backlight = on ? 1U : 0U;
    /* 백라이트 비트만 갱신 (E 펄스 없이 P3만 쓴다) */
    uint8_t b = lcd->backlight ? PIN_BL : 0U;
    (void)bus_tx(lcd, &b, 1U);
}

void LCD1602_DisplayOn(LCD1602_t *lcd, uint8_t on)
{
    send_cmd(lcd, (uint8_t)(CMD_DISPLAY_CTRL | (on ? 0x04U : 0x00U)));
}

void LCD1602_PutChar(LCD1602_t *lcd, char c)
{
    send_data(lcd, (uint8_t)c);
}

void LCD1602_Print(LCD1602_t *lcd, const char *s)
{
    if (s == NULL) { return; }
    while (*s != '\0') { send_data(lcd, (uint8_t)(*s++)); }
}

void LCD1602_PrintAt(LCD1602_t *lcd, uint8_t col, uint8_t row, const char *s)
{
    LCD1602_SetCursor(lcd, col, row);
    LCD1602_Print(lcd, s);
}

void LCD1602_WriteLine(LCD1602_t *lcd, uint8_t row, const char *s)
{
    char line[LCD1602_COLS + 1U];
    size_t n = 0U;

    if (s != NULL) {
        n = strlen(s);
        if (n > LCD1602_COLS) { n = LCD1602_COLS; }
        memcpy(line, s, n);
    }
    memset(&line[n], ' ', LCD1602_COLS - n);
    line[LCD1602_COLS] = '\0';

    LCD1602_SetCursor(lcd, 0U, row);
    LCD1602_Print(lcd, line);
}

void LCD1602_Printf(LCD1602_t *lcd, uint8_t col, uint8_t row, const char *fmt, ...)
{
    char buf[LCD1602_COLS + 1U];
    va_list ap;

    va_start(ap, fmt);
    (void)vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    LCD1602_PrintAt(lcd, col, row, buf);
}

void LCD1602_PrintfLine(LCD1602_t *lcd, uint8_t row, const char *fmt, ...)
{
    char buf[LCD1602_COLS + 1U];
    va_list ap;

    va_start(ap, fmt);
    (void)vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    LCD1602_WriteLine(lcd, row, buf);
}

void LCD1602_CreateChar(LCD1602_t *lcd, uint8_t slot, const uint8_t pattern[8])
{
    if ((pattern == NULL) || (slot > 7U)) { return; }

    send_cmd(lcd, (uint8_t)(CMD_SET_CGRAM | (uint8_t)(slot << 3)));
    for (uint8_t i = 0U; i < 8U; i++) {
        send_data(lcd, (uint8_t)(pattern[i] & 0x1FU));
    }
    /* CGRAM 접근 후에는 DDRAM 주소가 불확실하므로 호출부에서 SetCursor 필요 */
    LCD1602_SetCursor(lcd, 0U, 0U);
}
