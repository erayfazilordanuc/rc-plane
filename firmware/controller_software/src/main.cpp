// ============================================================
// RC Plane - KUMANDA (verici) yazilimi      -- UCUSA HAZIR SURUM --
// Kart: ESP32 DevKit (ESP-WROOM-32) + nRF24L01
//
// UCAK 3 KANALLI: GAZ (ESC) + ELEVATOR + RUDDER.  Aileron YOK.
//
// Ne yapar:
//   1. WiFi erisim noktasi acar  -> SSID: RC-Plane-TX
//   2. 192.168.4.1 adresinde gercek kumanda gorunumlu arayuz sunar:
//      iki gimbal (kol yuvasi), trim, expo/dual-rate, kalkis oncesi kontrol
//   3. Kol konumlarini expo/oran/trim'den gecirip 50 Hz'de ucaga gonderir
//   4. Ucaktan telemetri okur ve arayuzde gosterir
//   5. Servo kalibrasyon komutlarini ucaga iletir ve raporlari geri okur
//
// TASIMA: kol verisi WebSocket (port 81) uzerinden gelir. WebServer'in
// her cevaba "Connection: close" koymasi 20 Hz'de gecikmeyi dalgalandiriyor;
// WebSocket'te baglanti bir kez kuruluyor. Tarayici WebSocket kuramazsa
// otomatik olarak /c HTTP yoluna duser - iki yol da her zaman acik.
//
// GUVENLIK ZINCIRI:
//   - ARM icin gaz kolu minimumda olmali.
//   - ARM icin ucaktan TAZE TELEMETRI gelmeli. Ucak cevap vermiyorsa
//     arayuz ARMED gostermez; "arm ettim ama ucak duymadi" olusamaz.
//   - Ucaktan 1 sn telemetri gelmezse otomatik DISARM.
//   - Tarayici 1 sn komut gondermezse otomatik DISARM.
//   - Ucak kalibrasyon modundayken ARM reddedilir.
//   - Gaz tavani hem burada (kol siniri) hem ucakta (bagimsiz tavan) var.
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
#include <Preferences.h>
#include "rc_protocol.h"
#include "mini_ws.h"
#include "web_ui.h"

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
// TEZGAH MODU  --  UCUSTA false OLMALI
//
// Normalde ARM icin ucaktan TAZE TELEMETRI sart. Bu, "kumandada ARMED
// yaziyor ama ucak komutu hic duymadi" durumunu imkansiz kilan kilit.
// Tezgahta telemetri yolu bozukken (ornegin ucagin 3.3V hatti yayin
// aninda cokuyorken) bu kilit ARM etmeyi tamamen engelliyor.
//
// true iken:
//   - ARM, telemetri olmadan da kabul edilir
//   - "telemetri yok" gerekcesiyle otomatik DISARM devre disi kalir
// Degismeyenler: gaz kolu minimumda olma sarti, gaz siniri, web watchdog,
// ucak tarafindaki arm kilidi ve failsafe. Yani motor yine de ancak ucak
// paketleri gercekten aliyorsa doner.
//
// PERVANE TAKILIYKEN VEYA UCUSTA ASLA true BIRAKMA. Acikken hem boot
// logunda hem her saniye [TX] satirinda hem de web arayuzunde uyari cikar.
// ------------------------------------------------------------
static const bool TEZGAH_MODU = false;

// ------------------------------------------------------------
// WiFi erisim noktasi
// AP kanali 1 = 2.412 GHz, nRF24 kanali 108 = 2.508 GHz. Aralarinda 96 MHz
// var, yani WiFi trafigi telsiz linkini bozmuyor. RC_RF_CHANNEL'i
// degistirirsen bu kanali da gozden gecir - ust uste binmemeliler.
// ------------------------------------------------------------
static const char*   AP_SSID     = "RC-Plane-TX";
static const char*   AP_PASS     = "rcplane1234";  // en az 8 karakter
static const uint8_t AP_CHANNEL  = 1;
// TEK istemci: yalnizca telefon.
//
// Bir ara 2 yapilmisti (ucagin AP'ye iliskilenmesi denenecekti); o deneme
// ise yaramadi ve geri alindi (bkz. ucak tarafindaki AP_BAGLAN notu), yani
// ikinci yuva gereksiz kaldi.
//
// Ve zararsiz degil: mini_ws TEK WebSocket istemcisi destekliyor, yeni gelen
// eskisini atiyor. Ikinci bir cihaz (ya da ikinci bir sekme) baglanirsa iki
// taraf birbirini atip duruyor, her kopusta web watchdog devreye girip GAZI
// KESIYOR. Yuvayi 1'de tutmak bu yarisi bastan engelliyor.
static const uint8_t AP_MAX_CONN = 1;              // sadece telefon

// ------------------------------------------------------------
// ARAYUZ WATCHDOG - IKI KADEMELI
//
// WEB_TIMEOUT_MS: arayuzden bu sure komut gelmezse GUC kesilir (gaz sifir,
// yuzeyler notr) ama ARM DUSMEZ.
//
// 1 sn ile baslandi ve COK SIKI oldugu olculdu: tarayici/WiFi kaynakli kisa
// takilmalar 1 sn'yi asip gazi kesiyordu, oysa telsiz linki kusursuzdu ve
// kumandanin elinde gecerli bir kol degeri vardi. 2 sn bu takilmalari
// yutuyor; gercekten olmus bir tarayici icin zaten 2. kademe var.
//
// NEDEN ARM DUSMUYOR: eskiden burada disarm() cagriliyordu ve bu, ucusta
// KURTARILAMAZ bir durum yaratiyordu. Gaz kolu yukaridayken disarm olunca
// ARM kapisi "gaz cikisi minimumda olmali" dedigi icin bir daha arm
// edilemiyor, motor kesik kaliyordu. Olcum bunu birebir gosterdi:
//     [28.2s] DISARM: arayuz baglantisi kesildi
//     [31.4s] arm 0->1 thr=939  -> RED (gaz minimumda degil)
//     ... ayni red saniyede 46 kez, pilot gazi indirene kadar
// Yani telefonun 1 saniyelik takilmasi havada motoru kalici olarak
// kesiyordu - onlem degil, bizzat kaza sebebi.
//
// Dogru davranis: GUCU KES, KONTROLU KORU. Baglanti dondugunde pilot
// aninda gaz verebilsin diye ARM korunur.
//
// WEB_OLU_MS: bu sure sonunda tarayici gercekten olmus sayilir ve DISARM
// edilir. O noktada ucak zaten suzuluyor; disarm dogru karar ve motorun
// kendiliginden geri gelmesini de imkansiz kilar.
// ------------------------------------------------------------
static const uint32_t WEB_TIMEOUT_MS = 2000;
static const uint32_t WEB_OLU_MS     = 5000;

// Telsiz bulunamadiysa bu araliklarla yeniden dene
static const uint32_t RF_RETRY_MS = 3000;

// Sagir modul kurtarmasi - ucak tarafindaki ayni isimli sabitin esi.
// Kumanda 50 Hz yayin yaptigi halde bu sure boyunca HIC ACK gelmiyorsa
// (ucak kapali olsa bile zararsiz: yeniden kurulum yalnizca SPI yazmasi)
// cip bastan kurulur. Aksi halde radioOk true kaldigi icin mevcut yeniden
// deneme yolu hic devreye girmiyor ve link reset atilana kadar olu kaliyor.
static const uint32_t NRF_SAGIR_MS = 2000;

// Bir gonderim bu sureden uzun surerse takilmis kabul edilip FIFO temizlenir.
// setRetries(3,5) en kotu halde ~12 ms surer, 15 ms guvenli ust sinir.
static const uint32_t TX_TIMEOUT_US = 15000;

// Ucaktan bu sure boyunca gecerli telemetri gelmezse baglanti yok sayilir.
// Ucak 50 Hz'de cevap verdigi icin 1 sn = 50 kacirilmis cevap, fazlasiyla tolere.
static const uint32_t TLM_TIMEOUT_MS = 1000;

// Donanim ACK'i icin tazelik penceresi. 50 Hz'de 500 ms = 25 kacirilmis ACK.
// Telemetri penceresinden KISA olmasi bilincli: ACK'in tek isi "ileri yol su
// an ayakta mi" sorusuna cevap vermek, gecmisi temsil etmesi gerekmiyor.
static const uint32_t ACK_TIMEOUT_MS = 500;

// Arayuze durum gonderme araligi (WebSocket). Kontrol akisindan bagimsiz:
// kol verisi 20 Hz gidiyor, gosterge 10 Hz tazeleniyor.
static const uint32_t WS_DURUM_MS = 100;

// Kalibrasyon komutu ucaktan onay gelene kadar bu araliklarla tekrarlanir.
static const uint32_t CFG_TEKRAR_MS  = 60;
static const uint32_t CFG_VAZGEC_MS  = 1500;

// ------------------------------------------------------------
RF24            radio(PIN_CE, PIN_CSN, RF_SPI_HZ);
WebServer       server(80);
MiniWebSocket   ws(81);
Preferences     nvs;

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

// ============================================================
// PILOT AYARLARI (NVS)
//
// Trim, expo, oran ve kol modu KUMANDADA saklanir, tarayicida degil.
// Gercek RC vericilerinde de boyle: bunlar ucagin degil pilotun ayaridir.
// Telefonu degistirdiginde ya da sayfayi yenilediginde trim kaybolmaz.
// (Servo yonu / notru / uc noktalari ise UCAKTA saklanir - onlar
//  airframe'in ayari. Kalibrasyon ekranindan yonetiliyor.)
// ============================================================
// Trim araligi: kanal genisliginin (1000 us) %20'si = 200 us.
static const int16_t TRIM_MAX_US = 200;

struct TxAyar {
  uint32_t imza;
  int16_t  trimEle;   // us, +/-TRIM_MAX_US
  int16_t  trimRud;
  int16_t  trimThr;
  uint8_t  expoEle;   // 0..50 %
  uint8_t  expoRud;
  uint8_t  rateEle;   // 30..100 % (dual rate / travel)
  uint8_t  rateRud;
  uint8_t  thrLimit;  // 20..100 % - KUMANDA tarafindaki gaz tavani
  uint8_t  mod;       // 1, 2 veya 3 (kol duzeni)
  uint8_t  eleDogrudan;  // elevator kol yonu - asagiya bak
  int16_t  trimAil;   // us, +/-TRIM_MAX_US
  uint8_t  expoAil;   // 0..50 %
  uint8_t  rateAil;   // 30..100 %
  uint8_t  aileron;   // 0 = 3 kanalli ucak (aileron yok), 1 = 4 kanalli
};

