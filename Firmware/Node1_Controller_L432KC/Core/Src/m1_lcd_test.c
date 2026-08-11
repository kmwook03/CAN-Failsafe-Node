/**
 * @file    m1_lcd_test.c
 * @brief   M1-② 1602 I2C LCD 드라이버 검증 시나리오
 * @target  STM32L432KC (Node 1 : REMOTE)
 *
 *  main.c 를 건드리는 대신 이 파일 하나를 추가하고, CubeMX가 생성한 main() 의
 *  USER CODE 블록에서 아래 두 줄만 넣으면 된다.
 *
 *    /​* USER CODE BEGIN 2 *​/
 *      M1_LCD_Test_Run(&hi2c1, &huart2);
 *    /​* USER CODE END 2 *​/
 *
 *  printf 를 UART VCP로 빼려면 M0에서 이미 _write() 를 재정의해 두었을 것이다.
 *  안 되어 있으면 이 파일 하단의 __io_putchar 예시를 참고할 것.
 */

#include "lcd1602_i2c.h"
#include <stdio.h>
#include <string.h>

void M1_LCD_Test_Run(I2C_HandleTypeDef *hi2c, UART_HandleTypeDef *huart);

static LCD1602_t lcd;

/* 5x8 커스텀 문자 — 진행바용 세로 3칸 블록과 CAN 링크 아이콘 */
static const uint8_t GLYPH_BAR[8] = {
    0x1F, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x1F, 0x00
};
static const uint8_t GLYPH_LINK[8] = {
    0x00, 0x0E, 0x11, 0x00, 0x0E, 0x11, 0x00, 0x04
};

/* DWT 기반 경과시간 측정 (M6 실측 표에 그대로 옮겨 쓸 수 있게) */
static uint32_t s_t0;
static void     tic(void) { DWT->CYCCNT = 0U; s_t0 = DWT->CYCCNT; }
static uint32_t toc_us(void)
{
    uint32_t cyc = DWT->CYCCNT - s_t0;
    return cyc / (HAL_RCC_GetHCLKFreq() / 1000000U);
}

/* ══════════════════════════════════════════════════════════════════════
 *  STEP 1 — I2C 버스 스캔
 * ══════════════════════════════════════════════════════════════════════ */
static uint8_t step1_scan(I2C_HandleTypeDef *hi2c)
{
    uint8_t found[8];
    uint8_t n;

    printf("\r\n[STEP 1] I2C bus scan (0x08..0x77)\r\n");
    n = LCD1602_ScanBus(hi2c, found, (uint8_t)(sizeof(found)));

    if (n == 0U) {
        printf("  >> NO DEVICE FOUND.\r\n");
        printf("     check: (1) SB16/SB18 solder bridge  (2) VCC=5V, GND common\r\n");
        printf("            (3) SDA=PB7(D4) / SCL=PB6(D5) not swapped\r\n");
        printf("            (4) GPIO mode = Open-Drain, internal pull-up OFF\r\n");
        return 0U;
    }

    for (uint8_t i = 0U; i < n; i++) {
        printf("  found 7-bit 0x%02X  (HAL arg 0x%02X)%s\r\n",
               found[i], (unsigned)(found[i] << 1),
               (found[i] == 0x27U) ? "  <- PCF8574"
             : (found[i] == 0x3FU) ? "  <- PCF8574A" : "");
    }
    return found[0];
}

/* ══════════════════════════════════════════════════════════════════════
 *  STEP 2 — 초기화 + 정적 표시
 * ══════════════════════════════════════════════════════════════════════ */
static bool step2_init(I2C_HandleTypeDef *hi2c, uint8_t addr)
{
    printf("\r\n[STEP 2] LCD init (addr auto-detect if 0)\r\n");

    if (LCD1602_Init(&lcd, hi2c, addr) != HAL_OK) {
        printf("  >> INIT FAILED (err_count=%u)\r\n", lcd.err_count);
        return false;
    }
    printf("  ok. addr=0x%02X\r\n", lcd.addr7);

    LCD1602_PrintAt(&lcd, 0, 0, "RC-CAR NODE1");
    LCD1602_PrintAt(&lcd, 0, 1, "M1-2 LCD OK");
    HAL_Delay(1500);
    return true;
}

/* ══════════════════════════════════════════════════════════════════════
 *  STEP 3 — 문자셋 전수 확인 (DDRAM 주소 매핑 + 결점 픽셀 확인)
 * ══════════════════════════════════════════════════════════════════════ */
static void step3_charset(void)
{
    printf("\r\n[STEP 3] charset sweep 0x20..0x7F\r\n");

    for (uint8_t base = 0x20U; base < 0x80U; base = (uint8_t)(base + 32U)) {
        char l0[17], l1[17];
        for (uint8_t i = 0U; i < 16U; i++) {
            l0[i] = (char)(base + i);
            l1[i] = (char)(base + 16U + i);
        }
        l0[16] = '\0';
        l1[16] = '\0';

        LCD1602_WriteLine(&lcd, 0, l0);
        LCD1602_WriteLine(&lcd, 1, l1);
        HAL_Delay(1200);
    }
    printf("  done. 32 cells x 4 pages displayed.\r\n");
}

/* ══════════════════════════════════════════════════════════════════════
 *  STEP 4 — 커스텀 문자 (CGRAM)
 * ══════════════════════════════════════════════════════════════════════ */
