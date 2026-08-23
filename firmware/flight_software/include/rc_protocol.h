// ============================================================
// rc_protocol.h - Kumanda <-> Ucak ortak telsiz protokolu
// BU DOSYA IKI PROJEDE DE AYNI OLMALI.
// (controller_software/include/ ve flight_software/include/)
// Bir tarafta degistirdiginde digerine de kopyala.
// ============================================================
#pragma once
#include <stdint.h>

// ---- Protokol kimligi ----
static const uint8_t RC_MAGIC     = 0xA5;  // paket basi imzasi
static const uint8_t RC_PROTO_VER = 1;     // flags ust nibble'inda tasinir

// ---- Kanal degerleri (mikrosaniye, RC standardi) ----
static const uint16_t RC_US_MIN = 1000;
static const uint16_t RC_US_MID = 1500;
static const uint16_t RC_US_MAX = 2000;

// ---- Kanal indeksleri (RcPacket.ch[] icin) ----
enum RcChannel : uint8_t {
  RC_CH_THROTTLE = 0,  // 1000 = stop, 2000 = tam gaz
  RC_CH_AILERON  = 1,  // 1000 = sol,  1500 = notr, 2000 = sag
  RC_CH_ELEVATOR = 2,  // 1000 = burun asagi, 1500 = notr, 2000 = burun yukari
  RC_CH_RUDDER   = 3,  // 1000 = sol,  1500 = notr, 2000 = sag
  RC_CH_COUNT    = 4
};

// ---- flags bitleri ----
static const uint8_t RC_FLAG_ARMED = 0x01;  // 1 = motor calismasina izin var
// 0x02, 0x04, 0x08 ileride kullanilmak uzere bos (flap, mod, isik...)
// Ust nibble (0xF0) = protokol versiyonu

// ---- Telsiz ayarlari (iki tarafta ayni olmali) ----
static const uint8_t RC_RF_CHANNEL   = 108;      // 2.508 GHz - WiFi bandinin ustu
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
static const uint16_t RC_TX_PERIOD_MS  = 20;   // verici 50 Hz gonderir
static const uint16_t RC_FAILSAFE_MS   = 500;  // alicida bu sure paket yoksa failsafe

#pragma pack(push, 1)

// Kumanda -> Ucak. Toplam 12 byte.
struct RcPacket {
  uint8_t  magic;              // 0: RC_MAGIC (0xA5)
  uint8_t  seq;                // 1: her pakette 1 artar, 255'ten sonra 0'a doner
  uint16_t ch[RC_CH_COUNT];    // 2..9: little-endian, 1000..2000 us
  uint8_t  flags;              // 10: bit0 = ARMED, ust nibble = versiyon
  uint8_t  crc;                // 11: onceki 11 byte'in CRC-8'i
};

// Ucak -> Kumanda (ACK payload ile geri doner). Toplam 8 byte.
struct RcTelemetry {
  uint8_t  magic;    // 0: RC_MAGIC
  uint8_t  seqEcho;  // 1: en son alinan RcPacket.seq
  uint16_t vbatMv;   // 2..3: batarya gerilimi (mV). Olcum yoksa 0.
  uint16_t lossPct;  // 4..5: alicinin gordugu paket kaybi yuzdesi (0..100)
  uint8_t  status;   // 6: bit0 = armed, bit1 = failsafe aktif
  uint8_t  crc;      // 7: onceki 7 byte'in CRC-8'i
};

#pragma pack(pop)

static const uint8_t RC_STATUS_ARMED    = 0x01;
static const uint8_t RC_STATUS_FAILSAFE = 0x02;

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

// Paket gecerli mi? (imza + CRC)
static inline bool rcPacketValid(const RcPacket& p) {
  return p.magic == RC_MAGIC && p.crc == rcPacketCrc(p);
}

static inline bool rcTelemetryValid(const RcTelemetry& t) {
  return t.magic == RC_MAGIC && t.crc == rcTelemetryCrc(t);
}

// us degerini guvenli araliga sikistirir
static inline uint16_t rcClampUs(int32_t us) {
  if (us < RC_US_MIN) return RC_US_MIN;
  if (us > RC_US_MAX) return RC_US_MAX;
  return (uint16_t)us;
}