static TxAyar tx;
static const uint32_t TX_IMZA = 0x54583005;   // "TX" + surum 5 (aileron eklendi)

// ------------------------------------------------------------
// ELEVATOR KOL YONU
//
// Gercek RC vericilerinde kol ILERI itilince (senden uzaga) elevator ASAGI
// iner ve burun ASAGI gider; kolu GERI cekince burun YUKARI kalkar. Butun
// egitim materyali, simulatorler ve kas hafizasi buna gore. Dokunmatik
// ekranda "ileri" karsiligi "yukari".
//
//   eleDogrudan = 0  (VARSAYILAN, RC STANDARDI)
//        kol YUKARI  -> burun ASAGI  (dalis)
//        kol ASAGI   -> burun YUKARI (tirmanis)
//
//   eleDogrudan = 1  (ekran yonu)
//        kol YUKARI  -> burun YUKARI
//
// Ikinci secenek dokunmatikte daha "dogal" gelebilir ama gercek bir
// kumandaya gecince ters kas hafizasi olusturur. Bu yuzden varsayilan
// standart; tercih eden arayuzden degistirebilir.
//
// NOT: bu ayar KOL yonudur, servo yonu degil. Servo ters donuyorsa
// duzeltilecek yer ucagin kalibrasyonundaki "yon ters" kutusu.
// ------------------------------------------------------------

static void txAyarVarsayilan() {
  tx.imza     = TX_IMZA;
  tx.trimEle  = 0;
  tx.trimRud  = 0;
  tx.trimThr  = 0;
  tx.expoEle  = 25;    // ilk ucuslarda notr etrafini yumusatmak isabetli
  tx.expoRud  = 25;
  tx.rateEle  = 100;
  tx.rateRud  = 100;
  tx.thrLimit = 100;
  tx.mod      = 2;     // dunya standardi: gaz solda
  tx.eleDogrudan = 0;  // RC standardi: kol yukari = burun asagi
  tx.trimAil  = 0;
  tx.expoAil  = 25;
  tx.rateAil  = 100;
  tx.aileron  = 0;     // su anki ucak aileronsuz; kanat degisince 1 yap
}

static void txAyarSinirla() {
  tx.trimEle  = constrain(tx.trimEle, -TRIM_MAX_US, TRIM_MAX_US);
  tx.trimRud  = constrain(tx.trimRud, -TRIM_MAX_US, TRIM_MAX_US);
  tx.trimThr  = constrain(tx.trimThr, -TRIM_MAX_US, TRIM_MAX_US);
  tx.expoEle  = constrain(tx.expoEle,  0, 50);
  tx.expoRud  = constrain(tx.expoRud,  0, 50);
  tx.rateEle  = constrain(tx.rateEle, 30, 100);
  tx.rateRud  = constrain(tx.rateRud, 30, 100);
  tx.thrLimit = constrain(tx.thrLimit, 20, 100);
  // Kol duzeni. 3 kanalli ucakta aileron olmadigi icin bir yatay eksen bos
  // kaliyor; hangi tarafta bos kalacagi pilotun tercihi:
  //   1 = sol rudder+elevator, sag gaz
  //   2 = sol rudder+gaz,      sag elevator      (dunya standardi, varsayilan)
  //   3 = sol yalnizca gaz,    sag rudder+elevator
  // Gecersiz bir deger her zaman 2'ye duser - bilinmeyen bir mod, kollarin
  // hangi kanali surdugunun belirsiz olmasi demek olurdu.
  if (tx.mod != 1 && tx.mod != 3) tx.mod = 2;
  tx.eleDogrudan = tx.eleDogrudan ? 1 : 0;
  tx.trimAil  = constrain(tx.trimAil, -TRIM_MAX_US, TRIM_MAX_US);
  tx.expoAil  = constrain(tx.expoAil,  0, 50);
  tx.rateAil  = constrain(tx.rateAil, 30, 100);
  tx.aileron  = tx.aileron ? 1 : 0;
}

static void txAyarYukle() {
  nvs.begin("rctx", true);
  const size_t n = nvs.getBytes("tx", &tx, sizeof(tx));
  nvs.end();
  if (n != sizeof(tx) || tx.imza != TX_IMZA) txAyarVarsayilan();
  txAyarSinirla();
}

static void txAyarKaydet() {
  tx.imza = TX_IMZA;
  nvs.begin("rctx", false);
  nvs.putBytes("tx", &tx, sizeof(tx));
  nvs.end();
}

// ============================================================
// Kontrol durumu
// ============================================================
struct ControlState {
  // Arayuzden gelen HAM kol konumlari
  int16_t thrIn = 0;      // 0..1000
  int16_t eleIn = 0;      // -1000..1000
  int16_t rudIn = 0;
  int16_t ailIn = 0;
  bool    armed = false;

  // Karisim sonrasi telsize giden degerler (sadece log/arayuz icin saklanir)
  uint16_t thrUs = RC_US_MIN;
  uint16_t eleUs = RC_US_MID;
  uint16_t rudUs = RC_US_MID;
  uint16_t ailUs = RC_US_MID;
};

static ControlState ctrl;
static uint8_t      txSeq         = 0;
static uint32_t     lastTxMs      = 0;
static uint32_t     lastWebMs     = 0;
static uint32_t     lastStatMs    = 0;
static uint32_t     lastRfRetryMs = 0;
static bool         radioOk       = false;

// Ayrintili nRF24 hata metnini 3 sn'de bir tekrar basmamak icin.
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

// nRF24 DONANIM ACK'i (TX_DS) sayaci - telemetriden BAGIMSIZ.
//
// Neden ayri sayiliyor: ACK'i ucagin TELSIZI uretiyor, paketi CRC'siyle
// dogrulayip RX FIFO'suna aldigi an. Yani ACK "ileri yol saglam" demektir
// ama "ucagin yazilimi calisiyor" demez - bu yuzden ARM kapisi buna
// GUVENMEZ. Ama teshis icin kritik: ACK varken telemetri yoksa ariza ileri
// yolda degil GERI yolda (ACK payload), ve logda bunu ayirt edemeden
// "sinyal yok" diye okunuyordu.
static uint16_t winAck = 0, ackHz = 0;
static uint16_t nrfKurtarma   = 0;   // kac kez zorla yeniden kuruldu
static uint32_t lastKurtarmaMs = 0;  // son kurtarma zamani

// Kurtarmalar arasi en az bekleme. Kurtarma ~10 ms surdugu icin sik olmasi
// zararsiz, ama ucak tamamen kapaliyken bosa donmesin.
static const uint32_t NRF_KURTARMA_ARA_MS = 3000;

// En son DONANIM ACK'inin zamani. ESP-NOW geri cagrisindan degil, loop
// icindeki radioPoll()'dan yaziliyor - gorevler arasi degil, volatile gerekmez.
static uint32_t lastAckMs = 0;

// Son ACK'ten beri ust uste kac gonderim TAMAMLANIP cevapsiz kaldi (TX_DF =
// MAX_RT, yani paket havaya cikti ve butun tekrarlar tukendi).
//
// Sagir modul kararinin GERCEK kaniti bu, sure degil. Olcum bunu acikca
// gosterdi: arayuz tarafinda saniyede ~148 WebSocket baglan/kopar olurken
// loop boguluyor, radioPoll() ac kaliyor, TX_DS kacip lastAckMs
// tazelenmiyor ve yalnizca sureye bakan dedektor 161 kez BOSA yeniden
// kurulum yapiyordu. Telsizde bir sorun yoktu; loop mesguldu.
//
// TX_DF bu karistirmayi imkansiz kiliyor: loop ac kaldiginda gonderim
// TAMAMLANMAZ (txStuck artar), TX_DF uretilmez. TX_DF yalnizca "gonderdim,
// ucak cevap vermedi" durumunda artar.
static uint16_t txArdisikDf = 0;

// LOOP SAGLIGI: saniyede kac kez 50 Hz gonderim turu kosabildi.
//
// Sagir modul karari icin DOGRU ayrim bu. Uc durum ayni goruntuyu veriyordu:
//   a) Modul brown-out oldu  -> gonderim TAMAMLANMAZ, TX_DF uretilmez
//   b) Ucak kapali/menzil disi-> gonderim tamamlanir, TX_DF birikir
//   c) loop bogulmus         -> gonderim tamamlanmaz, tur sayisi DUSER
//
// Yalnizca sureye bakmak ucunu karistiriyordu (olcumde 161 bosa kurulum).
// Yalnizca TX_DF'e baglamak ise (a)'yi kaciriyor - asil onarmak istedigimiz
// durumu. Ayirt eden sey tur sayisi: (c)'de loop yavaslar, (a) ve (b)'de
// loop saglikli kalir. Bu yuzden kurtarma "loop saglikli AMA ACK yok"
// kosuluna bagli.
//
// (b) icin de tetiklenmesi zararsiz: yeniden kurulum artik ~10 ms (bkz.
// radioSetup hizli kipi) ve 2 sn'de birden sik olamaz.
static uint16_t tickSayac = 0, tickHz = 0;

// 50 Hz'de saglikli loop en az bu kadar tur kosar. Altina dusuyorsa sorun
// telsizde degil, loop'ta - ve cipi yeniden kurmak durumu KOTULESTIRIR.
static const uint16_t TICK_SAGLIKLI = 40;

// 50 Hz'de 60 ardisik cevapsiz gonderim ~1.2 sn demek. Menzil kenarindaki
// tek tek kayiplar buraya ulasmaz; ulasiyorsa link gercekten olmus.
static const uint16_t NRF_SAGIR_DF = 60;

// ESP-NOW HAM alim sayaclari.
//
// Kor nokta kapatiliyor: onEspNowRecv() bekledigi boy/imzaya uymayan her
// cerceveyi SESSIZCE atiyordu. "Ucagin yayini hic gelmiyor mu, geliyor da
// filtreden mi dusuyor" sorusunun cevabi hicbir yerde gorunmuyordu ve ikisi
// ekranda ayni sonucu veriyor: telemetri yok.
//
// nowHam   = gelen TUM ESP-NOW cerceveleri (boy/imza bakilmadan)
// nowAtilan= filtreden dusenler; > 0 ise sorun MENZIL degil UYUSMAZLIK
//            (boy, imza ya da surum) - yani yazilim hatasi.
// nowSonBoy/nowSonB0 = en son atilan cercevenin boyu ve ilk bayti
static volatile uint16_t nowHam    = 0;
static volatile uint16_t nowAtilan = 0;
static volatile uint8_t  nowSonBoy = 0;
static volatile uint8_t  nowSonB0  = 0;
static uint16_t nowHamHz = 0, nowAtilanHz = 0;
static uint8_t  nowKanal = 0;   // surucunun o anki gercek kanali

