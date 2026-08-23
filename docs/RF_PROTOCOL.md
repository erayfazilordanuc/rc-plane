# RC Plane - Telsiz Protokolü Raporu

Kumanda (ESP32 WROOM-32 + nRF24L01) → Uçak (ESP32-C3 + ESC/servolar) arası veri akışının
tam tanımı. İki firmware de (`firmware/controller_software`, `firmware/flight_software`)
bu belgeye göre yazıldı; protokolde bir şey değiştireceksen önce burayı güncelle.

---

## 1. Donanım / pin bağlantısı

### Kumanda tarafı (şemadaki bağlantı — kod bu pinlere göre yazıldı)

| nRF24L01 | ESP32 pini | Kablo rengi (şemadaki) |
|---|---|---|
| VCC  | **3V3** (5V değil!) | kırmızı |
| GND  | GND    | siyah |
| CE   | GPIO4  | mavi |
| CSN  | GPIO5  | magenta |
| SCK  | GPIO18 | turuncu |
| MOSI | GPIO23 | camgöbeği |
| MISO | GPIO19 | yeşil |
| IRQ  | bağlı değil | — |

SCK/MOSI/MISO ESP32'nin VSPI varsayılanı ama kod `SPI.begin(18, 19, 23, 5)` ile bunları
**açıkça** veriyor — hem belge görevi görüyor hem başka bir kütüphane SPI'ı farklı pinlerle
başlatmışsa onu eziyor.

**SPI hızı:** RF24 kütüphanesinin varsayılanı 10 MHz. Dupont kablo + breadboard ile bu hız
çoğu zaman güvenilir değil, bu yüzden `RF_SPI_HZ = 4000000` kullanılıyor.

**Kondansatör zorunlu:** nRF24 yayın anında akım tepesi çeker; kartın 3V3 regülatörü bunu
karşılamazsa modül brown-out olur ve `radio.begin()` bazen geçer bazen geçmez.
**VCC–GND arasına, modül bacağına en yakın noktaya 10–100 µF** koy.

**Konnektör yönü:** nRF24 header'ı 2×4'tür, **1. bacak GND, 2. bacak VCC**. Bir sıra kaydırmak
çok kolay ve VCC/GND ters gelirse modül yanar. Taktıktan sonra modüle dokun — ılıksa hemen çek.

### Telsiz çalışmıyorsa: otomatik teşhis

```
pio run -e rfdiag -t upload -t monitor
```

`firmware/controller_software/diag/rf_diag.cpp` — RF24 kütüphanesini hiç kullanmadan ham SPI ile 4 test yapar.
**Bu firmware WiFi açmaz**; kumandaya dönmek için `pio run -e esp32dev -t upload -t monitor`.

| Test | Ne bulur |
|---|---|
| 1. Kısa devre | Breadboard'da birbirine değen pinler |
| 2. MISO sürücü | CSN LOW iken MISO'yu bir şey sürüyor mu (pull-up/pull-down testi). Sürmüyorsa MISO teli kopuk/yanlış bacakta, ya da CSN ulaşmıyor, ya da modül beslenmiyor |
| 3. Pin sıralaması | SCK/MISO/MOSI ve CE/CSN'nin **12 kombinasyonunu** dener; teller yer değiştirmişse doğru sırayı söyler |
| 4. Hız taraması | Çalışan kombinasyonda en yüksek kararlı SPI hızı |

Beklenen register değerleri (reset sonrası): `STATUS=0x0E`, `CONFIG=0x08`, `SETUP_AW=0x03`,
`EN_AA=0x3F`, `RF_CH=0x02`, `RF_SETUP=0x0E`. Okunan değerler birbirinin **bit kaydırılmışı**
ise (örn. `0x08, 0x10, 0x21, 0x42, 0x84`) MISO hattı boştadır — modül cevap vermiyor, hat
sadece SCK'dan gürültü topluyor.

### Uçak tarafı (ESP32-C3 SuperMini) — uçuş firmware'indeki kablolama

| İşlev | C3 pini |
|---|---|
| ESC sinyal | GPIO0 (10k pulldown ile) |
| Aileron sol / sağ | GPIO1 / GPIO3 |
| Elevator | GPIO4 |
| Rudder | GPIO21 |
| nRF24 SCK | GPIO5 |
| nRF24 MISO | GPIO6 |
| nRF24 MOSI | GPIO7 |
| nRF24 CSN | GPIO10 |
| nRF24 CE | GPIO20 |

