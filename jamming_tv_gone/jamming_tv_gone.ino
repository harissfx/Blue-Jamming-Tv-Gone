/******************************************************************
 * ESP32 Combo: TV B Gone + 2.4GHz Jammer
 * ----------------------------------------------------
 * 
 * Kode ini merupakan kombinasi dari proyek TV B Gone dan Bluetooth/WiFi Jammer
 * menggunakan ESP32 sebagai kontroler utama.
 * 
 * Referensi:
 * - TV B Gone oleh Mitch Altman (dimodifikasi untuk ESP32)
 * - NRF24 Jammer oleh Smoochiee
 * 
 * Fitur:
 * 1. Mode JAMMING - Menggunakan dua modul NRF24L01 untuk mengirimkan carrier wave
 *    di band 2.4GHz, mengganggu komunikasi Bluetooth dan WiFi di sekitarnya.
 *    - Menggunakan dua modul NRF24L01 pada HSPI dan VSPI ESP32
 *    - Channel hopping otomatis untuk meningkatkan efektivitas
 *    - Tombol toggle untuk mengaktifkan/menonaktifkan serangan
 * 
 * 2. Mode IR SEND - Mengirimkan kode inframerah POWER untuk mematikan dan menyalakan berbagai
 *    perangkat TV dan STB, mirip dengan fungsi TV B Gone.
 *    - Database kode inframerah untuk TV Amerika Utara dan Eropa
 *    - Pengiriman kode inframerah bertahap dengan indikator LED
 *    - Bisa dihentikan setiap saat dengan tombol toggle
 * 
 * Kontrol:
 * - BOOT_PIN (GPIO0): Tombol untuk toggle antara mode JAMMING dan IR SEND
 * - TOGGLE_PIN (GPIO33): Tombol untuk mengaktifkan/menonaktifkan fitur yang dipilih
 * - LED_BUILTIN (GPIO2): Indikator status (nyala terus = jamming aktif, berkedip cepat = IR send)
 * 
 * Hardware yang dibutuhkan:
 * - ESP32 development board
 * - 2x modul NRF24L01+
 * - 2x kapasitor  10µF atau 100µF
 * - IR LED
 * - Tombol untuk GPIO0 dan GPIO33
 * - Power supply yang stabil (rekomendasi: baterai dengan regulator)
 * 
 * CATATAN PENGGUNAAN:
 * Kode ini hanya untuk tujuan pendidikan dan eksperimental.
 * Penggunaan jammer dapat melanggar regulasi telekomunikasi di berbagai negara.
 * Pengguna bertanggung jawab penuh atas penggunaan kode ini.
 ******************************************************************/


#include "RF24.h"
#include <SPI.h>
#include <ezButton.h>
#include "esp_bt.h"
#include "esp_wifi.h"
#include <IRsend.h>
#include "tvbgone_codes.h"

// === PIN DEFINITIONS ===
#define LED_BUILTIN 2  
#define TOGGLE_PIN 33  
#define BOOT_PIN 0     
const uint16_t kIrLed = 25; 

// === RADIO CONFIGURATION ===
SPIClass *sp = nullptr;
SPIClass *hp = nullptr;
RF24 radio(16, 15, 16000000);  
RF24 radio1(22, 21, 16000000);  

// === GLOBAL VARIABLES ===
unsigned int flag = 0;
unsigned int flagv = 0;
int ch = 45;
int ch1 = 45;

// === MODE CONFIGURATION ===
enum Mode {
  JAMMING,
  IR_SEND
};
Mode currentMode = JAMMING;
bool jammingActive = false;
unsigned long previousMillis = 0;
const long blinkInterval = 500;
bool ledState = LOW;
unsigned long lastToggleMillis = 0;
const long toggleDebounceTime = 50;

// === IR CONFIGURATION ===
IRsend irsend(kIrLed);
bool irActive = false;
uint8_t irIndex = 0;
unsigned long lastIrMillis = 0;
const IrCode* const* powerCodes;
uint8_t numCodes;
bool useEuropeanCodes = false;
bool irSendInitialized = false;

