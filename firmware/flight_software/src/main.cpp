// ============================================================
// RC Plane - UCAK (alici) yazilimi
// Kart: ESP32-C3 DevKitM-1 + nRF24L01
//
// Ne yapar:
//   1. Kumandadan 50 Hz'de RcPacket dinler - nRF24 ve/veya ESP-NOW uzerinden
//   2. magic + CRC + protokol versiyonu dogrular
//   3. Kanallari servolara ve ESC'ye LEDC ile mikrosaniye olarak yazar
//   4. Kumandaya telemetri (paket kaybi, durum) geri gonderir
//   5. RC_FAILSAFE_MS boyunca gecerli paket gelmezse failsafe'e duser
//
// GUVENLIK:
//   - ARMED biti gelmedikce ESC her zaman 1000 us (motor durur).
//   - THROTTLE_MAX_US ile gaz tavani ucak tarafinda da ayrica sinirlanir.
//     Kumandadaki sinira guvenilmez; iki taraf bagimsiz kisitlar.
//   - Failsafe: gaz 1000 us, tum yuzeyler notr, armed dusurulur.
//   - Boot aninda ESC 1000 us'e cekilip 3 sn beklenir (ESC arming).
//
// UYARI: Ilk denemelerde PERVANE TAKILI OLMASIN!
// Servolar ve ESC harici 5V UBEC'ten beslenmeli, GND'ler ortak olmali.
// ============================================================
// Bes cikisin hepsi cipin LEDC birimiyle surulur, ESP32Servo kullanilmaz:
//  - kutuphanenin pin izin listesi (GPIO 0'i reddediyor) devre disi kalir
//  - degerler dogrudan mikrosaniye yazilir, rc_protocol.h ile ayni dil
//  - derece <-> us donusumu olmadigi icin notr noktasi kaymaz

#include <Arduino.h>
#include <SPI.h>
#include <RF24.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
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
// iki kart AYNI WiFi KANALINDA olmali (kumandanin AP kanali = 1).
// ------------------------------------------------------------
static const bool    USE_NRF24      = true;
static const bool    USE_ESPNOW     = true;
static const uint8_t ESPNOW_CHANNEL = 1;   // kumandadaki AP_CHANNEL ile ayni

// ------------------------------------------------------------
// Cikis pinleri (GPIO 2, 8, 9 strapping; 18, 19 USB -> kullanilmaz)
// ------------------------------------------------------------
static const int PIN_ESC      = 0;   // ESC sinyal (10k pulldown var)
static const int PIN_AIL_SOL  = 1;
static const int PIN_AIL_SAG  = 3;
static const int PIN_ELEVATOR = 4;
static const int PIN_RUDDER   = 21;

// ------------------------------------------------------------
// nRF24L01 pinleri
//   VCC  -> 3V3   ** 5V DEGIL **   (yanina 10 uF kondansator sart)
//   GND  -> GND
//   SCK  -> GPIO5
//   MISO -> GPIO6
//   MOSI -> GPIO7
//   CSN  -> GPIO10
//   CE   -> GPIO20
//   IRQ  -> bagli degil
// C3'un GPIO matrisi sayesinde SPI herhangi bir pine yonlendirilebilir,
// bu yuzden SPI.begin() cagrisinda pinler acikca verilir.
// GPIO20 normalde UART0 RX; USB-CDC kullandigimiz icin serbest.
// ------------------------------------------------------------
static const uint8_t PIN_RF_SCK  = 5;
static const uint8_t PIN_RF_MISO = 6;
static const uint8_t PIN_RF_MOSI = 7;
static const uint8_t PIN_RF_CSN  = 10;
static const uint8_t PIN_RF_CE   = 20;

// SPI hizi. Kutuphane varsayilani 10 MHz; dupont kablo + lehimli tel ile bu hiz
// cogu zaman guvenilir degil, radio.begin() bazen geciyor bazen gecmiyor.
// Kumanda tarafi ayni sorunu yasayip 4 MHz'e dusmustu (controller_software
// PIN tanimlarinin yanindaki nota bak); ucak tarafi da ayni degeri kullansin.
static const uint32_t RF_SPI_HZ = 4000000;

