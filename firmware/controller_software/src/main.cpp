// ============================================================
// RC Plane - KUMANDA (verici) yazilimi
// Kart: ESP32 DevKit (ESP-WROOM-32) + nRF24L01
//
// Ne yapar:
//   1. WiFi erisim noktasi acar  -> SSID: RC-Plane-TX
//   2. 192.168.4.1 adresinde web arayuzu sunar (sliderlar ile manuel kumanda)
//   3. Web'den gelen degerleri 50 Hz'de nRF24 uzerinden ucaga gonderir
//   4. ACK payload ile ucaktan telemetri okur ve arayuzde gosterir
//
// GUVENLIK:
//   - ARM edilmeden gaz gitmez (throttle her zaman 1000 us).
//   - ARM icin gaz kolunun minimumda olmasi sart.
//   - ARM icin ucaktan TAZE TELEMETRI gelmesi sart. Ucak cevap vermiyorsa
//     arayuz ARMED gostermez; "arm ettim ama ucak duymadi" durumu olusamaz.
//   - Ucaktan 1 sn telemetri gelmezse otomatik DISARM.
//   - Tarayici 1 sn boyunca komut gondermezse otomatik DISARM.
//   - THROTTLE_MAX_US ile gaz tavani sinirlanir (asagiya bak).
//
// KUMANDA DURUMU != UCAK DURUMU. Ikisi arayuzde ayri gosterilir. Ucak,
// boot'ta ve her failsafe'ten sonra arm kilitli gelir: kumandadan ARMED=0
// bir paket gormeden arm olmaz. Bu yuzden kumanda "ARMED" derken ucak
// "DISARM" diyebilir; o zaman DISARM'a basip tekrar ARM etmek gerekir.
// ============================================================

#include <Arduino.h>
#include <SPI.h>
#include <RF24.h>
#include <WiFi.h>
#include <WebServer.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "rc_protocol.h"

// ------------------------------------------------------------
// IKI TASIMA KATMANI
//
// Ayni RcPacket iki yoldan da gidebilir:
//   nRF24  - ayri modul, SPI, 2.508 GHz
//   ESP-NOW- kartin kendi WiFi radyosu, ek donanim YOK
//
// ESP-NOW yayin (broadcast) adresi kullanir, yani iki kartin MAC adresini
// birbirine tanitmak gerekmez - ac ve calisir. nRF24 kablolamasi/modulu
// sorunluysa sistem yine ucar; ikisi birlikte aciksa yedekli link olur.
//
// Ucak tarafinda da ayni iki katman acik olmali (flight_software).
// ------------------------------------------------------------
static const bool USE_NRF24  = true;
static const bool USE_ESPNOW = true;

// ------------------------------------------------------------
// nRF24L01 pinleri - semadaki baglanti
//   VCC  -> 3V3    (kirmizi)   ** 5V DEGIL **
//   GND  -> GND    (siyah)
//   CE   -> GPIO4  (mavi)
//   CSN  -> GPIO5  (magenta)
//   SCK  -> GPIO18 (turuncu)
//   MOSI -> GPIO23 (camgobegi)
//   MISO -> GPIO19 (yesil)
//   IRQ  -> bagli degil (kullanilmiyor)
//
// nRF24 konnektoru 2x4'tur ve 1. bacak GND, 2. bacak VCC'dir. Konnektoru bir
// sira kaydirmak cok kolay ve VCC/GND ters gelirse modul yanar - taktiktan
// sonra module dokun, ilikse hemen cek.
//
// VCC-GND arasina 10-100uF kondansator SART. Modul yayin aninda akim tepesi
// cekiyor; kartin 3V3 regulatoru bunu karsilamazsa modul brown-out olur ve
// radio.begin() bazen gecer bazen gecmez.
// ------------------------------------------------------------
static const uint8_t PIN_CE   = 4;
static const uint8_t PIN_CSN  = 5;
static const uint8_t PIN_SCK  = 18;
static const uint8_t PIN_MISO = 19;
static const uint8_t PIN_MOSI = 23;

// SPI hizi. Kutuphane varsayilani 10 MHz; dupont kablo + breadboard ile
// bu hiz cogu zaman guvenilir degil. 4 MHz her kurulumda sorunsuz calisir.
// Hangi hizin saglam oldugunu ogrenmek icin: pio run -e rfdiag -t upload -t monitor
static const uint32_t RF_SPI_HZ = 4000000;

// ------------------------------------------------------------
// GAZ TAVANI
// Pervane takili degilken / ilk testlerde 1200'de birak.
// Ucusa hazir oldugunda 2000 yap.
// ------------------------------------------------------------
static const uint16_t THROTTLE_MAX_US = 2000;

// ------------------------------------------------------------
// TEZGAH MODU  --  UCUSTAN ONCE MUTLAKA false YAP
//
// Normalde ARM icin ucaktan TAZE TELEMETRI sart. Bu, "kumandada ARMED
// yaziyor ama ucak komutu hic duymadi" durumunu imkansiz kilan kilit.
// Tezgahta telemetri yolu bozukken (ornegin ucagin 3.3V hatti yayin
// aninda cokuyorken) bu kilit ARM etmeyi tamamen engelliyor.
//
// true iken:
//   - ARM, telemetri olmadan da kabul edilir
//   - "telemetri yok" gerekcesiyle otomatik DISARM devre disi kalir
// Degismeyenler: gaz kolu 1000'de olma sarti, gaz tavani, web watchdog,
// ucak tarafindaki arm kilidi ve failsafe. Yani motor yine de ancak ucak
// paketleri gercekten aliyorsa doner.
//
// PERVANE TAKILIYKEN VEYA UCUSTA ASLA true BIRAKMA. Acikken hem boot
// logunda hem her saniye [TX] satirinda hem de web arayuzunde uyari cikar.
// ------------------------------------------------------------
static const bool TEZGAH_MODU = true;

