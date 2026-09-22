// ============================================================
// RC Plane - UCAK (alici) yazilimi          -- UCUSA HAZIR SURUM --
// Kart: ESP32 DevKitC 38 pin (ESP-WROOM-32 / 32D) + nRF24L01
// Kablolama: docs/kablolama.html
//
// UCAK 3 KANALLI: GAZ (ESC) + ELEVATOR + RUDDER.  Aileron YOK.
// Yatis rudder ile veriliyor (polihedral kanat, klasik 3 kanal duzeni).
//
// Ne yapar:
//   1. Kumandadan 50 Hz'de RcPacket dinler - nRF24 ve/veya ESP-NOW uzerinden
//   2. magic + CRC + protokol versiyonu dogrular
//   3. Kanallari kalibrasyon egrisinden gecirip LEDC ile mikrosaniye yazar
//   4. Kumandaya telemetri (paket kaybi, rxHz, durum) geri gonderir
//   5. RC_FAILSAFE_MS boyunca gecerli paket gelmezse failsafe'e duser
//   6. KALIBRASYON MODU: servo yon / notr / uc noktalari telsizden ayarlanip
//      NVS'e kaydedilir. Firmware yeniden derlenmez.
//
// GUVENLIK ZINCIRI:
//   - ARMED biti gelmedikce ESC her zaman stop darbesindedir.
//   - Gaz tavani ucak tarafinda AYRICA sinirlanir (kalibrasyondaki
//     "tavan" degeri). Kumandadaki sinira guvenilmez; iki taraf bagimsiz.
//   - Failsafe: gaz stop, yuzeyler KALIBRE EDILMIS notre, armed duser.
//   - Arm kilidi: boot'ta ve her failsafe'ten sonra takilir; kumandadan
//     ARMED=0 bir paket gorulmeden acilmaz. Yani link geri geldiginde
//     motor kendiliginden donmez.
//   - Kalibrasyon modunda ARM tamamen reddedilir, ESC stop'ta tutulur.
//   - Kalibrasyon uc noktalari RC_US_HARD_MIN..MAX ile kirpilir.
//
// UYARI: Tezgah denemelerinde PERVANE TAKILI OLMASIN!
// Kart ve servolar ayri bir UBEC'in 5V hattindan beslenir (hatta kondansator
// var), ESC'nin BEC'i kapali; GND'ler ortak olmali.
// ============================================================
// Uc cikisin hepsi cipin LEDC birimiyle surulur, ESP32Servo kullanilmaz:
//  - kutuphanenin pin izin listesi (GPIO 0'i reddediyor) devre disi kalir
//  - degerler dogrudan mikrosaniye yazilir, rc_protocol.h ile ayni dil
//  - derece <-> us donusumu olmadigi icin notr noktasi kaymaz

#include <Arduino.h>
#include <SPI.h>
#include <RF24.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Preferences.h>
#include "rc_protocol.h"

// ------------------------------------------------------------
// IKI TASIMA KATMANI (kumanda tarafiyla ayni olmali)
//
//   nRF24   - ayri modul, SPI, 2.508 GHz
//   ESP-NOW - cipin kendi WiFi radyosu, ek donanim YOK
//
// Ikisi de ayni RcPacket'i tasir ve ayni dogrulama/failsafe yolundan gecer.
// Hangisinden gelirse gelsin son gecerli paket kazanir; ikisi birlikte
// acikken yedekli link olur. nRF24 kablolamasi sorunluysa ucak yine ucar.
//
// ESP-NOW yayin adresi kullanir -> MAC eslestirmesi gerekmez. Tek sart:
// kumandanin AP_CHANNEL degeri ile asagidaki ESPNOW_CHANNEL ayni olmali.
// ------------------------------------------------------------
static const bool    USE_NRF24      = true;
static const bool    USE_ESPNOW     = true;
static const uint8_t ESPNOW_CHANNEL = 1;   // kumandadaki AP_CHANNEL ile ayni

// ------------------------------------------------------------
// KUMANDANIN AP'SINE ILISKILENME
//
// ESP-NOW bu topolojide TEK YONLU calisiyordu. Olcum:
//   kumanda -> ucak : 48 paket/s, %0 kayip        (calisiyor)
//   ucak -> kumanda : 58 cerceve/s gonderiliyor,
//                     gonderim geri cagrisi cb=58/fail0 (yani havaya cikiyor),
//                     kumanda nowrx=0/s goruyor     (hic gelmiyor)
// Iki taraf da kanal 1'de ve kumandanin alim geri cagrisi kayitli.
//
// Kalan tek asimetri: kumanda softAP isletiyor, ucagin STA'si hicbir yere
// ILISKILENMEMIS durumda. Iliskilendirilmemis bir istasyondan softAP'ye
// giden cerceveler guvenilir teslim edilmiyor - bu depoda daha once unicast
// telemetri de tam bu yuzden olmustu (ack=0/nack=57, bkz. dosya basindaki
// "kumandanin adresi" notu). O zaman cozum yayina gecmekti; yayin da ayni
// duvara carpiyor.
//
// Bu yuzden STA artik kumandanin AP'sine baglaniyor. Iliskilendirilmis
// istasyon <-> AP, Espressif'in en iyi destekledigi ESP-NOW topolojisi ve
// kanal uyusmasi kendiliginden garanti.
//
// Tasima YINE YAYIN: iliskilenme yalnizca topolojiyi simetrik yapmak icin.
// Baglanti kopsa bile ESP-NOW calismaya devam eder, yani kontrol
// iliskilenmeye BAGLI DEGIL - bu bir iyilestirme, bagimlilik degil.
// DENENDI, ISE YARAMADI - false birakildi.
//
// Iliskilenme hic kurulmadi (log: ap=yok) ve kumanda yine nowrx=0/s gordu.
// Yani ESP-NOW'in tek yonlu olmasi iliskilenme eksikligi DEGIL. Sebep hala
// bulunmadi; bu bayrak, denenmis ve elenmis bir yolun kaydi olarak duruyor.
// true yapmadan once kumandada AP_MAX_CONN >= 2 oldugunu dogrula.
static const bool    AP_BAGLAN = false;
static const char*   AP_SSID   = "RC-Plane-TX";
static const char*   AP_PASS   = "rcplane1234";

// ------------------------------------------------------------
// CIKIS PINLERI  --  ESP32 DevKitC 38 pin
//
// Kartin SOL pin sirasi ucus tarafi: ESC (25), elevator (26), rudder (27),
// GND ve 5V ayni sirada; telsiz kablolari sag sirada kaliyor.
//
// BESLEME: ayri UBEC (5V/3A). Cikisi kartin 5V pinine gelir, servolar oradan
// beslenir; hatta 5V-GND arasi kondansator var. ESC'nin lineer BEC'i kapali.
//
// ESC GPIO25'te, sinyal ile GND arasinda 10k var: boot sirasinda pin yuksek
// empedansta, pulldown sayesinde ESC cop darbe yerine HIC darbe gormez.
// C3'teki gibi GPIO0'a ALINMADI - klasik ESP32'de GPIO0 boot modu pini;
// uzerindeki pulldown karti her acilista yukleme moduna sokar, ucak acilmaz.
//
// Klasik ESP32'de kacinilan pinler:
//   0, 2, 12, 15  strapping (12 boot'ta yuksekse flash 1.8 V secilir)
//   14            boot sirasinda PWM cikarir, ESC'ye cop darbe gider
//   6-11          dahili flash - kartta D0-D3, CMD, CLK yazar ("D2" GPIO2 DEGIL)
//   1, 3          UART0, seri port
//   34-39         yalnizca giris
// ------------------------------------------------------------
static const int PIN_ESC      = 25;  // ESC sinyal (10k pulldown)
static const int PIN_ELEVATOR = 26;
static const int PIN_RUDDER   = 27;

// AILERON - 4 kanalli bir kanat takildiginda kullanilir.
// Su anki ucak aileronsuz; bu iki cikis bos duruyor ve notrde bekliyor,
// hicbir zarari yok. Kanat degistirildiginde tek yapilacak sey servolari
// takmak ve kumandada "Aileron var" secmek - firmware degismiyor.
static const int PIN_AIL_SOL  = 32;
static const int PIN_AIL_SAG  = 33;

// ------------------------------------------------------------
// nRF24L01 pinleri - kumandayla AYNI GPIO'lar
//   VCC  -> 3V3   ** 5V DEGIL **   (yanina 10 uF kondansator sart)
//                 38 pinli kartta 3V3 SOL UST pin; kablo karta alttan gecer
//   GND  -> GND   (sag sira, 19'un ustu)
//   SCK  -> GPIO18
//   MISO -> GPIO19
//   MOSI -> GPIO23
//   CSN  -> GPIO5
//   CE   -> GPIO4
//   IRQ  -> bagli degil
// 18/19/23 VSPI'nin kendi pinleri. GPIO5 strapping ama boot'ta yuksek
// cekilir, bu da CSN'nin istenen bosta hali; kumanda ayni pinde calisiyor.
// SPI.begin() cagrisinda pinler yine acikca verilir: once SPI'yi kapan bir
// kutuphane varsayilani degistirirse bile modul yerinde kalsin.
// ------------------------------------------------------------
static const uint8_t PIN_RF_SCK  = 18;
static const uint8_t PIN_RF_MISO = 19;
static const uint8_t PIN_RF_MOSI = 23;
static const uint8_t PIN_RF_CSN  = 5;
static const uint8_t PIN_RF_CE   = 4;

// SPI hizi. Kutuphane varsayilani 10 MHz; dupont kablo + lehimli tel ile bu hiz
// cogu zaman guvenilir degil, radio.begin() bazen geciyor bazen gecmiyor.
// Kumanda tarafi ayni sorunu yasayip 4 MHz'e dusmustu; ucak da ayni degeri
// kullansin ki iki taraf ayrisamasin.
static const uint32_t RF_SPI_HZ = 4000000;

// ------------------------------------------------------------
// Baglanti geri geldiginde failsafe'ten cikmak icin gereken ardisik
// gecerli paket sayisi. 50 Hz'de 10 paket ~ 200 ms saglam link demek.
// Tek pakette geri donmek, link segirdiginde motorun atim atim calismasina
// yol acar; bu histerezis onu engeller.
// ------------------------------------------------------------
static const uint8_t RELINK_PAKET = 10;

// nRF24 bulunamazsa bu araliklarla yeniden denenir. Boylece kabloyu
// duzeltmek icin karti resetlemek gerekmez; durum satiri kendiliginden
// BAGLI'ya doner. Ayni sekilde calisirken kablo cikarsa YOK'a duser.
static const uint32_t RF_RETRY_MS = 3000;

// ------------------------------------------------------------
// SAGIR MODUL KURTARMASI
//
// Olculen ariza: nRF24 bir sure calisiyor, sonra hicbir paket almamaya
// basliyor - ama isChipConnected() HALA gecerli cevap donduruyor (register
// okumasi dusuk akimla calisiyor, radyo kismi calismiyor). Yani radioOk true
// kaliyor ve RF_RETRY_MS'lik yeniden deneme yolu HIC devreye girmiyor: link
// karta reset atilana kadar kalici olarak olu kaliyor.
//
// Kosul hassas olmali, yoksa kumanda kapaliyken bosa donuyoruz: DIGER tasima
// yolundan paket AKIYORSA kumanda kesin yayin yapiyor demektir, dolayisiyla
// nRF24'un de bir sey duymasi gerekir. Duymuyorsa modul sagir -> tam yeniden
// kurulum (radio.begin() cipi bastan ayarlar).
static const uint32_t NRF_SAGIR_MS = 2000;