// ------------------------------------------------------------
// GAZ TAVANI - ucak tarafindaki bagimsiz sinir
// Pervane takili degilken / ilk testlerde 1200'de birak.
// Ucusa hazir oldugunda 2000 yap. Kumandadaki THROTTLE_MAX_US ile
// birlikte iki kademeli koruma olusturur.
// ------------------------------------------------------------
static const uint16_t THROTTLE_MAX_US = 2000;

// ------------------------------------------------------------
// Yuzey yonleri ve trim
// Servo linkajini taktiktan sonra tezgahta dene; yanlis yone gidiyorsa
// ilgili TERS_ degerini true yap. Aileronlar karsit calisir: biri yukari
// kalkarken digeri asagi iner, bu yuzden sag kanat varsayilan olarak ters.
// TRIM_ degerleri mekanik notr kaymasini duzeltir (us cinsinden, +/-).
// ------------------------------------------------------------
static const bool TERS_AIL_SOL  = false;
static const bool TERS_AIL_SAG  = true;
static const bool TERS_ELEVATOR = false;
static const bool TERS_RUDDER   = false;

static const int16_t TRIM_AIL_SOL  = 0;
static const int16_t TRIM_AIL_SAG  = 0;
static const int16_t TRIM_ELEVATOR = 0;
static const int16_t TRIM_RUDDER   = 0;

// ------------------------------------------------------------
// Baglanti geri geldiginde failsafe'ten cikmak icin gereken ardisik
// gecerli paket sayisi. 50 Hz'de 10 paket ~ 200 ms saglam link demek.
// Tek pakette geri donmek, link segirdiginde motorun atim atim calismasina
// yol acar; bu histerezis onu engeller.
// ------------------------------------------------------------
static const uint8_t RELINK_PAKET = 10;

// ------------------------------------------------------------
// nRF24 bulunamazsa bu araliklarla yeniden denenir. Boylece kabloyu
// duzeltmek icin karti resetlemek gerekmez; durum satiri kendiliginden
// BAGLI'ya doner. Ayni sekilde calisirken kablo cikarsa YOK'a duser.
// ------------------------------------------------------------
static const uint32_t RF_RETRY_MS = 3000;

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
// LEDC cozunurlugu
//
// DIKKAT: ESP32-C3'un LEDC zamanlayicisi en fazla 14 BIT destekler
// (soc_caps.h -> SOC_LEDC_TIMER_BIT_WIDE_NUM = 14). Klasik ESP32'de bu
// deger 20'dir ve internetteki orneklerin cogu 16 bit kullanir; C3'e
// oldugu gibi kopyalanirsa ledcSetup() sessizce 0 doner, zamanlayici hic
// kurulmaz ve pinde PWM olusmaz - servolar hic kipirdamaz, olcerken de
// pinlerde 0 V okursun. Buraya 14'ten buyuk bir sayi YAZMA.
//
// 14 bit @ 50 Hz -> 16384 adim / 20000 us = ~1.22 us cozunurluk.
// Servo icin fazlasiyla yeterli (RC standardi 1 us adimlarla calisir).
// ------------------------------------------------------------
static const uint8_t  LEDC_BIT       = 14;
static const uint32_t LEDC_ADIM      = 1UL << LEDC_BIT;   // 16384
static const uint32_t PWM_PERIYOT_US = 20000;             // 50 Hz

// ------------------------------------------------------------
// LEDC kanallari (C3'te 0..5 arasi, dusuk hiz modu)
// ------------------------------------------------------------
static const int KANAL_ESC     = 0;
static const int KANAL_AIL_SOL = 1;
static const int KANAL_AIL_SAG = 2;
static const int KANAL_ELE     = 3;
static const int KANAL_RUD     = 4;

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
static uint8_t  lastSeq       = 0;
static bool     seqBasladi    = false;
static uint16_t winBeklenen   = 0;
static uint16_t winAlinan     = 0;
static uint16_t winBozuk      = 0;
static uint16_t winTekrar     = 0;   // ayni paketin ikinci yoldan gelen kopyasi
static uint8_t  lossPct       = 0;