// === FUNCTION DECLARATIONS ===
void quickflashLED();
void quickflashLEDx(uint8_t x);
void two();
void stopJamming();
void startJamming();
void initSP();
void initHP();
void initIrSend();
void sendIrStep();
uint8_t read_bits(const IrCode* powerCode, uint8_t* code_ptr, uint8_t* bits_r, uint8_t* bitsleft_r, uint8_t count);
void sendIrSignals();

uint8_t convertTimerValToFreq(uint8_t timer_val) {
  return 1000000UL / (timer_val * 2);
}

// === SETUP FUNCTION ===
void setup() {
  Serial.begin(115200);
  
  esp_bt_controller_deinit();
  esp_wifi_stop();
  esp_wifi_deinit();
  esp_wifi_disconnect();
  

  pinMode(TOGGLE_PIN, INPUT_PULLUP);
  pinMode(BOOT_PIN, INPUT_PULLUP);
  pinMode(LED_BUILTIN, OUTPUT);
  

  irsend.begin();

  digitalWrite(LED_BUILTIN, LOW);
  
  initHP();
  initSP();

  stopJamming();
}

// === MAIN LOOP ===
void loop() {
  if (digitalRead(BOOT_PIN) == LOW) {
    while (digitalRead(BOOT_PIN) == LOW);

    currentMode = (currentMode == JAMMING) ? IR_SEND : JAMMING;

    stopJamming();
    jammingActive = false;
    irActive = false;
    irSendInitialized = false;

    for (int i = 0; i < 3; i++) {
      digitalWrite(LED_BUILTIN, HIGH);
      delay(300);
      digitalWrite(LED_BUILTIN, LOW);
      delay(300);
    }

    Serial.print("Mode berganti ke: ");
    Serial.println(currentMode == JAMMING ? "JAMMING" : "IR SEND");
  }

  static bool lastToggleState = HIGH;
  bool toggleState = digitalRead(TOGGLE_PIN);

if (toggleState == LOW && lastToggleState == HIGH && millis() - lastToggleMillis > toggleDebounceTime) {
  lastToggleMillis = millis();
  
  if (currentMode == JAMMING) {
    jammingActive = !jammingActive;
    if (jammingActive) {
      startJamming();
      Serial.println("Jamming ON");
    } else {
      stopJamming();
      Serial.println("Jamming OFF");
    }
  } else {
    irActive = !irActive;
    if (irActive) {
      Serial.println("IR SEND ON");
      initIrSend();
    } else {
      Serial.println("IR SEND OFF");
    }
    // LED feedback tetap pakai delay() karena tidak kritis
    digitalWrite(LED_BUILTIN, HIGH);
    delay(200);
    digitalWrite(LED_BUILTIN, LOW);
  }
}
lastToggleState = toggleState;

  // === LED Indikator berdasarkan mode dan status ===
  if (currentMode == JAMMING && jammingActive) {
    two(); 
    digitalWrite(LED_BUILTIN, HIGH);
  } 
  else if (currentMode == JAMMING && !jammingActive) {
    unsigned long now = millis();
    if (now - previousMillis >= 500) {
      previousMillis = now;
      ledState = !ledState;
      digitalWrite(LED_BUILTIN, ledState);
    }
  }
  else if (currentMode == IR_SEND && irActive) {
    static unsigned long irPreviousMillis = 0;
    static bool irLedState = false;
    static bool irStepSent = false;

    if (digitalRead(TOGGLE_PIN) == LOW) {
      delay(50);
      if (digitalRead(TOGGLE_PIN) == LOW) {
        irActive = false;
        irSendInitialized = false;
        digitalWrite(LED_BUILTIN, LOW);
        Serial.println("IR SEND dihentikan dengan tombol");
        while(digitalRead(TOGGLE_PIN) == LOW);
        return;
      }
    }

    unsigned long now = millis();
    if (!irStepSent) {
      sendIrStep();
      digitalWrite(LED_BUILTIN, HIGH);
      irPreviousMillis = now;
      irStepSent = true;
    }
    else if (irStepSent && !irLedState && now - irPreviousMillis >= 30) {
      digitalWrite(LED_BUILTIN, LOW);
      irLedState = true;
      irPreviousMillis = now;
    }
    else if (irLedState && now - irPreviousMillis >= 200) {
      irStepSent = false;
      irLedState = false;
    }
  }
}