// ------------------------------------------------------------
// WiFi erisim noktasi
// AP kanali 1 = 2.412 GHz, nRF24 kanali 108 = 2.508 GHz. Aralarinda 96 MHz
// var, yani WiFi trafigi telsiz linkini bozmuyor. RC_RF_CHANNEL'i
// degistirirsen bu kanali da gozden gecir - ust uste binmemeliler.
// ------------------------------------------------------------
static const char*   AP_SSID     = "RC-Plane-TX";
static const char*   AP_PASS     = "rcplane1234";  // en az 8 karakter
static const uint8_t AP_CHANNEL  = 1;
static const uint8_t AP_MAX_CONN = 1;              // tek kumanda cihazi baglanabilir

// Tarayici bu sure boyunca komut gondermezse guvenli moda gec
static const uint32_t WEB_TIMEOUT_MS = 1000;

// Telsiz bulunamadiysa bu araliklarla yeniden dene
static const uint32_t RF_RETRY_MS = 3000;

// Bir gonderim bu sureden uzun surerse takilmis kabul edilip FIFO temizlenir.
// setRetries(3,5) en kotu halde ~12 ms surer, 15 ms guvenli ust sinir.
static const uint32_t TX_TIMEOUT_US = 15000;

// Ucaktan bu sure boyunca gecerli telemetri gelmezse baglanti yok sayilir.
// Telemetri ACK payload ile geliyor: gelmiyorsa ucak paketleri almiyor demek.
// Ucak 50 Hz'de cevap verdigi icin 1 sn = 50 kacirilmis ACK, fazlasiyla tolere.
static const uint32_t TLM_TIMEOUT_MS = 1000;

// ------------------------------------------------------------
RF24 radio(PIN_CE, PIN_CSN, RF_SPI_HZ);
WebServer server(80);

// rc_protocol.h'daki sayisal kodu kutuphanenin enum'una cevirir.
// Hiz ortak baslikta tutuluyor ki iki taraf ayrisamasin.
static rf24_datarate_e rfVeriHizi() {
  return RC_RF_DATARATE == 0 ? RF24_1MBPS
       : RC_RF_DATARATE == 1 ? RF24_2MBPS
                             : RF24_250KBPS;
}
static const char* rfHizAdi(rf24_datarate_e d) {
  return d == RF24_1MBPS ? "1Mbps" : d == RF24_2MBPS ? "2Mbps" : "250kbps";
}

struct ControlState {
  uint16_t thr = RC_US_MIN;
  uint16_t ail = RC_US_MID;
  uint16_t ele = RC_US_MID;
  uint16_t rud = RC_US_MID;
  bool     armed = false;

  // Tarayicinin ISTEDIGI gaz - maskelenmemis hali. DISARM'dayken ctrl.thr
  // her zaman 1000'e kirpilir, bu yuzden gaz kolunu tezgahta oynattiginda
  // logda hicbir sey degismis gibi gorunuyordu. Bu alan istegi oldugu gibi
  // saklar; sadece teshis/log icin, telsize giden pakete girmez.
  uint16_t thrIstek = RC_US_MIN;
};

static ControlState ctrl;
static uint8_t      txSeq = 0;
static uint32_t     lastTxMs = 0;
static uint32_t     lastWebMs = 0;
static uint32_t     lastStatMs = 0;
static uint32_t     lastRfRetryMs = 0;
static bool         radioOk = false;

// Ayrintili nRF24 hata metnini 3 sn'de bir tekrar basmamak icin.
// Durum zaten her saniye [TX] satirinda gorunuyor.
static bool         nrfHataBasildi = false;

// Asenkron gonderim durumu (bkz. radioTick / radioPoll)
static bool     txPending = false;
static uint32_t txStartUs = 0;
static uint16_t txStuck   = 0;   // takilip iptal edilen paket sayisi

