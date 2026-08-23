// ============================================================
// nRF24L01 TESHIS (diagnostic) firmware'i
//
// Calistir:   pio run -e rfdiag -t upload -t monitor
//   (Bu firmware WiFi acmaz. Kumandaya donmek icin:
//    pio run -e esp32dev -t upload -t monitor)
//
// Sirasiyla 4 test yapar:
//   TEST 1  Kisa devre  -> breadboard'da birbirine degen pinler
//   TEST 2  MISO surucu -> MISO hattinin ucunda gercekten modul var mi
//   TEST 3  Pin tarama  -> 3 veri teli yer degistirmis mi (12 kombinasyon)
//   TEST 4  Hiz tarama  -> calisan kombinasyonda en yuksek saglam SPI hizi
//
// TEST 3 en onemlisi: teller karismissa dogru sirayi soyler.
// ============================================================

#include <Arduino.h>
#include <SPI.h>

// main.cpp ile ayni pinler
static const uint8_t PIN_CE   = 4;
static const uint8_t PIN_CSN  = 5;
static const uint8_t PIN_SCK  = 18;
static const uint8_t PIN_MISO = 19;
static const uint8_t PIN_MOSI = 23;

// nRF24L01 komutlari / register adresleri
static const uint8_t R_REGISTER = 0x00;
static const uint8_t W_REGISTER = 0x20;
static const uint8_t REG_CONFIG     = 0x00;
static const uint8_t REG_EN_AA      = 0x01;
static const uint8_t REG_SETUP_AW   = 0x03;
static const uint8_t REG_RF_CH      = 0x05;
static const uint8_t REG_RF_SETUP   = 0x06;
static const uint8_t REG_RX_ADDR_P0 = 0x0A;

// Reset sonrasi beklenen degerler (nRF24L01+ datasheet, Table 27)
static const uint8_t EXP_STATUS   = 0x0E;
static const uint8_t EXP_CONFIG   = 0x08;
static const uint8_t EXP_SETUP_AW = 0x03;
static const uint8_t EXP_EN_AA    = 0x3F;

// ------------------------------------------------------------
// O anda test edilen pin/hiz kombinasyonu
// ------------------------------------------------------------
struct PinSet {
  uint8_t sck, miso, mosi, ce, csn;
};

static PinSet  cur   = {PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CE, PIN_CSN};
static uint32_t spiHz = 1000000;
static uint8_t  lastStatus = 0;

// ------------------------------------------------------------
// Ham SPI erisimi (RF24 kutuphanesi kullanilmiyor - araya girmesin)
// ------------------------------------------------------------
static void beginTx() {
  SPI.beginTransaction(SPISettings(spiHz, MSBFIRST, SPI_MODE0));
  digitalWrite(cur.csn, LOW);
}
static void endTx() {
  digitalWrite(cur.csn, HIGH);
  SPI.endTransaction();
}

static uint8_t regRead(uint8_t reg) {
  beginTx();
  lastStatus = SPI.transfer(R_REGISTER | reg);
  uint8_t v = SPI.transfer(0xFF);
  endTx();
  return v;
}

static void regReadBuf(uint8_t reg, uint8_t* buf, uint8_t len) {
  beginTx();
  lastStatus = SPI.transfer(R_REGISTER | reg);
  for (uint8_t i = 0; i < len; i++) buf[i] = SPI.transfer(0xFF);
  endTx();
}

static void regWriteBuf(uint8_t reg, const uint8_t* buf, uint8_t len) {
  beginTx();
  lastStatus = SPI.transfer(W_REGISTER | reg);
  for (uint8_t i = 0; i < len; i++) SPI.transfer(buf[i]);
  endTx();
}

// Verilen pin setiyle SPI'i yeniden baslatir
static void spiRestart(const PinSet& p) {
  SPI.end();
  pinMode(p.ce, OUTPUT);
  pinMode(p.csn, OUTPUT);
  digitalWrite(p.ce, LOW);      // standby
  digitalWrite(p.csn, HIGH);    // secili degil
  cur = p;
  SPI.begin(p.sck, p.miso, p.mosi, p.csn);
  delay(10);                    // power-on-reset payi (datasheet 4.5 ms + 14 us)
}

