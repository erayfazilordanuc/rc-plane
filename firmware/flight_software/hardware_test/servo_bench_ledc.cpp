// ============================================================
// TEZGAH TESTI - seri porttan servo/ESC denemesi
// Telsiz yok: sadece cikislari surer. Servo/ESC arizasini yazilimdan
// ayirmak, yon ve notr noktasi denemek icin.
//
//   Yuklemek icin : pio run -e bench -t upload
//   Ucus firmware : pio run -t upload
//
// Ayri bir PlatformIO ortami ([env:bench]) bu dosyayi src/main.cpp yerine
// derler, dolayisiyla dosya tasima/yer degistirme gerekmez.
//
// UYARI: PERVANE TAKILI OLMASIN!
// Kart ve servolar ESC'nin BEC 5V hattindan beslenir, GND'ler ortak olmali.
// 't' ve 'p' testlerinde ESC'yi ayir, karti USB'den besle.
// ============================================================
// Uc cikisin hepsi cipin LEDC birimiyle surulur, ESP32Servo kullanilmaz:
//  - kutuphanenin pin izin listesi (GPIO 0'i reddediyor) devre disi kalir
//  - degerler dogrudan mikrosaniye yazilir, rc_protocol.h ile ayni dil
//  - derece <-> us donusumu olmadigi icin notr noktasi kaymaz

#include <Arduino.h>

// Pin tanimlari - src/main.cpp ile AYNI olmali (ESP32 DevKitC 38 pin).
// Kablolama: docs/kablolama.html
const int PIN_ESC      = 25;  // ESC sinyal (10k pulldown var)
const int PIN_ELEVATOR = 26;
const int PIN_RUDDER   = 27;

// LEDC kanallari: 0 = ESC, 1..2 = servolar
const int KANAL_ESC = 0;
const int SERVO_ADET = 2;

// Sinirlar (rc_protocol.h ile ayni aralik: 1000..2000 us)
const int SERVO_MIN = 1000, SERVO_MID = 1500, SERVO_MAX = 2000;
const int ESC_MIN  = 1000;  // arming / durdurma degeri
const int ESC_TEST = 1150;  // 'm' komutunda kullanilan cok dusuk gaz
const int ESC_MAX  = 1200;  // asla bu degerin ustune cikilmaz

const int servoPinleri[SERVO_ADET]    = { PIN_ELEVATOR, PIN_RUDDER };
const char* servoIsimleri[SERVO_ADET] = { "Elevator", "Rudder" };
int secili = 0;  // 0..1, varsayilan elevator

// ------------------------------------------------------------
// PIN TARAMA icin aday GPIO listesi
//
// Elle lehimlenmis bir kartta telin hangi pad'e gittigi belirsiz olabilir;
// bu liste 38 pinli DevKitC'de disariya cikan ve guvenle surulebilen tum
// cikis pinlerini tasir. Listede OLMAYANLAR ve nedeni:
//   1, 3    UART0 - surulurse seri baglanti kopar, sonucu goremezsin
//   6-11    dahili flash (kartta D0-D3, CMD, CLK) - surulurse cip coker
//   34-39   yalnizca giris, 3.3 V veremez
// 0/2/12/15 strapping pinleridir ama strapping sadece BOOT aninda okunur;
// boot bittikten sonra cikis olarak surulebilirler. Taramada olmalarinin
// sebebi tam da bu: telin yanlislikla oraya lehimlenip lehimlenmedigini gormek.
// 16/17 WROOM-32D'de bos; WROVER modulunde PSRAM'e bagli, orada listeden cikar.
// ------------------------------------------------------------
const int taramaPinleri[] = { 0, 2, 4, 5, 12, 13, 14, 15, 16, 17, 18, 19,
                              21, 22, 23, 25, 26, 27, 32, 33 };
const int TARAMA_ADET = sizeof(taramaPinleri) / sizeof(taramaPinleri[0]);

// ------------------------------------------------------------
// 14 BIT, src/main.cpp ile ayni. Klasik ESP32 20 bite kadar izin veriyor
// ama ESP32-C3'un LEDC zamanlayicisi en fazla 14 bit; kart C3'e donerse
// 16 bit kullanan kod ledcSetup()'ta sessizce 0 doner, pinde 0 V okursun.
// ------------------------------------------------------------
const uint8_t  LEDC_BIT       = 14;
const uint32_t LEDC_ADIM      = 1UL << LEDC_BIT;   // 16384
const uint32_t PWM_PERIYOT_US = 20000;             // 50 Hz