// === RADIO INITIALIZATION FUNCTIONS ===
void initSP() {
  sp = new SPIClass(VSPI);
  sp->begin();
  if (radio1.begin(sp)) {
    Serial.println("SP Started !!!");
    radio1.setAutoAck(false);
    radio1.stopListening();
    radio1.setRetries(0, 0);
    radio1.setPALevel(RF24_PA_MAX, true);
    radio1.setDataRate(RF24_2MBPS);
    radio1.setCRCLength(RF24_CRC_DISABLED);
    radio1.printPrettyDetails();
  } else {
    Serial.println("SP couldn't start !!!");
  }
}

void initHP() {
  hp = new SPIClass(HSPI);
  hp->begin();
  if (radio.begin(hp)) {
    Serial.println("HP Started !!!");
    radio.setAutoAck(false);
    radio.stopListening();
    radio.setRetries(0, 0);
    radio.setPALevel(RF24_PA_MAX, true);
    radio.setDataRate(RF24_2MBPS);
    radio.setCRCLength(RF24_CRC_DISABLED);
    radio.printPrettyDetails();
  } else {
    Serial.println("HP couldn't start !!!");
  }
}

// === JAMMING CONTROL FUNCTIONS ===
void two() {
  if (flagv == 0) {  
    ch1 += 4;
  } else {  
    ch1 -= 4;
  }
  if (flag == 0) {  
    ch += 2;
  } else {  
    ch -= 2;
  }
  if ((ch1 > 79) && (flagv == 0)) {
    flagv = 1;                             
  } else if ((ch1 < 2) && (flagv == 1)) {  
    flagv = 0;                             
  }
  if ((ch > 79) && (flag == 0)) {
    flag = 1;                            
  } else if ((ch < 2) && (flag == 1)) {  
    flag = 0;                            
  }
  radio.setChannel(ch);
  radio1.setChannel(ch1);
}

void stopJamming() {
  radio.stopConstCarrier();
  radio1.stopConstCarrier();
  radio.powerDown();
  radio1.powerDown();
}

void startJamming() {
  radio.powerUp();
  radio1.powerUp();
  radio.startConstCarrier(RF24_PA_MAX, ch);
  radio1.startConstCarrier(RF24_PA_MAX, ch1);
}

// === IR FUNCTIONS ===
void initIrSend() {
  if (useEuropeanCodes) {
    powerCodes = EUpowerCodes;
    numCodes = num_EUcodes;
    Serial.println("Menggunakan kode Eropa");
  } else {
    powerCodes = NApowerCodes;
    numCodes = num_NAcodes;
    Serial.println("Menggunakan kode Amerika Utara");
  }
  irIndex = 0;
  irSendInitialized = true;
    Serial.print("Jumlah kode IR: ");
  Serial.println(numCodes);
}