// ------------------------------------------------------------
// Telemetri tazeleme araligi.
//
// Telemetri sadece paket GELDIGINDE gonderilirse, kumandaya giden yol gelen
// yola bagimli hale gelir: iki yoldan ayni paket geldiginde ikincisi "tekrar"
// sayilip atlanir, link bir an segirdiginde de bosluk olusur. Kumanda 1 sn
// icinde taze telemetri gormezse ARM'i reddediyor, yani kucuk bosluklar bile
// "ucaktan cevap yok" demeye yetiyor.
//
// Bu yuzden telemetri paket gelisinden BAGIMSIZ olarak da tazelenir.
// 100 ms -> saniyede 10 tazeleme, kumandanin 1 sn'lik penceresinin 10 kati pay.
// ------------------------------------------------------------
static const uint32_t TLM_YENILE_MS = 100;

// ------------------------------------------------------------
// KALIBRASYON ZAMAN ASIMLARI
//
// TEST komutu servoyu bir uc noktaya surer. Operator slider'i birakip
// gittiginde servo orada takili kalirsa dayanaga dayanip akim ceker ve
// isinir - bu yuzden her TEST kendini bu sure sonunda notre birakir.
// Her yeni TEST komutu sayaci sifirlar, yani slider'i cevirirken servo
// istedigin noktada kalir.
// ------------------------------------------------------------
static const uint32_t KALIB_TEST_MS   = 4000;
static const uint32_t KALIB_SWEEP_MS  = 3000;   // bir tam min->max->min turu
static const uint8_t  KALIB_SWEEP_TUR = 3;      // kac tur sonra kendiliginden dursun
static const uint32_t KALIB_ESC_HI_MS = 30000;  // ESC kalibrasyonu 1. adim tavani

// ------------------------------------------------------------
// LEDC cozunurlugu
//
// 14 BIT. Klasik ESP32 20 bite kadar izin veriyor, ama bu firmware once
// ESP32-C3'te calisti ve C3'un LEDC zamanlayicisi en fazla 14 bit
// (soc_caps.h -> SOC_LEDC_TIMER_BIT_WIDE_NUM = 14). Orada 16 bit yazmak
// ledcSetup()'in sessizce 0 donmesine, pinde hic PWM olusmamasina yol
// aciyordu. 14 iki kartta da calisir; kart C3'e donerse sessizce bozulmasin
// diye 14'te kaliyor. Buraya 14'ten buyuk bir sayi YAZMA.
//
// 14 bit @ 50 Hz -> 16384 adim / 20000 us = ~1.22 us cozunurluk.
// Servo icin fazlasiyla yeterli (RC standardi 1 us adimlarla calisir).
// ------------------------------------------------------------
static const uint8_t  LEDC_BIT       = 14;
static const uint32_t LEDC_ADIM      = 1UL << LEDC_BIT;   // 16384
static const uint32_t PWM_PERIYOT_US = 20000;             // 50 Hz

// LEDC kanallari. Klasik ESP32'de 0..7 yuksek hiz grubu ve kanallar
// zamanlayiciyi CIFTLER halinde paylasir (0-1, 2-3, 4-5). Hepsi 50 Hz / 14 bit
// oldugu icin paylasim zararsiz. Farkli frekansli bir cikis eklenirse onu
// ayri bir cifte koy: ledcSetup() ortak zamanlayiciyi yeniden kurar ve
// komsu kanalin frekansi sessizce degisir.
static const int KANAL_ESC     = 0;
static const int KANAL_ELE     = 1;
static const int KANAL_RUD     = 2;
static const int KANAL_AIL_SOL = 3;
static const int KANAL_AIL_SAG = 4;   // kalibrasyon yuzeyi degil - sol'un aynasi

// RcSurface -> LEDC kanal / pin / isim tablolari. Kalibrasyon kodu
// yuzeyleri indeksle gezdigi icin bu tablolar tek dogruluk kaynagi.
static const int  SURF_KANAL[RC_SURF_COUNT] = { KANAL_ELE, KANAL_RUD, KANAL_ESC, KANAL_AIL_SOL };
static const int  SURF_PIN[RC_SURF_COUNT]   = { PIN_ELEVATOR, PIN_RUDDER, PIN_ESC, PIN_AIL_SOL };
static const char* SURF_AD[RC_SURF_COUNT]   = { "Elevator", "Rudder", "ESC/Gaz", "Aileron" };

// ============================================================
// KALICI AYARLAR (NVS)
//
// Servo yonu, notru ve uc noktalari artik derleme sabiti degil: kalibrasyon
// modundan ayarlanip flash'a yaziliyor. Kart resetlense de kalir, uctaki
// kartta USB olmadan da degistirilebilir.
// ============================================================
struct CikisAyar {
  uint16_t minUs;   // yuzey: alt uc      | gaz: ESC stop darbesi
  uint16_t midUs;   // yuzey: notr        | gaz: GAZ TAVANI
  uint16_t maxUs;   // yuzey: ust uc      | gaz: ESC tam gaz darbesi
  uint8_t  ters;    // yuzey: 1 = yon ters | gaz: kullanilmaz
};

static CikisAyar ayar[RC_SURF_COUNT];
static CikisAyar ayarKayitli[RC_SURF_COUNT];   // NVS'teki hali (kirli tespiti icin)

static Preferences nvs;
static const char* NVS_AD    = "rcplane";
static const char* NVS_ANAHT = "cfg";
// Yapinin duzeni degisirse eski kayit okunmamali. Bu imzayi struct'i her
// degistirdiginde artir; aksi halde flash'taki eski byte'lar yeni alanlara
// oturur ve servo ucus sirasinda anlamsiz bir yere gider.
static const uint32_t NVS_IMZA = 0x52435004;   // "RCP" + surum 4 (aileron eklendi)

static void ayarVarsayilan() {
  ayar[RC_SURF_ELEVATOR] = { RC_US_MIN, RC_US_MID, RC_US_MAX, 0 };
  ayar[RC_SURF_RUDDER]   = { RC_US_MIN, RC_US_MID, RC_US_MAX, 0 };
  // Gaz: stop 1000, tavan 2000 (sinirsiz), tam gaz 2000.
  // Ilk ucuslarda tavani arayuzden dusur - firmware degistirmeye gerek yok.
  ayar[RC_SURF_THROTTLE] = { RC_US_MIN, RC_US_MAX, RC_US_MAX, 0 };
  ayar[RC_SURF_AILERON]  = { RC_US_MIN, RC_US_MID, RC_US_MAX, 0 };
}

// Gelen bir ayarin makul olup olmadigina karar verir. Bozuk telsiz paketi,
// yanlis surum kumanda ya da elle girilmis sacma bir deger buradan gecemez.
static bool ayarGecerli(uint8_t surf, uint16_t mn, uint16_t md, uint16_t mx) {
  if (surf >= RC_SURF_COUNT) return false;
  if (mn < RC_US_HARD_MIN || mx > RC_US_HARD_MAX) return false;

  if (surf == RC_SURF_THROTTLE) {
    // minUs = stop, midUs = tavan, maxUs = tam gaz
    return mn <= 1200 && mx >= 1700 && md >= mn && md <= mx;
  }
  // Yuzey: siralama bozulmamali ve her iki yone en az 50 us hareket kalmali,
  // yoksa "kalibre ettim ama yuzey hic oynamiyor" durumu olusur.
  return mn + 50 <= md && md + 50 <= mx;
}

static bool ayarKirliMi() {
  for (uint8_t i = 0; i < RC_SURF_COUNT; i++) {
    if (ayar[i].minUs != ayarKayitli[i].minUs ||
        ayar[i].midUs != ayarKayitli[i].midUs ||
        ayar[i].maxUs != ayarKayitli[i].maxUs ||
        ayar[i].ters  != ayarKayitli[i].ters) return true;
  }
  return false;
}

// NVS'ten oku. Imza tutmuyorsa ya da icerik gecersizse varsayilana doner -
// bozuk bir flash kaydi ucagi asla anlamsiz bir servo konumuna surmez.
static bool ayarYukle() {
  struct { uint32_t imza; CikisAyar s[RC_SURF_COUNT]; } blok;
  nvs.begin(NVS_AD, true);
  const size_t n = nvs.getBytes(NVS_ANAHT, &blok, sizeof(blok));
  nvs.end();

  if (n != sizeof(blok) || blok.imza != NVS_IMZA) {
    ayarVarsayilan();
    memcpy(ayarKayitli, ayar, sizeof(ayar));
    return false;
  }
  for (uint8_t i = 0; i < RC_SURF_COUNT; i++) {
    if (!ayarGecerli(i, blok.s[i].minUs, blok.s[i].midUs, blok.s[i].maxUs)) {
      ayarVarsayilan();
      memcpy(ayarKayitli, ayar, sizeof(ayar));
      return false;
    }
  }
  memcpy(ayar, blok.s, sizeof(ayar));
  memcpy(ayarKayitli, ayar, sizeof(ayar));
  return true;
}

static bool ayarKaydet() {
  struct { uint32_t imza; CikisAyar s[RC_SURF_COUNT]; } blok;
  blok.imza = NVS_IMZA;
  memcpy(blok.s, ayar, sizeof(ayar));

  nvs.begin(NVS_AD, false);
  const size_t n = nvs.putBytes(NVS_ANAHT, &blok, sizeof(blok));
  nvs.end();

  if (n == sizeof(blok)) { memcpy(ayarKayitli, ayar, sizeof(ayar)); return true; }
  return false;
}

// ------------------------------------------------------------
RF24 radio(PIN_RF_CE, PIN_RF_CSN, RF_SPI_HZ);

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

static bool     radioOk    = false;
static bool     espnowOk   = false;
static uint8_t  BCAST[6]   = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static bool     armed      = false;
static bool     failsafe   = true;   // paket gelene kadar failsafe kabul et
static uint32_t lastRxMs   = 0;
static uint32_t lastStatMs = 0;

// Ayrintili nRF24 hata metnini 3 sn'de bir tekrar basmamak icin.
// Durum zaten her saniye [RX] satirinda gorunuyor.
static bool     nrfHataBasildi = false;

// Arm kilidi: true iken ARMED biti gelse bile arm edilmez. Boot'ta ve her
// failsafe'ten sonra kalkar; ancak kumandadan ARMED=0 bir paket gorulunce
// acilir. Yani operator DISARM -> ARM yapmadan motor kendiliginden donmez.
static bool     armKilidi      = true;
static uint8_t  ardisikGecerli = 0;   // failsafe cikisi icin sayac

// Teshis icin: kumandadan en son gelen ARMED bitinin ham hali.
// "Neden arm olmuyor?" sorusunun cevabi ikiye ayrilir ve [RX] satirindan
// dogrudan okunur: kumanda biti hic gondermiyor mu (rxarm=0), yoksa bit
// geliyor ama arm kilidi mi acilmamis (rxarm=1 kilit=KAPALI)?
static bool     sonPaketArmed  = false;

// Paket kaybi olcumu (1 sn'lik pencere, seq bosluklarindan hesaplanir)
static uint8_t  lastSeq     = 0;
static bool     seqBasladi  = false;

