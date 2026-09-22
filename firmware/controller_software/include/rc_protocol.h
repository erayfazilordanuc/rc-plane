// ============================================================
// rc_protocol.h - Kumanda <-> Ucak ortak telsiz protokolu   (SURUM 2)
// BU DOSYA IKI PROJEDE DE AYNI OLMALI.
// (controller_software/include/ ve flight_software/include/)
// Bir tarafta degistirdiginde digerine de kopyala.
//
// SURUM 2'DE NE DEGISTI (surum 1'e gore):
//   - Kanal duzeni: GAZ + ELEVATOR + RUDDER (+ surum 3'te AILERON).
//   - Her paket tipinin KENDI magic degeri var. Onceden hepsi 0xA5'ti ve
//     tipler yalnizca boyuttan ayirt ediliyordu; kalibrasyon paketleri
//     eklenince bu yetersiz kaldi.
//   - Kalibrasyon paketleri eklendi (RcConfigPacket / RcConfigReport):
//     servo yon / notr / uc noktalari telsiz uzerinden ayarlanip ucagin
//     NVS'ine kaydediliyor. Artik firmware'i yeniden derlemek gerekmiyor.
//
// SURUM 1 FIRMWARE ILE UYUMSUZDUR. Iki karti da birlikte yukle.
// ============================================================
#pragma once
#include <stdint.h>

// ---- Protokol kimligi ----
// Her paket tipinin ayri imzasi var. Boyut kontrolu tek basina yeterli
// degil: bozuk bir paket dogru boyutta gelip yanlis tip olarak islenebilir.
static const uint8_t RC_MAGIC_CTRL = 0xA5;  // kumanda -> ucak, kontrol
static const uint8_t RC_MAGIC_TLM  = 0x5A;  // ucak -> kumanda, telemetri
static const uint8_t RC_MAGIC_CFG  = 0xC3;  // kumanda -> ucak, kalibrasyon
static const uint8_t RC_MAGIC_REP  = 0x3C;  // ucak -> kumanda, kalibrasyon raporu

static const uint8_t RC_PROTO_VER = 3;      // flags ust nibble'inda tasinir

// ---- Kanal degerleri (mikrosaniye, RC standardi) ----
static const uint16_t RC_US_MIN = 1000;
static const uint16_t RC_US_MID = 1500;
static const uint16_t RC_US_MAX = 2000;

// Kalibrasyonun cikabilecegi mutlak sinirlar. Servo bu araligin disinda
// mekanik dayanaga vurur ve akim ceker; ucak tarafi her cikisi bu arayla
// kirpar - bozuk ya da yanlis bir ayar paketi servoyu zorlayamaz.
static const uint16_t RC_US_HARD_MIN = 900;
static const uint16_t RC_US_HARD_MAX = 2100;

// ---- Kanal indeksleri (RcPacket.ch[] icin) ----
//
// SURUM 3'te AILERON kanali GERI GELDI, ama SONA eklendi: mevcut uc kanalin
// indeksi degismedi. Boylece kalibrasyon kayitlari ve kod yollari oldugu
// gibi kaldi, yalnizca dizi buyudu.
//
// Aileronsuz (3 kanalli) bir ucakta bu kanal her zaman notr gonderilir ve
// ucak tarafinda ona bagli cikislar notrde durur - zararsiz. Ayni firmware
// hem 3 hem 4 kanalli ucagi ucurur; fark yalnizca kumandadaki "aileron var"
// ayari ve ucaga servo takilip takilmadigi.
enum RcChannel : uint8_t {
  RC_CH_THROTTLE = 0,  // 1000 = stop, 2000 = tam gaz
  RC_CH_ELEVATOR = 1,  // 1000 = burun asagi, 1500 = notr, 2000 = burun yukari
  RC_CH_RUDDER   = 2,  // 1000 = sol, 1500 = notr, 2000 = sag
  RC_CH_AILERON  = 3,  // 1000 = sol yatis, 1500 = notr, 2000 = sag yatis
  RC_CH_COUNT    = 4
};