// Mikrosaniyeyi LEDC duty degerine cevirir
uint32_t usToDuty(int us) {
  return (uint32_t)((uint64_t)us * LEDC_ADIM / PWM_PERIYOT_US);
}

// Pin tarama testleri pinleri duz digital cikisa cevirir ve LEDC baglantisini
// bozar. Test bitince servo/ESC kanallarini eski haline dondurmek icin.
void ledcGeriBagla() {
  ledcSetup(KANAL_ESC, 50, LEDC_BIT);
  ledcAttachPin(PIN_ESC, KANAL_ESC);
  ledcWrite(KANAL_ESC, usToDuty(ESC_MIN));
  for (int i = 0; i < SERVO_ADET; i++) {
    ledcSetup(i + 1, 50, LEDC_BIT);
    ledcAttachPin(servoPinleri[i], i + 1);
    ledcWrite(i + 1, usToDuty(SERVO_MID));
  }
}

// ESC'ye deger yazar, guvenlik sinirlarini asla asmaz
void escYaz(int us) {
  if (us < ESC_MIN) us = ESC_MIN;
  if (us > ESC_MAX) us = ESC_MAX;
  ledcWrite(KANAL_ESC, usToDuty(us));
}

// Secili servoya mikrosaniye yazar ve raporlar
void servoYaz(int us) {
  if (us < SERVO_MIN) us = SERVO_MIN;
  if (us > SERVO_MAX) us = SERVO_MAX;
  ledcWrite(secili + 1, usToDuty(us));
  Serial.printf("%s -> %d us\n", servoIsimleri[secili], us);
}

void setup() {
  Serial.begin(115200);  // UART0, kartin USB koprusu uzerinden
  delay(500);

  // ESC kanalini kur ve arm et: once 1000 us, sonra 3 saniye bekle
  uint32_t f = ledcSetup(KANAL_ESC, 50, LEDC_BIT);
  ledcAttachPin(PIN_ESC, KANAL_ESC);
  escYaz(ESC_MIN);
  Serial.printf("ESC         pin %2d -> LEDC kanal %d (%u Hz)\n", PIN_ESC, KANAL_ESC, (unsigned)f);
  Serial.println("ESC arming... 3 saniye (PERVANE TAKILI OLMASIN!)");
  delay(3000);
  Serial.println("ESC hazir.");

  // Iki servo kanalini kur ve notre (1500 us) al
  for (int i = 0; i < SERVO_ADET; i++) {
    f = ledcSetup(i + 1, 50, LEDC_BIT);
    ledcAttachPin(servoPinleri[i], i + 1);
    ledcWrite(i + 1, usToDuty(SERVO_MID));
    Serial.printf("%-11s pin %2d -> LEDC kanal %d (%u Hz)\n",
                  servoIsimleri[i], servoPinleri[i], i + 1, (unsigned)f);
  }

  Serial.println("Komutlar: 1=elevator 2=rudder | a=1200 d=1800 s=1500 | x=supurme | m=motor | 0=stop");
  Serial.println("          v=multimetre testi (secili pine 5 sn %50 duty)");
  Serial.println("          t=toplu lehim testi | p=pin tarama  (BEC AYRI olmali!)");
  Serial.printf("Secili servo: %s\n", servoIsimleri[secili]);
}

