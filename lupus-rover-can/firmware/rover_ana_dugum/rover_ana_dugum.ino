#include <DroneCAN_ESP32.h>
#include "sbus.h"
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BNO055.h>
#include <utility/imumaths.h>
#define NODE_ID       100         // Bu ESP32'nin DroneCAN node ID'si
#define BITRATE       250000       // 250 kbps (Hz cinsinden)
#define BMS_QUERY_ID  0x18900140   // BMS'i uyandirmak icin gonderilecek sorgu ID'si
#define BMS_REPLY_ID  0x18904001   // BMS'in cevap verdigi ID
#define SBUS_RX_PIN 16   // KENDI KABLOLAMANIZA GORE DUZELTIN
#define SBUS_TX_PIN -1   // SBUS tek yonlu, TX kullanilmiyorsa -1 birakin
// ---- Jetson'a (can_bridge.cpp) GPS+BMS ozet frame'leri (2026-09-06 eklendi) ----
#define GPS_POS_ID     0x500
#define GPS_STATUS_ID  0x501
#define BMS_SUMMARY_ID 0x502
// ---- E-STOP (SBUS kanal 5 + SBUS iletisim kaybi) + OTONOM MOD (SBUS kanal 10, 3 pozisyon)
// -> Jetson, TEK frame'de (0x503) birlesik (2026-09-06) ----
#define ESTOP_ID       0x503
#define SBUS_ESTOP_KANAL 5   // 1-tabanli kanal numarasi (data.ch[SBUS_ESTOP_KANAL-1])
#define SBUS_ESTOP_ESIGI 1500 // VARSAYIM: kanal degeri bu esigin USTUNDEYSE e-stop AKTIF
                              // sayilir (tipik SBUS araligi ~172-1811, orta ~992). Gercek
                              // switch pozisyonunu test edip esigi/gerekirse yonunu ayarla.
#define SBUS_ZAMAN_ASIMI_MS 50 // Bu sure boyunca hic yeni SBUS karesi gelmezse (kutuphanenin
                                // kendi failsafe bitine EK olarak) iletisim koptu sayilir.

// ---- 3 POZISYONLU SWITCH (SBUS kanal 10): -100=MANUEL SURUS, 0=TARET, +100=OTONOM ----
#define OTONOM_MOD_KANAL   10
#define OTONOM_ESIK_DUSUK  500   // Bunun altinda -> MANUEL SURUS (-100 pozisyonu)
#define OTONOM_ESIK_YUKSEK 1500  // Bunun ustunde -> OTONOM (+100 pozisyonu)
                                 // Arada (500-1500) -> MANUEL TARET (0 pozisyonu)

// ---- ROLE TETIGI (SBUS kanal 9) ----
#define RELE_PIN     23      // GPIO19 arizali cikmisti, GPIO23'e tasindi
#define RELE_KANAL   9      // 1-tabanli kanal numarasi
#define RELE_ESIGI   1500   // Bu esigin ustunde -> role AKTIF
// Role modulu TERS mantik: aktif etmek icin pin LOW (GND'ye cekilmis) olmali.

// ---- TARET STEP MOTORLARI (SBUS kanal 10 == 0 -> taret modu) ---,

#define STEP2_STEP_PIN 17   // Pan (yatay)
#define STEP2_DIR_PIN  15
#define STEP3_STEP_PIN 26   // Tilt (dikey)
#define STEP3_DIR_PIN  13
#define ENA_PIN_SHARED 18

// ---- MANUEL STEP MOTOR (eski koddaki CH6 histerezisli yontem, bagimsiz 3. step motor) ----
#define MANUEL_STEP_PIN 32   // Pul (step)
#define MANUEL_DIR_PIN  33
#define MANUEL_EN_PIN   25
#define MANUEL_STEP_KANAL 6  // 1-tabanli SBUS kanal numarasi (data.ch[MANUEL_STEP_KANAL-1])

const int STEP_PWM_FREQ  = 1000;  // ledcAttach icin baslangic frekansi (sonra dinamik degisiyor)
const int STEP_PWM_RES   = 8;
const int STEP_DUTY      = 128;
const int TARET_MIN_FREQ = 100;   // hareket baslarken minimum hiz (Hz)
const int TARET_MAX_FREQ = 2000;  // stick tam dibe basildiginda maksimum hiz (Hz) -- kendi motorunuza gore ayarlayin
const int TARET_MAX_SAPMA = 800;  // SBUS'ta merkezden uca olan yaklasik mesafe (172/1811 - 1001)
const int TARET_MERKEZ = 1001;

// ---- CH6 step motor kodundaki KANITLANMIS histerezisli yontem: ----
// baslama esigi durma esiginden BUYUK olmali (START > STOP), boylece stick
// merkeze yakin bir yerde titresse bile motor surekli baslayip durmuyor.
const int TARET_BASLAMA_OFSET = 50; // merkezden bu kadar uzaklasinca hareket BASLAR
const int TARET_DURMA_OFSET   = 30;  // hareket baslaminca, merkeze bu kadar yaklasinca DURUR (< BASLAMA_OFSET)

// Uzeldogrusal (expo) hiz egrisi: frekans artik dogrusal degil, oran^KUVVET ile hesaplaniyor.
// KUVVET buyudukce stick'in ilk yarisinda hiz cok yavas artar (hassasiyet azalir),
// son yarisinda/uca yakinken hizla MAX_FREQ'e ulasir (tam basinca yine hizli olur).
// 1.0 = eskisi gibi dogrusal. 2-3 arasi genelde iyi bir baslangic noktasi.
const float TARET_EGRI_KUVVETI = 5;

bool taretModuAktif = false;
bool taretEnabledOnceki = false;

bool panIleri = false, panGeri = false;
bool tiltIleri = false, tiltGeri = false;

bool g_estopAktif = true; // estopDurumHesapla() icinde HER loop turunda guncellenir; motor/role kontrolleri buna bakar
                          // -- guvenli baslangic: ilk hesap yapilana kadar ESTOP AKTIF varsayilir