static void step4_cgram(void)
{
    printf("\r\n[STEP 4] CGRAM custom glyph\r\n");

    LCD1602_CreateChar(&lcd, 0, GLYPH_BAR);
    LCD1602_CreateChar(&lcd, 1, GLYPH_LINK);

    LCD1602_Clear(&lcd);
    LCD1602_PrintAt(&lcd, 0, 0, "CGRAM \x01 LINK");

    /*
     * 0~16칸 진행바 애니메이션.
     * CGRAM slot 0 은 문자코드 0x00 이고, 이는 C 문자열 종료 문자와 겹친다.
     * 따라서 문자열/Print 계열을 쓸 수 없고 PutChar 로 직접 밀어 넣어야 한다.
     * (M4 대시보드에서 진행바를 쓸 계획이면 slot 0 을 피하고 1~7 을 쓰는 편이 편하다)
     */
    for (uint8_t p = 0U; p <= 16U; p++) {
        LCD1602_SetCursor(&lcd, 0, 1);
        for (uint8_t i = 0U; i < 16U; i++) {
            LCD1602_PutChar(&lcd, (i < p) ? '\x00' : ' ');
        }
        HAL_Delay(80);
    }
    printf("  done.\r\n");
}

/* ══════════════════════════════════════════════════════════════════════
 *  STEP 5 — 갱신 시간 실측 (M6 표 입력값)
 * ══════════════════════════════════════════════════════════════════════ */
static void step5_timing(void)
{
    uint32_t t_full, t_line, t_16ch, t_cmd;

    printf("\r\n[STEP 5] transfer cost measurement\r\n");
    printf("  STRICT_TIMING = %d (%d bytes per nibble)\r\n",
           LCD1602_STRICT_TIMING, LCD1602_STRICT_TIMING ? 3 : 2);

    tic();  LCD1602_SetCursor(&lcd, 0, 0);                  t_cmd  = toc_us();
    tic();  for (int i = 0; i < 16; i++) { LCD1602_PutChar(&lcd, '0'); }
            t_16ch = toc_us();
    tic();  LCD1602_WriteLine(&lcd, 0, "TIMING TEST");       t_line = toc_us();
    tic();  LCD1602_WriteLine(&lcd, 0, "1234567890123456");
            LCD1602_WriteLine(&lcd, 1, "ABCDEFGHIJKLMNOP");  t_full = toc_us();

    printf("  1 command      : %6lu us\r\n", (unsigned long)t_cmd);
    printf("  16 chars       : %6lu us\r\n", (unsigned long)t_16ch);
    printf("  1 line (pad)   : %6lu us\r\n", (unsigned long)t_line);
    printf("  full 2x16      : %6lu us   <-- UiTask 100ms 주기 대비 %lu%%\r\n",
           (unsigned long)t_full, (unsigned long)(t_full / 1000U));
    printf("  err_count      : %u\r\n", lcd.err_count);
    printf("  ** Blocking 기준 수치. M3에서 DMA + dirty-diff 적용 후 재측정할 것.\r\n");
}

/* ══════════════════════════════════════════════════════════════════════
 *  STEP 6 — 실사용 레이아웃 리허설 (M4 대시보드 미리보기)
 *           깜빡임 없이 값만 갱신되는지 확인한다.
 * ══════════════════════════════════════════════════════════════════════ */
static void step6_dashboard(void)
{
    printf("\r\n[STEP 6] dashboard layout rehearsal (20 frames @100ms)\r\n");
    LCD1602_Clear(&lcd);

    for (int f = 0; f < 20; f++) {
        int8_t  thr  = (int8_t)((f * 11) % 201 - 100);
        int8_t  str  = (int8_t)((f * 7)  % 201 - 100);
        uint8_t dist = (uint8_t)(15 + (f * 3) % 90);

        /* 0123456789012345
           T+100 S-100
           RUN  D:105 E:0   */
        LCD1602_PrintfLine(&lcd, 0, "T%+4d S%+4d", thr, str);
        LCD1602_PrintfLine(&lcd, 1, "RUN  D:%3u E:%u", dist, lcd.err_count);
        HAL_Delay(100);
    }
    printf("  done. 깜빡임/잔상 있으면 Clear() 남용 여부를 확인할 것.\r\n");
}

/* ══════════════════════════════════════════════════════════════════════
 *  엔트리
 * ══════════════════════════════════════════════════════════════════════ */
void M1_LCD_Test_Run(I2C_HandleTypeDef *hi2c, UART_HandleTypeDef *huart)
{
    (void)huart;   /* printf 재지정 방식이면 미사용 */

    LCD1602_DelayInit();

    printf("\r\n===============================================\r\n");
    printf(" M1-2 : 1602 I2C LCD driver verification\r\n");
    printf(" HCLK = %lu Hz\r\n", (unsigned long)HAL_RCC_GetHCLKFreq());
    printf("===============================================\r\n");

    uint8_t addr = step1_scan(hi2c);
    if (addr == 0U) {
        printf("\r\n[ABORT] fix wiring first.\r\n");
        return;
    }

    if (!step2_init(hi2c, addr)) { return; }

    step3_charset();
    step4_cgram();
    step5_timing();
    step6_dashboard();

    LCD1602_Clear(&lcd);
    LCD1602_PrintAt(&lcd, 0, 0, "M1-2 DoD PASS");
    LCD1602_Printf(&lcd, 0, 1, "addr%02X err%u", lcd.addr7, lcd.err_count);

    printf("\r\n[RESULT] %s  (err_count=%u)\r\n",
           (lcd.err_count == 0U) ? "PASS" : "FAIL - I2C errors occurred",
           lcd.err_count);
}

/* ──────────────────────────────────────────────────────────────────────
 *  printf → UART2 VCP 재지정 (M0에서 이미 했다면 이 블록은 삭제)
 * ────────────────────────────────────────────────────────────────────── */
#if 0
extern UART_HandleTypeDef huart2;

int __io_putchar(int ch)
{
    HAL_UART_Transmit(&huart2, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
    return ch;
}
#endif