// ESP-NOW durumu
static bool     espnowOk = false;
static uint8_t  BCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Baglanti kalitesi (1 sn'lik pencere)
static uint16_t winSent = 0, winAcked = 0;
static uint8_t  linkQuality = 0;   // %

// Tarayicidan saniyede kac komut geldi. Telsiz linki kusursuzken bile arayuz
// olu olabiliyor (tarayici istegi hic gondermiyorsa) ve logda bunu ayirt
// etmenin yolu yoktu. web=0/s gorursen sorun telsizde degil, tarayicida.
static uint16_t winWeb = 0, webHz = 0;

// Ucaktan gelen son telemetri
static RcTelemetry tlm = {};
static bool     tlmFresh = false;
static uint32_t lastTlmMs = 0;

// ============================================================
// Telsiz
// ============================================================
static bool radioSetup() {
  if (!USE_NRF24) return false;

  // SPI pinlerini acikca ver. VSPI varsayilani zaten bunlar ama acik yazmak
  // hem belge gorevi goruyor hem de baska bir kutuphane SPI'i farkli
  // pinlerle baslatmissa onu eziyor.
  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CSN);
  delay(10);   // modulun power-on-reset'i (datasheet: 4.5 ms + 14 us)

  // Ilk deneme besleme oturmadan once denk gelebiliyor -> 3 kez dene
  bool up = false;
  for (uint8_t i = 0; i < 3 && !up; i++) {
    up = radio.begin();
    if (!up) delay(100);
  }

  if (!up) {
    // Ayrinti sadece ilk basarisizlikta basilir; sonrasinda RF_RETRY_MS'de bir
    // denendigi icin log akip gitmesin. Durum zaten [TX] satirinda gorunuyor.
    if (!nrfHataBasildi) {
      nrfHataBasildi = true;
      Serial.println("[RF] nRF24 BAGLI DEGIL! SPI'dan gecerli cevap gelmiyor.");
      Serial.println("[RF] Kontrol: 3V3 beslemesi (5V DEGIL), VCC-GND arasi 10-100uF,");
      Serial.printf ("[RF]          SCK=%u MISO=%u MOSI=%u CSN=%u CE=%u kablolari.\n",
                     PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CSN, PIN_CE);
      Serial.printf ("[RF] %lu ms'de bir yeniden denenecek. Teshis: "
                     "pio run -e rfdiag -t upload -t monitor\n",
                     (unsigned long)RF_RETRY_MS);
    }
    radioOk   = false;
    txPending = false;
    return false;
  }
  nrfHataBasildi = false;

  radio.setPALevel(RF24_PA_HIGH);       // modul resetleniyorsa RF24_PA_LOW dene

  // setDataRate() ayari yazip GERI OKUR; tutmadiysa false doner. Donus degeri
  // kontrol edilmezse klon cip sessizce 1 Mbps'te kalir ve link olur.
  const bool hizOk = radio.setDataRate(rfVeriHizi());

  radio.setChannel(RC_RF_CHANNEL);
  radio.setCRCLength(RF24_CRC_16);
  radio.setRetries(3, 5);               // 3*250us gecikme, 5 tekrar
  radio.enableDynamicPayloads();
  radio.enableAckPayload();             // ucak telemetriyi ACK ile geri gonderir
  radio.openWritingPipe(RC_RF_ADDRESS);
  radio.stopListening();                // verici modu
  radio.flush_tx();
  radio.flush_rx();
  radio.clearStatusFlags(RF24_IRQ_ALL);

  txPending = false;
  radioOk   = true;

  Serial.printf("[RF] nRF24 BAGLI. Kanal %u, adres %s, paket %u byte, SPI %lu Hz\n",
                RC_RF_CHANNEL, (const char*)RC_RF_ADDRESS, (unsigned)sizeof(RcPacket),
                (unsigned long)RF_SPI_HZ);

  // Yazdigimiz degil, cipin GERCEKTEN kabul ettigi ayarlar. Iki tarafin bu
  // satirlari birebir ayni degilse link kurulmaz - modul "BAGLI" olsa bile.
  Serial.printf("[RF] Dogrulama: p-variant=%s  hiz=%s  kanal=%u\n",
                radio.isPVariant() ? "EVET (gercek nRF24L01+)"
                                   : "HAYIR (klon olabilir)",
                rfHizAdi(radio.getDataRate()), radio.getChannel());
  if (!hizOk) {
    Serial.printf("[RF] !! VERI HIZI AYARLANAMADI: %s istendi, cip %s'te kaldi.\n",
                  rfHizAdi(rfVeriHizi()), rfHizAdi(radio.getDataRate()));
    Serial.println("[RF]    Bu modul klon. rc_protocol.h'da RC_RF_DATARATE = 0 yap");
    Serial.println("[RF]    ve IKI PROJEYI DE yeniden yukle.");
  }
  return true;
}

// nRF24 SU AN gercekten bagli mi? isChipConnected() canli bir register okumasi
// yapar (SETUP_AW okunup gecerli araliktalik kontrolu), yani boot'taki bayraga
// degil o anki duruma bakar. Kablo calisirken cikarsa yakalar.
static bool nrfBagli() {
  return radioOk && radio.isChipConnected();
}

// Hangi yoldan geldigi onemli degil: gecerli telemetri tek noktadan islenir.
// Ucak her aldigi paket icin bir telemetri dondurdugu icin bu ayni zamanda
// link kalitesi sayacidir.
static void telemetriUygula(const RcTelemetry& t) {
  if (!rcTelemetryValid(t)) return;
  tlm       = t;
  tlmFresh  = true;
  lastTlmMs = millis();
  winAcked++;
}

// ACK payload ile gelen telemetriyi oku (nRF24 yolu)
static void telemetryRead() {
  while (radio.available()) {
    const uint8_t len = radio.getDynamicPayloadSize();
    if (len != sizeof(RcTelemetry)) {   // beklenmeyen boy -> FIFO'yu bosalt
      radio.flush_rx();
      return;
    }
    RcTelemetry t;
    radio.read(&t, sizeof(t));
    telemetriUygula(t);
  }
}

// ------------------------------------------------------------
// ESP-NOW
// ------------------------------------------------------------

// WiFi gorevi baglaminda cagrilir - kisa tutulmali, sadece dogrula ve yaz.
static void onEspNowRecv(const uint8_t* mac, const uint8_t* data, int len) {
  (void)mac;
  if (len != sizeof(RcTelemetry)) return;
  RcTelemetry t;
  memcpy(&t, data, sizeof(t));
  telemetriUygula(t);
}

// softAP acildiktan SONRA cagrilmali (WiFi surucusu ayakta olmali).
static bool espnowSetup() {
  if (!USE_ESPNOW) return false;

  if (esp_now_init() != ESP_OK) {
    Serial.println("[NOW] esp_now_init basarisiz.");
    return false;
  }
  esp_now_register_recv_cb(onEspNowRecv);

  // Yayin adresini es olarak ekle. Sifreleme yok: sifreli ES-NOW yayin
  // desteklemiyor, zaten pakette CRC + magic dogrulamasi var.
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, BCAST, 6);
  peer.channel = AP_CHANNEL;
  peer.ifidx   = WIFI_IF_AP;   // sadece AP arayuzu acik
  peer.encrypt = false;

  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("[NOW] yayin esi eklenemedi.");
    return false;
  }

  Serial.printf("[NOW] Hazir. Yayin modu, kanal %u, MAC %s\n",
                AP_CHANNEL, WiFi.softAPmacAddress().c_str());
  return true;
}