// ---- Manuel step motor (eski koddaki CH6 histerezisli yontem) ayarlari ----
const int MANUEL_STEP_START_HIGH = 1350; // ileri baslamak icin bu degeri gecmeli
const int MANUEL_STEP_STOP_HIGH  = 1150; // bu degerin altina dusunce ileri hareket durur
const int MANUEL_STEP_START_LOW  = 700;  // geri baslamak icin bu degerin altina inmeli
const int MANUEL_STEP_STOP_LOW   = 900;  // bu degerin ustune cikinca geri hareket durur
const int MANUEL_STEP_MIN_FREQ   = 50;
const int MANUEL_STEP_MAX_FREQ   = 2000;

bool manuelStepIleri = false, manuelStepGeri = false;

// ---- Anten step motoru (ayni fiziksel motor, pin 32/33/25): OTONOM modda CAN 0x22'den gelen
// heading'e gore (hedef = arac heading + 180) BNO055 geri bildirimiyle kapali cevrim takip,
// diger modlarda (TARET/MANUEL SURUS) yukaridaki eski CH6 manuel kontrol devam ediyor. ----
#define HEADING_CAN_ID 0x22  // Payload: byte[0-1] = int16 LE, deger = arac_heading_derece * 10 (0.1 derece cozunurluk)

#define ANTEN_BNO_SDA_PIN 21
#define ANTEN_BNO_SCL_PIN 22
#define ANTEN_BNO_ADDR 0x29   // Anten diregi icindeki BNO055 -- ADR pinine gore 0x28/0x29, dogrulayin
Adafruit_BNO055 antenBno = Adafruit_BNO055(56, ANTEN_BNO_ADDR, &Wire);
bool g_antenBnoHazir = false;

volatile float g_araclHeading = 0;              // CAN 0x22'den gelen arac/referans heading'i (derece, 0-360)
volatile unsigned long g_headingSonAlmaZamani = 0;
const unsigned long HEADING_ZAMAN_ASIMI_MS = 500; // bu sureden fazla yeni heading gelmezse anten DURUR

float g_antenHeadingSuan = 0;                    // antenBno'dan okunan, antenin KENDI heading'i (derece, 0-360)
unsigned long g_antenBnoOkumaZamani = 0;
const unsigned long ANTEN_BNO_OKUMA_ARALIGI_MS = 50;

bool antenIleri = false, antenGeri = false;
bool antenEnabledOnceki = false;

// "1-2 derece kayma kabul edilebilir": DURMA esigi bunu, BASLAMA esigi (>DURMA) esik
// civarindaki kucuk salinimlari (histerezis, CH6'daki gibi) onlemeyi saglar.
const float ANTEN_BASLAMA_ESIGI_DERECE = 2.0;
const float ANTEN_DURMA_ESIGI_DERECE   = 1.0;
const int   ANTEN_MIN_FREQ = 50;
const int   ANTEN_MAX_FREQ = 600;   // anten hassas/yavas donmesi yeterli, taretten daha dusuk tutuldu
const bool  ANTEN_YON_TERS = false; // test edip motor beklenenin tersi yone donerse true yapin

// ---- Otonom mod: CAN 0x21 (atis_komutu, Jetson'dan) ile pan/tilt hiz + lazer tetikleme ----
volatile float g_canGx = 0, g_canGy = 0;
volatile uint8_t g_canLazer = 0;
volatile unsigned long g_canSonAlmaZamani = 0;
const unsigned long CAN_ATIS_ZAMAN_ASIMI_MS = 300; // bu sureden fazla yeni 0x21 gelmezse otonom hareket/lazer DURUR

// gx/gy'nin "tam hiz" sayilacagi mutlak deger. Jetson tarafindan gonderilen gercek araligi
// bilmiyoruz -- Serial ciktisindaki gx/gy degerlerini (asagida onRawFrame'de yazdiriliyor)
// gozlemleyip bu iki sabiti kendi sisteminize gore ayarlayin.
const float CAN_GX_MAX_DEGER = 1.0;
const float CAN_GY_MAX_DEGER = 1.0;
const float CAN_DEADBAND = 0.02; // CAN_Gx/GY_MAX_DEGER birimiyle orantili, bu kadarin altinda hareket yok

bfs::SbusRx sbus_rx(&Serial1, SBUS_RX_PIN, SBUS_TX_PIN, true);
bool sbus_failsafe_ = true;
unsigned long sonSbusOkumaZamani_ = 0;

unsigned long sonEstopGonderZamani = 0;
const unsigned long ESTOP_GONDER_ARALIGI_MS = 50;

DroneCAN_ESP32 dronecan;

// Kuresel degiskenler
volatile uint8_t global_uydu = 0, global_fix = 0;
volatile double gercek_enlem = 0.0, gercek_boylam = 0.0;
volatile float gVoltage = 0.0, gCurrent = 0.0, gSOC = 0.0;

uint32_t gpsPaketSayaci = 0;
uint32_t bmsPaketSayaci = 0;
unsigned long sonYazdirma = 0;
unsigned long sonSorguZamani = 0;
const unsigned long SORGU_ARALIGI_MS = 200;

unsigned long sonOzetGonderZamani = 0;
const unsigned long OZET_GONDER_ARALIGI_MS = 100;

uint8_t here4NodeId = 125;

unsigned long baslangicZamani = 0;
const unsigned long BOOT_BEKLEME_MS = 5000;

uint8_t ledOverrideStep = 0;
unsigned long ledStepZamani = 0;
const unsigned long LED_ADIM_ARALIGI_MS = 300;

unsigned long sonRenkGonderZamani = 0;
const unsigned long RENK_GONDER_ARALIGI_MS = 500;

// ---------------- DroneCAN Callback'leri (GPS / here4) ----------------
bool shouldAcceptTransfer(const CanardInstance* ins, uint64_t* signature, uint16_t id, CanardTransferType type, uint8_t node) {
  if (id == 1063) {
    *signature = 0xCA41E7000F37435F;
    return true;
  }
  return false;
}

void onTransferReceived(CanardInstance* ins, CanardRxTransfer* transfer) {
  if (transfer->data_type_id == 1063) {
    int64_t boylam_ham = 0, enlem_ham = 0;
    uint64_t uydu_sayisi_ham = 0, fix_durumu_ham = 0;

    canardDecodeScalar(transfer, 136, 37, true, &boylam_ham);
    canardDecodeScalar(transfer, 173, 37, true, &enlem_ham);
    canardDecodeScalar(transfer, 360, 6, false, &uydu_sayisi_ham);
    canardDecodeScalar(transfer, 366, 2, false, &fix_durumu_ham);

    global_uydu = (uint8_t)uydu_sayisi_ham;
    global_fix  = (uint8_t)fix_durumu_ham;
    gercek_enlem  = enlem_ham  / 100000000.0;
    gercek_boylam = boylam_ham / 100000000.0;

    here4NodeId = transfer->source_node_id;

    gpsPaketSayaci++;
  }
}

