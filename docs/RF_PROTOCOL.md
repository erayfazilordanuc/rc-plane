# RC Plane — Telsiz Protokolü ve Sistem Raporu (Sürüm 2)

Kumanda (ESP32 WROOM-32 + nRF24L01) ↔ Uçak (ESP32 DevKitC + ESC/servolar) arası veri akışının
tam tanımı. İki firmware de bu belgeye göre yazıldı.

> **Sürüm 2 uyarısı:** Uçak **3 kanallı** oldu (gaz + elevator + rudder), aileron kaldırıldı.
> Paket formatı ve `magic` değerleri değişti. **Sürüm 1 firmware ile uyumsuzdur** —
> iki kartı da birlikte yükle.

---

## 1. Sistem özeti

| | Kumanda (`controller_software`) | Uçak (`flight_software`) |
|---|---|---|
| Kart | ESP32 DevKit (WROOM-32) | ESP32 DevKitC 38 pin (WROOM-32D) |
| Rol | WiFi AP + web arayüzü + verici | Alıcı + servo/ESC sürücü |
| Port | COM5 (CP210x köprü) | COM6 (USB köprü — ilk takışta `pio device list` ile doğrula) |
| Kalıcı ayar | Pilot ayarları (trim/expo/oran/mod) | Servo kalibrasyonu (yön/nötr/uç) |

**Ayarların hangi tarafta durduğu bilinçli bir karar:**
uç noktalar ve nötr **uçağın** özelliği (linkaj mekaniği), trim ve expo **pilotun**
özelliği. Bu yüzden kalibrasyon uçağın NVS'inde, trim/expo kumandanın NVS'inde.
Telefonu değiştirmek hiçbirini kaybettirmez.

---

## 2. Donanım / pin bağlantısı

### Kumanda tarafı — nRF24L01

| nRF24L01 | ESP32 pini | Kablo rengi |
|---|---|---|
| VCC  | **3V3** (5V değil!) | kırmızı |
| GND  | GND    | siyah |
| CE   | GPIO4  | mavi |
| CSN  | GPIO5  | magenta |
| SCK  | GPIO18 | turuncu |
| MOSI | GPIO23 | camgöbeği |
| MISO | GPIO19 | yeşil |
| IRQ  | bağlı değil | — |

**SPI hızı:** `RF_SPI_HZ = 4000000`. Kütüphane varsayılanı 10 MHz; dupont kablo +
breadboard ile o hız çoğu zaman güvenilir değil.

**Kondansatör zorunlu:** nRF24 yayın anında akım tepesi çeker; kartın 3V3 regülatörü bunu
karşılamazsa modül brown-out olur ve `radio.begin()` bazen geçer bazen geçmez.
**VCC–GND arasına, modül bacağına en yakın noktaya 10–100 µF** koy.

**Konnektör yönü:** nRF24 header'ı 2×4'tür, **1. bacak GND, 2. bacak VCC**. Bir sıra
kaydırmak çok kolay ve VCC/GND ters gelirse modül yanar. Taktıktan sonra modüle dokun —
ılıksa hemen çek.

### Uçak tarafı (ESP32 DevKitC 38 pin) — 3 kanal

Tam şema: `flight_software/docs/kablolama.html`.

| İşlev | GPIO | Not |
|---|---|---|
| **ESC (gaz)** | **GPIO25** | Sinyal–GND arası 10k; boot sırasında ESC'ye çöp darbe gitmiyor |
| **Elevator servo** | **GPIO26** | |
| **Rudder servo** | **GPIO27** | |
| nRF24 SCK | GPIO18 | kumandayla aynı |
| nRF24 MISO | GPIO19 | kumandayla aynı |
| nRF24 MOSI | GPIO23 | kumandayla aynı |
| nRF24 CSN | GPIO5 | kumandayla aynı |
| nRF24 CE | GPIO4 | kumandayla aynı |