// ------------------------------------------------------------
// ARAYUZUN ARM NIYET SAYACI (epoch)
//
// Arayuz her ARM/DISARM degisiminde bu sayaci artirir ve HER kontrol
// mesajinda gonderir; kumanda da durum mesajinda geri yankilar. Arayuz,
// kendi son epoch'uyla uyusmayan bir durum mesajinin "armed" alanini yok
// sayar - cunku o mesaj bizim niyetimizden ESKIDIR.
//
// NEDEN GEREKLI: bu koruma arayuzde vardi ama yalnizca HTTP yolunda
// uygulaniyordu; WebSocket yolu (asil tasima) apply(s) cagirip epoch
// kontrolunu tumden atliyordu. Sonuc olculebilir bir salinim:
//   1. ARM'a basilir, arayuz armed=true olur ve arm=1 gonderir.
//   2. Kumandanin komutu ISLEMEDEN ONCE gonderdigi durum mesaji yolda ve
//      "armed:false" diyor; arayuz onu alip kendini disarm eder.
//   3. Arayuz arm=0 gonderir -> kumanda SESSIZCE disarm olur
//      (ctrl.armed = wantArm; log yok) ve ctrl.thrIn = 0 -> GAZ KESILIR.
//   4. Eski bir "armed:true" mesaji gelir, arayuz tekrar arm eder,
//      [SAFE] ARMED yeniden basilir. Salinim boyle suruyor.
// Logdaki imza tam buydu: [SAFE] ARMED ust uste tekrarliyor, hicbir yerde
// [SAFE] DISARM yok, ve gaz cikisi kolun tuttugu degil sifir.
static uint8_t webEpoch = 0;

// GAZ YENIDEN KAPILAMA KALDIRILDI.
//
// Bir ara sundu: kesintiden sonra arayuzden BIR KEZ sifira yakin gaz gelene
// kadar gaz uygulanmiyordu. Gerekce "kesinti aninda tarayicinin elindeki
// deger bayat olabilir" idi.
//
// Iki sebeple kaldirildi:
//   1. KORUDUGU BIR SEY YOK. ctrl.thrIn yalnizca GELEN komuttan set
//      ediliyor ve kesintide zaten sifirlaniyor. Baglanti dondugunde gelen
//      ilk deger pilotun O ANKI kol konumu - bayat degil.
//   2. ZARARI VAR ve olculdu: 1 saniyelik bir tarayici takilmasi gazi
//      kesiyor, pilot gazi YUKARIDA tuttugu surece kapi acilmiyor ve motor
//      kapali kaliyor. Kolu asagi-yukari oynatinca donuyor - pilotun
//      "gaz git gel yapiyor, motor dur kalk ediyor" dedigi sey tam buydu.
//
// Arayuz kesintisi su an suruyor mu (tekrar tekrar notrlemeyi onlemek icin).
static bool webKesik = false;

// Ucakla TEMAS (taze telemetri ya da donanim ACK'i) ne zaman kayboldu.
// 0 = temas var. Iki kademeli telsiz watchdog'u icin.
static uint32_t temasKayipMs = 0;
static bool     rfKesik      = false;

// Temas kaybindan bu sure sonra ARM da dusurulur. 1. kademe (gaz kesme)
// temas kaybolur kaybolmaz devreye girer; telemetriTaze() zaten 1 sn'lik
// pencere kullandigi icin orada ayrica beklemeye gerek yok.
static const uint32_t RF_TEMAS_OLU_MS = 4000;

// Arayuzden saniyede kac komut geldi. Telsiz linki kusursuzken bile arayuz
// olu olabiliyor (tarayici istegi hic gondermiyorsa) ve logda bunu ayirt
// etmenin yolu yoktu. web=0/s gorursen sorun telsizde degil, tarayicida.
static uint16_t winWeb = 0, webHz = 0;

// Ucaktan gelen son telemetri.
//
// tlmFresh / lastTlmMs BASKA BIR GOREVDEN yaziliyor: ESP-NOW geri cagrisi
// (onEspNowRecv) WiFi gorevinin baglaminda kosuyor, telemetriTaze() ise
// loop() icinden okunuyor. volatile olmadan derleyici bu ikisini loop
// boyunca bir yazmacta tutabilir ve ARM kapisi bayat bir degere bakar.
static RcTelemetry       tlm = {};
static volatile bool     tlmFresh = false;
static volatile uint32_t lastTlmMs = 0;

// Ucaktan gelen kalibrasyon raporlari
static RcConfigReport rapor[RC_SURF_COUNT];
static bool           raporVar[RC_SURF_COUNT] = { false, false, false };
static volatile bool  raporYeni[RC_SURF_COUNT] = { false, false, false };

// ARM'in EN SON NEDEN reddedildigi.
//
// Sebep eskiden yalnizca seri porta basiliyordu. Uctaki kumandada USB yok:
// pilot ARM'a basili tutuyor, tus doluyor, sonra kendiliginden "ARM"a geri
// donuyor ve EKRANDA HICBIR SEBEP YAZMIYOR. "Neden arm olmuyor" sorusunun
// cevabi kartin icinde kaliyordu. Artik arayuze de gidiyor.
//
// ASCII ve tirnaksiz tutulmali: dogrudan JSON'a gomuluyor.
static const char* armRed   = nullptr;
static uint32_t    armRedMs = 0;
static const uint32_t ARM_RED_GOSTER_MS = 4000;

static void armReddet(const char* sebep) {
  armRed   = sebep;
  armRedMs = millis();
}

// Bekleyen kalibrasyon komutu (onay gelene kadar tekrarlanir)
static RcConfigPacket cfgPaket;
static bool           cfgBekliyor    = false;
static uint32_t       cfgSonGonderim = 0;
static uint32_t       cfgBasladiMs   = 0;
static uint8_t        cfgSeq         = 0;
static bool           cfgBasarisiz   = false;

// ============================================================
// Kol karisimi: expo + dual rate + trim
//
// Expo formulu endustri standardi (Futaba/Spektrum ile ayni his):
//     y = x * ((1-e) + e * x^2)
// e = 0 -> dogrusal, e buyudukce notrun etrafi yumusar ama uc noktalar
// aynen korunur. Ilk ucuslarda %25 civari kolun asiri hassas olmasini
// engelliyor; uc noktalarda tam yetkiyi kaybetmiyorsun.
// ============================================================
static uint16_t eksenUs(int16_t giris, uint8_t expo, uint8_t rate, int16_t trim) {
  const float x = constrain((int)giris, -1000, 1000) / 1000.0f;
  const float e = expo / 100.0f;
  float y = x * ((1.0f - e) + e * x * x);
  y *= rate / 100.0f;
  return rcClampUs((int32_t)lroundf(RC_US_MID + y * 500.0f) + trim);
}

// Gaz: 0..1000 -> 1000..2000, kumanda tarafindaki tavan ile olceklenir.
// Ucakta AYRICA bagimsiz bir tavan var; ikisinden dusuk olan gecerlidir.
//
// Gaz trim'i rolanti noktasini kaydirir. POZITIF trim, kol tam asagidayken
// bile ESC'ye stop'tan buyuk bir darbe gonderir - yani arm edildigi anda
// motor doner. Bu ozelligi kaldirmak yerine ARM KAPISINI trim'e duyarli
// hale getirdik: arm karari kolun konumuna degil, gercekten gonderilecek
// darbeye bakiyor (bkz. kontrolUygula). Boylece pozitif trim'le arm etmek
// mumkun olmuyor ve ozellik guvenli sekilde duruyor.
static uint16_t gazUs(int16_t giris) {
  const int32_t g = (int32_t)constrain((int)giris, 0, 1000) * tx.thrLimit / 100;
  return rcClampUs(RC_US_MIN + g + tx.trimThr);
}

static void kanallariHesapla() {
  // Kol yonu: RC standardinda ekranda yukari = burun asagi (bkz. TxAyar).
  const int16_t eleGiris = tx.eleDogrudan ? ctrl.eleIn : (int16_t)(-ctrl.eleIn);
  ctrl.eleUs = eksenUs(eleGiris, tx.expoEle, tx.rateEle, tx.trimEle);
  ctrl.rudUs = eksenUs(ctrl.rudIn, tx.expoRud, tx.rateRud, tx.trimRud);
  // Aileronsuz ucakta kanal HER ZAMAN notr gider: ucaktaki cikislar bosta
  // durur ve kazara bir kol degeri onlari surukleyemez.
  ctrl.ailUs = tx.aileron ? eksenUs(ctrl.ailIn, tx.expoAil, tx.rateAil, tx.trimAil)
                          : RC_US_MID;
  ctrl.thrUs = gazUs(ctrl.thrIn);
}

