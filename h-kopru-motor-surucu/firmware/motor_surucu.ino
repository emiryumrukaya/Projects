// --- PIN TANIMLAMALARI ---
const int swIleri = 2;      
const int swGeri = 3;       
const int potPin = A1;      

// Üst Kat (2N2222 -> P-Kanal)
const int ustSol_Tetik = 12; 
const int ustSag_Tetik = 9;

// Alt Kat (IRFZ44N -> N-Kanal PWM)
const int altSol_PWM = 5;   
const int altSag_PWM = 6;

// --- AYARLAR ---
const int RAMPA_HIZLANMA_SURESI = 20;  // Hızlanma gecikmesi (ms)
const int RAMPA_YAVASLAMA_SURESI = 3;  // Yavaşlama gecikmesi (ms)
const int OLU_ZAMAN = 2000;            // Yön değiştirme beklemesi (ms)
const int OLU_BOLGE_SINIRI = 10;       // 0-255 arası gürültü eşiği

// --- DEĞİŞKENLER ---
int anlikPWM = 0;           
int hedefPWM = 0;           
int aktifYon = 0; // 0: Dur, 1: İleri, 2: Geri

// --- FONKSIYON PROTOTIPLERI (Hata almamak icin) ---
void motoruSur(int yon, int hiz);
void herSeyiDurdur();

void setup() {
  Serial.begin(9600);
  pinMode(swIleri, INPUT_PULLUP);
  pinMode(swGeri, INPUT_PULLUP);
  pinMode(ustSol_Tetik, OUTPUT);
  pinMode(ustSag_Tetik, OUTPUT);
  pinMode(altSol_PWM, OUTPUT);
  pinMode(altSag_PWM, OUTPUT);
  
  herSeyiDurdur();
  Serial.println("SISTEM HAZIR - GAZ BEKLIYOR");
}

void loop() {
  // 1. Pot Okuma
  int potDeger = analogRead(potPin);
  hedefPWM = map(potDeger, 0, 1023, 0, 255); 

  // --- GÜRÜLTÜ FİLTRESİ (DEADZONE) ---
  if (hedefPWM < OLU_BOLGE_SINIRI) {
    hedefPWM = 0; 
  }

  // 2. İstenen Yönü Belirle
  int istenenYon = 0;
  if (digitalRead(swIleri) == LOW) istenenYon = 1;
  else if (digitalRead(swGeri) == LOW) istenenYon = 2;
  else istenenYon = 0;

  // --- YÖN DEĞİŞİMİ VE FRENLEME MANTIĞI ---
  if (istenenYon != aktifYon) {
    
    if (anlikPWM > 0) Serial.println(">>> YAVASLAMA MODUNA GIRILIYOR...");
    
    // A) MEVCUT YÖNDE YAVAŞLA VE DUR (RAMPA AŞAĞI)
    while (anlikPWM > 0) {
      anlikPWM--; // Hızı azalt
      motoruSur(aktifYon, anlikPWM); // Azalan hızı motora gönder
      
      // EKRANA YAZDIR (Frenleme Anı)
      Serial.print("YAVASLIYOR -> YON: "); Serial.print(aktifYon);
      Serial.print(" | PWM: "); Serial.println(anlikPWM);
      
      delay(RAMPA_YAVASLAMA_SURESI); 
    }
    
    // B) TAM DURUŞ VE GÜVENLİK BEKLEMESİ
    herSeyiDurdur();
    anlikPWM = 0;
    
    // Sadece yön değişiyorsa ölü zaman koy
    if (aktifYon != 0 && istenenYon != 0) {
        Serial.println("!!! TAM DURUS - OLU ZAMAN (MOSFET SOGUTMA) !!!");
        delay(OLU_ZAMAN); 
    } else {
        delay(50); // Minik bekleme
    }
    
    // C) YENİ YÖNE GEÇİŞ
    aktifYon = istenenYon; 
    if (aktifYon != 0) Serial.println(">>> YON DEGISTI - HIZLANMA BASLIYOR");
  }

  // 3. Normal Çalışma (Rampa Yukarı / Hızlanma)
  if (aktifYon != 0) {
    bool degisimOldu = false;
    
    if (anlikPWM < hedefPWM) {
      anlikPWM++;
      degisimOldu = true;
      delay(RAMPA_HIZLANMA_SURESI);
    } else if (anlikPWM > hedefPWM) {
      anlikPWM--;
      degisimOldu = true;
      delay(RAMPA_HIZLANMA_SURESI);
    }
    
    if (degisimOldu) {
       motoruSur(aktifYon, anlikPWM);
       // EKRANA YAZDIR
       // Seri portu çok şişirmemek için sadece değişimde yaz
       if(anlikPWM % 10 == 0 || anlikPWM == hedefPWM) {
          Serial.print("RAMPA CIKIYOR -> YON: "); Serial.print(aktifYon);
          Serial.print(" | PWM: "); Serial.println(anlikPWM);
       }
    }
    
  } else {
    herSeyiDurdur(); // Yön 0 ise dur
    if (anlikPWM > 0) { // Sadece sıfır değilse sıfırla
        anlikPWM = 0;
        Serial.println("--- DURUYOR ---");
    }
  }
}

// --- YARDIMCI FONKSIYONLAR ---
void motoruSur(int yon, int hiz) {
  if (yon == 1) { // İLERİ
    digitalWrite(ustSag_Tetik, LOW);
    analogWrite(altSol_PWM, 0);
    digitalWrite(ustSol_Tetik, HIGH);
    analogWrite(altSag_PWM, hiz);
  } 
  else if (yon == 2) { // GERİ
    digitalWrite(ustSol_Tetik, LOW);
    analogWrite(altSag_PWM, 0);
    digitalWrite(ustSag_Tetik, HIGH);
    analogWrite(altSol_PWM, hiz);
  }
  else {
    herSeyiDurdur();
  }
}

void herSeyiDurdur() {
  digitalWrite(ustSol_Tetik, LOW);
  digitalWrite(ustSag_Tetik, LOW);
  analogWrite(altSol_PWM, 0);
  analogWrite(altSag_PWM, 0);
}