// ---------------- Ham CAN frame callback (BMS cevabi + Orin atis komutu icin) ----------------
void onRawFrame(const CanardCANFrame* frame) {
  uint32_t rawId = frame->id & CANARD_CAN_EXT_ID_MASK;

  if (rawId == BMS_REPLY_ID) {
    if (frame->data_len >= 8) {
      uint16_t v_ham = (frame->data[0] << 8) | frame->data[1];
      uint16_t i_ham = (frame->data[4] << 8) | frame->data[5];
      uint16_t s_ham = (frame->data[6] << 8) | frame->data[7];

      gVoltage = v_ham / 10.0;
      gCurrent = (i_ham - 30000) / 10.0;
      gSOC     = s_ham / 10.0;

      bmsPaketSayaci++;
    }
  }

  // ---- atis_komutu (0x21, Jetson'dan): pan/tilt hiz + lazer tetikleme icin gercek kontrol girdisi ----
  if (rawId == 0x21) {
    if (frame->data_len >= 5) {
      int16_t gx, gy;
      memcpy(&gx, &frame->data[0], 2);
      memcpy(&gy, &frame->data[2], 2);
      g_canGx = gx / 10000.0f;
      g_canGy = gy / 10000.0f;
      g_canLazer = frame->data[4];
      g_canSonAlmaZamani = millis();
    }

    // Debug: gelen degerleri gormek icin birakildi, gerekirse kaldirin.
    Serial.print(">>> 0x21 (atis_komutu) gx="); Serial.print(g_canGx, 4);
    Serial.print(" gy="); Serial.print(g_canGy, 4);
    Serial.print(" lazer="); Serial.println(g_canLazer);
  }

  // ---- heading (0x22): anten otonom takibi icin arac/referans heading'i ----
  if (rawId == HEADING_CAN_ID) {
    if (frame->data_len >= 2) {
      int16_t heading_ham;
      memcpy(&heading_ham, &frame->data[0], 2);
      float h = heading_ham / 10.0f;
      while (h < 0.0f)    h += 360.0f;
      while (h >= 360.0f) h -= 360.0f;
      g_araclHeading = h;
      g_headingSonAlmaZamani = millis();
    }
  }
}

// ---------------- BMS'e sorgu gonderen fonksiyon ----------------
void bmsSorgusuGonder() {
  CANMessage soru_paketi;
  soru_paketi.id = BMS_QUERY_ID;
  soru_paketi.ext = true;
  soru_paketi.rtr = false;
  soru_paketi.len = 8;
  for (int i = 0; i < 8; i++) soru_paketi.data[i] = 0;

  ACAN_ESP32::can.tryToSend(soru_paketi);
}

// ---------------- Jetson'a (can_bridge.cpp) GPS+BMS ozet frame'leri ----------------
void jetsonOzetGonder() {
  int32_t enlem_i32  = (int32_t)(gercek_enlem  * 1e7);
  int32_t boylam_i32 = (int32_t)(gercek_boylam * 1e7);

  CANMessage pos_paketi;
  pos_paketi.id = GPS_POS_ID;
  pos_paketi.ext = false;
  pos_paketi.rtr = false;
  pos_paketi.len = 8;
  memcpy(&pos_paketi.data[0], &enlem_i32, 4);
  memcpy(&pos_paketi.data[4], &boylam_i32, 4);
  ACAN_ESP32::can.tryToSend(pos_paketi);

  CANMessage status_paketi;
  status_paketi.id = GPS_STATUS_ID;
  status_paketi.ext = false;
  status_paketi.rtr = false;
  status_paketi.len = 2;
  status_paketi.data[0] = 0;
  status_paketi.data[1] = global_fix;
  ACAN_ESP32::can.tryToSend(status_paketi);

  uint16_t voltaj_u16 = (uint16_t)(gVoltage * 10.0);
  int16_t  akim_i16   = (int16_t)(gCurrent * 10.0);
  uint16_t soc_u16    = (uint16_t)(gSOC * 10.0);

  CANMessage bms_paketi;
  bms_paketi.id = BMS_SUMMARY_ID;
  bms_paketi.ext = false;
  bms_paketi.rtr = false;
  bms_paketi.len = 6;
  memcpy(&bms_paketi.data[0], &voltaj_u16, 2);
  memcpy(&bms_paketi.data[2], &akim_i16, 2);
  memcpy(&bms_paketi.data[4], &soc_u16, 2);
  ACAN_ESP32::can.tryToSend(bms_paketi);
}

// ---------------- Here4 parametre yazma (NTF_LED_OVERRIDE) ----------------
void here4ParamYaz(uint8_t node_id, int64_t deger) {
  struct uavcan_protocol_param_GetSetRequest req;
  memset(&req, 0, sizeof(req));

  const char* pname = "NTF_LED_OVERRIDE";
  req.name.len = strlen(pname);
  memcpy(req.name.data, pname, req.name.len);

  req.value.union_tag = UAVCAN_PROTOCOL_PARAM_VALUE_INTEGER_VALUE;
  req.value.integer_value = deger;

  uint8_t buffer[UAVCAN_PROTOCOL_PARAM_GETSET_REQUEST_MAX_SIZE];
  uint32_t len = uavcan_protocol_param_GetSetRequest_encode(&req, buffer);

  dronecan.requestOrRespond(node_id,
                            UAVCAN_PROTOCOL_PARAM_GETSET_REQUEST_SIGNATURE,
                            UAVCAN_PROTOCOL_PARAM_GETSET_REQUEST_ID,
                            CANARD_TRANSFER_PRIORITY_LOW,
                            CanardRequest,
                            buffer, (uint16_t)len);

  Serial.print("NTF_LED_OVERRIDE -> ");
  Serial.print((long)deger);
  Serial.print(" gonderildi (node ");
  Serial.print(node_id);
  Serial.println(")");
}