// Telemetri gonderim sayaclari (1 sn'lik pencere).
// Iki yol da sessizce basarisiz olabiliyor: writeAckPayload() FIFO doluysa
// false doner, esp_now_send() arayuz/es uyusmazliginda hata doner. Kumanda
// "ucaktan telemetri yok" derken sorunun HANGI yolda oldugunu ayirt etmek icin.
static uint16_t  tlmNrf     = 0;   // ACK payload'a basariyla yuklenen
static uint16_t  tlmNrfRed  = 0;   // FIFO dolu -> yuklenemeyen
static uint16_t  tlmNow     = 0;   // ESP-NOW ile kuyruga verilen
static uint16_t  tlmNowHata = 0;
static esp_err_t tlmSonHata = ESP_OK;

// esp_now_send()'in ESP_OK donmesi "kuyruga aldim" demek, "karsi taraf aldi"
// demek DEGIL. Gercek sonuc gonderim callback'inden gelir; yayin (broadcast)
// paketlerinde bu her zaman basarili raporlanir, unicast'te ise donanim
// ACK'ine dayanir. Asagidaki unicast gecisiyle birlikte bu sayaclar linkin
// gercekten iki yonlu olup olmadigini soyler.
static uint16_t  tlmNowAck  = 0;   // callback: teslim edildi
static uint16_t  tlmNowNack = 0;   // callback: teslim edilemedi

// ------------------------------------------------------------
// KUMANDANIN ADRESI
// Telemetri onceden yayin (FF:FF:...) olarak gonderiliyordu. Yayin cerceveleri
// en dusuk hizda, ACK'siz ve tekrarsiz gider; ayrica alici tarafta softAP'a
// bagli olmayan bir istasyondan gelen yayini surumune gore eleyebiliyor.
// Kumanda->ucak yonu calisip ucak->kumanda yonunun olmesi tam bu tabloydu.
//
// Cozum: kumandanin adresini ilk gelen paketten ogren ve telemetriyi ona
// UNICAST gonder. Unicast WiFi MAC katmaninda ACK'lenir ve tekrarlanir,
// ustelik gonderim callback'i gercek teslim durumunu verir.
// Adres ogrenilene kadar yayina devam edilir - yani ilk paket yine akar.
// ------------------------------------------------------------
static uint8_t   kumandaMac[6]   = {0};
static bool      kumandaMacGeldi = false;
static bool      kumandaEsiVar   = false;

// Son uygulanan kanal degerleri (log icin)
static uint16_t    sonThr      = RC_US_MIN;
static uint16_t    sonThrIstek = RC_US_MIN;  // kumandanin istedigi ham gaz
static const char* sonThrSebep = "disarm";   // uygulanan deger neden kirpildi
static uint16_t sonAil = RC_US_MID;
static uint16_t sonEle = RC_US_MID;
static uint16_t sonRud = RC_US_MID;

// ============================================================
// Cikislar
// ============================================================

// Mikrosaniyeyi LEDC duty degerine cevirir (50 Hz -> 20000 us periyot)
// Adim sayisi LEDC_BIT'ten turetilir; cozunurlugu degistirirsen burasi
// kendiliginden uyar, ikisi ayri dusmez.
static inline uint32_t usToDuty(uint16_t us) {
  return (uint32_t)((uint64_t)us * LEDC_ADIM / PWM_PERIYOT_US);
}

// Yon ve trim uygulayip guvenli araliga sikistirir
static inline uint16_t yuzeyDegeri(uint16_t us, bool ters, int16_t trim) {
  int32_t v = ters ? (3000 - (int32_t)us) : (int32_t)us;
  return rcClampUs(v + trim);
}