// ---- Kalibre edilebilir cikislar (RcConfigPacket.surf) ----
// AILERON sona eklendi - onceki uc indeks degismedi (bkz. RcChannel notu).
// Aileron TEK kalibrasyon girdisidir ama ucakta IKI cikis surer: sag kanat
// servosu kendi notru etrafinda aynalanir. Iki servoya ayri uc nokta vermek
// gerekseydi ayri bir yuzey eklenirdi; mekanik olarak aynalanan bir cift
// icin tek girdi hem yeterli hem daha az hata kaynagi.
enum RcSurface : uint8_t {
  RC_SURF_ELEVATOR = 0,
  RC_SURF_RUDDER   = 1,
  RC_SURF_THROTTLE = 2,   // ESC: minUs = stop, midUs = gaz TAVANI, maxUs = tam gaz
  RC_SURF_AILERON  = 3,
  RC_SURF_COUNT    = 4
};

// ---- RcPacket.flags bitleri ----
static const uint8_t RC_FLAG_ARMED = 0x01;  // 1 = motor calismasina izin var
static const uint8_t RC_FLAG_CALIB = 0x02;  // kumanda kalibrasyon ekraninda
// 0x04, 0x08 bos. Ust nibble (0xF0) = protokol versiyonu.

// ---- Telsiz ayarlari (iki tarafta ayni olmali) ----
static const uint8_t RC_RF_CHANNEL    = 108;      // 2.508 GHz - WiFi bandinin ustu
static const uint8_t RC_RF_ADDRESS[6] = "RCP01";  // 5 byte + '\0'

// Veri hizi:  0 = 1 Mbps,  1 = 2 Mbps,  2 = 250 kbps
//
// 250 kbps en uzun menzili verir AMA ucuz klon nRF24 cipleri (Si24R1 ve
// benzerleri) bu hizi DESTEKLEMEZ. setDataRate() sessizce basarisiz olur,
// o taraf 1 Mbps'te kalir, karsi taraf 250 kbps'te olur ve link tamamen
// oler - ustelik iki modul de SPI'dan cevap verdigi icin ikisi de "BAGLI"
// gorunur. Teshis edilmesi en zor ariza bu.
//
// Boot logunda "[RF] !! VERI HIZI AYARLANAMADI" gorursen o modul klondur:
// bu degeri 0 yap ve IKI PROJEYI DE yeniden yukle.
static const uint8_t RC_RF_DATARATE = 2;

// ---- Zamanlama ----
static const uint16_t RC_TX_PERIOD_MS = 20;   // verici 50 Hz gonderir
static const uint16_t RC_FAILSAFE_MS  = 500;  // alicida bu sure paket yoksa failsafe

#pragma pack(push, 1)

// ------------------------------------------------------------
// Kumanda -> Ucak, kontrol. Toplam 12 byte.
// ------------------------------------------------------------
struct RcPacket {
  uint8_t  magic;              // 0: RC_MAGIC_CTRL (0xA5)
  uint8_t  seq;                // 1: her pakette 1 artar, 255'ten sonra 0'a doner
  uint16_t ch[RC_CH_COUNT];    // 2..9: little-endian, 1000..2000 us
  uint8_t  flags;              // 10: bit0 ARMED, bit1 CALIB, ust nibble = versiyon
  uint8_t  crc;                // 11: onceki 11 byte'in CRC-8'i
};

// ------------------------------------------------------------
// Ucak -> Kumanda, telemetri (ACK payload / ESP-NOW). Toplam 8 byte.
// ------------------------------------------------------------
struct RcTelemetry {
  uint8_t  magic;    // 0: RC_MAGIC_TLM (0x5A)
  uint8_t  seqEcho;  // 1: en son alinan RcPacket.seq
  uint16_t vbatMv;   // 2..3: batarya gerilimi (mV). Olcum yoksa 0.
  uint8_t  lossPct;  // 4: alicinin gordugu paket kaybi yuzdesi (0..100)
  uint8_t  status;   // 5: RC_STATUS_* bitleri
  uint8_t  rxHz;     // 6: ucagin saniyede aldigi benzersiz paket sayisi
  uint8_t  crc;      // 7: onceki 7 byte'in CRC-8'i
};

