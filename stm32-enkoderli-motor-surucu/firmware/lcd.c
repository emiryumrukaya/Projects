/*
 * lcd.c  -  HD44780 16x2 LCD surucusu, 4-bit mod, sadece yazma (RW = GND)
 *
 * Pin eslemesi bu dosyanin basindaki tanimlardan degistirilebilir.
 * Turkce karakterler (c, s, g, i, o, u'nun noktali halleri) LCD'nin
 * karakter tablosunda yok; metinlerde ASCII kullan.
 */
#include "lcd.h"

/* ---- Pin tanimlari ---------------------------------------------------- */
#define LCD_RS_PORT  GPIOA
#define LCD_RS_PIN   GPIO_PIN_0
#define LCD_E_PORT   GPIOA
#define LCD_E_PIN    GPIO_PIN_1

/* Veri pinleri: indeks 0 = D4, 1 = D5, 2 = D6, 3 = D7 */
static GPIO_TypeDef * const veri_port[4] = { GPIOA, GPIOA, GPIOA, GPIOA };
static const uint16_t       veri_pin[4]  = { GPIO_PIN_5,   /* D4 */
                                             GPIO_PIN_4,   /* D5 */
                                             GPIO_PIN_3,   /* D6 */
                                             GPIO_PIN_2 }; /* D7 */

/* ---- Kucuk gecikme ----------------------------------------------------- */
/* Her dongu adimi en az ~4 cevrim surer, bu yuzden istenen sureden
   kisa olmaz (uzun olmasi LCD icin sorun degil). Saat 8 MHz de olsa
   72 MHz de olsa SystemCoreClock'a gore olceklenir. */
static void lcd_delay_us(uint32_t us)
{
    volatile uint32_t n = us * (SystemCoreClock / 1000000U) / 4U + 1U;
    while (n--) { }
}

/* ---- Alt seviye -------------------------------------------------------- */
static void lcd_pulse_e(void)
{
    HAL_GPIO_WritePin(LCD_E_PORT, LCD_E_PIN, GPIO_PIN_SET);
    lcd_delay_us(2);                        /* E yuksek en az 450 ns */
    HAL_GPIO_WritePin(LCD_E_PORT, LCD_E_PIN, GPIO_PIN_RESET);
    lcd_delay_us(2);
}

static void lcd_write4(uint8_t nibble)
{
    for (int i = 0; i < 4; i++) {
        HAL_GPIO_WritePin(veri_port[i], veri_pin[i],
                          (nibble & (1U << i)) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    }
    lcd_pulse_e();
}

static void lcd_write8(uint8_t value, uint8_t rs)
{
    HAL_GPIO_WritePin(LCD_RS_PORT, LCD_RS_PIN, rs ? GPIO_PIN_SET : GPIO_PIN_RESET);
    lcd_write4(value >> 4);                 /* once ust 4 bit */
    lcd_write4(value & 0x0F);               /* sonra alt 4 bit */
    lcd_delay_us(50);                       /* komut suresi ~37 us */
}

/* ---- Disari acik fonksiyonlar ------------------------------------------ */
void lcd_cmd(uint8_t cmd)
{
    lcd_write8(cmd, 0);
    if (cmd == 0x01 || cmd == 0x02) {       /* clear / home uzun surer */
        HAL_Delay(2);
    }
}

void lcd_data(uint8_t data)
{
    lcd_write8(data, 1);
}

void lcd_init(void)
{
    HAL_GPIO_WritePin(LCD_RS_PORT, LCD_RS_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LCD_E_PORT,  LCD_E_PIN,  GPIO_PIN_RESET);
    HAL_Delay(50);                          /* acilista LCD'nin kendine gelmesi */

    /* 4-bit moda gecis dizisi (HD44780 datasheet, Figure 24) */
    lcd_write4(0x03); HAL_Delay(5);
    lcd_write4(0x03); lcd_delay_us(150);
    lcd_write4(0x03); lcd_delay_us(150);
    lcd_write4(0x02); lcd_delay_us(150);

    lcd_cmd(0x28);   /* 4-bit, 2 satir, 5x8 font  */
    lcd_cmd(0x08);   /* ekran kapali              */
    lcd_cmd(0x01);   /* temizle                   */
    lcd_cmd(0x06);   /* yazdikca imlec saga kaysin */
    lcd_cmd(0x0C);   /* ekran acik, imlec gizli    */
}

void lcd_clear(void)
{
    lcd_cmd(0x01);
}

void lcd_gotoxy(uint8_t sutun, uint8_t satir)
{
    uint8_t adres = (satir == 0) ? 0x00 : 0x40;
    lcd_cmd(0x80 | (adres + sutun));
}

void lcd_print(const char *s)
{
    while (*s) {
        lcd_data((uint8_t)*s++);
    }
}
