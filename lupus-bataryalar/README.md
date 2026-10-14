# Lupus Rover Batarya Paketleri (12S8P ve 7S7P)

**Durum:** Araçta kullanılıyor · Lupus Rover Team · 2026 · Takım arkadaşım Can ile birlikte ürettik

Rover için iki Li-ion paket ürettik. Hücreleri punta kaynağıyla nikel şeritlerle bağladık. Her pakette bir Daly R24TM serisi (150 A) akıllı BMS var; iki paketin çıkışı da XT90.

| | 12S8P paket | 7S7P paket |
|---|---|---|
| Hücre | 4000 mAh Li-ion, 96 adet | 2400 mAh Li-ion, 49 adet |
| Gerilim aralığı (3,0–4,2 V/hücre) | 36–50,4 V (nominal 43,2 V) | 21–29,4 V (nominal 25,2 V) |
| Kapasite | 32 Ah (≈ 1,4 kWh) | 16,8 Ah (≈ 0,42 kWh) |
| BMS | Daly R24TM, 150 A | Daly R24TM, 150 A |

*Nominal gerilim ve enerji, hücre başına 3,6 V nominal alınarak hesaplandı.*

## BMS ayarları
- Hücre başı alt sınır 3,0 V, üst sınır 4,2 V
- Sıcaklık sınırı 45 °C
- BMS, rover'ın CAN hattında; ana ESP32 gerilim, akım ve şarj durumunu (SOC) 200 ms'de bir okuyup Jetson Orin'e iletiyor. Ayrıntı: [CAN haberleşmesi](../lupus-rover-can/)

## Fotoğraflar
| | |
|---|---|
| <img src="images/7s_montaj_bms.jpg" width="420"><br>7S paketin montajı: hücre tutucular, punta kaynaklı nikel şeritler, denge kabloları ve Daly BMS | <img src="images/12s_paket.jpg" width="420"><br>12S paket, makaronla kaplanmış hâli |
| <img src="images/12s_kutu.jpg" width="320"><br>12S paket, XT90 çıkışlı kutusunda | |