// Son 32 seq icin "goruldu" bit maskesi. Bit 0 = lastSeq, bit n = lastSeq-n.
// Tekrar tespitini tek bir "en yuksek seq" degerinden kurtariyor; gerekcesi
// paketIsle() icindeki uzun notta.
static uint32_t seqMaske    = 0;
static uint16_t winBeklenen = 0;
static uint16_t winAlinan   = 0;
static uint16_t winBozuk    = 0;
static uint16_t winTekrar   = 0;   // ayni paketin ikinci yoldan gelen kopyasi
static uint8_t  lossPct     = 0;
static uint8_t  rxHz        = 0;   // saniyedeki benzersiz paket sayisi

// Telemetri gonderim sayaclari (1 sn'lik pencere).
// Iki yol da sessizce basarisiz olabiliyor: writeAckPayload() FIFO doluysa
// false doner, esp_now_send() arayuz/es uyusmazliginda hata doner. Kumanda
// "ucaktan telemetri yok" derken sorunun HANGI yolda oldugunu ayirt etmek icin.
// nRF24'ten alinan toplam paket sayisi. Yalnizca "bu pencerede paket akti
// mi" sorusuna cevap veriyor (ACK payload kuyrugunun bayatlik tespiti).
static uint32_t  nrfAlinan     = 0;
static uint32_t  nrfAlinanOnce = 0;
static uint32_t  nrfSonRxMs    = 0;   // en son nRF24 paketi ne zaman geldi
static uint16_t  nrfKurtarma   = 0;   // kac kez zorla yeniden kuruldu

static uint16_t  tlmNrf     = 0;   // ACK payload'a basariyla yuklenen
static uint16_t  tlmNrfRed  = 0;   // FIFO dolu -> kurtarma sayisi
static uint16_t  tlmNow     = 0;   // ESP-NOW ile kuyruga verilen
static uint16_t  tlmNowHata = 0;
static esp_err_t tlmSonHata = ESP_OK;

// Gonderim GERI CAGRISININ durumu. esp_now_send()'in ESP_OK donmesi yalnizca
// "kuyruga girdi" demek; cercevenin gercekten havaya cikip cikmadigini ancak
// bu geri cagri soyluyor. Eski kod durumu tumden yok sayiyordu ("yayinda her
// zaman SUCCESS gelir" varsayimi) ve bu, dogrulanmamis tek nokta olarak
// kaldi: kumanda saniyede 0 cerceve alirken ucak 58 gonderdigini soyluyor.
static volatile uint16_t nowCbOk   = 0;
static volatile uint16_t nowCbFail = 0;
static uint16_t nowCbOkHz = 0, nowCbFailHz = 0;

// ------------------------------------------------------------
// KUMANDANIN ADRESI
// Telemetri YAYIN (broadcast) ile gonderiliyor. Unicast denendi ve olcum
// acikca reddetti: ack=0 / nack=57, yani saniyede 57 gonderimin HICBIRI ACK
// almadi, ardindan ESP_ERR_ESPNOW_NO_MEM geldi. Sebep: ucagin STA'si
// kumandanin AP'sine iliskilendirilmis degil; iliskilendirilmemis bir STA'dan
// AP'ye giden unicast cerceve MAC seviyesinde ACK almaz, ESP-NOW tekrar tekrar
// dener, gonderim kuyrugu dolar ve telemetri tumden durur.
// Adres yine de ogreniliyor: log'da gorunmesi hangi kumandanin konustugunu
// dogrulamaya yariyor.
// ------------------------------------------------------------
static uint8_t   kumandaMac[6]   = {0};
static bool      kumandaMacGeldi = false;
static bool      kumandaEsiVar   = false;

// Son uygulanan degerler (log ve rapor icin)
static uint16_t    sonThr      = RC_US_MIN;
static uint16_t    sonThrIstek = RC_US_MIN;  // kumandanin istedigi ham gaz
static const char* sonThrSebep = "disarm";   // uygulanan deger neden kirpildi
static uint16_t sonEle    = RC_US_MID;
static uint16_t sonRud    = RC_US_MID;
static uint16_t sonAil    = RC_US_MID;
static uint16_t sonAilSag = RC_US_MID;   // sag kanat (aynalanan cikis)
static uint16_t sonCikis[RC_SURF_COUNT] = { RC_US_MID, RC_US_MID, RC_US_MIN, RC_US_MID };

// ============================================================
// KALIBRASYON DURUMU
// ============================================================
static bool     kalibModu     = false;
static uint8_t  kalibSonSeq   = 0xFF;   // islenen son RcConfigPacket.seq
static uint32_t kalibSonSeqMs = 0;      // o komutun islendigi an
static bool     kalibRed      = false;  // son komut reddedildi mi
static uint8_t  raporSurf     = RC_SURF_ELEVATOR;  // rapor edilecek cikis

// Ayni seq'i "tekrar" saymak icin gecerli pencere. Kumanda komutu onay
// gelene kadar 1.5 sn boyunca tekrarliyor, yani kopyalar hep bu surenin
// icinde gelir.
//
// Pencere OLMASAYDI: kumanda resetlendiginde seq sayaci 1'den yeniden
// basliyor; ucak hala eski 1'i hatirladigi icin resetten sonraki ILK
// komutu kopya sanip yutuyordu. Sure sinirini eklemek bunu kokten cozer -
// kumandanin ne zaman resetlendigini bilmemize gerek kalmaz.
static const uint32_t KALIB_TEKRAR_MS = 3000;

// TEST: bir cikisi belirli bir ham degerde tutar
static uint8_t  testSurf   = 0xFF;
static uint32_t testBitisMs = 0;

// SWEEP: bir yuzeyi min <-> max tarar
static uint8_t  sweepSurf     = 0xFF;
static uint32_t sweepBaslaMs  = 0;

// ESC gaz araligi kalibrasyonu (telsizden, adim adim)
static bool     escKalibHi    = false;
static uint32_t escKalibHiMs  = 0;

static void raporGonder(uint8_t surf);   // ileri bildirim

// ============================================================
// Cikislar
// ============================================================

// Mikrosaniyeyi LEDC duty degerine cevirir (50 Hz -> 20000 us periyot).
// Adim sayisi LEDC_BIT'ten turetilir; cozunurlugu degistirirsen burasi
// kendiliginden uyar, ikisi ayri dusmez.
static inline uint32_t usToDuty(uint16_t us) {
  return (uint32_t)((uint64_t)us * LEDC_ADIM / PWM_PERIYOT_US);
}

// Tek gecis noktasi: her cikis yazimi buradan gecer, boylece mekanik
// guvenlik kirpmasi (rcClampHard) atlanamaz.
static void cikisYaz(uint8_t surf, uint16_t us) {
  const uint16_t v = rcClampHard(us);
  ledcWrite(SURF_KANAL[surf], usToDuty(v));
  sonCikis[surf] = v;

  // Aileron TEK kalibrasyon girdisi ama IKI cikis surer: sag kanat servosu
  // kendi notru etrafinda aynalanir (bir kanatcik kalkarken digeri iner).
  // Aynalamayi buraya koymak failsafe / TEST / TARA dahil HER yolun iki
  // servoyu birden surmesini garantiler - cagri yerlerinde unutulacak bir
  // adim kalmiyor.
  if (surf == RC_SURF_AILERON) {
    sonAilSag = rcClampHard(2 * (int32_t)ayar[RC_SURF_AILERON].midUs - (int32_t)v);
    ledcWrite(KANAL_AIL_SAG, usToDuty(sonAilSag));
  }
}

// ------------------------------------------------------------
// Kumanda degerini (1000..2000) kalibre edilmis servo darbesine cevirir.
//
// Endustri standardi "end point / travel adjust" davranisi: notrun iki
// yaninda BAGIMSIZ olcekleme var. Bir yone 100%, diger yone 70% hareket
// verebilirsin - mekanik linkajlar hicbir zaman simetrik degil.
//
//   giris > 1500  ->  midUs .. maxUs
//   giris < 1500  ->  minUs .. midUs
//
// Yon tersleme olcekleme ONCESINDE, notr etrafinda aynalanarak yapilir;
// boylece ters cevirmek uc noktalari birbirine karistirmaz.
// ------------------------------------------------------------
static uint16_t yuzeyMap(const CikisAyar& c, uint16_t giris) {
  int32_t v = c.ters ? (int32_t)RC_US_MIN + (int32_t)RC_US_MAX - (int32_t)giris
                     : (int32_t)giris;
  if (v > RC_US_MID) {
    return (uint16_t)((int32_t)c.midUs +
                      (v - RC_US_MID) * ((int32_t)c.maxUs - (int32_t)c.midUs) / 500);
  }
  if (v < RC_US_MID) {
    return (uint16_t)((int32_t)c.midUs -
                      (RC_US_MID - v) * ((int32_t)c.midUs - (int32_t)c.minUs) / 500);
  }
  return c.midUs;
}

// Gaz: 1000..2000 -> stop..tam gaz. Tavan kirpmasi cagirana birakilir,
// cunku "kirpildi mi" bilgisi log'da ayrica gosteriliyor.
static uint16_t gazMap(uint16_t giris) {
  const CikisAyar& c = ayar[RC_SURF_THROTTLE];
  int32_t v = (int32_t)c.minUs +
              ((int32_t)giris - RC_US_MIN) * ((int32_t)c.maxUs - (int32_t)c.minUs) / 1000;
  if (v < (int32_t)c.minUs) v = c.minUs;
  if (v > (int32_t)c.maxUs) v = c.maxUs;
  return (uint16_t)v;
}

// ESC'ye yazar. ARMED degilse, failsafe'deyse ya da kalibrasyon modundaysa
// motor her zaman durur.
//
// Kumandanin ISTEDIGI deger de saklanir: log'da sadece uygulanan degeri
// gormek "gaz gitmiyor" derdinde hicbir sey anlatmiyor. Istek 1600 ama
// uygulanan 1000 ise sorun kumandada degil, buradaki kapida.
static void escYaz(uint16_t giris) {
  sonThrIstek = giris;
  const CikisAyar& c = ayar[RC_SURF_THROTTLE];
  uint16_t us;

  if (kalibModu)      { sonThrSebep = "kalib";    us = c.minUs; }
  else if (failsafe)  { sonThrSebep = "failsafe"; us = c.minUs; }
  else if (!armed)    { sonThrSebep = "disarm";   us = c.minUs; }
  else {
    us = gazMap(giris);
    if (us > c.midUs) { us = c.midUs; sonThrSebep = "tavan"; }  // gaz tavani
    else                sonThrSebep = "-";
  }

  cikisYaz(RC_SURF_THROTTLE, us);
  sonThr = us;
}

static void yuzeyleriYaz(uint16_t ele, uint16_t rud, uint16_t ail) {
  cikisYaz(RC_SURF_ELEVATOR, yuzeyMap(ayar[RC_SURF_ELEVATOR], ele));
  cikisYaz(RC_SURF_RUDDER,   yuzeyMap(ayar[RC_SURF_RUDDER],   rud));
  cikisYaz(RC_SURF_AILERON,  yuzeyMap(ayar[RC_SURF_AILERON],  ail));
  sonEle = ele; sonRud = rud; sonAil = ail;
}

// Kalibrasyondaki gecici surusleri (test / tarama / ESC adimi) iptal eder.
static void kalibTestleriDurdur() {
  testSurf     = 0xFF;
  sweepSurf    = 0xFF;
  escKalibHi   = false;
}