// ------------------------------------------------------------
// Modul bu pin setiyle cevap veriyor mu?
// Sadece okuma yetmez: 5 baytlik deseni yazip geri okuyoruz.
// Boylece MOSI + SCK + CSN + MISO zincirinin tamami test edilir.
// ------------------------------------------------------------
static bool moduleResponds(bool verbose) {
  const uint8_t pattern[5] = {0xDE, 0xAD, 0xBE, 0xEF, 0x11};
  uint8_t back[5] = {0};

  regWriteBuf(REG_RX_ADDR_P0, pattern, 5);
  regReadBuf(REG_RX_ADDR_P0, back, 5);

  bool ok = true;
  for (uint8_t i = 0; i < 5; i++) if (back[i] != pattern[i]) ok = false;

  if (verbose) {
    Serial.print("    yazilan DE AD BE EF 11 / okunan ");
    for (uint8_t i = 0; i < 5; i++) Serial.printf("%02X ", back[i]);
    Serial.println(ok ? " -> ESLESTI" : " -> eslesmedi");
  }
  return ok;
}

// ============================================================
// TEST 1 - Kisa devre
// Her pini sirayla LOW cek, digerlerini pull-up'li giris yap.
// Modul hicbir ESP32 pinini birbirine baglamaz; LOW okunan varsa
// breadboard'da/lehimde kisa devre var.
// ============================================================
static bool testShorts() {
  Serial.println("\n[TEST 1] Pinler arasi kisa devre");
  const uint8_t pins[5]    = {PIN_CE, PIN_CSN, PIN_SCK, PIN_MISO, PIN_MOSI};
  const char*   names[5]   = {"CE(4)", "CSN(5)", "SCK(18)", "MISO(19)", "MOSI(23)"};
  bool temiz = true;

  SPI.end();
  for (uint8_t i = 0; i < 5; i++) {
    for (uint8_t j = 0; j < 5; j++) {
      if (i != j) pinMode(pins[j], INPUT_PULLUP);
    }
    pinMode(pins[i], OUTPUT);
    digitalWrite(pins[i], LOW);
    delayMicroseconds(200);

    for (uint8_t j = 0; j < 5; j++) {
      if (i == j) continue;
      if (digitalRead(pins[j]) == LOW) {
        Serial.printf("  !! %s ile %s KISA DEVRE\n", names[i], names[j]);
        temiz = false;
      }
    }
    pinMode(pins[i], INPUT);
  }

  Serial.println(temiz ? "  Kisa devre yok. [OK]" : "  -> Once bu kisa devreyi gider.");
  return temiz;
}

// ============================================================
// TEST 2 - MISO surucu testi
// CSN HIGH iken nRF24 MISO'yu birakir (yuksek empedans) -> pull yonunu izler.
// CSN LOW iken nRF24 MISO'yu SURER -> pull'a ragmen sabit kalir.
// CSN LOW'da da pull'u izliyorsa: o telin ucunda calisan bir modul YOK.
// ============================================================
static bool testMisoDriven() {
  Serial.println("\n[TEST 2] MISO hattini bir sey suruyor mu?");
  SPI.end();

  pinMode(PIN_CE, OUTPUT);
  digitalWrite(PIN_CE, LOW);
  pinMode(PIN_CSN, OUTPUT);

  auto oku = [](bool csnLow) {
    digitalWrite(PIN_CSN, csnLow ? LOW : HIGH);
    delayMicroseconds(500);
    pinMode(PIN_MISO, INPUT_PULLUP);
    delayMicroseconds(500);
    bool up = digitalRead(PIN_MISO);
    pinMode(PIN_MISO, INPUT_PULLDOWN);
    delayMicroseconds(500);
    bool down = digitalRead(PIN_MISO);
    return (uint8_t)((up ? 2 : 0) | (down ? 1 : 0));
  };

  uint8_t serbest = oku(false);   // CSN HIGH: bosta olmasi normal
  uint8_t secili  = oku(true);    // CSN LOW: surulmesi gerek

  // 0b10 = pull-up'ta 1, pull-down'da 0 okundu -> hat bosta
  const bool bostaSerbest = (serbest == 0b10);
  const bool bostaSecili  = (secili  == 0b10);

  digitalWrite(PIN_CSN, HIGH);
  pinMode(PIN_MISO, INPUT);

  Serial.printf("  CSN=HIGH -> %s\n", bostaSerbest ? "bosta (normal)" : "surulu");
  Serial.printf("  CSN=LOW  -> %s\n", bostaSecili  ? "BOSTA" : "surulu (normal)");

  if (bostaSecili) {
    Serial.println("  -> MISO hattini kimse surmuyor. Uc olasilik:");
    Serial.println("     a) MISO teli kopuk veya yanlis nRF24 bacaginda (IRQ'ya gitmis olabilir)");
    Serial.println("     b) CSN teli module ulasmiyor -> modul hic secilmiyor");
    Serial.println("     c) Modul beslenmiyor (VCC/GND yok) veya olu");
    return false;
  }
  Serial.println("  MISO surulüyor. [OK]");
  return true;
}