// ------------------------------------------------------------
// Bekleyen gonderimi cozer. loop()'ta her turda cagrilir.
//
// Neden bloklamayan gonderim: radio.write() ACK gelene ya da tum tekrarlar
// tukenene kadar bekliyor. Link koptugunda bu, 20 ms'lik butcenin ~12 ms'sini
// yiyor ve web arayuzunun cevap suresini bozuyor -- yani tam da kontrole en
// cok ihtiyac duyulan anda kumanda agirlasiyor. startWrite() + STATUS
// yoklamasiyla gonderim arka planda ilerliyor, HTTP istekleri aksamiyor.
// ------------------------------------------------------------
static void radioPoll() {
  if (!radioOk || !txPending) return;

  const uint8_t f = radio.update();   // NOP gonderir, STATUS'u tazeler

  if (f & (RF24_TX_DS | RF24_TX_DF)) {
    if (f & RF24_TX_DS) {             // ACK geldi -> paket ucaga ulasti
      telemetryRead();                // winAcked burada artiyor
    }
    radio.clearStatusFlags(RF24_IRQ_ALL);
    txPending = false;
    return;
  }

  if (micros() - txStartUs > TX_TIMEOUT_US) {   // takildi, kurtar
    radio.flush_tx();
    radio.clearStatusFlags(RF24_IRQ_ALL);
    txPending = false;
    txStuck++;
  }
}

// 20 ms'de bir bir paket olusturup ACIK OLAN TUM yollardan gonderir.
// Iki yol da acikken ucak ayni paketi iki kez alabilir; zararsiz, cunku
// ucak tarafi ayni seq'i tekrar islemeyi tolere ediyor (son deger kazanir).
static void radioTick() {
  RcPacket p;
  p.magic = RC_MAGIC;
  p.seq   = txSeq++;
  p.ch[RC_CH_THROTTLE] = ctrl.armed ? ctrl.thr : RC_US_MIN;
  p.ch[RC_CH_AILERON]  = ctrl.ail;
  p.ch[RC_CH_ELEVATOR] = ctrl.ele;
  p.ch[RC_CH_RUDDER]   = ctrl.rud;
  p.flags = (uint8_t)((RC_PROTO_VER << 4) | (ctrl.armed ? RC_FLAG_ARMED : 0));
  p.crc   = rcPacketCrc(p);

  if (!radioOk && !espnowOk) return;   // gidecek yol yok
  winSent++;

  // --- ESP-NOW: yayin, bloklamaz, ~50 us surer
  if (espnowOk) {
    esp_now_send(BCAST, (const uint8_t*)&p, sizeof(p));
  }

  // --- nRF24: asenkron gonderim
  if (radioOk) {
    if (txPending) {   // onceki bitmediyse zorla kapat, 50 Hz cadansi kaybetme
      radio.flush_tx();
      radio.clearStatusFlags(RF24_IRQ_ALL);
      txPending = false;
      txStuck++;
    }
    txStartUs = micros();
    txPending = radio.startWrite(&p, sizeof(p), false);
  }
}

// ============================================================
// Guvenlik
// ============================================================

// Ucaktan taze telemetri geliyor mu? Gelmiyorsa ucakla baglanti YOK demektir;
// radio.write() true donse bile (ACK var, payload yok) buna guvenilmez.
static bool telemetriTaze() {
  return tlmFresh && (millis() - lastTlmMs < TLM_TIMEOUT_MS);
}

// Ucagin KENDI bildirdigi durum. Telemetri yoksa ikisi de false.
// Bunlar kumandanin yerel ctrl.armed degeriyle karistirilmamali.
static bool ucakArmed()    { return telemetriTaze() && (tlm.status & RC_STATUS_ARMED); }
static bool ucakFailsafe() { return telemetriTaze() && (tlm.status & RC_STATUS_FAILSAFE); }

static void disarm(const char* sebep) {
  if (ctrl.armed) Serial.printf("[SAFE] DISARM: %s\n", sebep);
  ctrl.armed = false;
  ctrl.thr   = RC_US_MIN;
}

static void neutralSurfaces() {
  ctrl.ail = RC_US_MID;
  ctrl.ele = RC_US_MID;
  ctrl.rud = RC_US_MID;
}