// Butun cikislari guvenli konuma alir: yuzeyler KALIBRE EDILMIS notre,
// ESC stop darbesine. "Notr" 1500 degil, ayar[].midUs - linkaj kaymissa
// failsafe konumu da kaymis olurdu.
static void guvenliKonum() {
  cikisYaz(RC_SURF_ELEVATOR, ayar[RC_SURF_ELEVATOR].midUs);
  cikisYaz(RC_SURF_RUDDER,   ayar[RC_SURF_RUDDER].midUs);
  cikisYaz(RC_SURF_AILERON,  ayar[RC_SURF_AILERON].midUs);
  cikisYaz(RC_SURF_THROTTLE, ayar[RC_SURF_THROTTLE].minUs);
  sonEle = sonRud = sonAil = RC_US_MID;
  sonThr = ayar[RC_SURF_THROTTLE].minUs;
}

// Failsafe konumu: motor durur, yuzeyler notre gelir.
// Arm kilidi geri takilir; link donse bile operator DISARM -> ARM yapmali.
// Kalibrasyon modundaysak ondan da cikilir: link yokken servolari
// surmeye devam etmenin hicbir anlami yok, riski var.
static void failsafeUygula() {
  armed          = false;
  armKilidi      = true;
  ardisikGecerli = 0;
  if (kalibModu) {
    kalibModu = false;
    Serial.println("[KAL] Baglanti koptu - kalibrasyon modundan cikildi.");
  }
  kalibTestleriDurdur();
  guvenliKonum();
  sonThrSebep = "failsafe";
}

// ============================================================
// ESC GAZ ARALIGI KALIBRASYONU (seri port uzerinden, boot'ta)
//
// ESC, kalibrasyon sirasinda gordugu EN GENIS darbeyi "tam gaz", EN DAR
// darbeyi "stop" olarak ogrenir. Kalibre edilmemis bir ESC'de 1200 us hala
// stop bolgesinde kalabilir ve motor hic donmez - "gaz gidiyor ama motor
// donmuyor" sikayetinin en sik sebebi budur.
//
// Bu yordam gaz tavanini BILEREK asar: kalibrasyon icin ESC'nin gercek
// maxUs darbesini gormesi sart. Bu yuzden ucus kodundan asla cagrilmaz;
// yalnizca boot'ta seri porttan elle tetiklenir ve her adimda 'e' onayi ister.
//
// NEDEN "HERHANGI BIR TUS" DEGIL: C3'te seri port yerel USB'ydi ve pencere
// yalnizca USB bagliyken aciliyordu. Klasik ESP32'de seri port UART0 (USB
// koprusu) ve HardwareSerial her zaman true doner - pencere HER boot'ta,
// ucaktaki kartta da acilir. Hattaki gurultu baytlari "tus" sayilsaydi
// cikis tam gaz darbesine gidebilirdi. Tetik 'k', her adim ayrica 'e':
// rastgele baytlarin bu diziyi tutturmasi pratikte imkansiz.
//
// !! PERVANE TAKILI OLMAMALI !!
// ============================================================

// Seri porttan 'e' onayi bekler. Once tampondaki eski karakterler yutulur ki
// onceki adimdan artan bir tus yanlislikla onay sayilmasin; 'e' disindaki
// her bayt yok sayilir.
static void onayBekle(const char* mesaj) {
  while (Serial.available()) Serial.read();
  Serial.println(mesaj);
  for (;;) {
    if (Serial.available() && (Serial.read() | 0x20) == 'e') break;
    delay(10);
  }
  while (Serial.available()) Serial.read();
}

static void escKalibrasyon() {
  const CikisAyar& c = ayar[RC_SURF_THROTTLE];
  Serial.println();
  Serial.println("================ ESC GAZ ARALIGI KALIBRASYONU ================");
  Serial.println("!!! PERVANE TAKILI OLMAMALI - motor tam gaza kadar cikabilir !!!");
  Serial.println();

  onayBekle("1) LiPo'yu CIKAR (ESC tamamen gucsuz olsun). Bitince 'e' bas.");

  cikisYaz(RC_SURF_THROTTLE, c.maxUs);
  Serial.printf("2) Gaz TAM GAZ'a alindi (%u us).\n", c.maxUs);
  onayBekle("   Simdi LiPo'yu TAK. Bip seslerini duyunca 'e' bas.");

  cikisYaz(RC_SURF_THROTTLE, c.minUs);
  Serial.printf("3) Gaz STOP'a alindi (%u us). Onay bipleri geliyor...\n", c.minUs);
  delay(4000);

  Serial.println("4) Kalibrasyon tamam. ESC artik bu araligi biliyor.");
  Serial.println("   LiPo'yu cikar, karti resetle, normal ucus koduyla dene.");
  onayBekle("   Devam etmek icin 'e' bas.");
  Serial.println("==============================================================");
}

// ============================================================
// Telemetri ve kalibrasyon raporu
// ============================================================

// Batarya olcumu yok. ADC'ye gerilim bolucu eklendiginde burayi doldur;
// kumandadaki batarya gostergesi otomatik calisir. 0 = olcum yok demek.
// Bolucu bir ADC1 pinine (ornegin GPIO35) baglanmali: ESP-NOW WiFi radyosunu
// kullandigi icin ADC2 pinleri (0, 2, 4, 12-15, 25-27) okunamaz.
static uint16_t bataryaOku() {
  return 0;
}

// O anki durumu bir telemetri paketine doldurur (CRC dahil).
static void telemetriDoldur(RcTelemetry& t, uint8_t seqEcho) {
  t.magic   = RC_MAGIC_TLM;
  t.seqEcho = seqEcho;
  t.vbatMv  = bataryaOku();
  t.lossPct = lossPct;
  t.rxHz    = rxHz;
  t.status  = (uint8_t)((armed        ? RC_STATUS_ARMED    : 0) |
                        (failsafe     ? RC_STATUS_FAILSAFE : 0) |
                        (kalibModu    ? RC_STATUS_CALIB    : 0) |
                        (ayarKirliMi()? RC_STATUS_DIRTY    : 0));
  t.crc     = rcTelemetryCrc(t);
}

// ------------------------------------------------------------
// nRF24 GERI YOLU: ACK payload'i tazeler.
//
// ACK payload onceden yuklenir: simdi yazilan yuk, BUNDAN SONRA alinan
// pakete cevap olarak gider. Bu yuzden setup'ta da bir kez cagrilir.
//
// URETIM ile TUKETIM birebir esitlenmek ZORUNDA, ve bunu bozmak tam olarak
// "servolar oynuyor ama kumanda telemetri yok diyor" arizasini uretiyor:
//
//   - TUKETIM: ACK'i DONANIM uretir, paketi CRC'siyle dogruladigi an, MCU
//     paketi okumadan once. Alinan HER nRF24 paketi (kumandanin tekrarlari
//     dahil) FIFO'dan bir yuk goturur.
//   - Eski kod URETIMI paketIsle() icine, tekrar kontrolunden SONRAYA
//     koyuyordu. Ayni seq iki tasima yolundan geldiginde ikinci kopya
//     "tekrar" sayilip paketIsle() basinda donuyor - ama nRF24 o kopyayi da
//     ACK'lemis ve bir yuk tuketmis oluyor. Uretim tuketimin altina dusuyor,
//     FIFO bosaliyor ve ACK'ler payload'siz gidiyor: kumanda paketin ucaga
//     ULASTIGINI goruyor (ACK var) ama telemetri hic gelmiyor.
//
// Artik uretim tuketimin oldugu yerde: radioReceive(). Yuk yalnizca FIFO
// BOSSA yazilir, boylece her zaman TEK ve TAZE bir yuk hazir bekler.
// Dolu FIFO'ya yazmak flush gerektiriyor ve flush taze telemetriyi de atiyor.
// ------------------------------------------------------------
// FIFO'yu doldurmaya cadansi: 3 slot var, bir cagride en fazla bu kadar
// yazilir (sonsuz donguye karsi da ust sinir).
static const uint8_t ACK_FIFO_DERINLIK = 3;

static void telemetriAckYukle(uint8_t seqEcho) {
  if (!radioOk) return;

  // ---- NEDEN FIFO'YU **DOLU** TUTUYORUZ ----
  //
  // Tek taze yuk hazir tutmak menzilin ORTASINDA yetiyor, KENARINDA yetmiyor.
  // Menzil kenarinda su oluyor:
  //
  //   1. Kumanda paketi gonderir, ucak alir (servolar oynar), ucak ACK'i
  //      yukuyle birlikte doner.
  //   2. ACK **geri yolda** kaybolur - geri yol her zaman daha zayif: kumanda
  //      kartinin kendi WiFi AP'si telefona hizmet ederken yaninda duran
  //      nRF24'un alicisini korletiyor, ucakta boyle bir yerel girisim yok.
  //   3. Kumanda ACK gormedigi icin ayni paketi TEKRAR gonderir.
  //   4. Ucagin telsizi tekrari ayni PID'den kopya sayar: RX FIFO'ya KOYMAZ,
  //      yani MCU bunu bir "alinan paket" olarak hic gormez - AMA donanim
  //      yine ACK'ler ve FIFO'dan bir yuk daha GOTURUR.
  //
  // Sonuc: her kontrol paketi icin 1 yuk uretilir, 4'e kadar tuketilir.
  // FIFO bosalir, ACK'ler payload'siz doner ve telemetri BIR ANDA sifira
  // duser - kademeli zayiflamaz. Kumanda "baglanti kesildi" der, oysa ileri
  // yol kusursuz ve servolar kollari takip etmeye devam eder. Kullanicinin
  // gordugu "link %50, sonra birden kopuk" ucurumu tam olarak budur.
  //
  // Uc slotu dolu tutmak bu tekrarlari karsilar. Bayatlik sorun degil:
  // 50 Hz tuketimde en eski yuk ~60 ms gerideyken kumandanin tazelik
  // penceresi 1000 ms. Link bostayken bayatlayan kuyrugu asagidaki
  // ackFifoTazele() bosaltiyor.
  RcTelemetry t;
  telemetriDoldur(t, seqEcho);

  for (uint8_t i = 0; i < ACK_FIFO_DERINLIK; i++) {
    if (radio.isFifo(true) == RF24_FIFO_FULL) break;
    if (!radio.writeAckPayload(1, &t, sizeof(t))) { tlmNrfRed++; break; }
    tlmNrf++;
  }
}

// Link BOSTAYKEN kuyruktaki yukler bayatliyor: nRF24 hic paket getirmiyorsa
// kimse tuketmiyor ve FIFO bir FIFO oldugu icin en ESKI yuk ilk cikar. Link
// geri geldiginde kumandaya once o bayat durum giderdi.
//
// Bu yuzden paket AKMIYORSA kuyruk bosaltilip yeniden doldurulur. Akiyorsa
// dokunulmaz - tuketim zaten kuyrugu taze tutuyor.
static void ackFifoTazele(uint8_t seqEcho) {
  if (!radioOk) return;
  radio.flush_tx();
  telemetriAckYukle(seqEcho);
}

// ------------------------------------------------------------
// ESP-NOW GERI YOLU: telemetriyi yayinlar.
//
// Bu yol nRF24'ten bagimsiz ve farkli bir ritimle beslenir: yayin cercevesi
// ACK beklemedigi icin tuketim/uretim dengesi yok, her cagri dogrudan havaya
// cikar. Bu yuzden alinan her BENZERSIZ pakette cagrilabilir.
// ------------------------------------------------------------
static void telemetriYayinla(uint8_t seqEcho) {
  if (!espnowOk) return;

  RcTelemetry t;
  telemetriDoldur(t, seqEcho);

  // TELEMETRI HER ZAMAN YAYIN ile gider (yukaridaki "kumandanin adresi"
  // notuna bak - unicast bu topolojide kuyrugu dolduruyor).
  const esp_err_t r = esp_now_send(BCAST, (const uint8_t*)&t, sizeof(t));
  if (r == ESP_OK) tlmNow++;
  else { tlmNowHata++; tlmSonHata = r; }
}