Kaçınılan pinler: **GPIO 0, 2, 12, 15** (strapping), **14** (boot'ta PWM), **6–11**
(flash — kartta `D0–D3`, `CMD`, `CLK` yazar; `D2` GPIO2 değil), **1, 3** (UART0).

**Beslemeler:** nRF24 mutlaka 3.3 V (38 pinli kartta 3V3 sol üst pin). Ayrı UBEC yok:
ESC'nin BEC çıkışı kartın 5V pinine gelir, servolar oradan beslenir; BEC hattında 5V–GND
arası kondansatör var. GND'ler ortak olmalı.

Uçak boot logunda reset sebebini basıyor. `BROWNOUT` görüyorsan sorun besleme, yazılım
değil — kod bunu düzeltemez.

### Telsiz çalışmıyorsa: otomatik teşhis

```
pio run -e rfdiag -t upload -t monitor
```

`diag/rf_diag.cpp` — RF24 kütüphanesini hiç kullanmadan ham SPI ile 4 test yapar:
kısa devre, MISO sürücü, pin sıralaması (12 kombinasyon), hız taraması.
**Bu firmware WiFi açmaz**; kumandaya dönmek için `pio run -t upload -t monitor`.

Beklenen register değerleri (reset sonrası): `STATUS=0x0E`, `CONFIG=0x08`, `SETUP_AW=0x03`,
`EN_AA=0x3F`, `RF_CH=0x02`, `RF_SETUP=0x0E`. Okunan değerler birbirinin **bit
kaydırılmışı** ise (örn. `0x08, 0x10, 0x21, 0x42, 0x84`) MISO hattı boştadır.

---

## 3. İki taşıma katmanı: nRF24 + ESP-NOW

Aynı paket iki bağımsız yoldan gidebilir. Her ikisi de aynı doğrulamadan (magic + CRC +
protokol sürümü), aynı arm kilidinden ve aynı failsafe yolundan geçer — kurallar tek
yerde, uçak tarafında `paketIsle()` içinde.

| | nRF24 | ESP-NOW |
|---|---|---|
| Donanım | Ayrı modül + 7 kablo + kondansatör | **Yok** — çipin kendi WiFi radyosu |
| Frekans | 2.508 GHz (kanal 108) | 2.412 GHz (WiFi kanal 1) |
| Adresleme | `"RCP01"` pipe | Yayın — **MAC eşleştirmesi gerekmez** |
| Geri yol | ACK payload | Ayrı yayın paketi |

Açma/kapama, iki projede de dosyanın başındaki sabitlerle:

```cpp
static const bool USE_NRF24  = true;
static const bool USE_ESPNOW = true;
```

**Tek şart:** kumandanın `AP_CHANNEL` değeri ile uçağın `ESPNOW_CHANNEL` değeri aynı
olmalı (ikisi de 1).

İkisi birden açıkken yedekli link olur: uçak aynı paketi iki yoldan alabilir, `seq` farkı
≤ 0 olduğu için ikincisi tekrar sayılıp atılır (çıkışlar iki kez yazılmaz). Bir yol tamamen
ölse bile uçuş devam eder.

> **Telemetri neden yayın?** Unicast denendi ve ölçüm reddetti: `ack=0 / nack=57` — saniyede
> 57 gönderimin hiçbiri ACK almadı, ardından `ESP_ERR_ESPNOW_NO_MEM` geldi. Uçağın STA'sı
> kumandanın AP'sine ilişkilendirilmiş değil; ilişkilendirilmemiş bir STA'dan AP'ye giden
> unicast çerçeve MAC seviyesinde ACK almaz, ESP-NOW tekrar tekrar dener, kuyruk dolar ve
> telemetri tümden durur. Yayın çerçevesi ACK beklemez, kuyruğu doldurmaz.

### nRF24 ayarları (iki tarafta birebir aynı olmalı)

| Ayar | Değer |
|---|---|
| Kanal | `108` (2.508 GHz) |
| Adres | `"RCP01"` (5 byte) |
| Veri hızı | `RF24_250KBPS` (`RC_RF_DATARATE = 2`) |
| CRC | `RF24_CRC_16` (donanımsal) |
| Retries | `setRetries(5, 3)` — ARD 1500 µs, 3 tekrar |
| PA seviyesi | `RF24_PA_HIGH` — aşağıdaki nota bak |
| Dinamik payload | açık |
| ACK payload | açık (telemetri + kalibrasyon raporu) |
| Gönderim hızı | **50 Hz** (20 ms) |

**MAX_RT ve TX FIFO kilidi — "link ölüyor, bir daha geri gelmiyor":**
Oturumun en pahalıya patlayan hatası buydu. nRF24, bütün tekrarlar
tükendiğinde (`MAX_RT`) başarısız paketi TX FIFO'dan **atmaz**;
temizlemek yazılıma düşer. `radioPoll()` bayrakları temizliyor ama
`flush_tx()` çağırmıyordu. Zincir:

1. Üç ardışık başarısız gönderim → TX FIFO dolar
2. `startWrite()` `false` döner → `txPending` false kalır
3. `radioPoll()` `if (!txPending) return;` ile **bir daha hiç koşmaz**
4. FIFO sonsuza kadar dolu → telsiz **kalıcı olarak kilitli**
5. Cip SPI'dan cevap verdiği için durum satırı hâlâ `nrf=BAGLI` der

Teşhis imzası: `cevapsiz=` sayacı **tam 3'te** (FIFO derinliği) donup kalır,
`takilan=0`, `ack=0`, `tur=49/s` — yani loop sağlıklıyken gönderim hiç
başlamıyor. Menzil kenarında ya da uçak bir an resetlendiğinde üç ardışık
başarısızlık çok kolay oluşuyor.

Çözüm: `MAX_RT`'de `flush_tx()`, ve `startWrite()` false dönerse de
`flush_tx()`. Bayat bir kontrol çerçevesini saklamanın değeri yok — 20 ms
sonra tazesi geliyor.

**PA seviyesi — düşürüldü, sonra geri yükseltildi:**
Bir ara `PA_LOW`'a inilmişti: `PA_HIGH` ile link kumanda boot ettikten ~2 sn
sonra ölüyor ve **bir daha geri gelmiyordu**. Yayın anındaki akım tepesi gibi
görünüyordu.

O **kalıcılığın** asıl sebebi PA değildi — yukarıdaki FIFO kilidiydi. Kilit
kırıldıktan ve sağır modül kurtarması eklendikten sonra PA geri `HIGH`
yapıldı: brown-out olsa bile telsiz kilitlenmek yerine kendine geliyor.

Gerekçe: `PA_LOW`'da menzil kapalı alanda birkaç duvarı geçemiyordu.
`LOW → HIGH` ~+6 dB, kabaca menzilin iki katı.

Brown-out geri gelirse gizlenmiyor: uçakta `kurt=N`, kumandada `kurtarma=N`
sayacları artar. **Artıyorsa** besleme yetersiz — modül bacağına en yakın
noktada 10–100 µF **+ 100 nF seramik** (elektrolitik tek başına RF transient
hızında yavaştır), servolara ayrı UBEC, GND'ler ortak.

**Menzil beklentisi.** 250 kbps'te alıcı hassasiyeti ~−94 dBm (2 Mbps'te
~−85 dBm), yani seçilen hız tek başına ~9 dB / ~2.8 kat menzil kazandırıyor.
Gerçekçi rakamlar, görüş hattında:

| Kurulum | Menzil |
|---|---|
| PCB iz antenli küçük modül, `PA_HIGH` | ~50–70 m |
| Aynısı `PA_MAX` | ~80–100 m |
| PA+LNA'lı modül + harici anten | 200–500 m |