// ============================================================
// TEST 3 - Pin permutasyon taramasi
// SCK/MISO/MOSI'nin 6 sirasi x CE/CSN'nin 2 sirasi = 12 kombinasyon.
// Teller yer degistirmisse hangisinin nereye gittigini soyler.
// ============================================================
static bool testPermutations(PinSet& bulunan) {
  Serial.println("\n[TEST 3] Pin siralamasi taramasi (12 kombinasyon)");
  const uint8_t data[3] = {PIN_SCK, PIN_MISO, PIN_MOSI};
  // {sck, miso, mosi} indeksleri - 3! = 6 permutasyon
  const uint8_t perm[6][3] = {{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}};
  const uint8_t ctl[2][2]  = {{PIN_CE, PIN_CSN}, {PIN_CSN, PIN_CE}};

  spiHz = 1000000;   // tarama guvenli hizda

  for (uint8_t c = 0; c < 2; c++) {
    for (uint8_t k = 0; k < 6; k++) {
      PinSet p = { data[perm[k][0]], data[perm[k][1]], data[perm[k][2]],
                   ctl[c][0], ctl[c][1] };
      spiRestart(p);

      uint8_t cfg = regRead(REG_CONFIG);
      uint8_t st  = lastStatus;
      bool aday = (st != 0x00 && st != 0xFF && (st & 0x80) == 0);  // STATUS bit7 daima 0

      Serial.printf("  SCK=%-2u MISO=%-2u MOSI=%-2u CE=%u CSN=%u | STATUS=0x%02X CONFIG=0x%02X",
                    p.sck, p.miso, p.mosi, p.ce, p.csn, st, cfg);

      if (!aday) { Serial.println("  x"); continue; }
      Serial.println("  ? -> yazma testi");

      if (moduleResponds(true)) {
        bulunan = p;
        Serial.println("  ==> BU KOMBINASYON CALISIYOR.");
        return true;
      }
    }
  }
  Serial.println("  Hicbir siralama cevap vermedi.");
  return false;
}

// ============================================================
// TEST 4 - Calisan kombinasyonda hiz taramasi
// ============================================================
static uint32_t testSpeeds(const PinSet& p) {
  Serial.println("\n[TEST 4] SPI hiz taramasi");
  const uint32_t hizlar[] = {10000000, 8000000, 4000000, 2000000, 1000000};
  uint32_t enIyi = 0;

  for (uint8_t i = 0; i < sizeof(hizlar) / sizeof(hizlar[0]); i++) {
    spiRestart(p);
    spiHz = hizlar[i];

    bool ok = moduleResponds(false);
    uint16_t hata = 0;
    if (ok) {
      // 200 kez arka arkaya oku - hepsi ayni gelmeli
      uint8_t ref = regRead(REG_RF_CH);
      for (uint16_t n = 0; n < 200; n++) if (regRead(REG_RF_CH) != ref) hata++;
    }

    Serial.printf("  %8lu Hz : %s", (unsigned long)hizlar[i],
                  ok ? "yazma OK" : "yazma HATALI");
    if (ok) Serial.printf(", 200 okumada %u hata", hata);
    Serial.println();

    if (ok && hata == 0 && enIyi == 0) enIyi = hizlar[i];
  }
  return enIyi;
}