// Telemetriyi acik olan tum yollardan kumandaya gonderir.
static void telemetriYukle(uint8_t seqEcho) {
  telemetriAckYukle(seqEcho);
  telemetriYayinla(seqEcho);
}

// Kalibrasyon raporu: kumandanin ekraninda gorunen degerlerin kaynagi.
// Ucagin GERCEKTEN uyguladigi ayari ve o cikisin o anki darbesini tasir.
static void raporGonder(uint8_t surf) {
  if (surf >= RC_SURF_COUNT) return;

  RcConfigReport r;
  r.magic  = RC_MAGIC_REP;
  r.ackSeq = kalibSonSeq;
  r.surf   = surf;
  r.flags  = (uint8_t)((ayar[surf].ters ? RC_REPF_REVERSE : 0) |
                       (kalibModu       ? RC_REPF_CALIB   : 0) |
                       (ayarKirliMi()   ? RC_REPF_DIRTY   : 0) |
                       (kalibRed        ? RC_REPF_REJECT  : 0));
  r.minUs  = ayar[surf].minUs;
  r.midUs  = ayar[surf].midUs;
  r.maxUs  = ayar[surf].maxUs;
  r.liveUs = sonCikis[surf];
  r.crc    = rcReportCrc(r);

  if (radioOk) {
    if (!radio.writeAckPayload(1, &r, sizeof(r))) {
      radio.flush_tx();
      radio.writeAckPayload(1, &r, sizeof(r));
    }
  }
  if (espnowOk) esp_now_send(BCAST, (const uint8_t*)&r, sizeof(r));
}

// ============================================================
// Kalibrasyon komutlarinin islenmesi
// ============================================================
static void konfigIsle(const RcConfigPacket& c) {
  if (!rcConfigValid(c)) { winBozuk++; return; }

  // Ayni komut iki tasima yolundan da gelir ve onay gelene kadar tekrarlanir;
  // ikinci kopyayi uygulamak ozellikle SAVE/DEFAULT gibi komutlarda
  // istenmeyen tekrar demek. Yine de rapor gonderilir: kumanda cevabi
  // kacirmis olabilir, susarsak sonsuza kadar tekrarlar.
  if (c.seq == kalibSonSeq && millis() - kalibSonSeqMs < KALIB_TEKRAR_MS) {
    raporGonder(c.surf < RC_SURF_COUNT ? c.surf : raporSurf);
    return;
  }

  kalibSonSeq   = c.seq;
  kalibSonSeqMs = millis();
  kalibRed      = false;
  const uint8_t surf = c.surf < RC_SURF_COUNT ? c.surf : RC_SURF_ELEVATOR;
  raporSurf = surf;

  switch (c.op) {
    case RC_CFG_ENTER:
      // Kalibrasyona yalnizca motor kapaliyken girilir. ARMED iken gelen
      // bir ENTER komutu sessizce yok sayilmaz, acikca reddedilir.
      if (armed) { kalibRed = true; Serial.println("[KAL] ENTER reddedildi: ARMED."); break; }
      kalibModu = true;
      armKilidi = true;          // moddan cikinca yeniden DISARM->ARM gerekecek
      kalibTestleriDurdur();
      guvenliKonum();
      Serial.println("[KAL] Kalibrasyon modu ACIK. Motor kilitli.");
      break;

    case RC_CFG_EXIT:
      kalibModu = false;
      armKilidi = true;
      kalibTestleriDurdur();
      guvenliKonum();
      Serial.println("[KAL] Kalibrasyon modu KAPALI.");
      break;

    case RC_CFG_SET:
      if (!kalibModu) { kalibRed = true; break; }
      if (!ayarGecerli(surf, c.minUs, c.midUs, c.maxUs)) {
        kalibRed = true;
        Serial.printf("[KAL] %s ayari reddedildi: %u/%u/%u gecersiz.\n",
                      SURF_AD[surf], c.minUs, c.midUs, c.maxUs);
        break;
      }
      ayar[surf].minUs = c.minUs;
      ayar[surf].midUs = c.midUs;
      ayar[surf].maxUs = c.maxUs;
      ayar[surf].ters  = (c.flags & RC_CFGF_REVERSE) ? 1 : 0;
      // Yuzeyi hemen yeni notrune al: operator degisikligi aninda gorsun.
      if (surf != RC_SURF_THROTTLE && testSurf != surf && sweepSurf != surf) {
        cikisYaz(surf, ayar[surf].midUs);
      }
      Serial.printf("[KAL] %s = %u/%u/%u ters=%u\n", SURF_AD[surf],
                    c.minUs, c.midUs, c.maxUs, ayar[surf].ters);
      break;

    case RC_CFG_TEST:
      // midUs alani burada HAM darbe degeri tasiyor (uc nokta kontrolu).
      // Gaz cikisi bu yolla ASLA surulmez: motoru kalibrasyon ekranindan
      // dondurmenin guvenli bir yolu yok.
      if (!kalibModu || surf == RC_SURF_THROTTLE) { kalibRed = true; break; }
      sweepSurf   = 0xFF;
      testSurf    = surf;
      testBitisMs = millis() + KALIB_TEST_MS;
      cikisYaz(surf, c.midUs);
      break;

    case RC_CFG_SWEEP:
      if (!kalibModu || surf == RC_SURF_THROTTLE) { kalibRed = true; break; }
      testSurf     = 0xFF;
      sweepSurf    = surf;
      sweepBaslaMs = millis();
      Serial.printf("[KAL] %s tarama basladi.\n", SURF_AD[surf]);
      break;

    case RC_CFG_CENTER:
      if (!kalibModu) { kalibRed = true; break; }
      kalibTestleriDurdur();
      guvenliKonum();
      break;

    case RC_CFG_SAVE:
      if (!kalibModu) { kalibRed = true; break; }
      if (ayarKaydet()) Serial.println("[KAL] Ayarlar NVS'e KAYDEDILDI.");
      else { kalibRed = true; Serial.println("[KAL] NVS yazma HATASI."); }
      break;

    case RC_CFG_LOAD:
      if (!kalibModu) { kalibRed = true; break; }
      memcpy(ayar, ayarKayitli, sizeof(ayar));
      kalibTestleriDurdur();
      guvenliKonum();
      Serial.println("[KAL] Kayitli ayarlar geri yuklendi.");
      break;

    case RC_CFG_DEFAULT:
      if (!kalibModu) { kalibRed = true; break; }
      ayarVarsayilan();
      kalibTestleriDurdur();
      guvenliKonum();
      Serial.println("[KAL] Fabrika ayarlarina donuldu (henuz kaydedilmedi).");
      break;

    case RC_CFG_READ:
      break;   // sadece rapor istegi

    case RC_CFG_ESC_HI:
      // ESC gaz araligi kalibrasyonunun 1. adimi: cikisa TAM GAZ darbesi.
      // Uc kosul birden aranir - bu komut motoru gercekten dondurebilir.
      if (!kalibModu || armed || !(c.flags & RC_CFGF_PROP_OFF)) {
        kalibRed = true;
        Serial.println("[KAL] ESC_HI reddedildi (kalib modu / disarm / pervane onayi).");
        break;
      }
      escKalibHi   = true;
      escKalibHiMs = millis();
      cikisYaz(RC_SURF_THROTTLE, ayar[RC_SURF_THROTTLE].maxUs);
      Serial.printf("[KAL] ESC kalibrasyon 1/2: cikis %u us (TAM GAZ darbesi).\n",
                    ayar[RC_SURF_THROTTLE].maxUs);
      break;

    case RC_CFG_ESC_LO:
      escKalibHi = false;
      cikisYaz(RC_SURF_THROTTLE, ayar[RC_SURF_THROTTLE].minUs);
      Serial.printf("[KAL] ESC kalibrasyon 2/2: cikis %u us (STOP darbesi).\n",
                    ayar[RC_SURF_THROTTLE].minUs);
      break;

    default:
      kalibRed = true;
      break;
  }

  raporGonder(surf);
}

// Zaman asimli kalibrasyon hareketlerini yurutur. loop()'tan cagrilir.
static void kalibTick() {
  const uint32_t now = millis();

  // TEST: sure dolunca yuzeyi notre birak (servo dayanaga dayanip isinmasin)
  if (testSurf != 0xFF && (int32_t)(now - testBitisMs) >= 0) {
    cikisYaz(testSurf, ayar[testSurf].midUs);
    testSurf = 0xFF;
  }

  // SWEEP: min -> max -> min ucgen dalgasi
  if (sweepSurf != 0xFF) {
    const uint32_t gecen = now - sweepBaslaMs;
    if (gecen >= (uint32_t)KALIB_SWEEP_MS * KALIB_SWEEP_TUR) {
      cikisYaz(sweepSurf, ayar[sweepSurf].midUs);
      Serial.printf("[KAL] %s tarama bitti.\n", SURF_AD[sweepSurf]);
      sweepSurf = 0xFF;
    } else {
      const uint32_t faz = gecen % KALIB_SWEEP_MS;              // 0..3000
      const int32_t  yari = KALIB_SWEEP_MS / 2;
      const int32_t  ileri = (faz < (uint32_t)yari) ? (int32_t)faz
                                                    : (2 * yari - (int32_t)faz);
      const CikisAyar& c = ayar[sweepSurf];
      cikisYaz(sweepSurf, (uint16_t)((int32_t)c.minUs +
               ileri * ((int32_t)c.maxUs - (int32_t)c.minUs) / yari));
    }
  }

  // ESC kalibrasyonunda 1. adimda unutulmus olabiliriz - kendiliginden in.
  if (escKalibHi && now - escKalibHiMs > KALIB_ESC_HI_MS) {
    escKalibHi = false;
    cikisYaz(RC_SURF_THROTTLE, ayar[RC_SURF_THROTTLE].minUs);
    Serial.println("[KAL] ESC kalibrasyonu zaman asimi - cikis STOP'a alindi.");
  }
}

// ============================================================
// Paket isleme ve tasima katmanlari
// ============================================================