Kapalı alanda her iç duvar ~3–10 dB yiyor; iki-üç duvar 20 dB'yi geçiyor ve
20 dB menzilin **onda birine** düşmesi demek. Bu yüzden menzil ölçümü
**açık alanda, görüş hattında** yapılmalı. RC uçak için rahat 300 m+
isteniyorsa iz antenli modül yetmez.

**Sağır modül kurtarması:** `radioOk` true kalıp radyo susunca eski kodda
hiçbir kurtarma yolu devreye girmiyordu — mevcut yeniden deneme yalnızca
`!radioOk` iken çalışıyor, `isChipConnected()` ise sağır modülde de geçiyor.
İki taraf da artık şu koşulda cipi baştan kuruyor (`NRF_SAGIR_MS = 2000`):

- **Uçak:** diğer taşıma yolundan paket AKIYOR (yani kumanda kesin yayında)
  ama nRF24 2 sn boyunca hiçbir şey duymadı.
- **Kumanda:** 50 Hz yayın yapılıyor ama 2 sn boyunca hiç ACK gelmedi.

Sayaclar: uçakta `kurt=N`, kumandada `kurtarma=N`.

**İki kademeli watchdog'lar — neden disarm etmiyorlar:**
Hem arayüz hem telsiz watchdog'u eskiden doğrudan `disarm()` çağırıyordu ve
bu, uçuşta **kurtarılamaz** bir duruma sokuyordu: gaz kolu yukarıdayken disarm
olunca ARM kapısı "gaz çıkışı minimumda olmalı" dediği için bir daha arm
edilemiyor, motor kesik kalıyordu. Ölçümde birebir görüldü — aynı red
saniyede 46 kez, pilot gazı indirene kadar. Önlem değil, kaza sebebi.

Her ikisi de artık iki kademeli:

| | 1. kademe | 2. kademe |
|---|---|---|
| Arayüz (`WEB_TIMEOUT_MS`/`WEB_OLU_MS`) | 2 sn: gaz sıfır, yüzeyler nötr, **ARM korunur** | 5 sn: disarm |
| Telsiz (`RF_TEMAS_OLU_MS`) | temas kaybolur kaybolmaz: gaz sıfır, **ARM korunur** | 4 sn: disarm |

Temas = taze telemetri **ya da** donanım ACK'i. Arayüz eşiği 1 sn ile
başladı ve çok sıkı olduğu ölçüldü (telsiz kusursuzken tarayıcı kaynaklı
kısa takılmalar gazı kesiyordu); 2 sn bunları yutuyor.

Bir ara "gaz yeniden kapılama" da vardı — kesintiden sonra arayüzden bir kez
sıfıra yakın gaz gelene kadar gaz uygulanmıyordu. **Kaldırıldı:** koruduğu bir
şey yoktu (`ctrl.thrIn` yalnızca gelen komuttan set edilir ve kesintide
sıfırlanır, dönüşte gelen değer pilotun o anki kol konumudur) ama zararı
vardı: pilot gazı yukarıda tuttuğu sürece kapı açılmıyor, motor kapalı
kalıyordu.

**ARM kapısı bilerek daha sıkı kaldı:** havalanmadan önce taze telemetri şart,
körlemesine arm yok. Gevşeyen şey yalnızca **havadayken gazı kesme kararı**.

**Tek istemci kuralı.** `mini_ws` tek WebSocket istemcisi destekliyor. Eskiden
yeni gelen eskisini atıyordu; ikinci bir sekme/PWA açık kaldığında iki taraf
birbirini atıp duruyordu (ölçüm: saniyede ~148 kopuş), kumandanın loop'u
boğuluyor ve telsiz servisi aç kalıyordu. Artık **ilk gelen kazanır**: aktif
bir arayüz varken ikincisi reddedilir. Kaçış kapısı: mevcut istemci 3 sn hiç
veri göndermediyse ölü sayılıp yerini yenisine bırakır — sayfa yenilemesi ve
WiFi kopması bu yoldan geçiyor.

**ARD ve ACK payload — "servolar oynuyor ama telemetri yok" tuzağı:**
Tekrarlar arası bekleme (ARD) ACK **payload'ın havada geçirdiği süreden** uzun
olmak zorundadır. 250 kbps'te 8 byte'lık telemetri ACK'i ~550 µs, 13 byte'lık
kalibrasyon raporu ~710 µs sürer; üstüne alıcının RX→TX dönüşü (~130 µs) biner.
Eski ayar `setRetries(3, 5)` idi: ARD = 1000 µs, yani Nordic'in 250 kbps + ACK
payload için verdiği 1500 µs alt sınırın altında.

Sonuç sinsi bir arıza zinciri:

1. Paket uçağa **ilk denemede** ulaşır — servolar kolları takip eder.
2. Kumanda ACK'i beklemeyi erken bitirip paketi **tekrar** gönderir.
3. Uçağın telsizi tekrarı da ACK'ler ve her ACK, ACK payload FIFO'sundan
   (3 derinlik) bir yük daha **tüketir**.
4. Tüketim saniyede ~250'ye çıkar, üretim 60'ta kalır → FIFO sürekli boş →
   ACK'ler payload'sız döner.
5. Kumanda telemetri göremez, **ARM'ı reddeder** — oysa ileri yol kusursuz.

Aynı dengeyi uçak tarafı da bozabiliyordu: ACK payload'ı `paketIsle()` içinde,
**tekrar kontrolünden sonra** yükleniyordu. Aynı `seq` iki taşıma yolundan
geldiğinde ikinci kopya "tekrar" sayılıp atlıyor, ama nRF24 o kopyayı da ACK'leyip
bir yük tüketiyordu. Artık yük **tüketimin olduğu yerde** — `radioReceive()`
içinde, alınan her nRF24 paketi başına bir kez ve yalnızca FIFO boşsa —
yükleniyor: üretim ile tüketim birebir eşit.