// ============================================================
static void registerDump(const PinSet& p) {
  Serial.println("\n[REGISTER DUMP] (beklenen <- reset sonrasi datasheet degeri)");
  spiRestart(p);
  spiHz = 1000000;

  uint8_t cfg = regRead(REG_CONFIG);
  uint8_t st  = lastStatus;
  Serial.printf("  STATUS   = 0x%02X  (beklenen 0x%02X)\n", st,  EXP_STATUS);
  Serial.printf("  CONFIG   = 0x%02X  (beklenen 0x%02X)\n", cfg, EXP_CONFIG);
  Serial.printf("  SETUP_AW = 0x%02X  (beklenen 0x%02X)\n", regRead(REG_SETUP_AW), EXP_SETUP_AW);
  Serial.printf("  EN_AA    = 0x%02X  (beklenen 0x%02X)\n", regRead(REG_EN_AA),    EXP_EN_AA);
  Serial.printf("  RF_CH    = 0x%02X  (beklenen 0x02)\n",   regRead(REG_RF_CH));
  Serial.printf("  RF_SETUP = 0x%02X  (beklenen 0x0E veya 0x0F)\n", regRead(REG_RF_SETUP));
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n\n=== nRF24L01 TESHIS ===");
  Serial.printf("Beklenen pinler: CE=%u CSN=%u SCK=%u MISO=%u MOSI=%u\n",
                PIN_CE, PIN_CSN, PIN_SCK, PIN_MISO, PIN_MOSI);

  testShorts();
  bool misoOk = testMisoDriven();

  const PinSet beklenen = {PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CE, PIN_CSN};
  PinSet bulunan = beklenen;
  bool calisan = testPermutations(bulunan);

  registerDump(calisan ? bulunan : beklenen);

  Serial.println("\n=== SONUC ===");

  if (!calisan) {
    Serial.println("Modul hicbir pin siralamasinda cevap vermiyor.");
    Serial.println();
    Serial.println("EN ONEMLI: MODULE PARMAGINLA DOKUN.");
    Serial.println("  Ilik/sicaksa VCC ile GND yer degistirmis -> modul yanmis olabilir.");
    Serial.println("  nRF24 2x4 konnektorde 1. bacak GND, 2. bacak VCC'dir; konnektoru");
    Serial.println("  ters/kaydirmis olmak cok kolay. Modul uzerindeki yaziya bak.");
    Serial.println();
    Serial.println("Sonra sirayla:");
    Serial.println("  1. Multimetre: nRF24'un VCC-GND bacaklari arasi 3.2-3.6V olmali.");
    Serial.println("     ESP32'nin 3V3 pininde degil, MODULUN BACAGINDA olc.");
    Serial.println("  2. VCC-GND arasina 10-100uF kondansator (modul bacagina en yakin).");
    Serial.println("  3. 7 telin ikisi ucu arasi sureklilik (continuity) olc - tek tek.");
    Serial.println("     Dupont tellerin icindeki kopukluk gozle gorulmez.");
    if (!misoOk) {
      Serial.println("  4. TEST 2 MISO'yu kimsenin surmedigini soyledi: en cok sansli sucu");
      Serial.println("     MISO teli (yanlis bacak: IRQ'ya gitmis olabilir) ve CSN teli.");
    }
    Serial.println("  5. Hepsi tamamsa modul sahte/olu -> baska modulle dene.");
  } else {
    bool ayni = (bulunan.sck == PIN_SCK && bulunan.miso == PIN_MISO &&
                 bulunan.mosi == PIN_MOSI && bulunan.ce == PIN_CE && bulunan.csn == PIN_CSN);
    uint32_t hiz = testSpeeds(bulunan);

    Serial.println("\nModul CALISIYOR.");
    if (!ayni) {
      Serial.println("!! FAKAT TELLER KODDAKI SIRADA DEGIL. Gercek baglanti:");
      Serial.printf("     SCK  -> GPIO%u\n", bulunan.sck);
      Serial.printf("     MISO -> GPIO%u\n", bulunan.miso);
      Serial.printf("     MOSI -> GPIO%u\n", bulunan.mosi);
      Serial.printf("     CE   -> GPIO%u\n", bulunan.ce);
      Serial.printf("     CSN  -> GPIO%u\n", bulunan.csn);
      Serial.println("   Ya kablolari sema haline getir, ya main.cpp'deki PIN_* sabitlerini");
      Serial.println("   bu degerlere gore duzelt.");
    }
    if (hiz) {
      Serial.printf("En yuksek saglam SPI hizi: %lu Hz\n", (unsigned long)hiz);
      if (hiz < 4000000) {
        Serial.println("4 MHz'in altinda kaldi -> main.cpp'deki RF_SPI_HZ degerini bununla degistir.");
      } else {
        Serial.println("main.cpp'deki RF_SPI_HZ = 4000000 degeri uygun.");
      }
    } else {
      Serial.println("Hicbir hizda kararli degil -> 10-100uF kondansator ekle, telleri kisalt.");
    }
  }
}

void loop() {
  delay(1000);
}