// ---------------- E-STOP (kanal 5) + OTONOM/TARET/MANUEL (kanal 10, 3 pozisyon) -- TEK frame'de (0x503) ----------------
// Bit alanlari:
//   bit0 = genel estop
//   bit1 = (kullanilmiyor)
//   bit2 = sbus switch (kanal 5)
//   bit3 = sbus zaman asimi/failsafe
//   bit4 = otonom mod aktif (kanal 10 == +100)
//   bit5 = taret modu aktif (kanal 10 == 0)
//   bit6-7 = bos
// Su anki durumu hesaplayip global bayraklara yazar. Bu fonksiyon CAN gonderiminden
// AYRI ve her loop() turunda (yani mumkun olan en kisa surede) cagrilir -- kumanda/SBUS
// giderse motor/role kontrolleri g_estopAktif'i bir sonraki loop turunde gorur, 50ms'lik
// CAN gonderim periyoduna bagli kalmaz.
bool g_sbusSwitchAktif = false;
bool g_otonomAktif = false;
bool g_taretBildir = false;

void estopDurumHesapla() {
  bool sbusSwitchAktif = false;
  bool otonomAktif = false;
  bool taretBildir = false;

  if (!sbus_failsafe_) {
    int kanalDegeri = sbus_rx.data().ch[SBUS_ESTOP_KANAL - 1];
    sbusSwitchAktif = kanalDegeri > SBUS_ESTOP_ESIGI;

    int ch10 = sbus_rx.data().ch[OTONOM_MOD_KANAL - 1];
    otonomAktif = ch10 > OTONOM_ESIK_YUKSEK;                                   // +100
    taretBildir = (ch10 >= OTONOM_ESIK_DUSUK) && (ch10 <= OTONOM_ESIK_YUKSEK); // 0
    // ch10 < OTONOM_ESIK_DUSUK ise -> MANUEL SURUS (-100), ikisi de false kalir
  }

  bool sbusZamanAsimi = (millis() - sonSbusOkumaZamani_) > SBUS_ZAMAN_ASIMI_MS;
  bool estopAktif = sbusSwitchAktif || sbus_failsafe_ || sbusZamanAsimi;

  g_estopAktif       = estopAktif;
  g_sbusSwitchAktif  = sbusSwitchAktif;
  g_otonomAktif      = otonomAktif;
  g_taretBildir      = taretBildir;

  static bool oncekiEstopAktif = false;
  if (estopAktif != oncekiEstopAktif) {
    oncekiEstopAktif = estopAktif;
    Serial.print("!!! ESTOP durumu degisti -> ");
    Serial.print(estopAktif ? "AKTIF" : "PASIF");
    Serial.print("  (sbusSwitch:"); Serial.print(sbusSwitchAktif);
    Serial.print(" sbusKopuk:"); Serial.print(sbus_failsafe_ || sbusZamanAsimi);
    Serial.println(")");
  }

  static bool oncekiOtonomAktif = false;
  if (otonomAktif != oncekiOtonomAktif) {
    oncekiOtonomAktif = otonomAktif;
    Serial.print("!!! Otonom mod degisti -> ");
    Serial.println(otonomAktif ? "AKTIF" : "PASIF");
  }
}

// Az once hesaplanan durumu CAN'a yayinlar. Sadece bu kisim 50ms'de bir (20Hz) calisir --
// motor/role kontrolleri artik buna bagli DEGIL, sadece Jetson'a bilgi vermek icin.
void estopDurumGonder() {
  uint8_t durumByte = 0;
  if (g_estopAktif)      durumByte |= 0x01;
  if (g_sbusSwitchAktif) durumByte |= 0x04;
  if (sbus_failsafe_ || (millis() - sonSbusOkumaZamani_) > SBUS_ZAMAN_ASIMI_MS) durumByte |= 0x08;
  if (g_otonomAktif)     durumByte |= 0x10;
  if (g_taretBildir)     durumByte |= 0x20;

  CANMessage estop_paketi;
  estop_paketi.id  = ESTOP_ID;
  estop_paketi.ext = false;
  estop_paketi.rtr = false;
  estop_paketi.len = 1;
  estop_paketi.data[0] = durumByte;
  ACAN_ESP32::can.tryToSend(estop_paketi);
}

// ---------------- Here4'e LightsCommand ile renk gonderme ----------------
void here4RenkGonder(uint8_t red5, uint8_t green6, uint8_t blue5) {
  struct uavcan_equipment_indication_LightsCommand msg;
  memset(&msg, 0, sizeof(msg));

  msg.commands.len = 1;
  msg.commands.data[0].light_id = 0;
  msg.commands.data[0].color.red   = red5;
  msg.commands.data[0].color.green = green6;
  msg.commands.data[0].color.blue  = blue5;

  uint8_t buffer[UAVCAN_EQUIPMENT_INDICATION_LIGHTSCOMMAND_MAX_SIZE];
  uint32_t len = uavcan_equipment_indication_LightsCommand_encode(&msg, buffer);

  dronecan.broadcast(UAVCAN_EQUIPMENT_INDICATION_LIGHTSCOMMAND_SIGNATURE,
                      UAVCAN_EQUIPMENT_INDICATION_LIGHTSCOMMAND_ID,
                      CANARD_TRANSFER_PRIORITY_LOW,
                      buffer, (uint16_t)len);
}

// ---------------- LED yonetimi (boot bekleme + override + dinamik renk) ----------------
void here4LedYonetimi() {
  unsigned long simdi = millis();

  if (simdi - baslangicZamani < BOOT_BEKLEME_MS) return;

  if (ledOverrideStep < 2) {
    if (simdi - ledStepZamani < LED_ADIM_ARALIGI_MS) return;

    if (ledOverrideStep == 0) {
      here4ParamYaz(here4NodeId, 0);
      ledOverrideStep = 1;
    } else if (ledOverrideStep == 1) {
      here4ParamYaz(here4NodeId, 1);
      ledOverrideStep = 2;
      Serial.println("Here4 LED override aktif. Artik fix/uydu sayisina gore renk gonderilecek.");
    }
    ledStepZamani = simdi;
    return;
  }

  if (simdi - sonRenkGonderZamani < RENK_GONDER_ARALIGI_MS) return;
  sonRenkGonderZamani = simdi;

  if (global_fix == 0 && global_uydu == 0) {
    return;
  }

  if (global_fix == 3 && global_uydu < 15) {
    here4RenkGonder(31, 63, 0); // SARI
  }
  else if (global_fix == 2 && global_uydu <= 15) {
    here4RenkGonder(0, 0, 63);  // Mavi
  }
  else if (global_fix == 3 && global_uydu >= 15) {
    here4RenkGonder(63, 10, 0);  // YESIL
  }
}

