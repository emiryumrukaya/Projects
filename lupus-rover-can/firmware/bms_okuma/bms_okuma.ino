#include <ESP32-TWAI-CAN.hpp> // ESP32 TWAI (CAN) kütüphanesi.

#define RX_PIN 4 // ESP32'nin RX pini.
#define TX_PIN 5 // ESP32'nin TX pini.

// Verileri tutacağımız genel değişkenler.
float gVoltage = 0.0;
float gCurrent = 0.0;
float gSOC = 0.0;

void setup() {
  Serial.begin(115200); // Seri portu başlat.
  ESP32Can.setPins(TX_PIN, RX_PIN); // Pinleri kütüphaneye tanıt.
  
  // Daly BMS 250 kbit/s ile çalışıyor.
  if (ESP32Can.begin(ESP32Can.convertSpeed(250))) { 
    Serial.println("Hat hazir, BMS'e soru soruluyor..."); 
  }
}

void loop() {
  CanFrame soru_paketi = { 0 }; 
  soru_paketi.identifier = 0x18900140; 
  soru_paketi.extd = true; 
  soru_paketi.data_length_code = 8; 

  ESP32Can.writeFrame(soru_paketi); 
  
  // delay(50); Yİ SİLDİK! BMS zaten 5-10ms içinde anında cevap verir.

  unsigned long arama_baslangici = millis();
  
  // 100 milisaniye boyunca hattı dinleyecek bir pusu döngüsü kurduk.
  while (millis() - arama_baslangici < 100) {
      CanFrame cevap; 
      
      // ESP32Can.readFrame if içinde olduğu sürece kuyruktan durmadan paket çeker (Here4 çöplerini de dahil).
      if (ESP32Can.readFrame(cevap, 10)) { 
          
          // Kuyruktan çektiğimiz paket DALY BMS ise avı yakaladık demektir!
          if (cevap.identifier == 0x18904001) { 
              uint16_t v_ham = (cevap.data[0] << 8) | cevap.data[1]; 
              uint16_t i_ham = (cevap.data[4] << 8) | cevap.data[5]; 
              uint16_t s_ham = (cevap.data[6] << 8) | cevap.data[7]; 

              gVoltage = v_ham / 10.0; 
              gCurrent = (i_ham - 30000) / 10.0; 
              gSOC = s_ham / 10.0; 

              Serial.print("{\"voltage\": "); 
              Serial.print(gVoltage, 1);
              Serial.print(", \"current\": "); 
              Serial.print(gCurrent, 1);
              Serial.print(", \"soc\": "); 
              Serial.print(gSOC, 1);
              Serial.println("}");
              
              break; // BMS'i bulduğumuz an 100ms'nin dolmasını beklemeden while döngüsünü kırıp çıkarız.
          }
      }
  }
  
  delay(1000); 
}