GPIO20 normalde UART0 RX'tir; kart USB-CDC üzerinden konuştuğu için serbest kaldı.
ESC pinindeki 10k pulldown, kart boot ederken pinin havada kalıp ESC'ye çöp darbe
göndermesini engeller.

Uyulması gereken tek kural: **aynı GPIO iki işe verilmesin.** ESP32-C3'te SPI pinleri GPIO
matrisinden geçtiği için `SPI.begin(sck, miso, mosi, ss)` ile istediğin pine atanır —
donanım varsayılanına uymak zorunda değilsin.

Kaçınılacak pinler (ESP32-C3 SuperMini):
- **GPIO 2, 8, 9** — strapping pini, boot davranışını bozar
- **GPIO 18, 19** — USB D-/D+
- Kullanılabilir: 0, 1, 3, 4, 5, 6, 7, 10, 20, 21

**Besleme:** nRF24 mutlaka 3.3V. ESC ve servolar ayrı 5V UBEC'ten beslenmeli, kartın
regülatöründen değil — 9g servo kalkışta ~700 mA çeker ve kartı brown-out'a sokar.
GND'ler ortak olmalı.

---

## 2. İki taşıma katmanı: nRF24 + ESP-NOW

Aynı `RcPacket` iki bağımsız yoldan gidebilir. Her ikisi de aynı doğrulama (magic + CRC +
protokol sürümü), aynı arm kilidi ve aynı failsafe yolundan geçer — kurallar tek yerde,
uçak tarafında `paketIsle()` içinde tanımlı.

| | nRF24 | ESP-NOW |
|---|---|---|
| Donanım | Ayrı modül + 7 kablo + kondansatör | **Yok** — çipin kendi WiFi radyosu |
| Frekans | 2.508 GHz (kanal 108) | 2.412 GHz (WiFi kanal 1) |
| Adresleme | `"RCP01"` pipe | Yayın (broadcast) — **MAC eşleştirmesi gerekmez** |
| Telemetri | ACK payload | Ayrı yayın paketi |

Açma/kapama, iki projede de dosyanın başındaki sabitlerle:

```cpp
static const bool USE_NRF24  = true;
static const bool USE_ESPNOW = true;
```

**Tek şart:** iki kart aynı WiFi kanalında olmalı. Kumandanın `AP_CHANNEL` değeri ile uçağın
`ESPNOW_CHANNEL` değeri aynı olmalı (ikisi de 1).

İkisi birden açıkken yedekli link olur: uçak aynı paketi iki yoldan alabilir, `seq` farkı 0
olduğu için ikincisi tekrar sayılıp atılır (çıkışlar iki kez yazılmaz, telemetri iki kez
gönderilmez). Bir yol tamamen ölse bile uçuş devam eder.

Kumandanın seri logunda `yol=nRF24+NOW`, uçakta `nrf=BAGLI now=BAGLI` satırları hangi
yolun ayakta olduğunu gösterir; web arayüzünde "Telsiz yolu" alanı aynı bilgiyi verir.

> **Not:** ESP-NOW menzili LOS'ta nRF24 ile karşılaştırılabilir ama uçak tarafında WiFi
> radyosu nRF24'ten daha fazla akım çeker. UBEC'i buna göre seç.

---

## 2b. Telsiz ayarları (nRF24 — iki tarafta birebir aynı olmalı)