void loop() {
  if (!Serial.available()) return;
  char c = Serial.read();

  if (c >= '1' && c <= '0' + SERVO_ADET) {
    secili = c - '1';
    Serial.printf("Secili servo: %s\n", servoIsimleri[secili]);
  }
  else if (c == 'a') servoYaz(1200);
  else if (c == 'd') servoYaz(1800);
  else if (c == 's') servoYaz(SERVO_MID);
  else if (c == 'x') {
    // Tum servolar sirayla 1200-1500-1800-1500 supurme yapar
    const int adimlar[4] = { 1200, SERVO_MID, 1800, SERVO_MID };
    for (int i = 0; i < SERVO_ADET; i++) {
      Serial.printf("Supurme: %s\n", servoIsimleri[i]);
      for (int j = 0; j < 4; j++) {
        ledcWrite(i + 1, usToDuty(adimlar[j]));
        delay(500);
      }
    }
    Serial.println("Supurme bitti, tum servolar notrde.");
  }
  else if (c == 't') {
    // TOPLU LEHIM TESTI - "hangi tel karta gercekten baglanmis?"
    //
    // Tum aday pinleri ayni anda 3.3V'a ceker. PWM degil duz DC: multimetrede
    // ortalama alma derdi yok, ya 3.3 V okursun ya 0 V. Her lehimledigin telin
    // ucunda 3.3 V gormelisin; 0 V okuyan tel ya kartta degil ya da soguk lehim.
    //
    // Bunu yaparken BEC'i ve servolari TAMAMEN AYIR. 3.3V'luk bir cikisi
    // 5V'a bagli bir hatta surmek cipe zarar verir.
    Serial.println("TOPLU TEST: tum pinler 3.3V, 15 saniye.");
    Serial.println("BEC, servolar ve nRF24 AYRI olmali! Her telin ucunu GND'ye gore olc.");
    Serial.print  ("Surulen pinler: GPIO");
    for (int i = 0; i < TARAMA_ADET; i++) {
      pinMode(taramaPinleri[i], OUTPUT);
      digitalWrite(taramaPinleri[i], HIGH);
      Serial.printf("%s%d", i ? ", " : " ", taramaPinleri[i]);
    }
    Serial.println();
    delay(15000);
    for (int i = 0; i < TARAMA_ADET; i++) digitalWrite(taramaPinleri[i], LOW);
    ledcGeriBagla();
    Serial.println("Toplu test bitti, kanallar geri baglandi.");
  }
  else if (c == 'p') {
    // PIN TARAMA - "bu tel HANGI pine baglanmis?"
    //
    // Aday pinleri sirayla 3 saniye 3.3V'a ceker, hangisini surdugunu yazar.
    // Multimetreyi supheli telin ucunda tut; hangi pin yazarken 3.3 V'a
    // sicriyorsa o tel o GPIO'ya lehimlenmis demektir. Elle lehimlenmis
    // kartlarda baski uzerindeki numara ile gercek GPIO tutmayabilir,
    // bu tarama isin dogrusunu soyler.
    Serial.println("PIN TARAMA: her pin 3 saniye 3.3V. BEC, servolar ve nRF24 AYRI olmali!");
    for (int i = 0; i < TARAMA_ADET; i++) pinMode(taramaPinleri[i], OUTPUT);
    for (int i = 0; i < TARAMA_ADET; i++) {
      for (int j = 0; j < TARAMA_ADET; j++) {
        digitalWrite(taramaPinleri[j], j == i ? HIGH : LOW);
      }
      Serial.printf("  >>> GPIO%d yuksek\n", taramaPinleri[i]);
      delay(3000);
    }
    for (int i = 0; i < TARAMA_ADET; i++) digitalWrite(taramaPinleri[i], LOW);
    ledcGeriBagla();
    Serial.println("Tarama bitti, kanallar geri baglandi.");
  }
  else if (c == 'v') {
    // MULTIMETRE TESTI - "bu pinde sinyal var mi?" sorusunun kesin cevabi.
    //
    // Normal servo darbesi %5..%10 duty, yani multimetrede 0.16..0.33 V gibi
    // birbirine cok yakin degerler cikar; ucuz bir olcu aletinde bunu 0 V'tan
    // ayirt etmek zor. %50 duty ise 3.3/2 = ~1.65 V verir, hicbir suphe birakmaz.
    //
    // Siyah prob servo konnektorunun GND (kahverengi) pinine, kirmizi prob
    // sinyal (turuncu/beyaz) pinine:
    //   ~1.65 V -> pinde sinyal var, kablo saglam, GND ortak
    //   0.00 V  -> sinyal teli kopuk veya baska GPIO'ya bagli
    //   3.30 V  -> pin surekli high, LEDC bagli degil
    //
    // ESC'ye uygulanmaz: 10 ms darbe ESC icin gecersizdir. Sadece secili servo.
    // 5 saniye sonra kendiliginden 1500 us'e doner, unutulup kalmaz.
    Serial.printf("OLCUM: %s (pin %d) -> %%50 duty, 5 saniye. Beklenen ~1.65 V\n",
                  servoIsimleri[secili], servoPinleri[secili]);
    ledcWrite(secili + 1, LEDC_ADIM / 2);  // tam yari duty
    delay(5000);
    ledcWrite(secili + 1, usToDuty(SERVO_MID));
    Serial.println("Olcum bitti, servo 1500 us'e dondu.");
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