// ------------------------------------------------------------
// Gelen bir kontrol paketini dogrular ve uygular. IKI TASIMA KATMANI DA
// buraya girer, yani dogrulama / arm kilidi / failsafe kurallari tek yerde.
// ------------------------------------------------------------
static void paketIsle(const RcPacket& p) {
  if (!rcPacketValid(p)) {            // magic + CRC
    winBozuk++;
    ardisikGecerli = 0;               // "ardisik" sarti bozuldu
    return;
  }
  if ((uint8_t)(p.flags >> 4) != RC_PROTO_VER) {
    winBozuk++;                       // farkli protokol surumu
    ardisikGecerli = 0;
    return;
  }

  // ------------------------------------------------------------
  // Paket kaybi ve tekrar tespiti - KAYAN PENCERE ile.
  //
  // Iki tasima katmani ayni seq'i tasir ve gecikmeleri FARKLIDIR: nRF24
  // kopyasi 3 derinlikli bir FIFO'dan loop() icinde bosaltilir, ESP-NOW
  // kopyasi tek slotlu bir tampondan gelir. Bu yuzden paketler SIRASIZ
  // gelebilir: 100, 102, 101, 103...
  //
  // Isaretsiz fark zaten yanlisti (99 - 100 = 255) ve isaretli farka
  // gecilerek duzeltilmisti. Ama tek bir "en yuksek seq" degeri hala
  // yetmiyordu:
  //   100 gelir -> lastSeq = 100
  //   102 gelir -> beklenen += 2, alinan += 1, lastSeq = 102
  //   101 gelir -> delta < 0 -> "tekrar" sayilip ATILIR
  // Sonuc: dort paketin dordu de ulasmisken %25 kayip raporlanir.
  //
  // Olcum bunu dogruladi: kumanda saniyede 50 pakette 50 ACK aliyordu (ACK'i
  // ucagin telsizi paketi alip CRC'den gecirdigi an uretir, yani hepsi
  // ulasmisti) ama ucak %6-10 kayip bildiriyordu. Iki ifade birden dogru
  // olamaz; yanlis olan olcumdu. Ve yanlis gosterge teshisi saatlerce
  // yanlis yere goturuyor.
  //
  // Artik son 32 seq icin bir bit maskesi tutuluyor: bir paket YALNIZCA biti
  // zaten isaretliyse tekrardir.
  //
  // CIKISLARA UYGULAMA KURALI DEGISMEDI. Asagidaki "uygula" kapisi eskiden
  // "!tekrar" ile birebir ayni kosul: yalnizca ILERI giden paket uygulanir,
  // en yeni deger kazanir. Sirasiz gelen eski bir paketi uygulamak yuzeyi
  // 20 ms geriye goturmek olurdu. Degisen tek sey SAYACLAR.
  // ------------------------------------------------------------
  bool tekrar = false;   // bu seq daha once GORULDU mu (olcum)
  bool uygula = false;   // cikislara yazilacak mi (en yeni kazanir)

  if (!seqBasladi) {
    lastSeq     = p.seq;
    seqBasladi  = true;
    seqMaske    = 1;               // bit 0 = lastSeq goruldu
    winBeklenen += 1;
    winAlinan++;
    uygula = true;
  } else {
    const int8_t delta = (int8_t)((uint8_t)(p.seq - lastSeq));
    if (delta > 0) {
      // Ileri gidis: pencereyi kaydir. 32 ve uzeri bosluk pencereyi tumden
      // gecersiz kilar (o kadar bosluk zaten gercek kayip demek).
      seqMaske     = (delta >= 32) ? 0u : (seqMaske << delta);
      seqMaske    |= 1u;
      lastSeq      = p.seq;
      winBeklenen += (uint16_t)delta;
      winAlinan++;
      uygula = true;
    } else {
      const uint8_t geri = (uint8_t)(-delta);
      if (geri < 32 && !(seqMaske & (1u << geri))) {
        // Sirasiz geldi ama BU SEQ ILK KEZ goruluyor: kayip degil.
        // winBeklenen artmiyor - bu seq'in payi ileri giderken sayilmisti.
        seqMaske |= (1u << geri);
        winAlinan++;
      } else {
        tekrar = true;              // gercek tekrar ya da pencere disi
      }
    }
  }
  winTekrar += tekrar ? 1 : 0;
  lastRxMs = millis();

  // Ayni paketi iki kez uygulamanin/telemetriyi iki kez gondermenin anlami
  // yok; sirasiz gelen eski bir paketi uygulamanin da yok.
  if (!uygula) return;

  // Kumandadan bir kez ARMED=0 gorulunce arm kilidi acilir
  const bool paketArmed = (p.flags & RC_FLAG_ARMED) != 0;
  sonPaketArmed = paketArmed;
  if (armKilidi && !paketArmed) {
    armKilidi = false;
    Serial.println("[SAFE] Arm kilidi acildi (kumanda DISARM'da).");
  }

  // Failsafe'ten cikis tek pakete degil, ardisik gecerli pakete bagli
  if (failsafe) {
    if (++ardisikGecerli >= RELINK_PAKET) {
      failsafe       = false;
      ardisikGecerli = 0;
      Serial.println("[SAFE] Baglanti geri geldi.");
    }
  }

  // Kalibrasyon modunda ARM imkansiz: motor kilitli, yuzeyleri kalibrasyon
  // komutlari suruyor.
  armed = paketArmed && !armKilidi && !kalibModu;

  if (!kalibModu) {
    // Yuzeyler failsafe cikisini beklemeden takip eder (zararsiz, faydali);
    // gaz ise escYaz() icinde failsafe/armed kapisindan gecer.
    yuzeyleriYaz(rcClampUs(p.ch[RC_CH_ELEVATOR]),
                 rcClampUs(p.ch[RC_CH_RUDDER]),
                 rcClampUs(p.ch[RC_CH_AILERON]));
  }
  escYaz(rcClampUs(p.ch[RC_CH_THROTTLE]));

  // Yalnizca ESP-NOW yayini. nRF24 ACK payload'i buradan YUKLENMEZ: onu
  // radioReceive() yukluyor, cunku tuketim orada oluyor (bkz.
  // telemetriAckYukle notu). Burasi yalnizca BENZERSIZ paketlerde kosuyor,
  // oysa ACK'i alinan HER nRF24 paketi tuketiyor.
  //
  // Kalibrasyon modunda hic gondermiyoruz: geri yolu loop() icindeki sirali
  // tazeleme yonetiyor (telemetri / rapor donusumlu), yoksa 50 Hz telemetri
  // kalibrasyon raporlarini disari itiyor ve ekrandaki degerler donuyor.
  if (!kalibModu) telemetriYayinla(p.seq);
}

// ------------------------------------------------------------
// ESP-NOW
// ------------------------------------------------------------

// WiFi gorevi baglaminda cagrilir. Burada servo yazmak/log basmak yok:
// sadece kopyala ve isaretle, isi loop() yapar. Kontrol verisinde en yeni
// paket onemli oldugu icin bekleyen paket varsa uzerine yazilir; kalibrasyon
// komutlarinda ise HER komut onemli, bu yuzden kucuk bir halka tampon var.
static volatile bool espnowYeni = false;
static RcPacket      espnowBuf;

static const uint8_t CFG_KUYRUK = 4;
static RcConfigPacket   cfgKuyruk[CFG_KUYRUK];
static volatile uint8_t cfgYaz = 0, cfgOku = 0;

static portMUX_TYPE espnowMux = portMUX_INITIALIZER_UNLOCKED;

static void cfgKuyrugaAl(const RcConfigPacket& c) {
  const uint8_t sonraki = (uint8_t)((cfgYaz + 1) % CFG_KUYRUK);
  if (sonraki == cfgOku) return;      // dolu - en eskiyi korumak daha iyi
  cfgKuyruk[cfgYaz] = c;
  cfgYaz = sonraki;
}

static void onEspNowRecv(const uint8_t* mac, const uint8_t* data, int len) {
  if (len == sizeof(RcPacket) && data[0] == RC_MAGIC_CTRL) {
    portENTER_CRITICAL(&espnowMux);
    memcpy(&espnowBuf, data, sizeof(RcPacket));
    memcpy(kumandaMac, mac, 6);     // gonderenin adresini ogren
    kumandaMacGeldi = true;
    espnowYeni = true;
    portEXIT_CRITICAL(&espnowMux);
    return;
  }
  if (len == sizeof(RcConfigPacket) && data[0] == RC_MAGIC_CFG) {
    RcConfigPacket c;
    memcpy(&c, data, sizeof(c));
    portENTER_CRITICAL(&espnowMux);
    cfgKuyrugaAl(c);
    portEXIT_CRITICAL(&espnowMux);
  }
}

// Gonderim sonucu callback'i. Yayinda her zaman SUCCESS gelir; sadece
// gonderim yolunun ayakta oldugunu dogrulamaya yariyor.
static void onEspNowSend(const uint8_t* mac, esp_now_send_status_t durum) {
  (void)mac;
  if (durum == ESP_NOW_SEND_SUCCESS) nowCbOk++;
  else                               nowCbFail++;
}

// Kumandanin adresini es olarak kaydeder. esp_now_add_peer() WiFi gorevi
// icinden cagrilmamali, bu yuzden ogrenme callback'te, kayit burada.
static void kumandaEsiKaydet() {
  uint8_t mac[6];
  portENTER_CRITICAL(&espnowMux);
  memcpy(mac, kumandaMac, 6);
  portEXIT_CRITICAL(&espnowMux);

  if (esp_now_is_peer_exist(mac)) { kumandaEsiVar = true; return; }

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, mac, 6);
  peer.channel = ESPNOW_CHANNEL;
  peer.ifidx   = WIFI_IF_STA;
  peer.encrypt = false;

  if (esp_now_add_peer(&peer) == ESP_OK) {
    kumandaEsiVar = true;
    Serial.printf("[NOW] Kumanda adresi: %02X:%02X:%02X:%02X:%02X:%02X"
                  " (telemetri yayinla gidiyor)\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  }
}

static void espnowReceive() {
  if (!espnowOk) return;

  // Kalibrasyon komutlari once: kuyrukta bekleyen varsa hepsini isle.
  for (;;) {
    RcConfigPacket c;
    bool var = false;
    portENTER_CRITICAL(&espnowMux);
    if (cfgOku != cfgYaz) { c = cfgKuyruk[cfgOku]; cfgOku = (uint8_t)((cfgOku + 1) % CFG_KUYRUK); var = true; }
    portEXIT_CRITICAL(&espnowMux);
    if (!var) break;
    konfigIsle(c);
  }

  if (!espnowYeni) return;

  // Kumandanin adresi ilk pakette ogrenilir, es kaydi bir kez yapilir.
  if (kumandaMacGeldi && !kumandaEsiVar) kumandaEsiKaydet();

  RcPacket p;
  portENTER_CRITICAL(&espnowMux);
  memcpy(&p, &espnowBuf, sizeof(p));
  espnowYeni = false;
  portEXIT_CRITICAL(&espnowMux);

  paketIsle(p);
}

static bool espnowSetup() {
  if (!USE_ESPNOW) return false;

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);    // modem sleep acik kalirsa paket kaciriliyor

  if (AP_BAGLAN) {
    // BLOKLAMIYORUZ: baglanti arka planda kurulur. ESP-NOW iliskilenmeyi
    // beklemeden calisir, yani kontrol yolu bu satirda askiya alinmaz.
    // Iliskilenince kanal kumandanin AP kanalina kilitlenir.
    WiFi.setAutoReconnect(true);
    WiFi.begin(AP_SSID, AP_PASS);
    Serial.printf("[NOW] Kumandanin AP'sine baglaniliyor: %s\n", AP_SSID);
  } else {
    WiFi.disconnect();     // hicbir aga baglanmiyoruz, sadece radyo lazim
    // Baglanti kurmadan kanal sabitlemenin tek yolu: promiscuous'u kisa sureli ac
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);
  }

  if (esp_now_init() != ESP_OK) {
    Serial.println("[NOW] esp_now_init basarisiz.");
    return false;
  }
  esp_now_register_recv_cb(onEspNowRecv);
  esp_now_register_send_cb(onEspNowSend);

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, BCAST, 6);
  peer.channel = ESPNOW_CHANNEL;
  peer.ifidx   = WIFI_IF_STA;
  peer.encrypt = false;

  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("[NOW] yayin esi eklenemedi.");
    return false;
  }

  // Yazdigimiz degil, surucunun GERCEKTEN oturdugu kanal. Iki taraf ayni
  // kanalda gorunmuyorsa ESP-NOW tek yonlu calisabiliyor ve sebep burada
  // gorunur.
  uint8_t gercekKanal = 0;
  wifi_second_chan_t ikincil = WIFI_SECOND_CHAN_NONE;
  esp_wifi_get_channel(&gercekKanal, &ikincil);

  Serial.printf("[NOW] Hazir. Yayin modu (STA arayuzu), istenen kanal %u, "
                "GERCEK kanal %u, STA MAC %s\n",
                ESPNOW_CHANNEL, gercekKanal, WiFi.macAddress().c_str());
  if (gercekKanal != ESPNOW_CHANNEL)
    Serial.printf("[NOW] !! KANAL UYUSMUYOR: %u istendi, surucu %u'de. "
                  "Kumandanin AP_CHANNEL degeriyle ayni olmali.\n",
                  ESPNOW_CHANNEL, gercekKanal);
  return true;
}