| Ayar | Değer |
|---|---|
| Kanal | `108` (2.508 GHz — WiFi bandının üstü) |
| Adres | `"RCP01"` (5 byte) |
| Veri hızı | `RF24_250KBPS` |
| CRC | `RF24_CRC_16` (donanımsal) |
| Retries | `setRetries(3, 5)` |
| Dinamik payload | açık |
| ACK payload | açık (telemetri için) |
| Gönderim hızı | **50 Hz** (20 ms'de bir paket) |

Kumanda `openWritingPipe(RC_RF_ADDRESS)` + `stopListening()`,
uçak `openReadingPipe(1, RC_RF_ADDRESS)` + `startListening()` kullanır.

**Gönderim blokelenmez.** Kumanda `radio.write()` yerine `startWrite()` + `radio.update()`
ile STATUS yoklaması yapar. Sebebi: `write()` ACK gelene ya da tüm tekrarlar tükenene kadar
bekler; link koptuğunda bu 20 ms'lik bütçenin ~12 ms'sini yer ve web arayüzünün cevap süresini
bozar — yani tam da kontrole en çok ihtiyaç duyulan anda kumanda ağırlaşır. Bir gönderim
15 ms'yi (`TX_TIMEOUT_US`) geçerse TX FIFO temizlenip paket iptal edilir; seri logda
`takilan=N` olarak görünür.

**WiFi/nRF24 girişimi:** AP kanalı **1** (2.412 GHz) sabitlenmiştir, nRF24 kanalı 108
(2.508 GHz). Aralarında 96 MHz var. `RC_RF_CHANNEL`'i değiştirirsen `AP_CHANNEL`'ı da
gözden geçir — üst üste binmemeliler.

---

## 3. Paket formatı

Ortak tanımlar `include/rc_protocol.h` dosyasındadır ve **iki projede de aynı olmalıdır**
(dosya `flight_software/include/` altına kopyalandı). Struct'lar `#pragma pack(1)` ile
paketlenmiştir, ESP32'ler little-endian olduğu için doğrudan `memcpy` uyumludur.

### Kumanda → Uçak: `RcPacket` (12 byte)

| Offset | Alan | Tip | Açıklama |
|---|---|---|---|
| 0 | `magic` | u8 | Her zaman `0xA5` |
| 1 | `seq` | u8 | Her pakette +1, 255→0 sarar (kayıp tespiti için) |
| 2–3 | `ch[0]` THROTTLE | u16 | 1000 = stop … 2000 = tam gaz |
| 4–5 | `ch[1]` AILERON | u16 | 1000 sol / 1500 nötr / 2000 sağ |
| 6–7 | `ch[2]` ELEVATOR | u16 | 1000 burun aşağı / 1500 nötr / 2000 yukarı |
| 8–9 | `ch[3]` RUDDER | u16 | 1000 sol / 1500 nötr / 2000 sağ |
| 10 | `flags` | u8 | bit0 = ARMED, üst nibble = protokol versiyonu (1) |
| 11 | `crc` | u8 | İlk 11 byte'ın CRC-8'i (poly 0x31, init 0xFF) |

**Tüm kanallar mikrosaniye (µs) cinsindendir.** Böylece alıcıda dönüşüm gerekmez:
`servo.writeMicroseconds(deger)` doğrudan çalışır. Derece istersen
`map(us, 1000, 2000, 0, 180)`.

**ARMED bitini mutlaka kontrol et.** Bit 0 ise gaz kanalını yok say ve ESC'ye 1000 yaz —
kumanda zaten 1000 gönderir ama alıcı da bunu ayrıca doğrulamalı.

### Uçak → Kumanda: `RcTelemetry` (8 byte, ACK payload ile)

| Offset | Alan | Tip | Açıklama |
|---|---|---|---|
| 0 | `magic` | u8 | `0xA5` |
| 1 | `seqEcho` | u8 | Alınan son `RcPacket.seq` |
| 2–3 | `vbatMv` | u16 | Batarya gerilimi mV. Ölçmüyorsan **0** bırak. |
| 4–5 | `lossPct` | u16 | Alıcının gördüğü paket kaybı % (0–100) |
| 6 | `status` | u8 | bit0 = armed, bit1 = failsafe aktif |
| 7 | `crc` | u8 | İlk 7 byte'ın CRC-8'i |

Bu paket ayrı gönderilmez; `radio.writeAckPayload(1, &tlm, sizeof(tlm))` ile önceden
kuyruğa konur ve bir sonraki gelen pakete cevap olarak otomatik döner.
Kumanda web arayüzünde "Telemetri / Batarya" alanlarında gösterir.
Telemetri istemiyorsan ACK payload yazmayı hiç çağırma — kumanda sadece "YOK" gösterir,
kontrol çalışmaya devam eder.

---

## 4. Failsafe kuralları

| Nerede | Koşul | Sonuç |
|---|---|---|
| Uçak | 500 ms (`RC_FAILSAFE_MS`) paket gelmedi | ESC → 1000 µs, tüm yüzeyler → 1500 µs, armed = false |
| Uçak | CRC / magic hatalı | Paket sessizce atılır (son geçerli değerler korunur) |
| Kumanda | Tarayıcı 1 sn komut göndermedi | Otomatik DISARM, gaz 1000 |
| Kumanda | Gaz minimumda değilken ARM denemesi | ARM reddedilir |
| Kumanda | ACİL DURDURMA butonu / boşluk tuşu | Anında DISARM + nötr |

Failsafe'ten sonra tekrar paket gelmeye başlarsa alıcı **kendiliğinden arm olmamalı**;
kumandadan `RC_FLAG_ARMED` gelene kadar gaz kapalı kalır (kumanda da DISARM'a düştüğü için
kullanıcının tekrar ARM basması gerekir).

---

## 5. Alıcı tarafı uygulaması (`firmware/flight_software/src/main.cpp`)

Bu bölüm önceden iskelet koddu; uçuş firmware'i yazıldıktan sonra gerçek uygulamanın
özetiyle değiştirildi. Kural tanımları hâlâ yukarıdaki bölümlerde.

**Tek giriş noktası.** nRF24'ten de ESP-NOW'dan da gelen paket aynı `paketIsle()`
fonksiyonuna girer. Doğrulama (magic + CRC + protokol sürümü), tekrar eleme (`seq`
farkı 0 ise atlanır), arm kilidi ve failsafe kuralları tek yerde tanımlı — iki taşıma
katmanı arasında davranış farkı oluşamaz.

**Çıkışlar LEDC ile sürülür, `ESP32Servo` kullanılmaz.** Kütüphanenin pin izin listesi
GPIO0'ı reddediyor ve derece↔µs dönüşümü nötr noktasını kaydırıyordu. LEDC ile değerler
doğrudan mikrosaniye yazılır, yani `rc_protocol.h` ile aynı dil konuşulur.

> **ESP32-C3 tuzağı:** C3'ün LEDC zamanlayıcısı en fazla **14 bit**tir. İnternetteki
> örneklerin çoğu klasik ESP32 için 16 bit kullanır; C3'e olduğu gibi kopyalanırsa
> `ledcSetup()` sessizce 0 döner, pinde hiç PWM oluşmaz ve servolar kıpırdamaz.
> 14 bit @ 50 Hz → 16384 adım / 20000 µs ≈ 1.22 µs çözünürlük, RC için fazlasıyla yeterli.

**Arm kilidi.** Boot'ta ve her failsafe'ten sonra kilit takılır. Kumandadan `ARMED=0`
olan bir paket görülene kadar açılmaz — yani link geri geldiğinde motor kendiliğinden
dönmez, operatörün DISARM → ARM yapması gerekir.

**Failsafe histerezisi.** Failsafe'ten çıkmak için art arda `RELINK_PAKET` (10) geçerli
paket şarttır. 50 Hz'de bu ~200 ms sağlam link demek; tek pakette geri dönmek link
seğirdiğinde motorun atım atım çalışmasına yol açıyordu.

**Telemetri paket gelişinden bağımsız tazelenir** (`TLM_YENILE_MS` = 100 ms). Sadece
paket geldiğinde gönderilseydi, iki yoldan gelen aynı paketin ikincisi tekrar sayılıp
atlandığında telemetride boşluk oluşurdu — kumanda 1 sn taze telemetri göremezse ARM'ı
reddettiği için bu küçük boşluklar bile "uçaktan cevap yok" demeye yetiyordu.

**ESP-NOW telemetrisi unicast gider.** Önce yayın olarak gönderiliyordu; yayın çerçeveleri
en düşük hızda, ACK'siz ve tekrarsız gider ve alıcı tarafta elenebiliyor — kumanda→uçak
yönü çalışıp uçak→kumanda yönünün ölmesi tam bu tabloydu. Artık kumandanın MAC'i ilk gelen
paketten öğrenilip eş olarak kaydediliyor, telemetri ona unicast gidiyor.

**ESC gaz aralığı kalibrasyonu.** Boot'ta USB bağlıyken 5 saniye içinde seri porttan `k`
basılırsa kalibrasyon yordamı çalışır. Kalibre edilmemiş bir ESC'de 1200 µs hâlâ stop
bölgesinde kalabilir; "gaz gidiyor ama motor dönmüyor" şikâyetinin en sık sebebi budur.
Bu yordam `THROTTLE_MAX_US` sınırını bilerek aşar (ESC'nin gerçek 2000 µs'yi görmesi şart),
bu yüzden asla uçuş kodundan çağrılmaz. **Pervane takılı olmamalı.**

**Batarya ölçümü henüz yok** — `bataryaOku()` 0 döner, kumanda da "ölçüm yok" gösterir.
ADC'ye gerilim bölücü eklendiğinde sadece o fonksiyonu doldurmak yeterli, arayüz
kendiliğinden çalışır.

Yüzey yönleri ve nötr kaymaları dosyanın başındaki `TERS_*` / `TRIM_*` sabitleriyle
düzeltilir; aileronlar karşıt çalıştığı için sağ kanat varsayılan olarak terstir.

## 6. Web arayüzü (manuel kumanda)

Kumanda ESP32'si **erişim noktası** açar — internete gerek yok, telefondan/laptoptan bağlan:

- **SSID:** `RC-Plane-TX`  **Şifre:** `rcplane1234`
- **Adres:** `http://192.168.4.1`

| Endpoint | Açıklama |
|---|---|
| `GET /` | Arayüz sayfası (slider'lar, ARM, acil durdurma) |
| `GET /c?t=&a=&e=&r=&arm=` | Kanalları uygular, durum JSON'u döner. Sayfa bunu **100 ms'de bir** çağırır. |
| `GET /stop` | Acil durdurma: DISARM + nötr |
| `GET /status` | Sadece durum okur, kontrolü değiştirmez |

Dönen JSON: `{"armed":bool,"thr":u16,"link":0-100,"tmax":u16,"rx":bool,"loss":u16,"vbat":mV}`

Arayüz özellikleri: aileron/elevator/rudder slider'ları bırakınca 1500'e döner (yaylı kol
davranışı), gaz kolu yerinde kalır. Klavye: `W`/`S` gaz, oklar yüzeyler, `Boşluk` acil durdurma.

`/c` çağrısı aynı zamanda **watchdog beslemesidir** — sayfa kapanır ya da WiFi koparsa
kumanda 1 sn içinde DISARM olur, uçak da 500 ms sonra failsafe'e girer.

---

## 7. İlk çalıştırma sırası

1. **Pervaneyi sök.** Aşağıdaki adımların hiçbiri pervane takılıyken yapılmaz.
2. Uçuş kartını yükle (`firmware/flight_software`), seri porttan `[RX]` satırını izle.
3. Kumandayı yükle (`firmware/controller_software`), telefonla `RC-Plane-TX` ağına bağlan,
   `http://192.168.4.1` adresini aç.
4. Arayüzde `link` %90+ olmalı. Düşükse sırasıyla: anten yönü, nRF24 VCC–GND arasındaki
   10–100 µF kondansatör, `RF24_PA_LOW`. Telsiz hiç açılmıyorsa `rfdiag` ortamını çalıştır.
5. Motor kapalıyken önce **yüzeyleri** dene. Yanlış yöne gidiyorsa uçuş firmware'indeki
   `TERS_*` sabitini `true` yap, nötr kaymışsa `TRIM_*` ile düzelt.
6. Motor hiç dönmüyorsa ESC kalibrasyonunu yap: kartı USB'yle boot et, 5 sn içinde `k` bas.
7. ARM et, gazı çok az aç, motor **yönünü** kontrol et. Ters dönüyorsa motorun üç fazından
   ikisini yer değiştir.
8. Uçuştan önce kumandadaki `TEZGAH_MODU` sabitini **`false`** yap ve iki taraftaki
   `THROTTLE_MAX_US` değerinin 2000 olduğunu doğrula.

## 8. Değiştirmek isteyebileceğin sabitler

| Sabit | Yer | Varsayılan |
|---|---|---|
| `TEZGAH_MODU` | controller `src/main.cpp` | `true` — **uçuştan önce `false` yap** |
| `THROTTLE_MAX_US` | controller `src/main.cpp` | 2000 (ilk testlerde 1200 yap) |
| `THROTTLE_MAX_US` | flight `src/main.cpp` | 2000 (ilk testlerde 1200 yap) |
| `USE_NRF24` / `USE_ESPNOW` | iki tarafta da `src/main.cpp` | ikisi de `true` |
| `AP_SSID` / `AP_PASS` | controller `src/main.cpp` | RC-Plane-TX / rcplane1234 |
| `AP_CHANNEL` | controller `src/main.cpp` | 1 |
| `ESPNOW_CHANNEL` | flight `src/main.cpp` | 1 (`AP_CHANNEL` ile aynı olmalı) |
| `RF_SPI_HZ` | iki tarafta da `src/main.cpp` | 4000000 |
| `TLM_TIMEOUT_MS` | controller `src/main.cpp` | 1000 |
| `RELINK_PAKET` | flight `src/main.cpp` | 10 |
| `TLM_YENILE_MS` | flight `src/main.cpp` | 100 |
| `TERS_*` / `TRIM_*` | flight `src/main.cpp` | sağ aileron ters, trim'ler 0 |
| `RC_RF_CHANNEL` | `rc_protocol.h` | 108 |
| `RC_RF_ADDRESS` | `rc_protocol.h` | "RCP01" |
| `RC_RF_DATARATE` | `rc_protocol.h` | 2 (250 kbps — klon modülde 0 yap) |
| `RC_TX_PERIOD_MS` | `rc_protocol.h` | 20 (50 Hz) |
| `RC_FAILSAFE_MS` | `rc_protocol.h` | 500 |

`rc_protocol.h` iki projede de bulunur ve **birebir aynı olmalıdır**. Birinde
değiştirdiğinde diğerine kopyala, yoksa link sessizce ölür.
