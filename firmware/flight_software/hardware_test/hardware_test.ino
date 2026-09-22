// ============================================================
// ARSIV - Arduino IDE icin eski tezgah testi (ESP32Servo tabanli).
//
// GUNCEL TEZGAH TESTI BU DEGIL:
//     pio run -e bench -t upload -t monitor
// yani hardware_test/servo_bench_ledc.cpp. O surum ESP32Servo yerine
// dogrudan LEDC kullaniyor (kutuphanenin pin izin listesi GPIO0'i
// reddediyor ve derece<->us donusumu notru kaydiriyor) ve ayrica pin
// tarama / multimetre testleri iceriyor. platformio.ini'nin derledigi
// dosya odur; bu .ino hicbir ortamda derlenmiyor.
//
// Bu dosya yalnizca Arduino IDE ile hizli bir deneme icin duruyor.
// Uc kanalli duzene gore guncellendi ki yanlis pin bilgisi tasimasin.
//
// UYARI: Bu testi ilk kez calistirirken PERVANE TAKILI OLMASIN!
// Motorun donup donmedigini ve yonunu pervanesiz kontrol edin.
// Kart ve servolar ESC'nin BEC 5V hattindan beslenir, GND'ler ortak olmali.
// ============================================================

#include <ESP32Servo.h>

// Pin tanimlari - src/main.cpp ile ayni (ESP32 DevKitC 38 pin).
const int PIN_ESC      = 25;  // ESC sinyal (10k pulldown var)
const int PIN_ELEVATOR = 26;
const int PIN_RUDDER   = 27;
const int SERVO_ADET   = 2;

// ESC guvenlik sinirlari
const int ESC_MIN  = 1000;  // arming / durdurma degeri
const int ESC_TEST = 1150;  // 'm' komutunda kullanilan cok dusuk gaz
const int ESC_MAX  = 1200;  // asla bu degerin ustune cikilmaz

Servo esc;
Servo servolar[SERVO_ADET];
const int servoPinleri[SERVO_ADET] = { PIN_ELEVATOR, PIN_RUDDER };
const char* servoIsimleri[SERVO_ADET] = { "Elevator", "Rudder" };
int secili = 0;  // 0..1, varsayilan elevator

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
  Serial.begin(115200);  // UART0, kartin USB koprusu uzerinden
  delay(500);

  // ESC'yi baglat ve arm et: once 1000 us, sonra 3 saniye bekle
  esc.setPeriodHertz(50);
  esc.attach(PIN_ESC, 1000, 2000);
  escYaz(ESC_MIN);
  Serial.println("ESC arming... 3 saniye (PERVANE TAKILI OLMASIN!)");
  delay(3000);
  Serial.println("ESC hazir.");

  // Iki servoyu notr (90 derece) konumda baslat
  for (int i = 0; i < SERVO_ADET; i++) {
    servolar[i].setPeriodHertz(50);
    servolar[i].attach(servoPinleri[i], 500, 2400);
    servolar[i].write(90);
  }

  Serial.println("Komutlar: 1=elevator 2=rudder | a=60 d=120 s=90 | x=supurme | m=motor test | 0=stop");
  Serial.printf("Secili servo: %s\n", servoIsimleri[secili]);
}

void loop() {
  if (!Serial.available()) return;
  char c = Serial.read();

  if (c >= '1' && c <= '0' + SERVO_ADET) {
    secili = c - '1';
    Serial.printf("Secili servo: %s\n", servoIsimleri[secili]);
  }
  else if (c == 'a') servoYaz(60);
  else if (c == 'd') servoYaz(120);
  else if (c == 's') servoYaz(90);
  else if (c == 'x') {
    // Tum servolar sirayla 60-90-120-90 supurme yapar
    const int adimlar[4] = { 60, 90, 120, 90 };
    for (int i = 0; i < SERVO_ADET; i++) {
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