// ============================================================
// Telsiz
// ============================================================
// hizli = UCUS sirasindaki yeniden kurulum. Bekleme YOK, tek deneme.
//
// Neden ayri bir kip: bu fonksiyon acilis icin yazilmisti ve en kotu halde
// delay(10) + 3 x delay(100) = ~310 ms loop'u blokluyor. Acilista dogru
// (besleme oturmadan once denk gelen ilk deneme basarisiz olabiliyor), ama
// UCUSTA 310 ms sunlar demek:
//   - radioTick() 15 tur kaciriyor, yani 15 kontrol cercevesi gitmiyor
//   - ucagin 500 ms'lik failsafe esigine tehlikeli sekilde yaklasiyoruz
//   - ws.dongu() kosmadigi icin web watchdog'un 1 sn'lik payi yeniyor
// Sagir modul kurtarmasi ucus sirasinda tetiklendigi icin oradan HIZLI
// cagriliyor: modul zaten besleniyor, power-on-reset beklemesinin anlami yok.
static bool radioSetup(bool hizli = false) {
  if (!USE_NRF24) return false;

  // SPI pinlerini acikca ver. VSPI varsayilani zaten bunlar ama acik yazmak
  // hem belge gorevi goruyor hem de baska bir kutuphane SPI'i farkli
  // pinlerle baslatmissa onu eziyor.
  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CSN);
  if (!hizli) delay(10);   // modulun power-on-reset'i (datasheet: 4.5 ms + 14 us)

  // Acilista ilk deneme besleme oturmadan once denk gelebiliyor -> 3 kez dene.
  // Ucusta tek deneme: basarisiz olursa bir sonraki tur yine denenecek,
  // loop'u burada bloklamanin bedeli daha yuksek.
  bool up = false;
  const uint8_t denemeAdet = hizli ? 1 : 3;
  for (uint8_t i = 0; i < denemeAdet && !up; i++) {
    up = radio.begin();
    if (!up && !hizli) delay(100);
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

  // PA SEVIYESI - bir ara LOW'a dusuruldu, sonra GERI YUKSELTILDI. Gerekce
  // ucak tarafindaki ayni yorumda ayrintili yazili. Kisaca: linkin kopunca
  // geri gelmemesinin sebebi PA degil MAX_RT/FIFO kilidiydi ve o kirildi;
  // PA_LOW ise kapali alanda menzili duvarlarin altina dusuruyordu.
  // Brown-out geri gelirse "kurtarma=N" sayaci haber verir.
  radio.setPALevel(RF24_PA_HIGH);

  // setDataRate() ayari yazip GERI OKUR; tutmadiysa false doner. Donus degeri
  // kontrol edilmezse klon cip sessizce 1 Mbps'te kalir ve link olur.
  const bool hizOk = radio.setDataRate(rfVeriHizi());

  radio.setChannel(RC_RF_CHANNEL);
  radio.setCRCLength(RF24_CRC_16);
  // ARD (tekrarlar arasi bekleme) ACK PAYLOAD'IN HAVADA GECIRDIGI SUREDEN
  // UZUN OLMALI. 250 kbps'te 8 byte'lik telemetri ACK'i ~550 us, 13 byte'lik
  // kalibrasyon raporu ~710 us surer; ustune alicinin RX->TX donusu (~130 us)
  // biniyor. Eski ayar setRetries(3, 5) idi: ARD = 4*250 = 1000 us, yani
  // Nordic'in 250 kbps + ACK payload icin verdigi alt sinirin (1500 us)
  // altinda ve bu sureye hic pay birakmiyor.
  //
  // Sonucu tam olarak "servolar oynuyor ama telemetri yok" arizasi:
  //   1. Paket ucaga ILK denemede ulasir -> servolar kollari takip eder.
  //   2. Kumanda ACK'i beklemeyi erken bitirip paketi TEKRAR gonderir.
  //   3. Ucagin telsizi tekrarlanan paketi de ACK'ler ve her ACK, ucagin
  //      ACK PAYLOAD FIFO'sundan bir yuk daha TUKETIR (FIFO 3 derinliginde).
  //   4. 5 tekrar x 50 Hz ile tuketim saniyede ~250'ye cikar, uretim 60'ta
  //      kalir -> FIFO surekli bos -> ACK'ler payload'siz doner.
  //   5. Kumanda telemetri goremez, "ucaktan cevap yok" der ve ARM'i reddeder.
  //
  // delay=5 -> 1500 us. Tekrar sayisi 3'e indi: 50 Hz'de 20 ms sonra zaten
  // taze paket geliyor, 5 tekrar hem gereksiz hem FIFO'yu bosaltiyor.
  // En kotu hal 4 * (676 + 1500) ~ 8.7 ms; TX_TIMEOUT_US = 15 ms icinde.
  radio.setRetries(5, 3);
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

// Ucaktan gelen kalibrasyon raporu. Bekleyen komut onaylandiysa tekrar durur.
static void raporUygula(const RcConfigReport& r) {
  if (!rcReportValid(r) || r.surf >= RC_SURF_COUNT) return;
  rapor[r.surf]    = r;
  raporVar[r.surf] = true;
  raporYeni[r.surf]= true;

  // Rapor da ucaktan gelen bir hayat isareti: link olcumunde sayilmali,
  // yoksa kalibrasyon sirasinda link %0 gorunur.
  lastTlmMs = millis();
  tlmFresh  = true;
  winAcked++;

  if (cfgBekliyor && r.ackSeq == cfgPaket.seq) {
    cfgBekliyor  = false;
    cfgBasarisiz = (r.flags & RC_REPF_REJECT) != 0;
    if (cfgBasarisiz) Serial.printf("[KAL] Ucak komutu REDDETTI (op=%u).\n", cfgPaket.op);
  }
}

// ACK payload ile gelen paketleri oku (nRF24 yolu). Boyut tipi belirler.
static void telemetryRead() {
  while (radio.available()) {
    const uint8_t len = radio.getDynamicPayloadSize();
    if (len == sizeof(RcTelemetry)) {
      RcTelemetry t;
      radio.read(&t, sizeof(t));
      telemetriUygula(t);
    } else if (len == sizeof(RcConfigReport)) {
      RcConfigReport r;
      radio.read(&r, sizeof(r));
      raporUygula(r);
    } else {
      radio.flush_rx();     // beklenmeyen boy -> FIFO'yu bosalt
      return;
    }
  }
}

// ------------------------------------------------------------
// ESP-NOW
// ------------------------------------------------------------

// WiFi gorevi baglaminda cagrilir - kisa tutulmali, sadece dogrula ve yaz.
static void onEspNowRecv(const uint8_t* mac, const uint8_t* data, int len) {
  (void)mac;
  nowHam++;
  if (len == sizeof(RcTelemetry) && data[0] == RC_MAGIC_TLM) {
    RcTelemetry t;
    memcpy(&t, data, sizeof(t));
    telemetriUygula(t);
  } else if (len == sizeof(RcConfigReport) && data[0] == RC_MAGIC_REP) {
    RcConfigReport r;
    memcpy(&r, data, sizeof(r));
    raporUygula(r);
  } else {
    // Cerceve GELDI ama isimize yaramadi. Boyu ve ilk bayti kaydediyoruz:
    // ikisi birlikte hangi tipin/hangi surumun konustugunu soyluyor.
    nowAtilan++;
    nowSonBoy = (uint8_t)(len < 0 ? 0 : (len > 255 ? 255 : len));
    nowSonB0  = len > 0 ? data[0] : 0;
  }
}

// softAP acildiktan SONRA cagrilmali (WiFi surucusu ayakta olmali).
static bool espnowSetup() {
  if (!USE_ESPNOW) return false;

  if (esp_now_init() != ESP_OK) {
    Serial.println("[NOW] esp_now_init basarisiz.");
    return false;
  }
  const esp_err_t kayit = esp_now_register_recv_cb(onEspNowRecv);
  if (kayit != ESP_OK) {
    Serial.printf("[NOW] !! alim geri cagrisi KAYDEDILEMEDI: %s\n",
                  esp_err_to_name(kayit));
    return false;
  }

  // Yayin adresini es olarak ekle. Sifreleme yok: sifreli ESP-NOW yayin
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

  // Yazdigimiz degil, surucunun GERCEKTEN oturdugu kanal.
  //
  // Bu ayrim kritik: ESP-NOW gonderirken esin kanalina gecip donebiliyor,
  // yani kart kanal 1'de GONDERIP baska bir kanalda DINLIYOR olabilir. O
  // durumda link tam olarak gordugumuz sekilde tek yonlu calisir - ucak
  // paketleri alir, kumanda cevabi hic gormez.
  uint8_t gercekKanal = 0;
  wifi_second_chan_t ikincil = WIFI_SECOND_CHAN_NONE;
  esp_wifi_get_channel(&gercekKanal, &ikincil);

  Serial.printf("[NOW] Hazir. Yayin modu (STA arayuzu), istenen kanal %u, "
                "GERCEK kanal %u, STA MAC %s, AP MAC %s\n",
                AP_CHANNEL, gercekKanal, WiFi.macAddress().c_str(),
                WiFi.softAPmacAddress().c_str());
  if (gercekKanal != AP_CHANNEL)
    Serial.printf("[NOW] !! KANAL UYUSMUYOR: %u istendi, surucu %u'de.\n",
                  AP_CHANNEL, gercekKanal);
  return true;
}

// ------------------------------------------------------------
// Bekleyen gonderimi cozer. loop()'ta her turda cagrilir.
//
// Neden bloklamayan gonderim: radio.write() ACK gelene ya da tum tekrarlar
// tukenene kadar bekliyor. Link koptugunda bu, 20 ms'lik butcenin ~12 ms'sini
// yiyor ve arayuzun cevap suresini bozuyor -- yani tam da kontrole en cok
// ihtiyac duyulan anda kumanda agirlasiyor. startWrite() + STATUS
// yoklamasiyla gonderim arka planda ilerliyor, web istekleri aksamiyor.
// ------------------------------------------------------------
static void radioPoll() {
  if (!radioOk || !txPending) return;

  const uint8_t f = radio.update();   // NOP gonderir, STATUS'u tazeler

  // ACK payload RX FIFO'suna dustuyse ayni STATUS baytinda RX_DR de yanar,
  // yani bunu ogrenmek bedava. Okumayi TX_DS'e baglamak yetmiyordu: gonderim
  // bitmis ama o turda TX_DS'i gormemissek (loop web istegiyle mesgulse) bir
  // sonraki nrfGonder() bayraklari temizleyip payload'i cope atiyordu.
  if (f & RF24_RX_DR) telemetryRead();   // winAcked burada artiyor

  if (f & (RF24_TX_DS | RF24_TX_DF)) {
    if (f & RF24_TX_DS) {             // ucagin TELSIZI paketi aldi (bkz. winAck)
      winAck++;
      lastAckMs   = millis();
      txArdisikDf = 0;
    } else {
      // ---- MAX_RT: BASARISIZ PAKET TX FIFO'DA KALIR ----
      //
      // nRF24, butun tekrarlar tukendiginde paketi FIFO'dan ATMAZ;
      // temizlemek yazilima duser. Atmazsak su zincir isliyor:
      //   1. Uc ardisik basarisiz gonderim -> TX FIFO dolar
      //   2. startWrite() false doner -> txPending false kalir
      //   3. radioPoll() "if (!txPending) return;" ile BIR DAHA HIC kosmaz
      //   4. FIFO sonsuza kadar dolu -> telsiz KALICI olarak kilitli
      //   5. Cip SPI'dan cevap verdigi icin durum satiri hala "nrf=BAGLI"
      //
      // Olculdu: ucak kapaliyken cevapsiz sayaci tam 3'te (FIFO derinligi)
      // donup kaldi, takilan=0, ack=0, tur=49/s - yani loop saglikliyken
      // gonderim hic baslamiyordu. Menzil kenarinda ya da ucak bir an
      // resetlendiginde uc ardisik basarisizlik cok kolay olusuyor; "link
      // olup bir daha kendine gelmiyor" tablosunun altinda bu vardi.
      //
      // Bayat bir kontrol cercevesini saklamanin degeri yok - 20 ms sonra
      // tazesi geliyor. At ve devam et.
      radio.flush_tx();
      if (txArdisikDf < 0xFFFF) txArdisikDf++;
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

// nRF24 uzerinden asenkron gonderim. Hem kontrol hem kalibrasyon paketleri
// buradan cikar; ikisi de dinamik payload oldugu icin ucak tarafi boyuttan
// hangisi oldugunu anliyor.
static void nrfGonder(const void* veri, uint8_t boy) {
  if (!radioOk) return;
  if (txPending) {   // onceki bitmediyse zorla kapat, 50 Hz cadansi kaybetme
    // FIFO'da bekleyen bir ACK payload varsa flush/clear onu da siliyor.
    // Once oku: gecikmis telemetri bile bayat telemetriden iyidir, ustelik
    // ARM kapisi "1 sn icinde taze telemetri" istiyor.
    telemetryRead();
    radio.flush_tx();
    radio.clearStatusFlags(RF24_IRQ_ALL);
    txPending = false;
    txStuck++;
  }
  txStartUs = micros();
  txPending = radio.startWrite(veri, boy, false);

  // startWrite() false = TX FIFO dolu, gonderim HIC baslamadi. Bosaltmazsak
  // txPending false kaldigi icin radioPoll() kosmaz ve bu durumdan cikis
  // olmaz (bkz. radioPoll icindeki MAX_RT notu).
  if (!txPending) {
    radio.flush_tx();
    radio.clearStatusFlags(RF24_IRQ_ALL);
    txStuck++;
  }
}

// 20 ms'de bir bir paket olusturup ACIK OLAN TUM yollardan gonderir.
// Iki yol da acikken ucak ayni paketi iki kez alabilir; zararsiz, cunku
// ucak tarafi ayni seq'i tekrar islemeyi tolere ediyor (son deger kazanir).
static void radioTick() {
  tickSayac++;
  if (!radioOk && !espnowOk) return;   // gidecek yol yok

  // Bekleyen bir kalibrasyon komutu varsa bu tur ONA ayrilir. Kalibrasyon
  // yalnizca yerdeyken yapiliyor; tek bir kontrol paketini atlamak zararsiz,
  // buna karsilik komutun ucaga ulasmasi kritik.
  const uint32_t now = millis();
  if (cfgBekliyor && now - cfgSonGonderim >= CFG_TEKRAR_MS) {
    cfgSonGonderim = now;
    if (espnowOk) esp_now_send(BCAST, (const uint8_t*)&cfgPaket, sizeof(cfgPaket));
    nrfGonder(&cfgPaket, sizeof(cfgPaket));

    if (now - cfgBasladiMs > CFG_VAZGEC_MS) {
      cfgBekliyor  = false;
      cfgBasarisiz = true;
      Serial.printf("[KAL] Komut ucaga ulasmadi (op=%u).\n", cfgPaket.op);
    }
    return;
  }

  kanallariHesapla();

  RcPacket p;
  p.magic = RC_MAGIC_CTRL;
  p.seq   = txSeq++;
  p.ch[RC_CH_THROTTLE] = ctrl.armed ? ctrl.thrUs : RC_US_MIN;
  p.ch[RC_CH_ELEVATOR] = ctrl.eleUs;
  p.ch[RC_CH_RUDDER]   = ctrl.rudUs;
  p.ch[RC_CH_AILERON]  = ctrl.ailUs;
  p.flags = (uint8_t)((RC_PROTO_VER << 4) | (ctrl.armed ? RC_FLAG_ARMED : 0));
  p.crc   = rcPacketCrc(p);

  winSent++;

  // --- ESP-NOW: yayin, bloklamaz, ~50 us surer
  if (espnowOk) esp_now_send(BCAST, (const uint8_t*)&p, sizeof(p));

  // --- nRF24: asenkron gonderim
  nrfGonder(&p, sizeof(p));
}

// ============================================================
// Guvenlik
// ============================================================

// Ucaktan taze telemetri geliyor mu? Gelmiyorsa ucakla baglanti YOK demektir;
// radio.write() true donse bile (ACK var, payload yok) buna guvenilmez.
static bool telemetriTaze() {
  return tlmFresh && (millis() - lastTlmMs < TLM_TIMEOUT_MS);
}

// ILERI yol su an ayakta mi?
//
// Donanim ACK'i, ucagin telsizinin paketi alip CRC'sini dogruladigi an
// uretilir - yani "paket ucaga ULASTI"nin kesin kaniti. Telemetrinin
// kanitladigi sey bundan farkli: GERI yolun da ayakta oldugu.
//
// Ikisini ayirmak zorundayiz, cunku menzil kenarinda GERI yol ILERI yoldan
// once oluyor (kumandanin kendi WiFi AP'si yanindaki nRF24 alicisini
// korletiyor; ucakta boyle bir yerel girisim yok). O noktada telemetri
// sifira duserken paketler hala ulasiyor ve ucak tam kontrol edilebilir.
static bool ackTaze() {
  return radioOk && lastAckMs && (millis() - lastAckMs < ACK_TIMEOUT_MS);
}

// Ucagin KENDI bildirdigi durum. Telemetri yoksa hepsi false.
// Bunlar kumandanin yerel ctrl.armed degeriyle karistirilmamali.
static bool ucakArmed()    { return telemetriTaze() && (tlm.status & RC_STATUS_ARMED); }
static bool ucakFailsafe() { return telemetriTaze() && (tlm.status & RC_STATUS_FAILSAFE); }
static bool ucakKalib()    { return telemetriTaze() && (tlm.status & RC_STATUS_CALIB); }
static bool ucakKirli()    { return telemetriTaze() && (tlm.status & RC_STATUS_DIRTY); }

static void disarm(const char* sebep) {
  if (ctrl.armed) Serial.printf("[SAFE] DISARM: %s\n", sebep);
  ctrl.armed = false;
  ctrl.thrIn = 0;
}

static void notrYuzeyler() {
  ctrl.eleIn = 0;
  ctrl.rudIn = 0;
  ctrl.ailIn = 0;
}

// ============================================================
// Kalibrasyon komutu kuyruklama
// ============================================================
static void cfgKomut(uint8_t op, uint8_t surf, uint16_t mn, uint16_t md,
                     uint16_t mx, uint8_t flags) {
  cfgPaket.magic = RC_MAGIC_CFG;
  cfgPaket.seq   = ++cfgSeq;
  cfgPaket.op    = op;
  cfgPaket.surf  = surf;
  cfgPaket.minUs = mn;
  cfgPaket.midUs = md;
  cfgPaket.maxUs = mx;
  cfgPaket.flags = flags;
  cfgPaket.crc   = rcConfigCrc(cfgPaket);

  cfgBekliyor    = true;
  cfgBasarisiz   = false;
  cfgBasladiMs   = millis();
  cfgSonGonderim = 0;    // bir sonraki turda hemen gitsin

  // Kalibrasyon sessizce basarisiz olursa teshis edilemiyordu: komutun
  // cikip cikmadigi, ucagin cevap verip vermedigi log'dan gorunsun.
  Serial.printf("[KAL] -> op=%u surf=%u %u/%u/%u bayrak=%u seq=%u (yol: %s%s)\n",
                op, surf, mn, md, mx, flags, cfgPaket.seq,
                radioOk ? "nRF24 " : "", espnowOk ? "ESP-NOW" : "");
  if (!radioOk && !espnowOk)
    Serial.println("[KAL] !! HICBIR TELSIZ YOLU YOK - komut gonderilemiyor.");
}

// ============================================================
// Arayuze donen JSON
//
// armed  = KUMANDANIN yerel durumu
// parmed = UCAGIN telemetride bildirdigi durum   <- ikisi farkli olabilir
// ============================================================
static void durumJson(String& j) {
  const bool rxAlive = telemetriTaze();
  j = "{";
  j += "\"armed\":";  j += ctrl.armed ? "true" : "false";
  // Arayuzun epoch'u, oldugu gibi geri. Arayuz bunu kendi sayacyla
  // karsilastirip bayat mesajlari eliyor (bkz. webEpoch notu).
  j += ",\"ep\":";     j += webEpoch;
  j += ",\"uthr\":";  j += ctrl.armed ? ctrl.thrUs : RC_US_MIN;
  j += ",\"uele\":";  j += ctrl.eleUs;
  j += ",\"urud\":";  j += ctrl.rudUs;
  j += ",\"uail\":";  j += ctrl.ailUs;
  j += ",\"link\":";  j += linkQuality;
  j += ",\"rx\":";    j += rxAlive ? "true" : "false";
  j += ",\"rxhz\":";  j += rxAlive ? tlm.rxHz : 0;
  j += ",\"loss\":";  j += rxAlive ? tlm.lossPct : 0;
  j += ",\"vbat\":";  j += rxAlive ? tlm.vbatMv : 0;
  j += ",\"parmed\":";j += ucakArmed()    ? "true" : "false";
  j += ",\"pfs\":";   j += ucakFailsafe() ? "true" : "false";
  j += ",\"pcal\":";  j += ucakKalib()    ? "true" : "false";
  j += ",\"pdirty\":";j += ucakKirli()    ? "true" : "false";
  j += ",\"ack\":";   j += ackTaze() ? "true" : "false";
  // Hangi yol GERCEKTEN tasiyor? nrf/now bayraklari yalnizca "acik mi"
  // diyor; bu ikisi son saniyede veri akip akmadigina bakiyor. Arayuzdeki
  // link bolumu bunu yaziyor - "%100" tek basina hangi telsizle ucdugunu
  // soylemiyordu ve iki yol acikken hangisinin tasidigi hic gorunmuyordu.
  j += ",\"nrfa\":";  j += (ackHz > 0)    ? "true" : "false";
  j += ",\"nowa\":";  j += (nowHamHz > 0) ? "true" : "false";
  j += ",\"rf\":";    j += (radioOk || espnowOk) ? "true" : "false";
  j += ",\"nrf\":";   j += radioOk  ? "true" : "false";
  j += ",\"now\":";   j += espnowOk ? "true" : "false";
  j += ",\"tez\":";   j += TEZGAH_MODU ? "true" : "false";
  j += ",\"cfgerr\":";j += cfgBasarisiz ? "true" : "false";
  // Sebep yalnizca redden sonra kisa bir sure gosterilir: kalici birakmak
  // pilotun cozdugu bir sorunu ekranda tutar ve gercek uyarilari bastirir.
  j += ",\"armred\":\"";
  if (armRed && millis() - armRedMs < ARM_RED_GOSTER_MS) j += armRed;
  j += "\"";
  j += "}";
}

static void ayarJson(String& j) {
  j  = "{\"cfg\":{";
  j += "\"mode\":";     j += tx.mod;
  j += ",\"trimEle\":"; j += tx.trimEle;
  j += ",\"trimRud\":"; j += tx.trimRud;
  j += ",\"trimThr\":"; j += tx.trimThr;
  j += ",\"expoEle\":"; j += tx.expoEle;
  j += ",\"expoRud\":"; j += tx.expoRud;
  j += ",\"rateEle\":"; j += tx.rateEle;
  j += ",\"rateRud\":"; j += tx.rateRud;
  j += ",\"thrLimit\":";j += tx.thrLimit;
  j += ",\"eleDogrudan\":"; j += tx.eleDogrudan;
  j += ",\"trimAil\":";  j += tx.trimAil;
  j += ",\"expoAil\":";  j += tx.expoAil;
  j += ",\"rateAil\":";  j += tx.rateAil;
  j += ",\"aileron\":";  j += tx.aileron;
  j += "}}";
}

static void raporJsonGovde(String& j, uint8_t s) {
  const RcConfigReport& r = rapor[s];
  j += "{\"s\":";    j += s;
  j += ",\"mn\":";   j += r.minUs;
  j += ",\"md\":";   j += r.midUs;
  j += ",\"mx\":";   j += r.maxUs;
  j += ",\"live\":"; j += r.liveUs;
  j += ",\"rev\":";  j += (r.flags & RC_REPF_REVERSE) ? 1 : 0;
  j += "}";
}

// ============================================================
// Komut uygulama - WebSocket ve HTTP ayni kapidan gecer
// ============================================================
// Kontrol komutunun HANGI yoldan geldigi - teshis icin.
static const char* webKaynak = "?";


static void kontrolUygula(int16_t thrIn, int16_t eleIn, int16_t rudIn,
                          int16_t ailIn, bool wantArm) {
  winWeb++;

  const bool armOnce = ctrl.armed;
  const bool armIstek = wantArm;

  ctrl.thrIn = constrain((int)thrIn, 0, 1000);

  if (webKesik) {
    webKesik = false;
    Serial.println("[WEB] Arayuz geri geldi.");
  }
  ctrl.eleIn = constrain((int)eleIn, -1000, 1000);
  ctrl.rudIn = constrain((int)rudIn, -1000, 1000);
  ctrl.ailIn = constrain((int)ailIn, -1000, 1000);

  if (wantArm && !ctrl.armed) {
    // 1) Gercekten gonderilecek gaz darbesi minimumda olmali.
    //    Kolun konumuna degil CIKISA bakiyoruz: pozitif gaz trim'i kol tam
    //    asagidayken bile motoru dondurebilirdi, bu kapi onu da yakalar.
    if (gazUs(ctrl.thrIn) > RC_US_MIN + 20) {
      wantArm = false;
      armReddet("Gaz cikisi minimumda degil. Kolu tam asagi cek, gaz trimini sifirla.");
      Serial.printf("[SAFE] ARM reddedildi: gaz cikisi %u us (kol %d, trim %d).\n",
                    gazUs(ctrl.thrIn), ctrl.thrIn, tx.trimThr);
    }
    // 2) Ucak kalibrasyon modunda olmamali - o moddayken motor zaten kilitli,
    //    ARM etmek yanlis bir guven duygusu verir.
    else if (ucakKalib()) {
      wantArm = false;
      armReddet("Ucak kalibrasyon modunda. Once kalibrasyondan cik.");
      Serial.println("[SAFE] ARM reddedildi: ucak kalibrasyon modunda.");
    }
    // 3) Ucakla gercekten baglanti olmali. Telemetri yoksa ucagin paketleri
    //    aldigina dair hicbir kanit yok; korlemesine arm etme.
    //    Tezgah modunda bu kilit bilerek atlanir (bkz. TEZGAH_MODU).
    else if (!telemetriTaze() && !TEZGAH_MODU) {
      wantArm = false;
      // Servolarin oynamasi bu kapiyi acmaz ve acmamali: servolar ILERI
      // yolun saglam oldugunu gosterir, bu kapi GERI yolu ariyor. Ucagin
      // failsafe / armed durumunu goremeden arm etmek, ekranda ARMED
      // yazarken ucagin motoru kesmis olmasi demek.
      armReddet("Ucaktan telemetri gelmiyor. Servolar oynuyorsa ileri yol "
                "saglam, geri yol olu: kumandanin seri logunda [RF] satirina bak.");
      Serial.println("[SAFE] ARM reddedildi: ucaktan telemetri yok.");
    }
    else if (!telemetriTaze()) {
      Serial.println("[SAFE] ARMED -- TEZGAH MODU: telemetri YOK, ucagin "
                     "komutu aldigina dair kanit yok!");
    }
    else {
      armRed = nullptr;
      Serial.println("[SAFE] ARMED");
    }
  }

  ctrl.armed = wantArm;
  if (!ctrl.armed) ctrl.thrIn = 0;
  lastWebMs  = millis();
  kanallariHesapla();

  // Teshis: TALEBI degil SONUCU bas, ve reddi ayirt et.
  //
  // Onceki surum yalnizca "wantArm != ctrl.armed"e bakip talebi
  // yazdiriyordu; gaz yukaridayken saniyede 46 kez "arm 0->1" basiyor ve
  // salinim varmis gibi gorunuyordu. Oysa 371 satirin 371'i REDDI, yalnizca
  // 6'si gercek degisimdi - yanlis yerlestirilmis bir teshis satiri
  // arizanin yerini gizledi.
  if (armIstek != armOnce) {
    if (ctrl.armed != armOnce) {
      Serial.printf("[WEB] arm %d->%d yol=%s thr=%d web=%u/s\n",
                    armOnce ? 1 : 0, ctrl.armed ? 1 : 0, webKaynak,
                    (int)thrIn, webHz);
    } else if (armIstek) {
      // Istendi ama uygulanmadi. Sebep armRed'de; saniyede onlarca kez
      // basmamak icin 1 sn'de bir.
      static uint32_t sonRedMs = 0;
      if (millis() - sonRedMs >= 1000) {
        sonRedMs = millis();
        Serial.printf("[WEB] arm REDDEDILDI yol=%s thr=%d (%s)\n",
                      webKaynak, (int)thrIn, armRed ? armRed : "?");
      }
    }
  }
}

static void acilDurdur(const char* sebep) {
  disarm(sebep);
  notrYuzeyler();
  lastWebMs = millis();
  kanallariHesapla();
}

// Tek bir "anahtar=deger" pilot ayarini uygular ve kalici hale getirir.
static void ayarUygula(const char* k, long v) {
  if      (!strcmp(k, "mode"))     tx.mod     = (uint8_t)v;
  else if (!strcmp(k, "trimEle"))  tx.trimEle = (int16_t)v;
  else if (!strcmp(k, "trimRud"))  tx.trimRud = (int16_t)v;
  else if (!strcmp(k, "trimThr"))  tx.trimThr = (int16_t)v;
  else if (!strcmp(k, "expoEle"))  tx.expoEle = (uint8_t)v;
  else if (!strcmp(k, "expoRud"))  tx.expoRud = (uint8_t)v;
  else if (!strcmp(k, "rateEle"))  tx.rateEle = (uint8_t)v;
  else if (!strcmp(k, "rateRud"))  tx.rateRud = (uint8_t)v;
  else if (!strcmp(k, "thrLimit")) tx.thrLimit= (uint8_t)v;
  else if (!strcmp(k, "eleDogrudan")) tx.eleDogrudan = (uint8_t)v;
  else if (!strcmp(k, "trimAil"))  tx.trimAil = (int16_t)v;
  else if (!strcmp(k, "expoAil"))  tx.expoAil = (uint8_t)v;
  else if (!strcmp(k, "rateAil"))  tx.rateAil = (uint8_t)v;
  else if (!strcmp(k, "aileron"))  tx.aileron = (uint8_t)v;
  else return;
  txAyarSinirla();
  txAyarKaydet();
}

// ============================================================
// WebSocket mesaj isleyici
//
// Metin protokolu bilerek cok kisa: 20 Hz'de gonderilen kol mesaji
// ~20 bayt, JSON olsa uc kati olurdu.
//   C<thr>,<ele>,<rud>,<arm>      kol konumlari + arm istegi
//   X                             acil durdurma
//   S<anahtar>=<deger>            pilot ayari
//   K<op>,<surf>,<mn>,<md>,<mx>,<f>  kalibrasyon komutu
//   G                             ayarlari ve raporlari iste
// ============================================================
static void wsDurumGonder();
static void wsAyarGonder();
static void wsRaporGonder(uint8_t s);

// Virgulle ayrilmis tam sayilari cozer. Kac tane okundugunu doner.
//
// Eskiden bu, strtol(p, (char**)&p, 10) seklinde satir icindeydi: const
// char*'in adresi char** diye veriliyordu. Bu tanimsiz davranis; derleyici
// aliasing varsayimiyla p'yi yeniden okumayabilir ve ayiklama sessizce
// bozulabilir. Ayri bir char* uzerinden yapmak hem dogru hem okunur.
static uint8_t sayilariAyikla(const char* p, long* cikis, uint8_t adet) {
  uint8_t i = 0;
  while (i < adet && p && *p) {
    char* uc = nullptr;
    cikis[i++] = strtol(p, &uc, 10);
    if (uc == p) break;                 // sayi degil -> dur
    p = uc;
    if (*p == ',') p++;
  }
  return i;
}

static void wsMesaj(const char* m, size_t n) {
  (void)n;
  switch (m[0]) {
    case 'C': {
      // C<gaz>,<ele>,<rud>,<arm>[,<ail>] - aileron SONA eklendi, boylece
      // eski bir sayfa surumu de calismaya devam eder (o zaman notr kalir).
      // C<gaz>,<ele>,<rud>,<arm>[,<ail>[,<epoch>]]
      // Alanlar hep SONA eklenir: eski bir sayfa surumu de calisir.
      long a[6] = {0, 0, 0, 0, 0, 0};
      const uint8_t adet = sayilariAyikla(m + 1, a, 6);
      webKaynak = adet >= 6 ? "ws6" : adet == 5 ? "ws5" : "wsKISA";
      if (adet >= 6) webEpoch = (uint8_t)a[5];
      kontrolUygula((int16_t)a[0], (int16_t)a[1], (int16_t)a[2],
                    (int16_t)a[4], a[3] != 0);
      break;
    }
    case 'X':
      acilDurdur("acil durdurma");
      wsDurumGonder();
      break;
    case 'S': {
      char k[16];
      const char* eq = strchr(m + 1, '=');
      if (!eq) break;
      size_t kl = (size_t)(eq - (m + 1));
      if (kl >= sizeof(k)) break;
      memcpy(k, m + 1, kl); k[kl] = 0;
      ayarUygula(k, strtol(eq + 1, nullptr, 10));
      wsAyarGonder();
      break;
    }
    case 'K': {
      long a[6] = {0, 0, 0, 0, 0, 0};
      const uint8_t n2 = sayilariAyikla(m + 1, a, 6);
      if (n2 < 6) { Serial.printf("[KAL] Bozuk komut: \"%s\"\n", m); break; }
      cfgKomut((uint8_t)a[0], (uint8_t)a[1], (uint16_t)a[2],
               (uint16_t)a[3], (uint16_t)a[4], (uint8_t)a[5]);
      break;
    }
    case 'G':
      wsAyarGonder();
      for (uint8_t s = 0; s < RC_SURF_COUNT; s++) if (raporVar[s]) wsRaporGonder(s);
      wsDurumGonder();
      break;
    default:
      break;
  }
}

static void wsDurumGonder() { String j; durumJson(j); ws.gonder(j.c_str()); }
static void wsAyarGonder()  { String j; ayarJson(j);  ws.gonder(j.c_str()); }
static void wsRaporGonder(uint8_t s) {
  if (s >= RC_SURF_COUNT || !raporVar[s]) return;
  String j = "{\"rep\":";
  raporJsonGovde(j, s);
  j += "}";
  ws.gonder(j.c_str());
}

// ============================================================
// HTTP - WebSocket kurulamazsa devreye giren yedek yol
// ============================================================
static void handleRoot() {
  // Firmware guncellendiginde tarayici cebindeki eski sayfayi kullanmasin.
  // Basligi vermezsek tarayici sayfayi sezgisel olarak onbellege aliyor ve
  // karta yeni kod atsan da eski arayuzle ugrasiyorsun.
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "text/html; charset=utf-8", PAGE_INDEX);
}

// Ana ekrana eklenen kisayolun uygulama gibi (tarayici cubugu olmadan)
// acilmasi icin gereken iki kaynak. Ikisi de PROGMEM'de, web_ui.h'de.
static void handleManifest() {
  server.sendHeader("Cache-Control", "max-age=86400");
  server.send_P(200, "application/manifest+json; charset=utf-8", PAGE_MANIFEST);
}

static void handleIcon() {
  server.sendHeader("Cache-Control", "max-age=86400");
  server.send_P(200, "image/svg+xml; charset=utf-8", PAGE_ICON);
}

static void handleControl() {
  webKaynak = "http";
  if (server.hasArg("ep")) webEpoch = (uint8_t)server.arg("ep").toInt();
  kontrolUygula((int16_t)server.arg("t").toInt(),
                (int16_t)server.arg("e").toInt(),
                (int16_t)server.arg("r").toInt(),
                (int16_t)server.arg("a").toInt(),
                server.arg("arm").toInt() == 1);
  String j; durumJson(j);
  server.send(200, "application/json", j);
}

static void handleStop() {
  acilDurdur("web ACIL DURDURMA");
  String j; durumJson(j);
  server.send(200, "application/json", j);
}

// Durum + pilot ayarlari + kalibrasyon raporlari. HTTP yedek yolunda
// tarayici bunu 1 Hz cekiyor; WebSocket varken hic kullanilmiyor.
static void handleStatus() {
  String j; durumJson(j);
  String a; ayarJson(a);
  // durumJson'un son '}' karakterini atip ayar ve raporlari icine katiyoruz
  j.remove(j.length() - 1);
  j += ",\"cfg\":";
  a.remove(0, 7);                 // "{\"cfg\":" onekini at
  a.remove(a.length() - 1);       // kapatan '}'
  j += a;
  j += ",\"reps\":[";
  bool ilk = true;
  for (uint8_t s = 0; s < RC_SURF_COUNT; s++) {
    if (!raporVar[s]) continue;
    if (!ilk) j += ",";
    raporJsonGovde(j, s);
    ilk = false;
  }
  j += "]}";
  server.send(200, "application/json", j);
}

static void handleSet() {
  const String k = server.arg("k");
  ayarUygula(k.c_str(), server.arg("v").toInt());
  String j; ayarJson(j);
  server.send(200, "application/json", j);
}

static void handleCfg() {
  cfgKomut((uint8_t)server.arg("op").toInt(),
           (uint8_t)server.arg("s").toInt(),
           (uint16_t)server.arg("mn").toInt(),
           (uint16_t)server.arg("md").toInt(),
           (uint16_t)server.arg("mx").toInt(),
           (uint8_t)server.arg("f").toInt());
  String j; durumJson(j);
  server.send(200, "application/json", j);
}

// ============================================================
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== RC Plane kumanda basliyor ===");
  Serial.printf("[BOOT] Protokol surumu %u, kanal sayisi %u\n",
                RC_PROTO_VER, (unsigned)RC_CH_COUNT);

  txAyarYukle();
  Serial.printf("[PILOT] mod=%u aileron=%s | trim ele=%d rud=%d ail=%d thr=%d | "
                "expo %u/%u/%u | oran %u/%u/%u | gaz siniri %%%u | elevator kol=%s\n",
                tx.mod, tx.aileron ? "VAR" : "yok",
                tx.trimEle, tx.trimRud, tx.trimAil, tx.trimThr,
                tx.expoEle, tx.expoRud, tx.expoAil,
                tx.rateEle, tx.rateRud, tx.rateAil, tx.thrLimit,
                tx.eleDogrudan ? "ekran yonu" : "RC standardi");

  if (TEZGAH_MODU) {
    Serial.println("\n*****************************************************");
    Serial.println("*  TEZGAH MODU ACIK - telemetri olmadan ARM edilir. *");
    Serial.println("*  UCUSTAN ONCE main.cpp'de TEZGAH_MODU = false YAP.*");
    Serial.println("*****************************************************\n");
  }

  radioSetup();

  // NOT: AP_STA + ESP-NOW'u STA arayuzune tasimak DENENDI ve ISE YARAMADI -
  // kumanda yine 'nowrx=0/s' gordu. Ikinci bir WiFi arayuzu acmak, nRF24'un
  // yanindaki gurultuyu da artiriyor. Bu yuzden sadece-AP'ye geri donuldu.
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS, AP_CHANNEL, false, AP_MAX_CONN);
  WiFi.setSleep(false);   // guc tasarrufu kapali -> HTTP/WS gecikmesi dusuk kalir
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
  server.on("/set", handleSet);
  server.on("/cfg", handleCfg);
  server.on("/manifest.json", handleManifest);
  server.on("/icon.svg", handleIcon);
  // Tarayici her sayfada favicon ister; onNotFound'a birakirsak koca HTML'i
  // bir daha gonderiyoruz. Bos cevapla kes.
  server.on("/favicon.ico", []() { server.send(204, "image/x-icon", ""); });
  server.onNotFound(handleRoot);
  server.begin();

  ws.basla(wsMesaj);

  lastWebMs = millis();
  lastAckMs      = millis();   // temas zamanlayicisinin sifir noktasi
  lastKurtarmaMs = millis();
  Serial.println("[WEB] HTTP :80 ve WebSocket :81 hazir.");
}

void loop() {
  ws.dongu();                // kol verisi (ana yol) - bloklamaz
  server.handleClient();     // sayfa + HTTP yedek yolu
  radioPoll();               // bekleyen gonderimi coz (bloklamaz)

  const uint32_t now = millis();

  // nRF24 bagli degilse periyodik olarak yeniden dene. Kabloyu duzeltince
  // kart resetlemeye gerek kalmadan durum satiri BAGLI'ya doner.
  if (USE_NRF24 && !radioOk && now - lastRfRetryMs >= RF_RETRY_MS) {
    lastRfRetryMs = now;
    radioSetup(true);          // ucus sirasinda: bekleme yok
  }

  // ---- ARAYUZ WATCHDOG (iki kademeli, bkz. WEB_TIMEOUT_MS notu) ----
  const uint32_t webSessiz = now - lastWebMs;

  // 1. kademe: GUCU kes, kontrolu koru. ARM DUSMEZ, boylece baglanti
  //    dondugunde pilot aninda gaz verebilir - gaz kolunu indirip yeniden
  //    arm etmek zorunda kalmaz (havada bunu yapmak dead-stick demekti).
  if (webSessiz > WEB_TIMEOUT_MS && !webKesik) {
    webKesik   = true;
    ctrl.thrIn = 0;
    notrYuzeyler();
    kanallariHesapla();
    Serial.printf("[WEB] Arayuz sustu (%lu ms) - GAZ KESILDI, yuzeyler notr."
                  " ARM korunuyor.\n", (unsigned long)webSessiz);
  }

  // 2. kademe: tarayici gercekten olmus. Artik disarm dogru karar.
  if (webSessiz > WEB_OLU_MS && ctrl.armed) {
    disarm("arayuz oldu");
  }

  // ---- TELSIZ TEMASI WATCHDOG (IKI KADEMELI) ----
  //
  // Temas = taze telemetri YA DA donanim ACK'i. ACK, paketin ucaga
  // ULASTIGININ donanim kaniti; ikisi de nRF24 geri yoluna bagli.
  //
  // NEDEN IKI KADEMELI: bu kapi tek kademeliydi ve dogrudan disarm ediyordu.
  // Menzil kenarinda tam ters etki yapiyor:
  //   1. nRF24 geri yolu ileri yoldan ONCE oluyor - kontrol ESP-NOW'dan
  //      devam ediyor, yani ucak hala kumanda edilebilir.
  //   2. Telemetri + ACK birden gidiyor -> DISARM.
  //   3. Gaz kolu yukarida oldugu icin ARM kapisi bir daha acilmiyor ve
  //      ekranda "gaz cikisi minimumda degil" yaziyor.
  // Yani pilot, ucak en uzaktayken ve hala kontrol edilebilirken motoru
  // KALICI olarak kaybediyor. Onlem degil, bizzat kaza sebebi. Ayni hata
  // arayuz watchdog'unda duzeltilmisti, burada kalmisti.
  //
  // 1. kademe: gucu kes, KONTROLU KORU. ARM dusmuyor, temas donunce pilot
  //            aninda gaz verebiliyor - gaz kolunu indirip yeniden arm
  //            etmek zorunda kalmiyor (havada bu dead-stick demek).
  // 2. kademe: RF_TEMAS_OLU_MS sonunda gercekten kopmus sayilir -> disarm.
  //
  // Bu KORUMASIZ birakmak degil: gercek link kaybinda ucagin KENDI
  // failsafe'i devrede ve geri yola hic bagli degil - RC_FAILSAFE_MS (500
  // ms) boyunca gecerli paket gelmezse gaz stop, yuzeyler notr, armed duser.
  // Asil koruma orasi.
  //
  // ARM kapisi bilerek DAHA SIKI kaliyor (bkz. kontrolUygula): havalanmadan
  // once telemetri sart, korlemesine arm yok. Gevseyen sey yalnizca
  // HAVADAYKEN gazi kesme karari.
  const bool temas = telemetriTaze() || ackTaze();
  if (temas) {
    temasKayipMs = 0;
    if (rfKesik) {
      rfKesik = false;
      Serial.println("[SAFE] Ucakla temas geri geldi.");
    }
  } else if (!temasKayipMs) {
    temasKayipMs = now;
  }

  if (!TEZGAH_MODU && !temas && temasKayipMs) {
    if (!rfKesik) {
      rfKesik    = true;
      ctrl.thrIn = 0;
      notrYuzeyler();
      kanallariHesapla();
      Serial.println("[SAFE] Ucakla temas yok - GAZ KESILDI, yuzeyler notr."
                     " ARM korunuyor.");
    }
    if (now - temasKayipMs > RF_TEMAS_OLU_MS && ctrl.armed) {
      disarm("ucakla temas kalici olarak yok");
    }
  }

  // Ucak kalibrasyon moduna girdiyse kumanda da ARM'da kalmamali.
  if (ctrl.armed && ucakKalib()) {
    disarm("ucak kalibrasyon moduna girdi");
  }

  // 50 Hz telsiz gonderimi
  if (now - lastTxMs >= RC_TX_PERIOD_MS) {
    lastTxMs = now;
    radioTick();
  }

  // Arayuze durum ve yeni raporlar
  static uint32_t lastWsMs = 0;
  if (ws.bagli()) {
    for (uint8_t s = 0; s < RC_SURF_COUNT; s++) {
      if (raporYeni[s]) { raporYeni[s] = false; wsRaporGonder(s); }
    }
    if (now - lastWsMs >= WS_DURUM_MS) {
      lastWsMs = now;
      wsDurumGonder();
    }
  }

  // 1 sn'de bir link kalitesi + log
  if (now - lastStatMs >= 1000) {
    lastStatMs = now;
    // Link kalitesi: gonderilen pakete karsilik ucaktan DONEN her sey.
    // ACK de telemetri de "ulasti" kaniti; eskiden yalnizca telemetri
    // sayiliyordu ve nRF24 geri yolu zayiflayinca gosterge, ileri yol
    // kusursuzken %0'a duserek pilota yanlis bilgi veriyordu.
    // Buyuk olani aliyoruz: ESP-NOW tek yolken ACK olmaz, nRF24 tek yolken
    // telemetri ACK'e biner - ikisi de ayni pencerede ayni paketi sayar.
    const uint32_t donen = winAck > winAcked ? winAck : winAcked;
    const uint32_t q = winSent ? (donen * 100UL) / winSent : 0;
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

    // SAGIR MODUL: cip SPI'dan cevap veriyor ama 50 Hz yayina hic ACK
    // donmuyor. radioOk true oldugu icin asagidaki RF_RETRY_MS yolu hic
    // tetiklenmiyordu; cipi bastan kurmak tek cikis.
    // IKI SART BIRDEN: ACK yok VE loop saglikli.
    //
    // Loop sagligi sarti yanlis alarmi kesiyor (bkz. tickHz notu): loop
    // bogulunca ACK'ler kacirilir ama telsizde bir sey yoktur, ve cipi
    // yeniden kurmak durumu kotulestirir.
    // lastAckMs boot'ta millis() ile kuruluyor: ucak kumanda acilirken
    // kapaliysa "hic ACK gelmedi" diye kurtarmayi tumden kapatmak yanlisti -
    // o durumda modul sagirsa hicbir zaman toparlanmazdi.
    // Kurtarmanin KENDI zamanlayicisi var (lastKurtarmaMs).
    //
    // Eskiden burada "kurtarmaya sans ver" diye lastAckMs = now yaziliyordu.
    // Ama lastAckMs TEMAS KANITI: ackTaze() tam o degeri okuyor. Yani
    // kurtarma temasi TAKLIT ediyordu ve iki hasari vardi:
    //   1. Her kurtarmada yalanci bir "Ucakla temas geri geldi" basiliyor,
    //      gaz kesilip donuyor -> git-gel. Olcumde 3 saniyede bir, 14 kez.
    //   2. temasKayipMs surekli sifirlandigi icin telsiz watchdog'unun
    //      2. KADEMESI HIC tetiklenemiyor: ucak tamamen kapaliyken kumanda
    //      asla disarm etmiyordu. Olculdu - ucak yokken DISARM: 0.
    // Temas kanitini yalnizca GERCEK ACK tazeleyebilir.
    if (USE_NRF24 && radioOk && nrf &&
        now - lastAckMs > NRF_SAGIR_MS && tickHz >= TICK_SAGLIKLI &&
        now - lastKurtarmaMs > NRF_KURTARMA_ARA_MS) {
      nrfKurtarma++;
      Serial.printf("[RF] nRF24 SAGIR (%lu ms ACK yok, loop saglikli"
                    " %u tur/s, cevapsiz=%u) - cip bastan kuruluyor.\n",
                    (unsigned long)(now - lastAckMs), tickHz, txArdisikDf);
      radioOk   = false;
      txPending = false;
      radioSetup(true);          // ucus sirasinda: bekleme yok
      lastKurtarmaMs = now;      // lastAckMs'e DOKUNMUYORUZ (bkz. ustteki not)
      txArdisikDf    = 0;
    }

    const bool rxAlive = telemetriTaze();
    webHz  = winWeb;
    winWeb = 0;
    ackHz  = winAck;
    winAck = 0;
    tickHz    = tickSayac;
    tickSayac = 0;
    nowHamHz    = nowHam;     nowHam    = 0;
    nowAtilanHz = nowAtilan;  nowAtilan = 0;

    // Surucunun O ANKI kanali - calisirken kayarsa ESP-NOW sessizce tek
    // yonlu olur ve boot'taki tek satir bunu yakalamaz.
    if (espnowOk) {
      wifi_second_chan_t ik = WIFI_SECOND_CHAN_NONE;
      esp_wifi_get_channel(&nowKanal, &ik);
    }

    Serial.printf("[TX] nrf=%-11s now=%-6s ws=%-3s | web=%u/s | kumanda=%s ucak=%s "
                  "thr=%u ele=%u rud=%u ail=%u | link=%u%% ack=%u/s "
                  "nowrx=%u/s kanal=%u tur=%u/s | rx=%s %uHz kayip=%%%u vbat=%umV",
                  !USE_NRF24  ? "KAPALI" : nrf      ? "BAGLI" : "BAGLI-DEGIL",
                  !USE_ESPNOW ? "KAPALI" : espnowOk ? "BAGLI" : "YOK",
                  ws.bagli() ? "VAR" : "yok",
                  webHz,
                  ctrl.armed ? "ARMED " : "DISARM",
                  !rxAlive         ? "CEVAP-YOK"
                  : ucakKalib()    ? "KALIBRAS"
                  : ucakFailsafe() ? "FAILSAFE "
                  : ucakArmed()    ? "ARMED    "
                                   : "DISARM   ",
                  ctrl.armed ? ctrl.thrUs : RC_US_MIN, ctrl.eleUs, ctrl.rudUs, ctrl.ailUs,
                  linkQuality, ackHz, nowHamHz, (unsigned)nowKanal, tickHz,
                  rxAlive ? "OK" : "--",
                  rxAlive ? tlm.rxHz : 0,
                  rxAlive ? tlm.lossPct : 0,
                  rxAlive ? tlm.vbatMv : 0);
    if (txStuck)     { Serial.printf(" | takilan=%u", txStuck); txStuck = 0; }
    if (txArdisikDf) { Serial.printf(" | cevapsiz=%u", txArdisikDf); }
    if (nrfKurtarma) { Serial.printf(" | kurtarma=%u", nrfKurtarma); nrfKurtarma = 0; }
    if (ucakKirli())  Serial.print(" | *KAYDEDILMEMIS KALIBRASYON*");
    if (TEZGAH_MODU)  Serial.print(" | *TEZGAH MODU*");
    Serial.println();

    // Teshis uyarilari icin ortak zamanlayici.
    //
    // 10 sn'de bir basiyorlar. Her saniye basmak iki sekilde zarar veriyor:
    // 115200'de ~18 ms loop yiyor, ve gercek olay satirlarini (ARM / DISARM /
    // FAILSAFE / SAGIR) gurultunun icinde kaybediyor. Ucak kapaliyken log
    // tamamen okunamaz hale geliyordu.
    static uint32_t nowUyariMs = 0;
    const bool nowUyarZamani = (now - nowUyariMs >= 10000);
    if (nowUyarZamani) nowUyariMs = now;

    // "Servolar oynuyor ama kumanda telemetri yok diyor" arizasinin teshisi.
    // Bu iki durumu ayirmak zorundayiz, cunku ikisi de ekranda "sinyal yok"
    // goruntusu veriyor ama sebepleri ve cozumleri bambaska.
    if (!rxAlive && ackHz && nowUyarZamani) {
      Serial.printf("[RF] !! ILERI YOL SAGLAM (%u ACK/s) ama GERI YOL OLU.\n"
                    "[RF]    Paket ucaga ulasiyor, telemetri donmuyor. Ucagin\n"
                    "[RF]    [RX] satirina bak: 'tlm nrf=0' ise ucak ACK\n"
                    "[RF]    payload yukleyemiyor, 'red' buyukse FIFO tasiyor.\n",
                    ackHz);
    } else if (!rxAlive && !ackHz && espnowOk && nowUyarZamani) {
      Serial.println("[RF] !! Ne ACK ne telemetri var. Ucak paketleri ESP-NOW'dan"
                     " aliyorsa servolar oynar ama nRF24 geri yolu yoktur;"
                     " ucakta 'now=BAGLI' ve 'tlm now=' sayacini kontrol et.");
    }

    // ESP-NOW geri yolu: gelmiyor mu, geliyor da uymuyor mu?
    if (espnowOk && !nowHamHz && nowUyarZamani) {
      Serial.println("[NOW] !! Ucaktan HIC ESP-NOW cercevesi gelmiyor. Ikinci geri"
                     " yol tumden kapali, telemetri yalnizca nRF24 ACK'ine kalmis."
                     " Ucakta ESPNOW_CHANNEL ile kumandada AP_CHANNEL ayni mi?");
    } else if (nowAtilanHz && nowUyarZamani) {
      Serial.printf("[NOW] !! ESP-NOW cercevesi GELIYOR ama filtreden dusuyor:"
                    " %u/s atildi (son boy=%u ilk bayt=0x%02X).\n"
                    "[NOW]    Bu MENZIL sorunu DEGIL, UYUSMAZLIK: iki tarafin"
                    " rc_protocol.h'si ayni mi, iki kart da yeni mi yuklendi?\n",
                    nowAtilanHz, nowSonBoy, nowSonB0);
    }
  }
}