// ESC'ye yazar. ARMED degilse veya failsafe'deyse motor her zaman durur.
//
// Kumandanin ISTEDIGI deger de saklanir: log'da sadece uygulanan degeri
// gormek "gaz gitmiyor" derdinde hicbir sey anlatmiyor. Istek 1600 ama
// uygulanan 1000 ise sorun kumandada degil, buradaki kapida.
static void escYaz(uint16_t us) {
  sonThrIstek = us;                      // kumandanin gonderdigi ham deger

  if (!armed || failsafe) {
    sonThrSebep = failsafe ? "failsafe" : "disarm";
    us = RC_US_MIN;
  } else if (us > THROTTLE_MAX_US) {
    sonThrSebep = "tavan";               // THROTTLE_MAX_US ile kirpildi
    us = THROTTLE_MAX_US;
  } else {
    sonThrSebep = "-";                   // serbest geciyor
  }
  if (us < RC_US_MIN) us = RC_US_MIN;

  ledcWrite(KANAL_ESC, usToDuty(us));
  sonThr = us;
}

static void yuzeyleriYaz(uint16_t ail, uint16_t ele, uint16_t rud) {
  ledcWrite(KANAL_AIL_SOL, usToDuty(yuzeyDegeri(ail, TERS_AIL_SOL,  TRIM_AIL_SOL)));
  ledcWrite(KANAL_AIL_SAG, usToDuty(yuzeyDegeri(ail, TERS_AIL_SAG,  TRIM_AIL_SAG)));
  ledcWrite(KANAL_ELE,     usToDuty(yuzeyDegeri(ele, TERS_ELEVATOR, TRIM_ELEVATOR)));
  ledcWrite(KANAL_RUD,     usToDuty(yuzeyDegeri(rud, TERS_RUDDER,   TRIM_RUDDER)));
  sonAil = ail; sonEle = ele; sonRud = rud;
}

// Failsafe konumu: motor durur, yuzeyler notre gelir.
// Arm kilidi geri takilir; link donse bile operator DISARM -> ARM yapmali.
static void failsafeUygula() {
  armed          = false;
  armKilidi      = true;
  ardisikGecerli = 0;
  escYaz(RC_US_MIN);
  yuzeyleriYaz(RC_US_MID, RC_US_MID, RC_US_MID);
}

// ============================================================
// ESC GAZ ARALIGI KALIBRASYONU
//
// ESC, kalibrasyon sirasinda gordugu EN GENIS darbeyi "tam gaz", EN DAR
// darbeyi "stop" olarak ogrenir. Kalibre edilmemis bir ESC'de 1200 us hala
// stop bolgesinde kalabilir ve motor hic donmez - "gaz gidiyor ama motor
// donmuyor" sikayetinin en sik sebebi budur.
//
// Bu yordam THROTTLE_MAX_US sinirini BILEREK asar: kalibrasyon icin ESC'nin
// gercek RC_US_MAX'i gormesi sart. Bu yuzden ucus kodundan asla cagrilmaz;
// yalnizca boot'ta, USB bagliyken, elle tetiklenir ve her adimda onay ister.
//
// !! PERVANE TAKILI OLMAMALI !!
// ============================================================

// Seri porttan tek tus bekler. Once tampondaki eski karakterler yutulur ki
// onceki adimdan artan bir tus yanlislikla onay sayilmasin.
static void tusBekle(const char* mesaj) {
  while (Serial.available()) Serial.read();
  Serial.println(mesaj);
  while (!Serial.available()) delay(10);
  while (Serial.available()) Serial.read();
}

static void escKalibrasyon() {
  Serial.println();
  Serial.println("================ ESC GAZ ARALIGI KALIBRASYONU ================");
  Serial.println("!!! PERVANE TAKILI OLMAMALI - motor tam gaza kadar cikabilir !!!");
  Serial.println();

  tusBekle("1) LiPo'yu CIKAR (ESC tamamen gucsuz olsun). Bitince bir tusa bas.");

  ledcWrite(KANAL_ESC, usToDuty(RC_US_MAX));
  Serial.printf("2) Gaz TAM GAZ'a alindi (%u us).\n", RC_US_MAX);
  tusBekle("   Simdi LiPo'yu TAK. Bip seslerini duyunca bir tusa bas.");

  ledcWrite(KANAL_ESC, usToDuty(RC_US_MIN));
  Serial.printf("3) Gaz MINIMUM'a alindi (%u us). Onay bipleri geliyor...\n", RC_US_MIN);
  delay(4000);

  Serial.println("4) Kalibrasyon tamam. ESC artik 1000-2000 araligini biliyor.");
  Serial.println("   LiPo'yu cikar, karti resetle, normal ucus koduyla dene.");
  tusBekle("   Devam etmek icin bir tusa bas.");
  Serial.println("==============================================================");
}