Teşhis: kumandanın `[TX]` satırında `ack=N/s` alanı var. `ack` > 0 ama `rx=--`
ise ileri yol sağlam, **geri yol** ölüdür; kumanda bunu ayrıca bağıra bağıra
yazıyor. Uçağın `[RX]` satırındaki `tlm nrf=` / `red` sayacları hangi tarafın
yükleyemediğini söyler.

**Klon çip tuzağı:** ucuz nRF24 klonları (Si24R1 vb.) 250 kbps desteklemez.
`setDataRate()` sessizce başarısız olur, bir taraf 1 Mbps'te kalır, link tamamen ölür —
üstelik iki modül de "BAĞLI" görünür. Boot logunda `[RF] !! VERI HIZI AYARLANAMADI`
görürsen `rc_protocol.h`'da `RC_RF_DATARATE = 0` yap ve **iki projeyi de** yeniden yükle.

**Gönderim bloklamaz.** Kumanda `radio.write()` yerine `startWrite()` + `radio.update()`
ile STATUS yoklaması yapar. `write()` ACK gelene ya da tüm tekrarlar tükenene kadar bekler;
link koptuğunda bu 20 ms'lik bütçenin ~12 ms'sini yer — yani tam da kontrole en çok ihtiyaç
duyulan anda kumanda ağırlaşır. 15 ms'yi (`TX_TIMEOUT_US`) geçen gönderim iptal edilir;
logda `takilan=N` olarak görünür.

---

## 4. Paket formatı

Ortak tanımlar `include/rc_protocol.h` dosyasındadır ve **iki projede de aynı olmalıdır**.
Struct'lar `#pragma pack(1)` ile paketlenmiştir; ESP32'ler little-endian olduğu için
doğrudan `memcpy` uyumludur.

**Paket tipleri `magic` + boyut ikilisiyle ayırt edilir.** Sürüm 1'de hepsi `0xA5`'ti ve
sadece boyut bakılıyordu; kalibrasyon paketleri eklenince bu yetersiz kaldı.

| Tip | Yön | magic | Boyut |
|---|---|---|---|
| `RcPacket` | kumanda → uçak | `0xA5` | 10 |
| `RcTelemetry` | uçak → kumanda | `0x5A` | 8 |
| `RcConfigPacket` | kumanda → uçak | `0xC3` | 12 |
| `RcConfigReport` | uçak → kumanda | `0x3C` | 13 |

Boyutlar `static_assert` ile kilitli — bir alan eklersen derleme durur.

### 4.1 Kumanda → Uçak: `RcPacket` (10 byte)

| Offset | Alan | Tip | Açıklama |
|---|---|---|---|
| 0 | `magic` | u8 | `0xA5` |
| 1 | `seq` | u8 | Her pakette +1, 255→0 sarar (kayıp tespiti) |
| 2–3 | `ch[0]` **THROTTLE** | u16 | 1000 = stop … 2000 = tam gaz |
| 4–5 | `ch[1]` **ELEVATOR** | u16 | 1000 burun aşağı / 1500 nötr / 2000 yukarı |
| 6–7 | `ch[2]` **RUDDER** | u16 | 1000 sol / 1500 nötr / 2000 sağ |
| 8 | `flags` | u8 | bit0 = ARMED, bit1 = CALIB, üst nibble = sürüm (2) |
| 9 | `crc` | u8 | İlk 9 byte'ın CRC-8'i (poly 0x31, init 0xFF) |

Tüm kanallar mikrosaniye cinsindendir. **Aileron kanalı yoktur** — yatış rudder ile
veriliyor (polihedral kanat, klasik 3 kanallı eğitim uçağı düzeni).

### 4.2 Uçak → Kumanda: `RcTelemetry` (8 byte)

| Offset | Alan | Tip | Açıklama |
|---|---|---|---|
| 0 | `magic` | u8 | `0x5A` |
| 1 | `seqEcho` | u8 | Alınan son `RcPacket.seq` |
| 2–3 | `vbatMv` | u16 | Batarya mV. Ölçüm yoksa **0** |
| 4 | `lossPct` | u8 | Alıcının gördüğü paket kaybı % |
| 5 | `status` | u8 | bit0 armed, bit1 failsafe, bit2 kalibrasyon, bit3 kaydedilmemiş ayar |
| 6 | `rxHz` | u8 | Uçağın saniyede aldığı **benzersiz** paket sayısı |
| 7 | `crc` | u8 | |

`rxHz` sürüm 2'de eklendi: link kalitesi yüzdesi "kaç paket ulaştı"yı anlatmıyordu,
50 Hz'de 50 bekliyoruz ve sapma doğrudan görünüyor.

Telemetri hem ACK payload ile hem 100 ms'de bir bağımsız olarak gönderilir. Bağımsız
tazeleme şart: kumanda 1 sn içinde taze telemetri görmezse ARM'ı reddediyor, ve telemetri
yalnızca paket geldiğinde gönderilseydi tek bir boşluk bile "uçaktan cevap yok" demeye
yeterdi.

### 4.3 Kalibrasyon: `RcConfigPacket` (12) / `RcConfigReport` (13)

`RcConfigPacket`: `magic, seq, op, surf, minUs, midUs, maxUs, flags, crc`
`RcConfigReport`: `magic, ackSeq, surf, flags, minUs, midUs, maxUs, liveUs, crc`

`surf` (RcSurface): `0 = ELEVATOR`, `1 = RUDDER`, `2 = THROTTLE`.

**Gaz çıkışında alanlar farklı anlam taşır:**

| Alan | Yüzey | Gaz (`surf = 2`) |
|---|---|---|
| `minUs` | alt uç | ESC **stop** darbesi |
| `midUs` | **nötr** | **gaz tavanı** (uygulanacak en yüksek darbe) |
| `maxUs` | üst uç | ESC **tam gaz** darbesi |

