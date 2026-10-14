/*
 * lcd.h  -  HD44780 16x2 LCD, 4-bit mod, sadece yazma (RW = GND)
 *
 * Baglanti (Bitirme Projesi):
 *   RS -> PA0    E  -> PA1
 *   D7 -> PA2    D6 -> PA3    D5 -> PA4    D4 -> PA5
 *
 * CubeMX: PA0..PA5 = GPIO_Output (push-pull, baslangic seviyesi Low)
 */
#ifndef LCD_H
#define LCD_H

#include "main.h"

void lcd_init(void);
void lcd_clear(void);
void lcd_gotoxy(uint8_t sutun, uint8_t satir);   /* sutun 0..15, satir 0..1 */
void lcd_print(const char *s);
void lcd_cmd(uint8_t cmd);
void lcd_data(uint8_t data);

#endif /* LCD_H */