// ---------------- Tek eksen icin histerezisli step surme (calisan CH6 step motor kodundaki yontem) ----------------
// ileri/geri bayraklari kalici durum tutar: hareket BASLAMA_OFSET esiginde baslar,
// ancak stick merkeze DURMA_OFSET esigine gelene kadar durmaz. DURMA_OFSET < BASLAMA_OFSET
// oldugu icin esik civarindaki kucuk SBUS titremeleri motoru surekli baslatip durdurmaz.
// oran (0..1) -> frekans: oran^TARET_EGRI_KUVVETI ile hesaplanir. KUVVET>1 oldugunda
// dusuk oranlarda (merkeze yakinken) frekans dogrusala gore çok daha yavas artar,
// oran 1'e (uca) yaklastikca hizla MAX_FREQ'e ulasir.
int taretFrekansHesapla(int chRaw, int durmaEsigi, int ucDegeri) {
  float oran = (float)(chRaw - durmaEsigi) / (float)(ucDegeri - durmaEsigi);
  oran = constrain(oran, 0.0f, 1.0f);
  float egrili = pow(oran, TARET_EGRI_KUVVETI);
  int frek = TARET_MIN_FREQ + (int)(egrili * (TARET_MAX_FREQ - TARET_MIN_FREQ));
  return constrain(frek, TARET_MIN_FREQ, TARET_MAX_FREQ);
}

void taretEksenGuncelle(int chRaw, bool &ileri, bool &geri, int dirPin, int stepPin, bool yonTersCevir = false) {
  int baslamaYuksek = TARET_MERKEZ + TARET_BASLAMA_OFSET;
  int durmaYuksek   = TARET_MERKEZ + TARET_DURMA_OFSET;
  int baslamaDusuk  = TARET_MERKEZ - TARET_BASLAMA_OFSET;
  int durmaDusuk    = TARET_MERKEZ - TARET_DURMA_OFSET;

  if (!ileri && !geri) {
    if (chRaw > baslamaYuksek)      ileri = true;
    else if (chRaw < baslamaDusuk)  geri  = true;
  } else if (ileri) {
    if (chRaw < durmaYuksek) ileri = false;
  } else if (geri) {
    if (chRaw > durmaDusuk) geri = false;
  }

  if (ileri) {
    digitalWrite(dirPin, yonTersCevir ? LOW : HIGH);
    int frek = taretFrekansHesapla(chRaw, durmaYuksek, TARET_MERKEZ + TARET_MAX_SAPMA);
    ledcChangeFrequency(stepPin, frek, STEP_PWM_RES);
    ledcWrite(stepPin, STEP_DUTY);
  } else if (geri) {
    digitalWrite(dirPin, yonTersCevir ? HIGH : LOW);
    int frek = taretFrekansHesapla(chRaw, durmaDusuk, TARET_MERKEZ - TARET_MAX_SAPMA);
    ledcChangeFrequency(stepPin, frek, STEP_PWM_RES);
    ledcWrite(stepPin, STEP_DUTY);
  } else {
    ledcWrite(stepPin, 0);
  }
}

// ---------------- Otonom eksen surme (CAN 0x21'den gelen gx/gy, normalize/isaretli deger) ----------------
int otonomFrekansHesapla(float deger, float maxDeger) {
  float oran = constrain(fabs(deger) / maxDeger, 0.0f, 1.0f);
  float egrili = pow(oran, TARET_EGRI_KUVVETI);
  int frek = TARET_MIN_FREQ + (int)(egrili * (TARET_MAX_FREQ - TARET_MIN_FREQ));
  return constrain(frek, TARET_MIN_FREQ, TARET_MAX_FREQ);
}

void otonomEksenGuncelle(float deger, float maxDeger, int dirPin, int stepPin, bool yonTersCevir) {
  if (fabs(deger) <= CAN_DEADBAND) {
    ledcWrite(stepPin, 0);
    return;
  }
  bool ileriYon = deger > 0;
  digitalWrite(dirPin, (ileriYon != yonTersCevir) ? HIGH : LOW);
  int frek = otonomFrekansHesapla(deger, maxDeger);
  ledcChangeFrequency(stepPin, frek, STEP_PWM_RES);
  ledcWrite(stepPin, STEP_DUTY);
}

// CAN 0x21 (atis_komutu) ile pan/tilt + lazer. CAN veri zaman asimina ugrarsa VEYA
// e-stop aktifse hem motorlar hem lazer guvenlik icin durdurulur.
void otonomTaretYonetimi() {
  bool canZamanAsimi = (millis() - g_canSonAlmaZamani) > CAN_ATIS_ZAMAN_ASIMI_MS;

  if (g_estopAktif || canZamanAsimi) {
    ledcWrite(STEP2_STEP_PIN, 0);
    ledcWrite(STEP3_STEP_PIN, 0);
    digitalWrite(RELE_PIN, HIGH); // TERS mantik: HIGH = lazer PASIF
    return;
  }

  otonomEksenGuncelle(g_canGx, CAN_GX_MAX_DEGER, STEP2_DIR_PIN, STEP2_STEP_PIN, false); // pan
  otonomEksenGuncelle(g_canGy, CAN_GY_MAX_DEGER, STEP3_DIR_PIN, STEP3_STEP_PIN, true);  // tilt (fiziksel yon ters)

  bool lazerAktif = (g_canLazer != 0);
  digitalWrite(RELE_PIN, lazerAktif ? LOW : HIGH); // TERS mantik: LOW = lazer AKTIF
}