// ------------------------------------------------------------
// Kumanda -> Ucak, kalibrasyon komutu. Toplam 12 byte.
//
// Yuzeyler icin: minUs / midUs / maxUs = alt uc / notr / ust uc.
// Gaz (RC_SURF_THROTTLE) icin alanlar farkli anlam tasir:
//   minUs = ESC stop darbesi     (tipik 1000)
//   midUs = GAZ TAVANI           (uygulanacak en yuksek darbe)
//   maxUs = ESC tam gaz darbesi  (tipik 2000)
// ------------------------------------------------------------
struct RcConfigPacket {
  uint8_t  magic;    // 0: RC_MAGIC_CFG (0xC3)
  uint8_t  seq;      // 1: komut sirasi - ucak bunu raporda geri yankilar
  uint8_t  op;       // 2: RcCfgOp
  uint8_t  surf;     // 3: RcSurface
  uint16_t minUs;    // 4..5
  uint16_t midUs;    // 6..7  (RC_CFG_TEST'te: cikisa yazilacak HAM us degeri)
  uint16_t maxUs;    // 8..9
  uint8_t  flags;    // 10: RC_CFGF_* bitleri
  uint8_t  crc;      // 11
};

// ------------------------------------------------------------
// Ucak -> Kumanda, kalibrasyon raporu. Toplam 13 byte.
// Arayuzdeki degerlerin kaynagi budur: ekranda gorunen sey ucagin
// GERCEKTEN uyguladigi ayardir, kumandanin gonderdigi degil.
// ------------------------------------------------------------
struct RcConfigReport {
  uint8_t  magic;    // 0: RC_MAGIC_REP (0x3C)
  uint8_t  ackSeq;   // 1: islenen son RcConfigPacket.seq
  uint8_t  surf;     // 2: hangi cikisin ayari
  uint8_t  flags;    // 3: RC_REPF_* bitleri
  uint16_t minUs;    // 4..5
  uint16_t midUs;    // 6..7
  uint16_t maxUs;    // 8..9
  uint16_t liveUs;   // 10..11: o an bu cikisa yazilan gercek darbe
  uint8_t  crc;      // 12
};

#pragma pack(pop)

// ---- RcTelemetry.status bitleri ----
static const uint8_t RC_STATUS_ARMED    = 0x01;
static const uint8_t RC_STATUS_FAILSAFE = 0x02;
static const uint8_t RC_STATUS_CALIB    = 0x04;  // ucak kalibrasyon modunda
static const uint8_t RC_STATUS_DIRTY    = 0x08;  // kaydedilmemis ayar var

// ---- Kalibrasyon komutlari (RcConfigPacket.op) ----
enum RcCfgOp : uint8_t {
  RC_CFG_NOP     = 0,
  RC_CFG_ENTER   = 1,   // kalibrasyon moduna gir (sadece DISARM iken kabul edilir)
  RC_CFG_EXIT    = 2,   // moddan cik, cikislari notre al
  RC_CFG_SET     = 3,   // surf icin min/mid/max + yon ayarini RAM'e uygula
  RC_CFG_TEST    = 4,   // surf cikisini midUs'taki HAM degere sur (zaman asimli)
  RC_CFG_SWEEP   = 5,   // surf'u min <-> max arasinda yavasca tara
  RC_CFG_CENTER  = 6,   // butun yuzeyler notre, ESC stop
  RC_CFG_SAVE    = 7,   // ayarlari NVS'e yaz
  RC_CFG_LOAD    = 8,   // NVS'ten geri yukle (degisiklikleri at)
  RC_CFG_DEFAULT = 9,   // fabrika degerlerine don (kaydetmez)
  RC_CFG_READ    = 10,  // surf icin rapor iste
  RC_CFG_ESC_HI  = 11,  // ESC gaz araligi kalibrasyonu 1. adim (tam gaz darbesi)
  RC_CFG_ESC_LO  = 12   // ESC gaz araligi kalibrasyonu 2. adim (stop darbesi)
};

// ---- RcConfigPacket.flags ----
static const uint8_t RC_CFGF_REVERSE  = 0x01;  // servo yonu ters
static const uint8_t RC_CFGF_PROP_OFF = 0x02;  // "pervane sokuldu" onayi (ESC_HI icin SART)