`op` (RcCfgOp): `1 ENTER`, `2 EXIT`, `3 SET`, `4 TEST`, `5 SWEEP`, `6 CENTER`, `7 SAVE`,
`8 LOAD`, `9 DEFAULT`, `10 READ`, `11 ESC_HI`, `12 ESC_LO`.

`RC_CFG_TEST`'te `midUs` alanı ham darbe değeri taşır (uç nokta kontrolü için).

**Güvenilirlik:** kumanda komutu, uçaktan `ackSeq` eşleşen rapor gelene kadar 60 ms'de bir
tekrarlar; 1.5 sn'de onay gelmezse arayüzde hata gösterir. Uçak tarafında aynı `seq`
3 saniye içinde tekrar gelirse kopya sayılır ve **uygulanmaz** (yalnız rapor yeniden
gönderilir) — yoksa `SAVE` gibi komutlar birden çok kez çalışırdı. Süre sınırı, kumanda
resetlendiğinde `seq` sayacının 1'den başlaması durumunu da çözer.

`RcConfigReport.liveUs` o çıkışa **o an yazılan gerçek darbe**yi taşır. Arayüzdeki
değerlerin kaynağı budur: ekranda gördüğün şey uçağın uyguladığı ayardır, kumandanın
gönderdiği değil.

---

## 5. Servo kalibrasyonu

Servo yönü, nötrü ve uç noktaları artık derleme sabiti değil: kalibrasyon modundan
ayarlanıp uçağın **NVS'ine** yazılıyor. Kart resetlense de kalıyor, uçtaki kartta USB
olmadan da değiştirilebiliyor.

### Eşleme matematiği

Endüstri standardı **end point / travel adjust** davranışı — nötrün iki yanında
**bağımsız** ölçekleme:

```
giriş > 1500  →  midUs .. maxUs   arasına ölçeklenir
giriş < 1500  →  minUs .. midUs   arasına ölçeklenir
```

Bir yöne %100, diğerine %70 hareket verebilirsin; mekanik linkajlar hiçbir zaman simetrik
değildir. Yön terslemesi ölçeklemeden **önce**, nötr etrafında aynalanarak yapılır — ters
çevirmek uç noktaları birbirine karıştırmaz.

Gaz: `1000..2000 → stop..tam gaz`, sonra **tavan** ile kırpılır.

### Güvenlik kuralları

| Kural | Nerede zorlanıyor |
|---|---|
| Kalibrasyona yalnızca DISARM iken girilir | Uçak, `RC_CFG_ENTER` |
| Kalibrasyon modunda ARM **imkânsız** | Uçak (`armed = ... && !kalibModu`) ve kumanda |
| Kalibrasyon modunda ESC her zaman stop | Uçak, `escYaz()` |
| `TEST`/`SWEEP` gaz çıkışına uygulanmaz | Uçak, `konfigIsle()` |
| Her çıkış `900..2100 µs` ile kırpılır | Uçak, `cikisYaz()` → `rcClampHard()` |
| Geçersiz ayar reddedilir (sıralama, ≥50 µs hareket) | Uçak, `ayarGecerli()` |
| `TEST` 4 sn sonra kendiliğinden nötre döner | Uçak, `kalibTick()` |
| Link koparsa kalibrasyon modundan çıkılır | Uçak, `failsafeUygula()` |
| Bozuk NVS kaydı → fabrika ayarı | Uçak, `ayarYukle()` (imza + geçerlilik) |

`TEST`'in zaman aşımlı olması önemli: servo bir uç noktada dayanağa dayanırsa akım çeker
ve ısınır. Her yeni `TEST` sayacı sıfırlar, yani slider'ı çevirirken servo istediğin
noktada durur.

### ESC gaz aralığı kalibrasyonu

ESC, kalibrasyon sırasında gördüğü **en geniş** darbeyi "tam gaz", **en dar** darbeyi
"stop" olarak öğrenir. Kalibre edilmemiş bir ESC'de 1200 µs hâlâ stop bölgesinde kalabilir
ve motor hiç dönmez — *"gaz gidiyor ama motor dönmüyor"* şikâyetinin en sık sebebi budur.

İki yol var:

1. **Seri port (USB bağlıyken):** boot'ta 5 saniye içinde `k` bas, adımları takip et.
2. **Arayüzden:** Ayarlar → ESC gaz aralığı kalibrasyonu. "Pervaneyi söktüm" kutusunu
   işaretlemeden butonlar açılmaz; uçak tarafı da `RC_CFGF_PROP_OFF` bitini arar ve
   kalibrasyon modunda + DISARM değilse reddeder. 1. adımda unutulursa 30 sn sonra
   kendiliğinden stop'a döner.

**Her iki yolda da pervane sökülü olmalı.** Arayüz yolunda uçak kartı USB'den beslenmeli,
LiPo yalnızca ESC'ye gitmeli.

---

## 6. Failsafe ve arm zinciri

| Nerede | Koşul | Sonuç |
|---|---|---|
| Uçak | 500 ms (`RC_FAILSAFE_MS`) paket gelmedi | ESC → stop, yüzeyler → **kalibre edilmiş** nötr, armed = false, arm kilidi takılır |
| Uçak | CRC / magic / sürüm hatalı | Paket sessizce atılır, `ardisikGecerli` sıfırlanır |
| Uçak | Link geri geldi | **10 ardışık** geçerli paket (`RELINK_PAKET`) beklenir |
| Uçak | Boot ve her failsafe sonrası | **Arm kilidi** takılı: kumandadan `ARMED=0` görülmeden arm olmaz |
| Kumanda | Tarayıcı 1 sn komut göndermedi | Otomatik DISARM |
| Kumanda | Uçaktan 1 sn telemetri yok | Otomatik DISARM |
| Kumanda | Gaz kolu minimumda değilken ARM | Reddedilir |
| Kumanda | Uçak kalibrasyon modunda iken ARM | Reddedilir |
| Kumanda | ACİL butonu / boşluk tuşu | Anında DISARM + nötr |
| Tarayıcı | Sayfa arka plana alındı | Gaz 0, yüzeyler nötr, DISARM |