// ============================================================
// Telemetri
// ============================================================

// Batarya olcumu yok. ADC'ye gerilim bolucu eklendiginde burayi doldur;
// kumandadaki batarya gostergesi otomatik calisir. 0 = olcum yok demek.
static uint16_t bataryaOku() {
  return 0;
}

// Telemetriyi acik olan tum yollardan kumandaya gonderir.
//
// nRF24: ACK payload onceden yuklenir - simdi yazilan paket, BUNDAN SONRAKI
//        alinan pakete cevap olarak gider. Bu yuzden setup'ta da bir kez cagrilir.
// ESP-NOW: dogrudan yayin olarak gonderilir.
static void telemetriYukle(uint8_t seqEcho) {
  RcTelemetry t;
  t.magic   = RC_MAGIC;
  t.seqEcho = seqEcho;
  t.vbatMv  = bataryaOku();
  t.lossPct = lossPct;
  t.status  = (uint8_t)((armed ? RC_STATUS_ARMED : 0) |
                        (failsafe ? RC_STATUS_FAILSAFE : 0));
  t.crc     = rcTelemetryCrc(t);

  // nRF24: ACK payload FIFO'su 3 derinliginde ve ancak nRF24 uzerinden bir
  // paket GELDIGINDE tuketilir. Paket ESP-NOW'dan gelip nRF24'ten gelmediginde
  // her seferinde bir yuk ekleniyor ama hicbiri tuketilmiyor; FIFO 3'te doluyor
  // ve writeAckPayload() KALICI olarak false donmeye basliyor. Telemetri o
  // andan sonra bir daha hic gitmiyor, kumanda "ucaktan cevap yok" diyor.
  //
  // Cozum: dolarsa FIFO'yu bosalt ve en TAZE telemetriyi tek basina koy.
  // Bayat telemetriyi kuyrukta tutmanin zaten bir degeri yok - kumanda her
  // zaman en son durumu istiyor.
  if (radioOk) {
    if (radio.writeAckPayload(1, &t, sizeof(t))) {
      tlmNrf++;
    } else {
      radio.flush_tx();                          // bayat yukleri at
      if (radio.writeAckPayload(1, &t, sizeof(t))) tlmNrf++;
      tlmNrfRed++;                               // kurtarma sayisi
    }
  }

  if (espnowOk) {
    // TELEMETRI HER ZAMAN YAYIN (broadcast) ile gider.
    //
    // Unicast denendi ve olcum acikca reddetti: ack=0 / nack=57, yani saniyede
    // 57 gonderimin HICBIRI ACK almadi, ardindan ESP_ERR_ESPNOW_NO_MEM geldi.
    // Sebep: ucagin STA'si kumandanin AP'sine ilislkilendirilmis degil.
    // Iliskilendirilmemis bir STA'dan AP'ye giden unicast cerceve MAC
    // seviyesinde ACK almaz; ESP-NOW tekrar tekrar dener, gonderim kuyrugu
    // dolar ve telemetri tumden durur.
    //
    // Yayin cercevesi ACK beklemez, tekrar etmez, kuyrugu doldurmaz. Kumanda
    // zaten yayini dinliyor (onEspNowRecv) ve pakette magic + CRC var.
    const uint8_t* hedef = BCAST;

    // ESP_ERR_ESPNOW_IF (arayuz/es uyusmazligi) ve ESP_ERR_ESPNOW_NOT_FOUND
    // (es kayitli degil) en sik gorulenler; ikisi de sessizce olursa kumanda
    // sadece "telemetri yok" der.
    esp_err_t r = esp_now_send(hedef, (const uint8_t*)&t, sizeof(t));
    if (r == ESP_OK) {
      tlmNow++;
    } else {
      tlmNowHata++;
      tlmSonHata = r;
    }
  }
}

// ============================================================
// Paket isleme ve tasima katmanlari
// ============================================================