void sendIrStep() {
  if (digitalRead(TOGGLE_PIN) == LOW) {
    Serial.println("Pengiriman IR dihentikan oleh pengguna.");
    irActive = false;
    irSendInitialized = false;
    digitalWrite(LED_BUILTIN, LOW);
    return;
  }

  if (irIndex >= numCodes) {
    Serial.println("Selesai kirim semua kode IR.");
    irActive = false;
    irSendInitialized = false;
    return;
  }

  unsigned long now = millis();
  if (now - lastIrMillis < 205) return;
  lastIrMillis = now;

  const IrCode* powerCode = powerCodes[irIndex];
  const uint8_t freq = convertTimerValToFreq(powerCode->timer_val);
  const uint8_t numpairs = powerCode->numpairs;
  const uint8_t bitcompression = powerCode->bitcompression;

  uint16_t rawData[300];
  uint8_t code_ptr = 0;
  uint8_t bitsleft_r = 0;
  uint8_t bits_r = 0;

  for (uint8_t k = 0; k < numpairs; k++) {
    uint16_t ti = (read_bits(powerCode, &code_ptr, &bits_r, &bitsleft_r, bitcompression)) * 2;
    rawData[k*2] = powerCode->times[ti] * 10;
    rawData[k*2+1] = powerCode->times[ti+1] * 10;
  }

  irsend.sendRaw(rawData, numpairs * 2, freq);
  quickflashLED();

  Serial.print("Kirim kode IR ke-");
  Serial.println(irIndex);

  irIndex++;
    Serial.print("Kode ke: ");
  Serial.print(irIndex);
  Serial.print(" dari ");
  Serial.println(numCodes);
}


void sendIrSignals() {
  Serial.println("Mengirim sinyal IR ke berbagai TV dan STB...");
  const IrCode* const* powerCodes;
  uint8_t numCodes;
  bool useEuropeanCodes = false;
  if (useEuropeanCodes) {
    powerCodes = EUpowerCodes;
    numCodes = num_EUcodes;
    Serial.println("Menggunakan kode Eropa");
  } else {
    powerCodes = NApowerCodes;
    numCodes = num_NAcodes;
    Serial.println("Menggunakan kode Amerika Utara");
  }
  for (uint8_t i = 0; i < numCodes; i++) {
    const IrCode* powerCode = powerCodes[i];
    const uint8_t freq = 38;
    const uint8_t numpairs = powerCode->numpairs;
    const uint8_t bitcompression = powerCode->bitcompression;
    uint16_t rawData[300];
    uint8_t code_ptr = 0;
    uint8_t bitsleft_r = 0;
    uint8_t bits_r = 0;
    for (uint8_t k = 0; k < numpairs; k++) {
      uint16_t ti;
      ti = (read_bits(powerCode, &code_ptr, &bits_r, &bitsleft_r, bitcompression)) * 2;
      uint16_t ontime = powerCode->times[ti];
      uint16_t offtime = powerCode->times[ti+1];
      rawData[k*2] = ontime * 10;
      rawData[(k*2)+1] = offtime * 10;
    }
    irsend.sendRaw(powerCode->times, powerCode->numpairs * 2, freq);
    quickflashLED();
    delay(205);
    if (digitalRead(TOGGLE_PIN) == LOW) {
      while (digitalRead(TOGGLE_PIN) == LOW);
      quickflashLEDx(4);
      break;
    }
  }
  Serial.println("Sinyal IR selesai dikirim ke semua perangkat.");
}

uint8_t read_bits(const IrCode* powerCode, uint8_t* code_ptr, uint8_t* bits_r, uint8_t* bitsleft_r, uint8_t count) {
  uint8_t i;
  uint8_t tmp = 0;

  for (i = 0; i < count; i++) {
    if (*bitsleft_r == 0) {
      *bits_r = powerCode->codes[(*code_ptr)++];
      *bitsleft_r = 8;
    }
    (*bitsleft_r)--;
    tmp |= (((*bits_r >> (*bitsleft_r)) & 1) << (count-1-i));
  }
  return tmp;
}


// === LED INDICATOR FUNCTIONS ===
void quickflashLED() {
  digitalWrite(LED_BUILTIN, HIGH);
  delay(30);
  digitalWrite(LED_BUILTIN, LOW);
}

void quickflashLEDx(uint8_t x) {
  for (uint8_t i = 0; i < x; i++) {
    quickflashLED();
    delay(150);
  }
}