// ---------------- Taret/otonom kontrol dagitici (CH10: -100=OTONOM, 0=TARET, +100=MANUEL SURUS) ----------------
// Pan/tilt motorlarini ve ENA hattini iki farkli kontrol kaynagindan (SBUS stick veya CAN
// atis_komutu) besler; hangisinin aktif oldugunu CH10 belirler. E-stop aktifken (SBUS kopuk,
// failsafe veya CH5 switch) her iki kaynak da motorlari ve lazeri devre disi birakir.
void taretKontrolGuncelle() {
  int ch10 = sbus_rx.data().ch[OTONOM_MOD_KANAL - 1];
  bool otonomAktif        = (ch10 > OTONOM_ESIK_YUKSEK);
  bool manuelTaretAktif   = (ch10 >= OTONOM_ESIK_DUSUK) && (ch10 <= OTONOM_ESIK_YUKSEK);
  bool manuelSurusModu    = !otonomAktif && !manuelTaretAktif; // CH10 < OTONOM_ESIK_DUSUK
  taretModuAktif = otonomAktif || manuelTaretAktif; // sadece bilgi/log amacli

  // ENA'yi e-stop DISINDA HER ZAMAN acik tutuyoruz -- boylece step motorlar hangi
  // moddaysa olsun (surus modu dahil) holding torque ile son pozisyonunda kilitli
  // kalir, arac sarsintisi/egimi taretin oynamasina sebep olmaz. Sadece e-stop
  // motorlari gercekten enerjisiz birakir.
  bool enaGerekli = !g_estopAktif;
  if (enaGerekli != taretEnabledOnceki) {
    taretEnabledOnceki = enaGerekli;
    digitalWrite(ENA_PIN_SHARED, enaGerekli ? LOW : HIGH); // TB6600: LOW=enable
    Serial.println(enaGerekli ? "TARET MOTORLARI: KILITLI (enerjili)" : "TARET MOTORLARI: SERBEST (enerjisiz)");
  }

  if (!enaGerekli) {
    ledcWrite(STEP2_STEP_PIN, 0);
    ledcWrite(STEP3_STEP_PIN, 0);
    digitalWrite(RELE_PIN, HIGH); // guvenlik: lazer PASIF
    panIleri = panGeri = false;
    tiltIleri = tiltGeri = false;
    return;
  }

  if (otonomAktif) {
    otonomTaretYonetimi();
  } else if (manuelTaretAktif) {
    taretEksenGuncelle(sbus_rx.data().ch[0], panIleri, panGeri, STEP2_DIR_PIN, STEP2_STEP_PIN);            // CH1 -> pan
    taretEksenGuncelle(sbus_rx.data().ch[1], tiltIleri, tiltGeri, STEP3_DIR_PIN, STEP3_STEP_PIN, true);    // CH2 -> tilt (yon ters)
  } else {
    // MANUEL SURUS modu: hareket komutu YOK, motorlar enerjili kalip son pozisyonda kilitli durur.
    ledcWrite(STEP2_STEP_PIN, 0);
    ledcWrite(STEP3_STEP_PIN, 0);
    panIleri = panGeri = false;
    tiltIleri = tiltGeri = false;
  }
}

// ---------------- Anten BNO055 okuma (rate-limited) ----------------
void antenBnoOku() {
  if (!g_antenBnoHazir) return;
  unsigned long simdi = millis();
  if (simdi - g_antenBnoOkumaZamani < ANTEN_BNO_OKUMA_ARALIGI_MS) return;
  g_antenBnoOkumaZamani = simdi;

  sensors_event_t event;
  antenBno.getEvent(&event);
  g_antenHeadingSuan = event.orientation.x; // 0-360, 0=Kuzey (manyetik)
}

// hedef - suanki farkini en kisa yon uzerinden [-180, +180] araligina normalize eder
float antenAciFarkiHesapla(float hedef, float suanki) {
  float fark = hedef - suanki;
  while (fark > 180.0f)  fark -= 360.0f;
  while (fark < -180.0f) fark += 360.0f;
  return fark;
}

int antenFrekansHesapla(float mutlakFark) {
  float oran = constrain((mutlakFark - ANTEN_DURMA_ESIGI_DERECE) / (180.0f - ANTEN_DURMA_ESIGI_DERECE), 0.0f, 1.0f);
  int frek = ANTEN_MIN_FREQ + (int)(oran * (ANTEN_MAX_FREQ - ANTEN_MIN_FREQ));
  return constrain(frek, ANTEN_MIN_FREQ, ANTEN_MAX_FREQ);
}

// OTONOM modda calisir: hedef = (arac heading + 180) mod 360. BNO055'ten okunan GERCEK anten
// heading'iyle kiyaslar, CH6'daki gibi histerezisli (baslama>durma) duzeltme yapar.
void antenOtonomTakipYap() {
  bool headingZamanAsimi = (millis() - g_headingSonAlmaZamani) > HEADING_ZAMAN_ASIMI_MS;

  if (!g_antenBnoHazir || headingZamanAsimi) {
    ledcWrite(MANUEL_STEP_PIN, 0);
    antenIleri = antenGeri = false;
    return;
  }

  float hedefHeading = g_araclHeading + 180.0f;
  while (hedefHeading >= 360.0f) hedefHeading -= 360.0f;

  float fark = antenAciFarkiHesapla(hedefHeading, g_antenHeadingSuan);
  float mutlakFark = fabs(fark);

  if (!antenIleri && !antenGeri) {
    if (mutlakFark > ANTEN_BASLAMA_ESIGI_DERECE) {
      if (fark > 0) antenIleri = true; else antenGeri = true;
    }
  } else if (antenIleri) {
    if (mutlakFark < ANTEN_DURMA_ESIGI_DERECE) antenIleri = false;
  } else if (antenGeri) {
    if (mutlakFark < ANTEN_DURMA_ESIGI_DERECE) antenGeri = false;
  }

  if (antenIleri) {
    digitalWrite(MANUEL_DIR_PIN, ANTEN_YON_TERS ? LOW : HIGH);
    int frek = antenFrekansHesapla(mutlakFark);
    ledcChangeFrequency(MANUEL_STEP_PIN, frek, STEP_PWM_RES);
    ledcWrite(MANUEL_STEP_PIN, STEP_DUTY);
  } else if (antenGeri) {
    digitalWrite(MANUEL_DIR_PIN, ANTEN_YON_TERS ? HIGH : LOW);
    int frek = antenFrekansHesapla(mutlakFark);
    ledcChangeFrequency(MANUEL_STEP_PIN, frek, STEP_PWM_RES);
    ledcWrite(MANUEL_STEP_PIN, STEP_DUTY);
  } else {
    ledcWrite(MANUEL_STEP_PIN, 0); // hedefe yeterince yakin -- ENA acik oldugu icin bu pozisyonda kilitli kalir
  }
}