// ============================================================
// Telsiz (nRF24)
// ============================================================
static void radioSetup() {
  if (!USE_NRF24) return;

  SPI.begin(PIN_RF_SCK, PIN_RF_MISO, PIN_RF_MOSI, PIN_RF_CSN);
  delay(10);   // modulun power-on-reset'i (datasheet: 4.5 ms + 14 us)

  if (!radio.begin(&SPI)) {
    // Ayrinti sadece ilk basarisizlikta basilir; sonrasinda 3 sn'de bir
    // denendigi icin log akip gitmesin. Durum zaten [RX] satirinda gorunuyor.
    if (!nrfHataBasildi) {
      nrfHataBasildi = true;
      Serial.println("[RF] nRF24 BAGLI DEGIL! SPI'dan gecerli cevap gelmiyor.");
      Serial.println("[RF] Kontrol: 3V3 beslemesi (5V DEGIL), VCC-GND arasi 10uF,");
      Serial.printf ("[RF]          SCK=%u MISO=%u MOSI=%u CSN=%u CE=%u kablolari.\n",
                     PIN_RF_SCK, PIN_RF_MISO, PIN_RF_MOSI, PIN_RF_CSN, PIN_RF_CE);
      Serial.printf ("[RF] %lu ms'de bir yeniden denenecek.\n", (unsigned long)RF_RETRY_MS);
    }
    radioOk = false;
    return;
  }
  nrfHataBasildi = false;

  // Ayarlarin hepsi kumanda tarafiyla birebir ayni olmali
  //
  // PA SEVIYESI - bir ara LOW'a dusuruldu, sonra GERI YUKSELTILDI.
  //
  // Neden dusurulmustu: link kumanda boot ettikten ~2 sn sonra oluyor, modul
  // SPI'dan cevap vermeye devam ederken radyo susuyor ve BIR DAHA KENDINE
  // GELMIYORDU. Yayin anindaki akim tepesi gibi gorunuyordu.
  //
  // Neden geri yukseltildi: o KALICILIGIN asil sebebi PA degildi - MAX_RT'de
  // TX FIFO'nun bosaltilmamasiydi (bkz. kumanda tarafindaki radioPoll notu).
  // Uc ardisik basarisiz gonderim telsizi kalici olarak kilitliyordu ve
  // menzil kenarinda uc ardisik basarisizlik cok kolay olusuyor. O kilit
  // kirildi, ustune sagir modul kurtarmasi eklendi: brown-out olsa bile
  // telsiz kilitlenmek yerine kendine geliyor.
  //
  // PA_LOW'da menzil kapali alanda birkac duvari gecemiyordu. LOW -> HIGH
  // ~+6 dB, kabaca menzilin iki kati. Brown-out geri gelirse gizlenmiyor:
  // ucakta "kurt=N", kumandada "kurtarma=N" sayaclari artar. Artiyorsa
  // besleme yetersiz (modul bacagina 10-100uF + 100nF seramik, servolara
  // ayri UBEC) ve ancak o zaman PA'yi dusurmek anlamli.
  radio.setPALevel(RF24_PA_HIGH);

  // setDataRate() ayari yazip GERI OKUR; tutmadiysa false doner. Donus degeri
  // kontrol edilmezse klon cip sessizce 1 Mbps'te kalir ve link olur.
  const bool hizOk = radio.setDataRate(rfVeriHizi());

  radio.setChannel(RC_RF_CHANNEL);
  radio.setCRCLength(RF24_CRC_16);
  radio.enableDynamicPayloads();
  radio.enableAckPayload();             // telemetri ACK ile geri doner
  radio.openReadingPipe(1, RC_RF_ADDRESS);
  radio.startListening();               // alici modu
  radioOk = true;
  telemetriYukle(lastSeq);              // ilk ACK icin payload'i onden yukle

  Serial.printf("[RF] nRF24 BAGLI. Kanal %u, adres %s, paket %u byte\n",
                RC_RF_CHANNEL, (const char*)RC_RF_ADDRESS, (unsigned)sizeof(RcPacket));

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
}

// nRF24 SU AN gercekten bagli mi? isChipConnected() canli bir register
// okumasi yapar (SETUP_AW okunup gecerli araliktalik kontrolu), yani boot'taki
// bayraga degil o anki duruma bakar. Kablo calisirken cikarsa yakalar.
static bool nrfBagli() {
  return radioOk && radio.isChipConnected();
}

// Gelen paketleri isler. Boyut + magic ikilisi paket tipini belirler.
static void radioReceive() {
  if (!radioOk) return;

  uint8_t pipe;
  uint8_t alinan = 0;                   // bu turda nRF24'ten kac paket okundu
  while (radio.available(&pipe)) {
    alinan++;
    nrfAlinan++;
    nrfSonRxMs = millis();
    const uint8_t len = radio.getDynamicPayloadSize();

    // Surum 3'te RcPacket ve RcConfigPacket AYNI boyutta (12 byte), bu
    // yuzden tip artik boyuttan degil MAGIC'ten ayirt ediliyor. Boyut
    // yalnizca ikinci dogrulama.
    uint8_t ham[32];
    if (len == 0 || len > sizeof(ham)) { radio.flush_rx(); winBozuk++; break; }
    radio.read(ham, len);

    if (ham[0] == RC_MAGIC_CTRL && len == sizeof(RcPacket)) {
      RcPacket p;
      memcpy(&p, ham, sizeof(p));
      paketIsle(p);
    } else if (ham[0] == RC_MAGIC_CFG && len == sizeof(RcConfigPacket)) {
      RcConfigPacket c;
      memcpy(&c, ham, sizeof(c));
      konfigIsle(c);
    } else {
      winBozuk++;                       // yabanci imza/boy -> at
    }
  }

  // Okunan her nRF24 paketi bir ACK payload TUKETTI (ACK'i donanim gonderdi,
  // biz paketi okumadan once). Tuketileni hemen yerine koy - telemetrinin
  // nRF24 uzerinden akmasi tam olarak buna bagli.
  //
  // Kalibrasyon modunda karismiyoruz: o modda geri yolu loop() icindeki
  // sirali tazeleme yonetiyor, buradan yuk basmak raporlari disari itiyor.
  if (alinan && !kalibModu) telemetriAckYukle(lastSeq);
}

// ============================================================
void setup() {
  Serial.begin(115200);  // UART0, kartin USB koprusu uzerinden
  delay(500);
  Serial.println("\n=== RC Plane ucak (alici) basliyor ===");
  Serial.printf("[BOOT] Protokol surumu %u, kanal sayisi %u\n",
                RC_PROTO_VER, (unsigned)RC_CH_COUNT);

  // NEDEN yeniden basladik? "Motora guc verince disarm oluyor" sikayetinin
  // en olasi sebebi kartin o anda RESETLENMESI: reset -> failsafe + arm
  // kilidi -> DISARM. Bu satir tahmini bitirir.
  //   BROWNOUT  = besleme cokmesi. ESC/BEC akim tepesini karsilamiyor ya da
  //               servolar karti besliyor. Kod bunu duzeltemez.
  //   PANIC/WDT = yazilim hatasi.
  //   POWERON   = normal ilk acilis.
  //   SW/USB    = yukleme veya elle reset.
  // Bu satiri motora guc verdikten sonra TEKRAR goruyorsan sorun besleme.
  const esp_reset_reason_t sebep = esp_reset_reason();
  const char* sebepAd =
      sebep == ESP_RST_POWERON  ? "POWERON (normal acilis)"
    : sebep == ESP_RST_BROWNOUT ? "BROWNOUT (BESLEME COKTU!)"
    : sebep == ESP_RST_PANIC    ? "PANIC (yazilim hatasi)"
    : sebep == ESP_RST_INT_WDT  ? "INT_WDT"
    : sebep == ESP_RST_TASK_WDT ? "TASK_WDT"
    : sebep == ESP_RST_WDT      ? "WDT"
    : sebep == ESP_RST_SW       ? "SW (yazilimdan reset)"
    : sebep == ESP_RST_DEEPSLEEP? "DEEPSLEEP"
    : sebep == ESP_RST_EXT      ? "EXT (harici reset pini)"
                                : "BILINMIYOR";
  Serial.printf("[BOOT] Reset sebebi: %s (kod %d)\n", sebepAd, (int)sebep);
  if (sebep == ESP_RST_BROWNOUT) {
    Serial.println("[BOOT] !! BESLEME COKMESI. Servo/ESC akimi karti dusuruyor.");
    Serial.println("[BOOT]    5V hattindaki kondansatoru ve GND baglantisini kontrol et;");
    Serial.println("[BOOT]    surerse UBEC akimina ve kablo kesitine bak. YAZILIM sorunu degil.");
  }

  // Kalibrasyon ayarlarini once yukle: LEDC kanallari ilk degerlerini
  // kalibre edilmis notrden alsin, 1500'den degil.
  const bool nvsVar = ayarYukle();
  Serial.printf("[KAL] Ayarlar %s.\n",
                nvsVar ? "NVS'ten yuklendi" : "VARSAYILAN (NVS bos ya da gecersiz)");
  for (uint8_t i = 0; i < RC_SURF_COUNT; i++) {
    Serial.printf("[KAL] %-9s min=%4u %-5s=%4u max=%4u ters=%u\n",
                  SURF_AD[i], ayar[i].minUs,
                  i == RC_SURF_THROTTLE ? "tavan" : "notr",
                  ayar[i].midUs, ayar[i].maxUs, ayar[i].ters);
  }

  // ESC kanalini kur ve arm et: once stop darbesi, sonra 3 saniye bekle.
  // ledcSetup 0 dondurduyse kanal HIC kurulmamistir ve o pinde asla PWM
  // olusmaz. Sessizce basarisiz olmasi cok pahaliya patliyor (servolar
  // olu, pinlerde 0 V, sebep gorunmuyor) - bu yuzden gurultulu bagiriyoruz.
  uint32_t f = ledcSetup(KANAL_ESC, 50, LEDC_BIT);
  ledcAttachPin(PIN_ESC, KANAL_ESC);
  cikisYaz(RC_SURF_THROTTLE, ayar[RC_SURF_THROTTLE].minUs);
  Serial.printf("ESC       pin %2d -> LEDC kanal %d (%u Hz)\n",
                PIN_ESC, KANAL_ESC, (unsigned)f);
  if (f == 0) Serial.println("[!!] ESC LEDC KANALI KURULAMADI - pinde sinyal olmayacak!");

  // Yuzey servolarini kur ve kalibre edilmis notrune al.
  // Aileron cikislari 3 kanalli ucakta bos durur - servo takili degilse
  // pinde sinyal olmasi zararsiz, kanat degisiminde hazir bekliyor.
  const uint8_t servoSurf[3] = { RC_SURF_ELEVATOR, RC_SURF_RUDDER, RC_SURF_AILERON };
  for (uint8_t i = 0; i < 3; i++) {
    const uint8_t s = servoSurf[i];
    f = ledcSetup(SURF_KANAL[s], 50, LEDC_BIT);
    ledcAttachPin(SURF_PIN[s], SURF_KANAL[s]);
    cikisYaz(s, ayar[s].midUs);
    Serial.printf("%-9s pin %2d -> LEDC kanal %d (%u Hz)\n",
                  SURF_AD[s], SURF_PIN[s], SURF_KANAL[s], (unsigned)f);
    if (f == 0) Serial.printf("[!!] %s LEDC KANALI KURULAMADI - pinde sinyal olmayacak!\n",
                              SURF_AD[s]);
  }

  // Sag aileron: ayri bir kalibrasyon yuzeyi degil, sol'un aynasi.
  {
    const uint32_t fa = ledcSetup(KANAL_AIL_SAG, 50, LEDC_BIT);
    ledcAttachPin(PIN_AIL_SAG, KANAL_AIL_SAG);
    cikisYaz(RC_SURF_AILERON, ayar[RC_SURF_AILERON].midUs);   // ikisini de notre alir
    Serial.printf("%-9s pin %2d -> LEDC kanal %d (%u Hz)  [sol'un aynasi]\n",
                  "Ail sag", PIN_AIL_SAG, KANAL_AIL_SAG, (unsigned)fa);
    if (fa == 0) Serial.println("[!!] Ail sag LEDC KANALI KURULAMADI!");
  }

  // ESC gaz araligi kalibrasyon penceresi. Klasik ESP32'de "USB bagli mi"
  // bilinemedigi icin her boot'ta 5 sn acik (bkz. onayBekle notu). Bu surede
  // ESC stop darbesinde, telsizler kurulmamis ve failsafe true - pencere
  // ucusa hicbir sey sizdirmiyor, yalnizca acilisi uzatiyor.
  while (Serial.available()) Serial.read();   // boot'tan kalan copu yut
  Serial.println("ESC gaz araligi kalibrasyonu icin 5 saniye icinde 'k' bas.");
  {
    const uint32_t bitis = millis() + 5000;
    while ((int32_t)(millis() - bitis) < 0) {
      if (Serial.available() && (Serial.read() | 0x20) == 'k') {
        escKalibrasyon();
        break;
      }
      delay(10);
    }
  }

  Serial.println("ESC arming... 3 saniye (PERVANE TAKILI OLMASIN!)");
  delay(3000);
  Serial.println("ESC hazir.");

  espnowOk = espnowSetup();   // WiFi radyosunu ayaga kaldirir
  radioSetup();               // nRF24 (varsa)

  // telemetriYukle radioOk/espnowOk'a bakiyor, ikisi de kurulduktan sonra cagir
  telemetriYukle(0);

  if (!radioOk && !espnowOk) {
    Serial.println("[!] HICBIR TELSIZ YOLU ACIK DEGIL - kumandadan veri alinamaz.");
  } else {
    Serial.printf("[RF] Aktif yol: %s\n",
                  (radioOk && espnowOk) ? "nRF24 + ESP-NOW"
                  : radioOk ? "nRF24" : "ESP-NOW");
  }

  failsafe   = true;      // ilk gecerli pakete kadar guvenli konumda kal
  lastRxMs   = millis();
  lastStatMs = millis();
  Serial.printf("[SAFE] Gaz tavani %u us. Failsafe %u ms.\n",
                ayar[RC_SURF_THROTTLE].midUs, RC_FAILSAFE_MS);
}

