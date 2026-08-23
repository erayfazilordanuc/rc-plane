// ============================================================
// UYARI: Bu testi ilk kez calistirirken PERVANE TAKILI OLMASIN!
// Motorun donup donmedigini ve yonunu pervanesiz kontrol edin.
// Servolar ve ESC harici 5V UBEC'ten beslenmeli, GND'ler ortak olmali.
// ============================================================

#include <ESP32Servo.h>

// Pin tanimlari (GPIO 2, 8, 9 strapping pini -> kullanilmiyor)
const int PIN_ESC      = 0;   // ESC sinyal (10k pulldown var)
const int PIN_AIL_SOL  = 1;
const int PIN_AIL_SAG  = 3;
const int PIN_ELEVATOR = 4;
const int PIN_RUDDER   = 21;

// ESC guvenlik sinirlari
const int ESC_MIN  = 1000;  // arming / durdurma degeri
const int ESC_TEST = 1150;  // 'm' komutunda kullanilan cok dusuk gaz
const int ESC_MAX  = 1200;  // asla bu degerin ustune cikilmaz

Servo esc;
Servo servolar[4];
const int servoPinleri[4] = { PIN_AIL_SOL, PIN_AIL_SAG, PIN_ELEVATOR, PIN_RUDDER };
const char* servoIsimleri[4] = { "Aileron Sol", "Aileron Sag", "Elevator", "Rudder" };
int secili = 0;  // 0..3, varsayilan aileron sol

// ESC'ye deger yazar, sinirlari asla asmaz
void escYaz(int us) {
  if (us < ESC_MIN) us = ESC_MIN;
  if (us > ESC_MAX) us = ESC_MAX;
  esc.writeMicroseconds(us);
}

// Secili servoyu istenen aciya getirir ve raporlar
void servoYaz(int aci) {
  servolar[secili].write(aci);
  Serial.printf("%s -> %d derece\n", servoIsimleri[secili], aci);
}

void setup() {
  Serial.begin(115200);  // C3'te USB-CDC uzerinden cikar
  delay(500);

  // ESC'yi baglat ve arm et: once 1000 us, sonra 3 saniye bekle
  esc.setPeriodHertz(50);
  esc.attach(PIN_ESC, 1000, 2000);
  escYaz(ESC_MIN);
  Serial.println("ESC arming... 3 saniye (PERVANE TAKILI OLMASIN!)");
  delay(3000);
  Serial.println("ESC hazir.");

  // Tum servolari notr (90 derece) konumda baslat
  for (int i = 0; i < 4; i++) {
    servolar[i].setPeriodHertz(50);
    servolar[i].attach(servoPinleri[i], 500, 2400);
    servolar[i].write(90);
  }

  Serial.println("Komutlar: 1-4 servo sec | a=60 d=120 s=90 | x=supurme | m=motor test | 0=stop");
  Serial.printf("Secili servo: %s\n", servoIsimleri[secili]);
}

void loop() {
  if (!Serial.available()) return;
  char c = Serial.read();

  if (c >= '1' && c <= '4') {
    secili = c - '1';
    Serial.printf("Secili servo: %s\n", servoIsimleri[secili]);
  }
  else if (c == 'a') servoYaz(60);
  else if (c == 'd') servoYaz(120);
  else if (c == 's') servoYaz(90);
  else if (c == 'x') {
    // Tum servolar sirayla 60-90-120-90 supurme yapar
    const int adimlar[4] = { 60, 90, 120, 90 };
    for (int i = 0; i < 4; i++) {
      Serial.printf("Supurme: %s\n", servoIsimleri[i]);
      for (int j = 0; j < 4; j++) {
        servolar[i].write(adimlar[j]);
        delay(500);
      }
    }
    Serial.println("Supurme bitti, tum servolar notrde.");
  }
  else if (c == 'm') {
    // Motor testi: 2 saniye cok dusuk gaz, sonra otomatik durdurma
    Serial.printf("MOTOR TEST: %d us, 2 saniye...\n", ESC_TEST);
    escYaz(ESC_TEST);
    delay(2000);
    escYaz(ESC_MIN);
    Serial.println("Motor durduruldu (1000 us).");
  }
  else if (c == '0') {
    escYaz(ESC_MIN);
    Serial.println("ACIL DURDURMA: ESC 1000 us.");
  }
}