**Failsafe nötrü 1500 değil, `ayar[].midUs`.** Linkaj kaymışsa failsafe konumu da kaymış
olurdu; kalibre edilmiş nötre gitmek doğru olan.

**Arm kilidi neden var:** link bir an kesilip geri geldiğinde uçak kendiliğinden arm
olmamalı. Kilit ancak kumandadan `ARMED=0` bir paket görüldüğünde açılır — yani operatör
DISARM → ARM yapmadan motor dönmez.

> **KUMANDA DURUMU ≠ UÇAK DURUMU.** İkisi arayüzde ayrı gösterilir. Kumanda "ARMED"
> derken uçak "DISARM" diyebilir — o zaman uçakta arm kilidi açılmamıştır; DISARM'a basıp
> tekrar ARM etmek gerekir.

---

## 7. Kumanda arayüzü

WiFi erişim noktası — internete gerek yok, telefondan/laptoptan bağlan:

- **SSID:** `RC-Plane-TX`  **Şifre:** `rcplane1234`  **Adres:** `http://192.168.4.1`

### Taşıma: WebSocket (birincil) + HTTP (yedek)

Kol verisi **WebSocket** (port 81) üzerinden 20 Hz gider. `WebServer` her cevaba
`Connection: close` koyuyor ve tek istemciyi seri işliyor; 20 Hz'de bu her komut için yeni
bir TCP el sıkışması demek ve gecikme dalgalanıyor. WebSocket'te bağlantı bir kez kuruluyor.

WebSocket sunucusu `include/mini_ws.h` içinde, **kütüphanesiz** (SHA-1 ve base64 dahil).
Uçuşa hazır bir firmware'in derlemesi internete bağlı olmamalı. Tarayıcı WebSocket
kuramazsa sayfa otomatik olarak `/c` HTTP yoluna düşer.

| Endpoint | Açıklama |
|---|---|
| `GET /` | Arayüz sayfası |
| WS `:81` `C<gaz>,<ele>,<rud>,<arm>` | Kol konumları (gaz 0..1000, yüzeyler −1000..1000) |
| WS `:81` `X` | Acil durdurma |
| WS `:81` `S<anahtar>=<değer>` | Pilot ayarı |
| WS `:81` `K<op>,<surf>,<mn>,<md>,<mx>,<f>` | Kalibrasyon komutu |
| WS `:81` `G` | Ayarları ve raporları iste |
| `GET /c?t=&e=&r=&arm=` | HTTP yedek yolu, durum JSON'u döner |
| `GET /status` | Durum + pilot ayarları + kalibrasyon raporları |
| `GET /stop`, `/set?k=&v=`, `/cfg?op=&s=&...` | HTTP yedek yolu |

`C` mesajı aynı zamanda **watchdog beslemesidir** — sayfa kapanır ya da WiFi koparsa
kumanda 1 sn içinde DISARM olur, uçak da 500 ms sonra failsafe'e girer.

### Kol düzeni

İki gimbal (kol yuvası), tıpkı fiziksel bir vericide olduğu gibi. Üç düzen var; uçak
3 kanallı olduğu için aileron ekseni her düzende boş kalıyor, değişen şey **boş eksenin
yeri**:

| | Sol kol | Sağ kol |
|---|---|---|
| **Mod 1** | rudder (yatay) + elevator (dikey) | gaz (dikey) |
| **Mod 2** *(varsayılan)* | rudder (yatay) + gaz (dikey) | elevator (dikey) |
| **3 Kanal** | yalnızca gaz (dikey) | rudder (yatay) + elevator (dikey) |

Mod 2 simülatör/dünya standardı. **3 Kanal** düzeni, gerçek vericide rudder'ı aileron
kanalına alıp iki yüzeyi de sağ kola toplama alışkanlığının karşılığı — sol kol saf gaz
kolu olur. Boş eksen ekranda soluk çizgiyle ve `KILITLI` etiketiyle gösteriliyor.

Geçersiz bir mod değeri her zaman 2'ye düşer: bilinmeyen bir mod, kolların hangi kanalı
sürdüğünün belirsiz olması demek olurdu.

**Yerleşim:** kollar **ekranın alt köşelerinde**, kenar uzunluğu
`clamp(140px, min(34vw,46vh), 300px)` — yani hiçbir ekranda 140 dp'nin altına inmiyor.
Arayüz yatay ekran için tasarlandı; dikeyde orta sütun kolların üstüne alınır ve bir uyarı
şeridi çıkar, ama **kontrol kesilmez** — uçuş sırasında telefon dönünce kolların
kaybolması kabul edilemez.

**Yaylı vs yaysız:** elevator ve rudder bırakılınca nötre döner (gerçek kumandadaki yay);
gaz kolu bulunduğu yerde kalır (gerçek kumandadaki tırtıl).

Bırakıldığında yaylı eksenler nötre gider, gaz yerinde kalır.

### Bağlı (anchored) kol modeli

**Dokunmak komut değil.** Parmağın değdiği an, kolun *o anki değeri* o noktaya sabitlenir;
kanalı değiştiren şey dokunuştan **sonraki** harekettir. Parmağın yuvanın neresine indiği
önemsiz.

Önce mutlak eşleme vardı — kolun konumu doğrudan kanal değeriydi. İki kusuru vardı ve
ikincisi uçuş güvenliğini ilgilendiriyor:

1. Kolu tutmak için tam ortasına basmak gerekiyordu; isabet edilemeyince kol parmağın
   düştüğü yere **zıplıyordu**.
2. Uçuşta ele çarpan bir parmak, yüzeyi bir anda tam sapmaya götürüyordu.

Bağlı modelde çarpma yalnızca bir referans noktası belirler, hiçbir kanal kıpırdamaz.

**Origin push** — bu modelin bilinen tuzağı ve çözümü. Referans sabit kalsaydı, yuvanın
tepesinden tutup daha yukarı çekmek mümkün olmaz, tam sapmaya tek hamlede ulaşılamazdı.
Sınır aşıldığında *taban değeri* kaydırılarak parmak hep anlamlı kalır: nereden tutarsan tut
tam sapmaya ulaşırsın ve geri çekince değer **anında** düşer, doyuma yapışıp kalmaz. Tam
sapmadayken yuva kenarlığı sarıya döner.

**Ölçek:** 100 px parmak hareketi = kanal genişliğinin yarısı. Yüzeylerde ortadan kenara
tam sapma, gazda dipten tepeye %0→%100 — ikisi de uçtan uca aynı yolu kat eder, tıpkı
fiziksel bir gimbalde olduğu gibi. Kaldırıp yeniden tutmak birikimlidir.

**Çoklu dokunma** destekli: her gimbal kendi `pointerId`'sini yakalar
(`setPointerCapture`), böylece "bırakma" ile "dışarı taşma" birbirine karışmaz ve iki kol
aynı anda kullanılabilir. Aynı yuvadaki ikinci parmak yok sayılır.

**Gecikme.** Kol verisi zamanlayıcıyla değil, **dokunma olayının kendisinde** gönderilir
(en fazla 10 ms'de bir; 120 Hz dokunmatik örneklemede WebSocket'i boğmamak için).
Arayüz tarafında tamponlama yok — toplam gecikme pratikte telsizin 20 ms'lik çerçeve
periyodundan ibaret. Ayrıca 50 ms'de bir keepalive gider; watchdog beslemesi odur.

**Titreşim:** ARM, DISARM, failsafe'e giriş ve basılı-tut onayının tamamlanmasında
`navigator.vibrate()`. Failsafe titreşimi yalnızca durum *değiştiğinde* tetiklenir.

### Kazara dokunmaya karşı

Uçuş sırasında el bir yere çarpar. Alınan kararlar:

| Risk | Karar |
|---|---|
| Ele çarpan parmak yüzeyi savurur | Bağlı kol modeli — dokunmak kanalı değiştirmez |
| Avuç + başparmak aynı yuvada çakışır | Aynı yuvadaki ikinci `pointerId` yok sayılır |
| Yuva dışına dokunma | Kolların dışında hiçbir dokunuş kanal sürmez |
| Ele çarpan ARM/ACİL tuşu | **Basılı tutma** ister (ARM 600 ms, ACİL 400 ms); parmak 44 px kayarsa onay iptal |
| AYAR ekranı uçuşta açılır, kolları kapatır | ARMED iken AYAR tuşu kilitli |
| Geri hareketi / uzun basma menüsü | Yutulur (`popstate` + `contextmenu`) |
| Kazara yenileme | ARMED iken tarayıcı onay sorar (`beforeunload`) |
| Sistem kenar hareketi kola değer | Kollar kenardan 16 px içeride |
| URL çubuğu / kenar hareketleri | İlk dokunuşta tam ekran + yatay kilit denenir |

Acil durdurmanın basılı tutma istemesi bilinçli: ele çarpan bir parmağın motoru kesip
zorunlu inişe sokması, gerçek bir acil durumda 400 ms beklemekten çok daha pahalı.

**Ekran kilidi ve bildirimler uçuşu kesmez.** Wake Lock ile ekran açık tutulur; bildirim
gölgesi açıldığında (`blur`) hiçbir kanal değişmez. Uygulama gerçekten arka plana atılırsa
tarayıcı zamanlayıcıları kıstığı için kumandanın 1 sn'lik watchdog'u zaten devreye girer —
korumayı oraya bırakmak, geçici bir bildirimi ölümcül yapmaktan iyi.

**Uçuş ekranında animasyon, geçiş efekti, gradyan ve gölge yok.** Her piksel ya bir değer
gösteriyor ya bir kontrol. Renkler güneş altında okunacak şekilde seçildi: en sönük etiket
bile zemine karşı ~11:1 kontrastta, değerler ~18:1.

**Gamepad:** tarayıcının Gamepad API'si açıksa gerçek bir joystick ya da USB moduna
alınmış bir RC kumandası doğrudan kullanılabilir (%8 ölü bölge).

**Klavye (tezgâh testi):** `W`/`S` gaz, oklar elevator, `A`/`D` rudder, `Boşluk` acil
durdurma, `X` gaz kes.

### Pilot ayarları (kumanda NVS'inde)

| Ayar | Aralık | Varsayılan |
|---|---|---|
| Kol modu | 1 / 2 / 3 (3 kanal) | 2 |
| Trim (gaz / ele / rud) | ±200 µs = kanalın **±%20**'si | 0 |
| Expo (ele / rud) | 0–50 % | 25 |
| Oran / dual rate | 30–100 % | 100 |
| Gaz sınırı | 20–100 % | 100 |

Trim uçuş ekranındaki `+`/`−` tuşlarından 5 µs adımlarla ayarlanır.

**Gaz trim'i ve ARM kapısı.** Pozitif gaz trim'i, kol tam aşağıdayken bile ESC'ye stop'tan
büyük bir darbe gönderir — yani arm edildiği anda motor döner. Özelliği kırpmak yerine arm
kapısını trim'e duyarlı hale getirdik: ARM kararı kolun konumuna değil **gerçekten
gönderilecek darbeye** bakıyor. Pozitif trim varken arm etmek mümkün değil, özellik ise
olduğu gibi duruyor.