// ---- RcConfigReport.flags ----
static const uint8_t RC_REPF_REVERSE = 0x01;  // bu cikisin yonu ters
static const uint8_t RC_REPF_CALIB   = 0x02;  // ucak kalibrasyon modunda
static const uint8_t RC_REPF_DIRTY   = 0x04;  // RAM'deki ayar NVS'tekinden farkli
static const uint8_t RC_REPF_REJECT  = 0x08;  // son komut REDDEDILDI (gecersiz/izinsiz)

// ---- CRC-8 (Dallas/Maxim, poly 0x31, init 0xFF) ----
static inline uint8_t rcCrc8(const uint8_t* data, uint8_t len) {
  uint8_t crc = 0xFF;
  for (uint8_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t b = 0; b < 8; b++) {
      crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
    }
  }
  return crc;
}

static inline uint8_t rcPacketCrc(const RcPacket& p) {
  return rcCrc8(reinterpret_cast<const uint8_t*>(&p), sizeof(RcPacket) - 1);
}
static inline uint8_t rcTelemetryCrc(const RcTelemetry& t) {
  return rcCrc8(reinterpret_cast<const uint8_t*>(&t), sizeof(RcTelemetry) - 1);
}
static inline uint8_t rcConfigCrc(const RcConfigPacket& c) {
  return rcCrc8(reinterpret_cast<const uint8_t*>(&c), sizeof(RcConfigPacket) - 1);
}
static inline uint8_t rcReportCrc(const RcConfigReport& r) {
  return rcCrc8(reinterpret_cast<const uint8_t*>(&r), sizeof(RcConfigReport) - 1);
}

// Paket gecerli mi? (imza + CRC)
static inline bool rcPacketValid(const RcPacket& p) {
  return p.magic == RC_MAGIC_CTRL && p.crc == rcPacketCrc(p);
}
static inline bool rcTelemetryValid(const RcTelemetry& t) {
  return t.magic == RC_MAGIC_TLM && t.crc == rcTelemetryCrc(t);
}
static inline bool rcConfigValid(const RcConfigPacket& c) {
  return c.magic == RC_MAGIC_CFG && c.crc == rcConfigCrc(c);
}
static inline bool rcReportValid(const RcConfigReport& r) {
  return r.magic == RC_MAGIC_REP && r.crc == rcReportCrc(r);
}

// us degerini kumanda araligina (1000..2000) sikistirir
static inline uint16_t rcClampUs(int32_t us) {
  if (us < RC_US_MIN) return RC_US_MIN;
  if (us > RC_US_MAX) return RC_US_MAX;
  return (uint16_t)us;
}

// us degerini servonun mekanik guvenli araligina sikistirir.
// Kalibrasyon uc noktalari 1000..2000'in bir miktar disina cikabilir;
// bu fonksiyon dayanaga vurmayi engelleyen son kapidir.
static inline uint16_t rcClampHard(int32_t us) {
  if (us < RC_US_HARD_MIN) return RC_US_HARD_MIN;
  if (us > RC_US_HARD_MAX) return RC_US_HARD_MAX;
  return (uint16_t)us;
}

// Derleme aninda boyut kilidi. Bir yapiya alan eklerken bu satirlar seni
// uyandirir.
//
// DIKKAT: surum 3'te RcPacket 12 byte'a cikti ve RcConfigPacket ile AYNI
// boyutta. Bu yuzden alici taraf artik tipi BOYUTTAN degil MAGIC'ten ayirt
// eder; boyut yalnizca dogrulama icin kullanilir. Iki yonde de her tipin
// kendi imzasi var, dolayisiyla belirsizlik yok.
static_assert(sizeof(RcPacket)       == 12, "RcPacket boyutu degisti");
static_assert(sizeof(RcTelemetry)    ==  8, "RcTelemetry boyutu degisti");
static_assert(sizeof(RcConfigPacket) == 12, "RcConfigPacket boyutu degisti");
static_assert(sizeof(RcConfigReport) == 13, "RcConfigReport boyutu degisti");
static_assert(RC_MAGIC_CTRL != RC_MAGIC_CFG, "kumanda->ucak imzalari cakisiyor");
static_assert(RC_MAGIC_TLM  != RC_MAGIC_REP, "ucak->kumanda imzalari cakisiyor");