// ============================================================
// Web arayuzu
// ============================================================
static const char PAGE_INDEX[] PROGMEM = R"rawliteral(
<meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no">
<title>RC Plane Kumanda</title>
<style>
  :root{--bg:#11151c;--card:#1b212b;--line:#2c3542;--fg:#e6edf5;--dim:#8b98a8;
        --ok:#3ddc84;--warn:#ffb020;--bad:#ff4d4f;--acc:#4c9aff}
  *{box-sizing:border-box}
  body{margin:0;padding:14px;background:var(--bg);color:var(--fg);
       font:15px/1.4 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
  h1{font-size:17px;margin:0 0 12px;letter-spacing:.4px}
  .card{background:var(--card);border:1px solid var(--line);border-radius:12px;
        padding:14px;margin-bottom:12px}
  .row{display:flex;justify-content:space-between;align-items:center;margin-bottom:6px}
  .lbl{font-size:13px;color:var(--dim);text-transform:uppercase;letter-spacing:.6px}
  .val{font-variant-numeric:tabular-nums;font-weight:600}
  /* touch-action:none SART. Bu satir olmadan telefonda parmagi surunce
     tarayici hareketi "sayfa kaydirma" sanip pointercancel firlatiyor,
     asagidaki yayli kol mantigi bunu birakma sanip kolu 1500'e geri
     civiliyordu - yani kumanda yuzeyleri hic oynatilamiyordu. */
  input[type=range]{width:100%;height:38px;background:transparent;
       -webkit-appearance:none;touch-action:none}
  input[type=range]::-webkit-slider-runnable-track{height:8px;border-radius:4px;background:var(--line)}
  input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:34px;height:34px;
        margin-top:-13px;border-radius:50%;background:var(--acc);border:3px solid #0e1218}
  input[type=range]::-moz-range-track{height:8px;border-radius:4px;background:var(--line)}
  input[type=range]::-moz-range-thumb{width:30px;height:30px;border-radius:50%;
        background:var(--acc);border:3px solid #0e1218}
  #thr::-webkit-slider-thumb{background:var(--warn)}
  #thr::-moz-range-thumb{background:var(--warn)}
  button{width:100%;padding:16px;font-size:16px;font-weight:700;border:0;
         border-radius:10px;color:#0e1218;cursor:pointer;letter-spacing:.5px;
         touch-action:manipulation}
  .arm{background:var(--ok)}
  .armed{background:var(--warn)}
  .stop{background:var(--bad);color:#fff;margin-top:10px}
  .grid{display:grid;grid-template-columns:repeat(2,1fr);gap:8px 14px;font-size:13px}
  .grid div{display:flex;justify-content:space-between}
  .pill{padding:2px 8px;border-radius:99px;font-size:12px;font-weight:700}
  .p-ok{background:rgba(61,220,132,.15);color:var(--ok)}
  .p-bad{background:rgba(255,77,79,.15);color:var(--bad)}
  .p-warn{background:rgba(255,176,32,.15);color:var(--warn)}
  .hint{font-size:12px;color:var(--dim);margin-top:8px}
  .banner{border-radius:10px;padding:11px 13px;margin-bottom:12px;
          font-size:13px;font-weight:700;line-height:1.35;
          background:rgba(255,77,79,.15);border:1px solid var(--bad);color:var(--bad)}
</style>

<h1>RC PLANE - MANUEL KUMANDA</h1>

<div id="warn" class="banner" style="display:none"></div>

<div class="card">
  <div class="row"><span class="lbl">Kumanda</span><span id="state" class="pill p-bad">DISARMED</span></div>
  <div class="row"><span class="lbl">Ucak</span><span id="pstate" class="pill p-bad">CEVAP YOK</span></div>
  <div class="grid">
    <div><span class="lbl">Link</span><span id="link" class="val">-</span></div>
    <div><span class="lbl">Telemetri</span><span id="tlm" class="val">-</span></div>
    <div><span class="lbl">Batarya</span><span id="vbat" class="val">-</span></div>
    <div><span class="lbl">Gaz tavani</span><span id="tmax" class="val">-</span></div>
    <div><span class="lbl">Telsiz yolu</span><span id="tx" class="val">-</span></div>
  </div>
  <div class="hint">Kumanda ve ucak durumu ayri gosterilir. ARM icin ikisinin de
  ARMED olmasi gerekir; sadece kumanda ARMED ise ucak komutu duymamis demektir.</div>
</div>

<div class="card">
  <div class="row"><span class="lbl">Gaz (Throttle)</span><span class="val" id="vthr">1000</span></div>
  <input type="range" id="thr" min="1000" max="2000" step="5" value="1000">

  <div class="row"><span class="lbl">Aileron (Roll)</span><span class="val" id="vail">1500</span></div>
  <input type="range" id="ail" min="1000" max="2000" step="5" value="1500" data-spring="1">

  <div class="row"><span class="lbl">Elevator (Pitch)</span><span class="val" id="vele">1500</span></div>
  <input type="range" id="ele" min="1000" max="2000" step="5" value="1500" data-spring="1">

  <div class="row"><span class="lbl">Rudder (Yaw)</span><span class="val" id="vrud">1500</span></div>
  <input type="range" id="rud" min="1000" max="2000" step="5" value="1500" data-spring="1">

  <div class="hint">Kumanda yuzeyleri birakinca notre (1500) doner. Gaz kolu kalir.</div>
</div>

<div class="card">
  <button id="armBtn" class="arm">ARM</button>
  <button id="stopBtn" class="stop">ACIL DURDURMA</button>
  <div class="hint">ARM icin gaz kolu 1000'de olmali VE ucaktan telemetri geliyor
  olmali. Sayfa kapanirsa, ucak susarsa veya baglanti koparsa kumanda 1 saniye
  icinde otomatik DISARM olur.</div>
</div>

<script>
const $ = id => document.getElementById(id);
const sliders = {thr:$('thr'), ail:$('ail'), ele:$('ele'), rud:$('rud')};
const labels  = {thr:$('vthr'), ail:$('vail'), ele:$('vele'), rud:$('vrud')};
let armed = false;

// Yoldaki istek durumu. busy'yi tek basina bir bayrak olarak tutmak
// tehlikeliydi: bir fetch takilirsa .finally hic calismiyor, busy sonsuza
// kadar true kaliyor ve arayuz tamamen susuyordu - telsiz linki %100 iken
// bile kumandaya tek komut gitmiyor. Simdi hem zaman asimi hem abort var.
let inflight = false, inflightSince = 0;

// ARM/DISARM niyeti icin surum sayaci. Kullanici tusa bastiginda artar;
// bu istekten ONCE yola cikmis bir cevap geri geldiginde niyeti ezmesin.
let armEpoch = 0;

const REQ_TIMEOUT_MS = 400;

function paint(){ for(const k in sliders) labels[k].textContent = sliders[k].value; }

for(const k in sliders) sliders[k].addEventListener('input', paint);

// Yayli kol davranisi. Olayi window'da dinliyoruz: parmak/fare slider'in
// disinda kaldirilirsa element uzerindeki dinleyici hic tetiklenmiyor ve
// kumanda yuzeyi son degerinde takili kaliyordu - ucusta tehlikeli.
let dragging = null;
for(const k in sliders){
  if(sliders[k].dataset.spring)
    sliders[k].addEventListener('pointerdown', () => dragging = k);
}
function release(){
  if(dragging === null) return;
  sliders[dragging].value = 1500;
  dragging = null;
  paint();
  send();
}
['pointerup','pointercancel','touchend','touchcancel','mouseup','blur']
  .forEach(e => window.addEventListener(e, release));

$('armBtn').onclick = () => {
  if(!armed && sliders.thr.value != 1000){
    alert('Once gaz kolunu 1000\'e cek.');
    return;
  }
  armed = !armed;
  armEpoch++;   // yoldaki eski cevap bu niyeti geri alamasin
  send();       // yetisemezse 100 ms icindeki heartbeat zaten arm=1 tasir
};

$('stopBtn').onclick = () => {
  armed = false;
  armEpoch++;
  sliders.thr.value = 1000;
  for(const k in sliders) if(k!='thr') sliders[k].value = 1500;
  paint();
  const epoch = armEpoch;
  fetch('/stop', {cache:'no-store'})
    .then(r=>r.json()).then(s => apply(s, epoch)).catch(()=>{});
};

function apply(s, epoch){
  // Bu cevap yola ciktiktan sonra ARM/DISARM'a basildiysa sunucunun eski
  // durumu yerel niyeti ezmemeli. Eskiden ezyordu: tusa tam bir cevap
  // gelmeden basarsan armed sessizce false'a doniyor ve "ARM'a bastim,
  // hicbir sey olmadi" oluyordu.
  if(epoch === undefined || epoch === armEpoch) armed = s.armed;

  // Kumandanin kendi durumu
  $('state').textContent = s.armed ? 'ARMED' : 'DISARMED';
  $('state').className = 'pill ' + (s.armed ? 'p-ok' : 'p-bad');
  $('armBtn').textContent = s.armed ? 'DISARM' : 'ARM';
  $('armBtn').className = s.armed ? 'armed' : 'arm';

  // Ucagin telemetride bildirdigi durum - kumandaninkiyle ayni olmayabilir
  const pTxt = !s.rx ? 'CEVAP YOK' : s.pfs ? 'FAILSAFE'
                                   : s.parmed ? 'ARMED' : 'DISARM';
  const pCls = !s.rx ? 'p-bad' : s.pfs ? 'p-warn'
                               : s.parmed ? 'p-ok' : 'p-bad';
  $('pstate').textContent = pTxt;
  $('pstate').className = 'pill ' + pCls;

  $('link').textContent = s.link + '%';
  $('tmax').textContent = s.tmax + ' us';
  $('tlm').textContent = s.rx ? ('kayip %' + s.loss) : 'YOK';
  $('vbat').textContent = s.vbat ? (s.vbat/1000).toFixed(2) + ' V' : '-';

  $('tx').textContent = !s.rf ? 'YOK'
                      : (s.nrf && s.now) ? 'nRF24 + ESP-NOW'
                      : s.nrf ? 'nRF24' : 'ESP-NOW';

  // Tek satirlik, en oncelikli uyari.
  // Tezgah modu her seyin ustunde: guvenlik kilidi kapaliyken bunu
  // ekranda kaybetmek, tam da kaybedilmemesi gereken bilgiyi kaybetmek olur.
  let w = '';
  if(s.tez)            w = 'TEZGAH MODU ACIK - telemetri olmadan ARM ediliyor. ' +
                           'UCUSTAN ONCE main.cpp\'de TEZGAH_MODU = false YAP.';
  else if(!s.rf)       w = 'HICBIR TELSIZ YOLU YOK - ucaga veri gitmiyor. ' +
                           'main.cpp icinde USE_NRF24 / USE_ESPNOW ayarlarina bak.';
  else if(!s.rx)       w = 'UCAKTAN CEVAP YOK - ucak acik mi, ayni yolu kullaniyor mu? ' +
                           'Baglanti kurulmadan ARM edilemez.';
  else if(s.pfs)       w = 'UCAK FAILSAFE MODUNDA - motor durdu, yuzeyler notrde.';
  else if(s.armed && !s.parmed)
                       w = 'UCAK ARM OLMADI - ucakta arm kilidi acik. ' +
                           'DISARM\'a basip tekrar ARM edin.';
  $('warn').textContent = w;
  $('warn').style.display = w ? 'block' : 'none';
}

function send(){
  const now = Date.now();
  // Yoldaki istegi bekle, ama sonsuza kadar degil: takilan tek bir istek
  // arayuzu bir daha konusmaz hale getirmemeli.
  if(inflight && now - inflightSince < REQ_TIMEOUT_MS) return;
  inflight = true;
  inflightSince = now;

  const epoch = armEpoch;
  const q = `/c?t=${sliders.thr.value}&a=${sliders.ail.value}` +
            `&e=${sliders.ele.value}&r=${sliders.rud.value}&arm=${armed?1:0}`;

  // AbortController olmadan fetch'in zaman asimi yok - kart mesgulken istek
  // suresiz asili kalabiliyor. Abort, .finally'nin calismasini garantiler.
  const ac = new AbortController();
  const zamanAsimi = setTimeout(() => ac.abort(), REQ_TIMEOUT_MS);

  fetch(q, {signal: ac.signal, cache: 'no-store'})
    .then(r=>r.json())
    .then(s=>apply(s, epoch))
    .catch(()=>{ $('state').textContent='BAGLANTI YOK';
                 $('state').className='pill p-bad'; })
    .finally(()=>{ clearTimeout(zamanAsimi); inflight = false; });
}

// Klavye: W/S gaz, oklar aileron+elevator, A/D rudder, bosluk acil durdurma
const keyMap = {
  'w':['thr',+25],          's':['thr',-25],
  'ArrowRight':['ail',+25], 'ArrowLeft':['ail',-25],
  'ArrowUp':['ele',+25],    'ArrowDown':['ele',-25],
  'd':['rud',+25],          'a':['rud',-25]
};
document.addEventListener('keydown', e => {
  if(e.key === ' '){ $('stopBtn').click(); e.preventDefault(); return; }
  const m = keyMap[e.key];
  if(!m) return;
  const [k, d] = m;
  sliders[k].value = Math.min(2000, Math.max(1000, +sliders[k].value + d));
  e.preventDefault();
  paint();
});
document.addEventListener('keyup', e => {
  const m = keyMap[e.key];
  if(!m || m[0] === 'thr') return;   // gaz kolu yerinde kalir, yuzeyler notre doner
  sliders[m[0]].value = 1500;
  paint();
});

paint();
setInterval(send, 100);   // 10 Hz heartbeat - ayni zamanda watchdog beslemesi
</script>
)rawliteral";

// Arayuze donen durum JSON'u.
// armed  = KUMANDANIN yerel durumu
// parmed = UCAGIN telemetride bildirdigi durum   <- ikisi farkli olabilir
// pfs    = ucak failsafe'te mi
static String statusJson() {
  const bool rxAlive = telemetriTaze();
  String j = "{";
  j += "\"armed\":";  j += ctrl.armed ? "true" : "false";
  j += ",\"thr\":";   j += ctrl.armed ? ctrl.thr : RC_US_MIN;
  j += ",\"link\":";  j += linkQuality;
  j += ",\"tmax\":";  j += THROTTLE_MAX_US;
  j += ",\"rx\":";    j += rxAlive ? "true" : "false";
  j += ",\"loss\":";  j += rxAlive ? tlm.lossPct : 0;
  j += ",\"vbat\":";  j += rxAlive ? tlm.vbatMv : 0;
  j += ",\"parmed\":"; j += ucakArmed() ? "true" : "false";
  j += ",\"pfs\":";    j += ucakFailsafe() ? "true" : "false";
  j += ",\"rf\":";     j += (radioOk || espnowOk) ? "true" : "false";
  j += ",\"nrf\":";    j += radioOk ? "true" : "false";
  j += ",\"now\":";    j += espnowOk ? "true" : "false";
  j += ",\"tez\":";    j += TEZGAH_MODU ? "true" : "false";
  j += "}";
  return j;
}

static void handleRoot() {
  // Firmware guncellendiginde tarayici cebindeki eski sayfayi kullanmasin.
  // Basligi vermezsek tarayici sayfayi sezgisel olarak onbellege aliyor ve
  // karta yeni kod atsan da eski arayuzle ugrasiyorsun.
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "text/html; charset=utf-8", PAGE_INDEX);
}

static void handleControl() {
  winWeb++;

  uint16_t thr = rcClampUs(server.arg("t").toInt());
  if (thr > THROTTLE_MAX_US) thr = THROTTLE_MAX_US;
  ctrl.thrIstek = thr;   // maskelenmemis hali - sadece log icin

  ctrl.ail = rcClampUs(server.arg("a").toInt());
  ctrl.ele = rcClampUs(server.arg("e").toInt());
  ctrl.rud = rcClampUs(server.arg("r").toInt());

  bool wantArm = (server.arg("arm").toInt() == 1);

  if (wantArm && !ctrl.armed) {
    // ARM sadece gaz minimumdayken kabul edilir
    if (thr > RC_US_MIN + 10) {
      wantArm = false;
      Serial.println("[SAFE] ARM reddedildi: gaz minimumda degil.");
    }
    // ...ve ucakla gercekten baglanti varken. Telemetri yoksa ucagin
    // paketleri aldigina dair hicbir kanit yok; korlemesine arm etme.
    // Tezgah modunda bu kilit bilerek atlanir (bkz. TEZGAH_MODU).
    else if (!telemetriTaze() && !TEZGAH_MODU) {
      wantArm = false;
      Serial.println("[SAFE] ARM reddedildi: ucaktan telemetri yok.");
    }
    else if (!telemetriTaze()) {
      Serial.println("[SAFE] ARMED -- TEZGAH MODU: telemetri YOK, ucagin "
                     "komutu aldigina dair kanit yok!");
    }
    else {
      Serial.println("[SAFE] ARMED");
    }
  }

  ctrl.armed = wantArm;
  ctrl.thr   = ctrl.armed ? thr : RC_US_MIN;
  lastWebMs  = millis();

  server.send(200, "application/json", statusJson());
}

static void handleStop() {
  disarm("web ACIL DURDURMA");
  neutralSurfaces();
  lastWebMs = millis();
  server.send(200, "application/json", statusJson());
}

static void handleStatus() {
  server.send(200, "application/json", statusJson());
}

// ============================================================
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== RC Plane kumanda basliyor ===");

  if (TEZGAH_MODU) {
    Serial.println("\n*****************************************************");
    Serial.println("*  TEZGAH MODU ACIK - telemetri olmadan ARM edilir. *");
    Serial.println("*  UCUSTAN ONCE main.cpp'de TEZGAH_MODU = false YAP. *");
    Serial.println("*****************************************************\n");
  }

  if (THROTTLE_MAX_US < RC_US_MAX) {
    Serial.printf("[!] GAZ TAVANI %u us ile sinirli (test modu).\n"
                  "    Ucus icin main.cpp'de THROTTLE_MAX_US = 2000 yap.\n",
                  THROTTLE_MAX_US);
  }

  radioSetup();

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS, AP_CHANNEL, false, AP_MAX_CONN);
  WiFi.setSleep(false);   // guc tasarrufu kapali -> HTTP gecikmesi dusuk kalir
  Serial.printf("[WiFi] AP: %s / %s  kanal %u  ->  http://%s\n",
                AP_SSID, AP_PASS, AP_CHANNEL, WiFi.softAPIP().toString().c_str());

  espnowOk = espnowSetup();   // softAP ayakta olmali, bu yuzden buradan sonra

  if (!radioOk && !espnowOk) {
    Serial.println("[!] HICBIR TELSIZ YOLU ACIK DEGIL - ucaga veri gitmiyor.");
  }

  server.on("/", handleRoot);
  server.on("/c", handleControl);
  server.on("/stop", handleStop);
  server.on("/status", handleStatus);
  // Tarayici her sayfada favicon ister; onNotFound'a birakirsak koca HTML'i
  // bir daha gonderiyoruz. Bos cevapla kes.
  server.on("/favicon.ico", []() { server.send(204, "image/x-icon", ""); });
  server.onNotFound(handleRoot);
  server.begin();

  lastWebMs = millis();
  Serial.println("[WEB] Sunucu hazir.");
}

