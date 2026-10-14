# Elektronik Projeler — Mustafa Emir Yumrukaya

Selçuk Üniversitesi Elektrik-Elektronik Mühendisliği 4. sınıf öğrencisiyim. Lupus Rover Team'in elektronik takım kaptanıyım. Bu repoda güç elektroniği ve gömülü sistemler projelerimin kodları, şemaları, PCB'leri ve ölçüm sonuçları duruyor.

*Hardware & embedded projects: motor drivers, a multi-output DC-DC power board and a rover CAN bus. Docs are in Turkish; code comments are mostly Turkish.*

| Proje | Durum | İçerik |
|---|---|---|
| [STM32 Enkoderli Motor Sürücü](stm32-enkoderli-motor-surucu/) — bitirme projesi | Çalışıyor | STM32F103 (HAL) firmware, PI + ileri besleme hız kontrolü, IR2103'lü tam H-köprü, osiloskop ölçümleri |
| [H-Köprü DC Motor Sürücü](h-kopru-motor-surucu/) — ders projesi, 2 kişilik ekip | Çalışır prototip | 24 V / 10 A P-N köprü, KiCad şema ve PCB, Arduino kodu |
| [Lupus Buck-V1](lupus-buck-v1/) — rover güç kartı | Üretim öncesi | 24 / 12 / 5 V çıkışlı güç kartı, KiCad şema ve PCB, tasarım hesapları, LTspice sonuçları |
| [Lupus Rover CAN Haberleşmesi](lupus-rover-can/) | Araçta çalışıyor | Here4 GPS, Daly BMS, ODrive, STM32, ESP32 ve Jetson Orin'li 250 kbit/s CAN hattı; ESP32 ana düğüm kodu, mesaj tablosu, DroneCAN kütüphane yaması |

Klasörlerin yapısı: `README.md` (projenin özeti ve sonuçları), `firmware/` (kod), `hardware/` (şema ve PCB, varsa) ve `images/` (fotoğraflar, ölçümler).

İletişim: memiryumrukaya@gmail.com
