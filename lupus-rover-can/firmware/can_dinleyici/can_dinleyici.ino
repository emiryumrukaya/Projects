  #include <ESP32-TWAI-CAN.hpp> // TWAI (CAN) kütüphanesini çağırır.

  #define RX_PIN 4 // VC230 modülünün RX (CRX) pininin ESP'de gireceği yer.
  #define TX_PIN 5 // VC230 modülünün TX (CTX) pininin ESP'de gireceği yer.

  void setup() {
    Serial.begin(115200); // Bilgisayarla iletişimi 115200 hızında başlatır.
    ESP32Can.setPins(TX_PIN, RX_PIN); // ESP32'ye CAN pinlerinin yerini öğretir.
    
    // Hattı 250k hızında başlatır (Cube ve Here 4'ün 250k'da olduğundan eminiz).
    if (ESP32Can.begin(ESP32Can.convertSpeed(250))) { 
      Serial.println("Hat hazir! FİLTRELER KAPALI, akan her sey basiliyor..."); // Başarı mesajı.
    } else {
      Serial.println("HATA: CAN baslatilamadi!"); // Modül bozuksa veya pinler yanlışsa uyarır.
    }
  }

  void loop() {
    CanFrame mesaj; // Gelen veriyi tutacak boş kutu oluşturur.

    // Hat üzerinde mesaj varsa ve okunabildiyse içeri girer (10ms bekleme süresi).
    if (ESP32Can.readFrame(mesaj, 10)) { 
      
      uint32_t tam_id = mesaj.identifier; // Gelen paketin devasa kimlik numarasını alır.
      uint8_t gonderen_node_id = tam_id & 0x7F; // Sağdan 7 biti keserek cihazın kimliğini (10 veya 125) bulur.
      uint16_t mesaj_turu_id = (tam_id >> 8) & 0xFFFF; // Ortadaki 16 biti keserek paketin ne olduğunu (örn: 1063) bulur.
      
      // HİÇBİR FİLTRE YOK! Gelen her şeyi direkt ekrana kusuyoruz.
      Serial.print("Node: "); 
      Serial.print(gonderen_node_id); 
      Serial.print(" | Mesaj ID: "); 
      Serial.println(mesaj_turu_id); 
    }
  }