void loop() {
  radioReceive();     // nRF24
  espnowReceive();    // ESP-NOW
  kalibTick();        // kalibrasyon test/tarama zaman asimlari

  const uint32_t now = millis();

  // Kumanda sustuysa failsafe
  if (!failsafe && (now - lastRxMs > RC_FAILSAFE_MS)) {
    failsafe = true;
    failsafeUygula();
    Serial.println("[SAFE] FAILSAFE: paket yok, motor durdu, yuzeyler notr.");
  }

  // Telemetriyi paket gelisinden bagimsiz olarak tazele. Kumandanin ARM sarti
  // "1 sn icinde taze telemetri"; bu tazeleme olmadan tek bir paket boslugu
  // bile kumandanin "ucaktan cevap yok" demesine yol acabiliyor.
  //
  // Kalibrasyon modunda telemetri ile rapor SIRAYLA gonderilir: ayni geri
  // yol (ACK payload) ikisini de tasiyor, biri digerini ac birakmamali.
  static uint32_t lastTlmYenileMs = 0;
  static bool     raporSirasi     = false;
  static uint8_t  raporDonguSurf  = 0;
  if (now - lastTlmYenileMs >= (kalibModu ? TLM_YENILE_MS / 2 : TLM_YENILE_MS)) {
    lastTlmYenileMs = now;
    if (kalibModu && (raporSirasi = !raporSirasi)) {
      // Uc cikisi SIRAYLA raporla. Sadece son komut verilen cikisi
      // gondermek, kalibrasyon ekranindaki diger iki karti kumandanin
      // ayrica READ istemesine bagimli birakiyordu; boylece ekran
      // kendiliginden ve tam doluyor.
      raporGonder(raporDonguSurf);
      raporDonguSurf = (uint8_t)((raporDonguSurf + 1) % RC_SURF_COUNT);
    } else {
      // nRF24'ten bu pencerede paket geldi mi? Geldiyse kuyruk tuketiliyor
      // ve zaten taze; gelmediyse bayat yukleri atip yeniden doldur.
      if (nrfAlinan == nrfAlinanOnce) ackFifoTazele(lastSeq);
      else                            telemetriAckYukle(lastSeq);
      nrfAlinanOnce = nrfAlinan;
      telemetriYayinla(lastSeq);
    }
  }

  // nRF24 bulunamadiysa periyodik olarak yeniden dene. Kabloyu duzeltince
  // kart resetlemeye gerek kalmadan durum satiri BAGLI'ya doner.
  static uint32_t lastRfRetryMs = 0;
  if (USE_NRF24 && !radioOk && now - lastRfRetryMs >= RF_RETRY_MS) {
    lastRfRetryMs = now;
    radioSetup();
  }

  // SAGIR MODUL: radioOk true, cip SPI'dan cevap veriyor, ama radyo hicbir
  // sey duymuyor. Kumandanin yayin yaptigini DIGER yoldan gelen paketlerden
  // biliyoruz (lastRxMs taze), dolayisiyla nRF24 de duymali. Duymuyorsa
  // cipi bastan kur - yoksa link reset atilana kadar olu kalir.
  if (USE_NRF24 && radioOk && !failsafe &&
      now - lastRxMs   < RC_FAILSAFE_MS &&
      now - nrfSonRxMs > NRF_SAGIR_MS &&
      now - lastRfRetryMs >= RF_RETRY_MS) {
    lastRfRetryMs = now;
    nrfKurtarma++;
    Serial.printf("[RF] nRF24 SAGIR (%lu ms paket yok, kumanda yayinda) -"
                  " cip bastan kuruluyor.\n",
                  (unsigned long)(now - nrfSonRxMs));
    radioOk = false;        // radioSetup() bunu tekrar true yapacak
    radioSetup();
    nrfSonRxMs = now;       // kurtarmaya sans ver, hemen tekrar tetiklenmesin
  }

  // 1 sn'de bir paket kaybi + log
  if (now - lastStatMs >= 1000) {
    lastStatMs = now;

    // Surucunun O ANKI kanali. Calisirken kayarsa ESP-NOW sessizce tek yonlu
    // olur; boot'ta bir kez basmak bunu yakalamiyor.
    uint8_t nowKanal = 0;
    wifi_second_chan_t ikincilKanal = WIFI_SECOND_CHAN_NONE;
    if (espnowOk) esp_wifi_get_channel(&nowKanal, &ikincilKanal);
    // winAlinan sadece benzersiz paketleri sayiyor, yani her zaman
    // winBeklenen'den kucuk esit. Yine de tasmaya karsi acik korumali:
    // isaretsiz cikarma bir kez negatife duserse yuzde 255'e firlar.
    if (!winBeklenen)                  lossPct = 100;
    else if (winAlinan >= winBeklenen) lossPct = 0;
    else lossPct = (uint8_t)(100 - (uint32_t)winAlinan * 100 / winBeklenen);
    rxHz = (uint8_t)(winAlinan > 255 ? 255 : winAlinan);

    // Modulun O ANKI durumu. Calisirken kablo cikarsa bayragi dusur ki
    // yukaridaki yeniden deneme devreye girsin.
    const bool nrf = nrfBagli();
    if (radioOk && !nrf) {
      radioOk = false;
      nrfHataBasildi = false;   // kopus ayrintisi tekrar basilabilsin
      Serial.println("[RF] nRF24 BAGLI DEGIL - modul cevap vermeyi birakti.");
    }

    Serial.printf("[RX] nrf=%-11s now=%-6s | %s%s%s rxarm=%u kilit=%-5s relink=%u/%u | "
                  "thr=%u<-%u(%s) ele=%u->%u rud=%u->%u ail=%u->%u | %u paket, tekrar %u, "
                  "kayip %%%u, bozuk %u | tlm nrf=%u/red%u now=%u/hata%u "
                  "cb=%u/fail%u kanal=%u ap=%s kurt=%u %s\n",
                  !USE_NRF24 ? "KAPALI" : nrf ? "BAGLI" : "BAGLI-DEGIL",
                  !USE_ESPNOW ? "KAPALI" : espnowOk ? "BAGLI" : "YOK",
                  kalibModu ? "KALIBRASYON " : "",
                  failsafe ? "FAILSAFE " : "",
                  armed ? "ARMED " : "DISARM",
                  (unsigned)sonPaketArmed,
                  armKilidi ? "KAPALI" : "ACIK",
                  (unsigned)ardisikGecerli, (unsigned)RELINK_PAKET,
                  sonThr, sonThrIstek, sonThrSebep,
                  sonEle, sonCikis[RC_SURF_ELEVATOR],
                  sonRud, sonCikis[RC_SURF_RUDDER],
                  sonAil, sonCikis[RC_SURF_AILERON],
                  winAlinan, winTekrar, lossPct, winBozuk,
                  tlmNrf, tlmNrfRed, tlmNow, tlmNowHata,
                  nowCbOkHz, nowCbFailHz, (unsigned)nowKanal,
                  !AP_BAGLAN ? "kapali" : WiFi.status() == WL_CONNECTED ? "BAGLI" : "yok",
                  nrfKurtarma,
                  tlmNowHata ? esp_err_to_name(tlmSonHata) : "");

    winBeklenen = winAlinan = winBozuk = winTekrar = 0;
    tlmNrf = tlmNrfRed = tlmNow = tlmNowHata = 0;
    nowCbOkHz = nowCbOk;     nowCbOk   = 0;
    nowCbFailHz = nowCbFail; nowCbFail = 0;
    if (failsafe) {                     // baglanti koptuysa seq takibini sifirla
      seqBasladi = false;
      seqMaske   = 0;
    }
  }
}