// ------------------------------------------------------------
// Gelen bir paketi dogrular ve uygular. IKI TASIMA KATMANI DA buraya girer,
// yani dogrulama / arm kilidi / failsafe kurallari tek yerde tanimli.
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

  // Paket kaybi ve tekrar tespiti.
  //
  // Iki tasima katmani ayni paketi tasir, yani her seq IKI kez gelir ve
  // aralarinda gecikme farki vardir - nRF24 kopyasi ESP-NOW kopyasindan sonra
  // da gelebilir. Fark isaretsiz hesaplanirsa 99 - 100 = 255 cikar ve "255
  // paket dustu" sayilir; kayip yuzdesi %99'a firlarken aslinda hicbir sey
  // kaybolmamistir. Isaretli fark bunu dogru cozer:
  //   delta > 0  -> ileri gidis, aradaki bosluk gercek kayip
  //   delta <= 0 -> ayni paket diger yoldan geldi ya da sirasiz geldi
  //
  // lastSeq sadece ileri giderken ilerletilir, yoksa iki yol birbirini
  // surekli geri cekip sayaci bozar.
  bool tekrar = false;
  if (seqBasladi) {
    const int8_t delta = (int8_t)((uint8_t)(p.seq - lastSeq));
    if (delta <= 0) {
      tekrar = true;
    } else {
      winBeklenen += (uint16_t)delta;
      lastSeq      = p.seq;
      winAlinan++;                    // sadece BENZERSIZ paketler sayilir,
    }                                 // yoksa alinan > beklenen olup tasar
  } else {
    winBeklenen += 1;
    winAlinan++;
    lastSeq    = p.seq;
    seqBasladi = true;
  }
  winTekrar += tekrar ? 1 : 0;
  lastRxMs = millis();

  // Ayni paketi iki kez uygulamanin/telemetriyi iki kez gondermenin anlami yok
  if (tekrar) return;

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

  armed = paketArmed && !armKilidi;

  // Yuzeyler failsafe cikisini beklemeden takip eder (zararsiz, faydali);
  // gaz ise escYaz() icinde failsafe/armed kapisindan gecer.
  yuzeyleriYaz(rcClampUs(p.ch[RC_CH_AILERON]),
               rcClampUs(p.ch[RC_CH_ELEVATOR]),
               rcClampUs(p.ch[RC_CH_RUDDER]));
  escYaz(rcClampUs(p.ch[RC_CH_THROTTLE]));

  telemetriYukle(p.seq);              // bir sonraki ACK'e bindir / yayinla
}

// ------------------------------------------------------------
// ESP-NOW
// ------------------------------------------------------------

// WiFi gorevi baglaminda cagrilir. Burada servo yazmak/log basmak yok:
// sadece kopyala ve isaretle, isi loop() yapar. Kontrol verisinde en yeni
// paket onemli oldugu icin bekleyen paket varsa uzerine yazilir.
static volatile bool espnowYeni = false;
static RcPacket      espnowBuf;
static portMUX_TYPE  espnowMux = portMUX_INITIALIZER_UNLOCKED;

static void onEspNowRecv(const uint8_t* mac, const uint8_t* data, int len) {
  if (len != sizeof(RcPacket)) return;
  portENTER_CRITICAL(&espnowMux);
  memcpy(&espnowBuf, data, sizeof(RcPacket));
  memcpy(kumandaMac, mac, 6);     // gonderenin adresini ogren
  kumandaMacGeldi = true;
  espnowYeni = true;
  portEXIT_CRITICAL(&espnowMux);
}