// ---------------- Anten step kontrol dagitici (pin 32/33/25) ----------------
// OTONOM modda BNO055 heading takibi (antenOtonomTakipYap), diger modlarda (TARET/MANUEL SURUS)
// eski CH6 SBUS manuel kontrol. ENA, taret motorlariyla ayni mantikla e-stop disinda HER ZAMAN
// acik tutuluyor -- boylece anten duzeltme yapmadigi anlarda da holding torque ile kilitli kalir.
void antenStepYonetimi() {
  bool enaGerekli = !g_estopAktif;
  if (enaGerekli != antenEnabledOnceki) {
    antenEnabledOnceki = enaGerekli;
    digitalWrite(MANUEL_EN_PIN, enaGerekli ? LOW : HIGH); // TB6600: LOW=enable
    Serial.println(enaGerekli ? "ANTEN MOTORU: KILITLI (enerjili)" : "ANTEN MOTORU: SERBEST (enerjisiz)");
  }

  if (!enaGerekli) {
    ledcWrite(MANUEL_STEP_PIN, 0);
    antenIleri = antenGeri = false;
    manuelStepIleri = manuelStepGeri = false;
    return;
  }

  if (g_otonomAktif) {
    antenOtonomTakipYap();
    return;
  }

  // OTONOM disinda (TARET veya MANUEL SURUS): eski CH6 manuel kontrol aynen devam ediyor.
  antenIleri = antenGeri = false; // otonom bayraklarini temizle, moddan cikinca temiz baslasin
  int ch6Raw = sbus_rx.data().ch[MANUEL_STEP_KANAL - 1];

  if (!manuelStepIleri && !manuelStepGeri) {
    if (ch6Raw > MANUEL_STEP_START_HIGH) manuelStepIleri = true;
    else if (ch6Raw < MANUEL_STEP_START_LOW) manuelStepGeri = true;
  } else if (manuelStepIleri) {
    if (ch6Raw < MANUEL_STEP_STOP_HIGH) manuelStepIleri = false;
  } else if (manuelStepGeri) {
    if (ch6Raw > MANUEL_STEP_STOP_LOW) manuelStepGeri = false;
  }

  if (manuelStepIleri) {
    digitalWrite(MANUEL_DIR_PIN, HIGH);
    int frek = map(ch6Raw, MANUEL_STEP_STOP_HIGH, 1800, MANUEL_STEP_MIN_FREQ, MANUEL_STEP_MAX_FREQ);
    frek = constrain(frek, MANUEL_STEP_MIN_FREQ, MANUEL_STEP_MAX_FREQ);
    ledcChangeFrequency(MANUEL_STEP_PIN, frek, STEP_PWM_RES);
    ledcWrite(MANUEL_STEP_PIN, STEP_DUTY);
  } else if (manuelStepGeri) {
    digitalWrite(MANUEL_DIR_PIN, LOW);
    int frek = map(ch6Raw, MANUEL_STEP_STOP_LOW, 200, MANUEL_STEP_MIN_FREQ, MANUEL_STEP_MAX_FREQ);
    frek = constrain(frek, MANUEL_STEP_MIN_FREQ, MANUEL_STEP_MAX_FREQ);
    ledcChangeFrequency(MANUEL_STEP_PIN, frek, STEP_PWM_RES);
    ledcWrite(MANUEL_STEP_PIN, STEP_DUTY);
  } else {
    ledcWrite(MANUEL_STEP_PIN, 0);
  }
}

// ---------------- Role yonetimi (CH9 ile, TERS mantik: aktif = LOW) ----------------
// SADECE manuel taret modunda calisir -- otonom modda role/lazer'i otonomTaretYonetimi() yonetir.
void releYonetimi() {
  int ch10 = sbus_rx.data().ch[OTONOM_MOD_KANAL - 1];
  bool otonomAktif = (ch10 > OTONOM_ESIK_YUKSEK);
  if (otonomAktif) return;

  bool releAktif = false;
  if (!sbus_failsafe_ && !g_estopAktif) {
    int ch9 = sbus_rx.data().ch[RELE_KANAL - 1];
    releAktif = ch9 > RELE_ESIGI;
  }

  static bool oncekiReleAktif = false;
  digitalWrite(RELE_PIN, releAktif ? LOW : HIGH); // TERS: aktif -> LOW (GND'ye cekili)

  if (releAktif != oncekiReleAktif) {
    oncekiReleAktif = releAktif;
    Serial.print("ROLE durumu degisti -> ");
    Serial.println(releAktif ? "AKTIF (LOW)" : "PASIF (HIGH)");
  }
}

// ---------------- Setup ----------------
void setup() {
  Serial.begin(115200);
  delay(300);

  dronecan.setAcceptCallback(shouldAcceptTransfer);
  dronecan.setReceiveCallback(onTransferReceived);
  dronecan.setRawFrameCallback(onRawFrame);

  if (!dronecan.begin(BITRATE, NODE_ID)) {
    Serial.println("DroneCAN/CAN baslatilamadi! Pin/baglanti/bitrate kontrol edin.");
    while (1) delay(1000);
  }
  sbus_rx.Begin();

  // ---- Taret step motorlari kurulumu ----
  pinMode(STEP2_DIR_PIN, OUTPUT);
  pinMode(STEP3_DIR_PIN, OUTPUT);
  pinMode(ENA_PIN_SHARED, OUTPUT);
  digitalWrite(ENA_PIN_SHARED, HIGH); // baslangicta devre disi

  ledcAttach(STEP2_STEP_PIN, STEP_PWM_FREQ, STEP_PWM_RES);
  ledcAttach(STEP3_STEP_PIN, STEP_PWM_FREQ, STEP_PWM_RES);
  ledcWrite(STEP2_STEP_PIN, 0);
  ledcWrite(STEP3_STEP_PIN, 0);

  // ---- Manuel/Anten step motor kurulumu ----
  pinMode(MANUEL_DIR_PIN, OUTPUT);
  pinMode(MANUEL_EN_PIN, OUTPUT);
  digitalWrite(MANUEL_EN_PIN, HIGH); // baslangicta devre disi
  ledcAttach(MANUEL_STEP_PIN, STEP_PWM_FREQ, STEP_PWM_RES);
  ledcWrite(MANUEL_STEP_PIN, 0);

  // ---- Anten BNO055 kurulumu (basarisiz olursa sistemi durdurmuyoruz, sadece otonom
  // heading takibi devre disi kalir -- CH6 manuel kontrol yine calisir) ----
  Wire.begin(ANTEN_BNO_SDA_PIN, ANTEN_BNO_SCL_PIN);
  if (!antenBno.begin()) {
    Serial.println("UYARI: Anten BNO055 bulunamadi! Otonom modda heading takibi CALISMAYACAK.");
    g_antenBnoHazir = false;
  } else {
    delay(200);
    antenBno.setExtCrystalUse(true);
    g_antenBnoHazir = true;
    Serial.println("Anten BNO055 hazir.");
  }

  // ---- Role kurulumu (guvenlik: baslangicta PASIF/HIGH) ----
  pinMode(RELE_PIN, OUTPUT);
  digitalWrite(RELE_PIN, HIGH);

  baslangicZamani = millis();
  ledStepZamani = millis();
  Serial.println("Kurulum tamam. Dinleniyor...");
}