void loop() {
  server.handleClient();
  radioPoll();               // bekleyen gonderimi coz (bloklamaz)

  const uint32_t now = millis();

  // nRF24 bagli degilse periyodik olarak yeniden dene. Kabloyu duzeltince
  // kart resetlemeye gerek kalmadan durum satiri BAGLI'ya doner.
  // (radioSetup basarili olursa "[RF] nRF24 BAGLI." satirini kendisi basar.)
  if (USE_NRF24 && !radioOk && now - lastRfRetryMs >= RF_RETRY_MS) {
    lastRfRetryMs = now;
    radioSetup();
  }

  // Tarayici sustuysa guvenli moda gec
  if (now - lastWebMs > WEB_TIMEOUT_MS && ctrl.armed) {
    disarm("web baglantisi kesildi");
    neutralSurfaces();
  }

  // Ucak cevap vermiyorsa guvenli moda gec. Ucak zaten kendi failsafe'ine
  // duser; bu, kumandanin arayuzunun gerceklige uymasi icin. Yoksa ekranda
  // ARMED yazarken ucak coktan motoru kesmis olur.
  if (ctrl.armed && !telemetriTaze() && !TEZGAH_MODU) {
    disarm("ucaktan telemetri yok");
  }

  // 50 Hz telsiz gonderimi
  if (now - lastTxMs >= RC_TX_PERIOD_MS) {
    lastTxMs = now;
    radioTick();
  }

  // 1 sn'de bir link kalitesi + log
  if (now - lastStatMs >= 1000) {
    lastStatMs = now;
    // Iki yol birden acikken ayni pakete iki telemetri donebilir -> 100'e kirp
    uint32_t q = winSent ? (winAcked * 100UL) / winSent : 0;
    linkQuality = (uint8_t)(q > 100 ? 100 : q);
    winSent = winAcked = 0;

    // Modulun O ANKI durumu. Calisirken kablo cikarsa bayragi dusur ki
    // yukaridaki yeniden deneme devreye girsin.
    const bool nrf = nrfBagli();
    if (radioOk && !nrf) {
      radioOk        = false;
      txPending      = false;
      nrfHataBasildi = false;   // kopus ayrintisi tekrar basilabilsin
      Serial.println("[RF] nRF24 BAGLI DEGIL - modul cevap vermeyi birakti.");
    }

    const bool rxAlive = telemetriTaze();
    webHz  = winWeb;
    winWeb = 0;

    Serial.printf("[TX] nrf=%-11s now=%-6s | web=%u/s | kumanda=%s ucak=%s "
                  "thr=%u(istek %u) ail=%u ele=%u rud=%u | link=%u%% | "
                  "rx=%s kayip=%%%u vbat=%umV",
                  !USE_NRF24  ? "KAPALI" : nrf      ? "BAGLI" : "BAGLI-DEGIL",
                  !USE_ESPNOW ? "KAPALI" : espnowOk ? "BAGLI" : "YOK",
                  webHz,
                  ctrl.armed ? "ARMED " : "DISARM",
                  !rxAlive        ? "CEVAP-YOK"
                  : ucakFailsafe() ? "FAILSAFE "
                  : ucakArmed()    ? "ARMED    "
                                   : "DISARM   ",
                  ctrl.armed ? ctrl.thr : RC_US_MIN, ctrl.thrIstek,
                  ctrl.ail, ctrl.ele, ctrl.rud,
                  linkQuality,
                  rxAlive ? "OK" : "--",
                  rxAlive ? tlm.lossPct : 0,
                  rxAlive ? tlm.vbatMv : 0);
    if (txStuck) { Serial.printf(" | takilan=%u", txStuck); txStuck = 0; }
    if (TEZGAH_MODU) Serial.print(" | *TEZGAH MODU*");
    Serial.println();
  }
}