// Gonderim sonucu callback'i. Yayinda her zaman SUCCESS gelir; unicast'te
// donanim ACK'ine dayanir, yani "kumanda gercekten aldi mi" sorusunun tek
// durust cevabi burasi.
static void onEspNowSend(const uint8_t* mac, esp_now_send_status_t durum) {
  (void)mac;
  if (durum == ESP_NOW_SEND_SUCCESS) tlmNowAck++;
  else                               tlmNowNack++;
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

  const esp_err_t r = esp_now_add_peer(&peer);
  if (r == ESP_OK) {
    kumandaEsiVar = true;
    // Telemetri yine de YAYIN ile gidiyor (bkz. telemetriYukle icindeki not:
    // unicast, iliskilendirilmemis STA -> AP yonunde ACK alamayip kuyrugu
    // dolduruyordu). Es kaydi ileride unicast gerekirse hazir dursun diye
    // tutuluyor; su an sadece kumandanin adresini bilmeye yariyor.
    Serial.printf("[NOW] Kumanda adresi ogrenildi: %02X:%02X:%02X:%02X:%02X:%02X"
                  " (telemetri yayinla gidiyor)\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  } else {
    Serial.printf("[NOW] Kumanda esi eklenemedi: %s\n", esp_err_to_name(r));
  }
}

static void espnowReceive() {
  if (!espnowOk || !espnowYeni) return;

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
  WiFi.disconnect();       // hicbir aga baglanmiyoruz, sadece radyo lazim
  WiFi.setSleep(false);    // modem sleep acik kalirsa paket kaciriliyor

  // Baglanti kurmadan kanal sabitlemenin tek yolu: promiscuous'u kisa sureli ac
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

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

  Serial.printf("[NOW] Hazir. Yayin modu, kanal %u, MAC %s\n",
                ESPNOW_CHANNEL, WiFi.macAddress().c_str());
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
  radio.setPALevel(RF24_PA_HIGH);       // modul resetleniyorsa RF24_PA_LOW dene

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

// Gelen paketleri isler
static void radioReceive() {
  if (!radioOk) return;

  uint8_t pipe;
  while (radio.available(&pipe)) {
    uint8_t len = radio.getDynamicPayloadSize();
    if (len != sizeof(RcPacket)) {
      radio.flush_rx();                 // yabanci/bozuk boy -> at
      winBozuk++;
      break;
    }

    RcPacket p;
    radio.read(&p, sizeof(p));
    paketIsle(p);
  }
}

// ============================================================
void setup() {
  Serial.begin(115200);  // C3'te USB-CDC uzerinden cikar
  delay(500);
  Serial.println("\n=== RC Plane ucak (alici) basliyor ===");

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
    Serial.println("[BOOT]    Ayri UBEC kullan, GND'leri ortakla, karti ESC'nin");
    Serial.println("[BOOT]    BEC'inden besleme. Bu bir YAZILIM sorunu degil.");
  }

  // ESC kanalini kur ve arm et: once 1000 us, sonra 3 saniye bekle
  // ledcSetup 0 dondurduyse kanal HIC kurulmamistir ve o pinde asla PWM
  // olusmaz. Sessizce basarisiz olmasi cok pahaliya patliyor (servolar
  // olu, pinlerde 0 V, sebep gorunmuyor) - bu yuzden gurultulu bagiriyoruz.
  uint32_t f = ledcSetup(KANAL_ESC, 50, LEDC_BIT);
  ledcAttachPin(PIN_ESC, KANAL_ESC);
  ledcWrite(KANAL_ESC, usToDuty(RC_US_MIN));
  Serial.printf("ESC         pin %2d -> LEDC kanal %d (%u Hz)\n",
                PIN_ESC, KANAL_ESC, (unsigned)f);
  if (f == 0) Serial.println("[!!] ESC LEDC KANALI KURULAMADI - pinde sinyal olmayacak!");

  // Dort servo kanalini kur ve notre (1500 us) al
  const int servoPinleri[4]  = { PIN_AIL_SOL, PIN_AIL_SAG, PIN_ELEVATOR, PIN_RUDDER };
  const int servoKanallari[4] = { KANAL_AIL_SOL, KANAL_AIL_SAG, KANAL_ELE, KANAL_RUD };
  const char* servoIsimleri[4] = { "Aileron Sol", "Aileron Sag", "Elevator", "Rudder" };
  for (int i = 0; i < 4; i++) {
    f = ledcSetup(servoKanallari[i], 50, LEDC_BIT);
    ledcAttachPin(servoPinleri[i], servoKanallari[i]);
    Serial.printf("%-11s pin %2d -> LEDC kanal %d (%u Hz)\n",
                  servoIsimleri[i], servoPinleri[i], servoKanallari[i], (unsigned)f);
    if (f == 0) Serial.printf("[!!] %s LEDC KANALI KURULAMADI - pinde sinyal olmayacak!\n",
                              servoIsimleri[i]);
  }
  yuzeyleriYaz(RC_US_MID, RC_US_MID, RC_US_MID);

  // Kalibrasyon penceresi. Sadece USB bagliyken acilir: uctaki kartta seri
  // port yoktur, o yuzden ucusta bu 5 saniye hic beklenmez.
  if (Serial) {
    Serial.println("ESC gaz araligi kalibrasyonu icin 5 saniye icinde 'k' bas.");
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
                THROTTLE_MAX_US, RC_FAILSAFE_MS);
}

void loop() {
  radioReceive();     // nRF24
  espnowReceive();    // ESP-NOW

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
  static uint32_t lastTlmYenileMs = 0;
  if (now - lastTlmYenileMs >= TLM_YENILE_MS) {
    lastTlmYenileMs = now;
    telemetriYukle(lastSeq);
  }

  // nRF24 bulunamadiysa periyodik olarak yeniden dene. Kabloyu duzeltince
  // kart resetlemeye gerek kalmadan durum satiri BAGLI'ya doner.
  static uint32_t lastRfRetryMs = 0;
  if (USE_NRF24 && !radioOk && now - lastRfRetryMs >= RF_RETRY_MS) {
    lastRfRetryMs = now;
    radioSetup();
  }

  // 1 sn'de bir paket kaybi + log
  if (now - lastStatMs >= 1000) {
    lastStatMs = now;
    // winAlinan artik sadece benzersiz paketleri sayiyor, yani her zaman
    // winBeklenen'den kucuk esit. Yine de tasmaya karsi acik korumali:
    // isaretsiz cikarma bir kez negatife duserse yuzde 255'e firlar.
    if (!winBeklenen)                  lossPct = 100;
    else if (winAlinan >= winBeklenen) lossPct = 0;
    else lossPct = (uint8_t)(100 - (uint32_t)winAlinan * 100 / winBeklenen);

    // Modulun O ANKI durumu. Calisirken kablo cikarsa bayragi dusur ki
    // yukaridaki yeniden deneme devreye girsin.
    const bool nrf = nrfBagli();
    if (radioOk && !nrf) {
      radioOk = false;
      nrfHataBasildi = false;   // kopus ayrintisi tekrar basilabilsin
      Serial.println("[RF] nRF24 BAGLI DEGIL - modul cevap vermeyi birakti.");
    }

    Serial.printf("[RX] nrf=%-11s now=%-6s | %s%s rxarm=%u kilit=%-5s relink=%u/%u | "
                  "thr=%u<-%u(%s) ail=%u ele=%u rud=%u | %u paket, tekrar %u, "
                  "kayip %%%u, bozuk %u | tlm nrf=%u/red%u now=%u/hata%u %s\n",
                  !USE_NRF24 ? "KAPALI" : nrf ? "BAGLI" : "BAGLI-DEGIL",
                  !USE_ESPNOW ? "KAPALI" : espnowOk ? "BAGLI" : "YOK",
                  failsafe ? "FAILSAFE " : "",
                  armed ? "ARMED " : "DISARM",
                  (unsigned)sonPaketArmed,
                  armKilidi ? "KAPALI" : "ACIK",
                  (unsigned)ardisikGecerli, (unsigned)RELINK_PAKET,
                  sonThr, sonThrIstek, sonThrSebep, sonAil, sonEle, sonRud,
                  winAlinan, winTekrar, lossPct, winBozuk,
                  tlmNrf, tlmNrfRed, tlmNow, tlmNowHata,
                  tlmNowHata ? esp_err_to_name(tlmSonHata) : "");

    winBeklenen = winAlinan = winBozuk = winTekrar = 0;
    tlmNrf = tlmNrfRed = tlmNow = tlmNowHata = 0;
    tlmNowAck = tlmNowNack = 0;
    if (failsafe) seqBasladi = false;   // baglanti koptuysa seq takibini sifirla
  }
}
