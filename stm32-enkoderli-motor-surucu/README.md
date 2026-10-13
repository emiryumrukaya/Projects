# STM32 Tabanlı Enkoderli DC Motor Sürücüsü (Bitirme Projesi)

**Durum:** Çalışıyor · 2026

[Motorobit 12 V enkoderli DC motorun](https://www.motorobit.com/12v-encoderli-dc-motor) hızını kapalı çevrimde kontrol eden sürücü. Hedef rpm LCD ve butonlarla giriliyor; ölçülen rpm hedefe oturtuluyor. Kartın PCB'sini kendim tasarlayıp ürettim.

<p>
  <img src="images/kart.jpg" width="300">
  <img src="images/pcb.jpg" width="230">
</p>

## Donanım
- **Mikrodenetleyici:** STM32F103 (HAL, CubeMX), 8 MHz kristal × 9 = 72 MHz
- **Güç katı:** 4 N-kanal MOSFET'li tam H-köprü, her kolda bir IR2103 kapı sürücüsü (bootstrap ile üst taraf)
- **Arayüz:** 16×2 HD44780 LCD (4-bit mod, PA0–PA5), 4 buton PB12–PB15 (artır, azalt, başlat/durdur, yön)

## Firmware ([`main.c`](firmware/main.c), [`lcd.c`](firmware/lcd.c), [`lcd.h`](firmware/lcd.h))
| Bölüm | Uygulama |
|---|---|
| PWM | TIM1, 72 MHz / 3600 = **20 kHz**. Görev oranı %95 ile sınırlı; böylece bootstrap kondansatörü her periyotta dolabiliyor (`PWM_MAX`). |
| Ölü zaman | IR2103'ün dahili ölü zamanı kullanılıyor (TIM1 ölü zaman ayarı 0). Osiloskopla doğrulandı. |
| Hız ölçümü | TIM4 harici saat modu: enkoder kanalının yükselen kenarları donanımda sayılıyor (filtre = 8). Her 50 ms'de sayaç farkından rpm hesaplanıyor. |
| Denetleyici | PI + ileri besleme, 50 ms periyot. Kp = 15, Ki = 60; ileri besleme kazancı 80 rpm'de tam görev oranına karşılık gelecek şekilde. Doyumda integrali donduran anti-windup. |
| Referans | 80 rpm/s rampa; ani hedef değişiminde akım sıçramasını önlüyor. |
| Yön değişimi | Hedef 0'a çekiliyor, motor durunca iki alt MOSFET açık (fren) 500 ms bekleniyor, sonra yön değişiyor. |
| Arayüz | Butonlar 10 ms'de bir taranıyor (kenar algılama), LCD 200 ms'de bir güncelleniyor. |

## Sorun → çözüm: arızalı enkoder
Motorun enkoderinin bir kanalı arızalı çıktı, bu yüzden timer'ın iki kanallı enkoder modu kullanılamadı. Sağlam kanalı TIM4'e harici saat olarak bağlayıp yükselen kenarları donanımda saydım. Tek kanaldan yön ölçülemediği için yön bilgisi komuttan geliyor.

## Osiloskop ölçümleri
| | |
|---|---|
| <img src="images/osiloskop_kapi_sinyalleri.jpg" width="380"><br>IR2103 çıkışları: mavi üst taraf, sarı alt taraf | <img src="images/osiloskop_olu_zaman.jpg" width="380"><br>Geçiş anı yakın çekim: ölü zaman |
| <img src="images/osiloskop_20khz.jpg" width="380"><br>50 rpm hedefinde kapı sinyali, 20 kHz | <img src="images/osiloskop_pwm_50.jpg" width="380"><br>%50 görev oranı: sarı PWM, mavi motor gerilimi |

> Not: LCD sürücüsünü (`lcd.c`) HAL üzerinde kendim yazdım. CubeMX proje dosyası (`.ioc`) repoda yok; çevre birimi ayarları `main.c` içindeki `MX_..._Init` fonksiyonlarında.