// ---------------- Loop ----------------
void loop() {
  dronecan.update();

  if (sbus_rx.Read()) {
    sonSbusOkumaZamani_ = millis();
    sbus_failsafe_ = sbus_rx.data().failsafe;
  }

  if (millis() - sonSorguZamani >= SORGU_ARALIGI_MS) {
    sonSorguZamani = millis();
    bmsSorgusuGonder();
  }

  if (millis() - sonOzetGonderZamani >= OZET_GONDER_ARALIGI_MS) {
    sonOzetGonderZamani = millis();
    jetsonOzetGonder();
  }

  // Guvenlik durumu HER loop turunda hesaplanir (kumanda/SBUS kaybina en hizli tepki icin).
  estopDurumHesapla();

  // CAN'a bildirim ise 50ms'de bir (20Hz) -- bu sadece Jetson'a bilgi, motor/role
  // kontrolleri zaten guncel g_estopAktif'i her turda kullaniyor.
  if (millis() - sonEstopGonderZamani >= ESTOP_GONDER_ARALIGI_MS) {
    sonEstopGonderZamani = millis();
    estopDurumGonder();
  }

  here4LedYonetimi();

  antenBnoOku();

  taretKontrolGuncelle();
  releYonetimi();
  antenStepYonetimi();

  if (millis() - sonYazdirma > 500) {
    sonYazdirma = millis();
    durumRaporuYazdir();
  }
}

// ---------------- Tum sistem durumunu tek yerde Seriale basar (mod, estop, CAN, GPS, BMS, cikislar) ----------------
void durumRaporuYazdir() {
  const char* modAdi = g_otonomAktif ? "OTONOM" : (g_taretBildir ? "TARET" : "MANUEL SURUS");

  Serial.println("======= DURUM RAPORU =======");

  Serial.printf("MOD -> %s  (CH10:%d)\n", modAdi, sbus_rx.data().ch[OTONOM_MOD_KANAL - 1]);

  Serial.printf("ESTOP -> %s  (sbusSwitch:%d  sbusFailsafe:%d  sbusZamanAsimi:%d  son_paket_once_ms:%lu)\n",
                g_estopAktif ? "AKTIF" : "PASIF",
                g_sbusSwitchAktif, sbus_failsafe_,
                (millis() - sonSbusOkumaZamani_) > SBUS_ZAMAN_ASIMI_MS,
                millis() - sonSbusOkumaZamani_);

  Serial.printf("SBUS -> CH1(pan):%d  CH2(tilt):%d  CH6(manuelStep):%d  CH9(role):%d  CH10:%d\n",
                sbus_rx.data().ch[0], sbus_rx.data().ch[1],
                sbus_rx.data().ch[MANUEL_STEP_KANAL - 1],
                sbus_rx.data().ch[RELE_KANAL - 1],
                sbus_rx.data().ch[OTONOM_MOD_KANAL - 1]);

  bool canZamanAsimi = (millis() - g_canSonAlmaZamani) > CAN_ATIS_ZAMAN_ASIMI_MS;
  Serial.printf("CAN atis_komutu -> gx:%.4f  gy:%.4f  lazer:%d  zamanAsimi:%d  son_alis_once_ms:%lu\n",
                g_canGx, g_canGy, g_canLazer, canZamanAsimi, millis() - g_canSonAlmaZamani);

  Serial.printf("CIKISLAR -> taretENA:%s  role/lazer:%s  antenENA:%s (otonomIleri:%d otonomGeri:%d manuelIleri:%d manuelGeri:%d)\n",
                taretEnabledOnceki ? "KILITLI" : "SERBEST",
                (digitalRead(RELE_PIN) == LOW) ? "AKTIF" : "PASIF",
                antenEnabledOnceki ? "KILITLI" : "SERBEST",
                antenIleri, antenGeri, manuelStepIleri, manuelStepGeri);

  float hedefAntenHeading = g_araclHeading + 180.0f;
  while (hedefAntenHeading >= 360.0f) hedefAntenHeading -= 360.0f;
  bool headingZamanAsimi = (millis() - g_headingSonAlmaZamani) > HEADING_ZAMAN_ASIMI_MS;

  Serial.printf("ANTEN -> bnoHazir:%d  aracHeading:%.1f  hedef:%.1f  antenSuanki:%.1f  fark:%.1f  headingZamanAsimi:%d\n",
                g_antenBnoHazir, g_araclHeading, hedefAntenHeading, g_antenHeadingSuan,
                antenAciFarkiHesapla(hedefAntenHeading, g_antenHeadingSuan), headingZamanAsimi);

  Serial.printf("GPS -> uydu:%d fix:%d enlem:%.7f boylam:%.7f (paket:%lu) node:%d\n",
                global_uydu, global_fix, gercek_enlem, gercek_boylam, gpsPaketSayaci, here4NodeId);

  Serial.printf("BMS -> V:%.1f  I:%.1f  SOC:%.1f%%  (paket:%lu)\n",
                gVoltage, gCurrent, gSOC, bmsPaketSayaci);

  Serial.println("=============================");
}