**Gaz kanalında ters çevirme bilerek yok.** Ters gaz, kol aşağıdayken tam gaz demek olurdu;
yüzeylerde anlamlı olan bu ayarın gaz kanalında karşılığı bir arıza modu.

Expo formülü endüstri standardı: `y = x · ((1−e) + e·x²)`. Nötrün etrafını yumuşatır,
uç noktalarda tam yetkiyi korur. İlk uçuşlarda %25 civarı kolun aşırı hassas olmasını
engelliyor.

**Gaz iki kademe sınırlı:** kumandadaki `thrLimit` ve uçaktaki `midUs` tavanı birbirinden
bağımsız; ikisinden düşük olan geçerlidir.

### Kalkış öncesi kontrol

Ayarlar ekranındaki liste, ARM'ı engelleyen şeyi tek bakışta gösterir: telsiz yolu,
telemetri, link ≥ %50, failsafe yok, gaz minimumda, kalibrasyon modu kapalı, kalibrasyon
kaydedilmiş.

---

## 8. İlk çalıştırma sırası

1. **Pervaneyi sök.**
2. `flight_software`'i yükle (`pio run -t upload -t monitor`). Boot logunda LEDC
   kanallarının kurulduğunu ve reset sebebinin `POWERON` olduğunu doğrula.
3. Kumandayı yükle, telefonla `RC-Plane-TX`'e bağlan, `192.168.4.1`'i aç.
4. `LINK` %90+, `UCAK RX` ~50 Hz olmalı. Düşükse: anten yönü (iki modülün
   anteni birbirine paralel; uç uça bakan PCB anten en kötü yön), nRF24'ü
   ESP32'nin anteninden uzaklaştır, modül bacağında 10–100 µF + 100 nF.
   Üst şeritteki link etiketi bağlantının **hangi yolla** sağlandığını yazıyor
   (`nRF24`, `NOW`, `nRF24+NOW`).
5. **Servo kalibrasyonu** (motor kapalı):
   Ayarlar → KALIBRASYONA GIR → her yüzey için yön, nötr, uç noktalar → `TARA` ile
   dayanağa vurmadığını doğrula → **UÇAĞA KAYDET** → ÇIK.
6. Kolları oynat, yüzeylerin doğru yöne gittiğini kontrol et
   (elevator kolu yukarı → burun yukarı; rudder sağa → uçak sağa).
7. Gerekiyorsa ESC gaz aralığı kalibrasyonu (bkz. §5).
8. ARM et, gazı çok az aç, motor yönünü kontrol et.
9. İlk uçuş için: gaz sınırı %70–80, expo %25–30, oran %70. Güvendikçe aç.

---

## 9. Değiştirmek isteyebileceğin sabitler

| Sabit | Yer | Varsayılan |
|---|---|---|
| `TEZGAH_MODU` | controller `src/main.cpp` | **false** (uçuş) |
| `AP_SSID` / `AP_PASS` | controller `src/main.cpp` | RC-Plane-TX / rcplane1234 |
| `AP_CHANNEL` | controller `src/main.cpp` | 1 |
| `RF_SPI_HZ` | her iki `src/main.cpp` | 4000000 |
| `TLM_TIMEOUT_MS` | controller `src/main.cpp` | 1000 |
| `PIN_ESC` / `PIN_RUDDER` / `PIN_ELEVATOR` | flight `src/main.cpp` | 0 / 3 / 4 |
| `RELINK_PAKET` | flight `src/main.cpp` | 10 |
| `KALIB_TEST_MS` | flight `src/main.cpp` | 4000 |
| `RC_RF_CHANNEL` | `rc_protocol.h` | 108 |
| `RC_RF_ADDRESS` | `rc_protocol.h` | "RCP01" |
| `RC_RF_DATARATE` | `rc_protocol.h` | 2 (250 kbps) |
| `RC_TX_PERIOD_MS` | `rc_protocol.h` | 20 (50 Hz) |
| `RC_FAILSAFE_MS` | `rc_protocol.h` | 500 |

Gaz tavanı ve servo uç noktaları **artık sabit değil** — arayüzden ayarlanıyor.

---

## 10. Seri log okuma

### Uçak (`[RX]` satırı)

```
[RX] nrf=BAGLI now=BAGLI | DISARM rxarm=0 kilit=ACIK relink=0/10 |
     thr=1000<-1000(disarm) ele=1500->1500 rud=1500->1500 |
     50 paket, tekrar 50, kayip %0, bozuk 0 | tlm nrf=50/red0 now=50/hata0
```

| Alan | Anlamı |
|---|---|
| `rxarm` | Kumandadan gelen ARMED bitinin ham hali |
| `kilit` | Arm kilidi. `KAPALI` iken ARMED gelse bile arm olmaz |
| `thr=A<-B(sebep)` | A uygulanan, B kumandanın istediği, sebep neden kırpıldı |
| `ele=A->B` | A kumandadan gelen, B kalibrasyondan sonra çıkışa yazılan |
| `tekrar` | İkinci taşıma yolundan gelen kopya sayısı (normal, kayıp değil) |

*"Gaz gidiyor ama motor dönmüyor"* derdinde `thr=1000<-1600(disarm)` yazıyorsa sorun
kumandada değil, arm kapısında.

### Kumanda (`[TX]` satırı)

```
[TX] nrf=BAGLI now=BAGLI ws=VAR | web=20/s | kumanda=DISARM ucak=DISARM
     thr=1000 ele=1500 rud=1500 | link=98% | rx=OK 50Hz kayip=%0 vbat=0mV
```

`web=0/s` görüyorsan sorun telsizde değil, tarayıcıda — sayfa istek göndermiyor.
`ws=yok` ise WebSocket kurulamamış, HTTP yedek yolu çalışıyor demektir.
