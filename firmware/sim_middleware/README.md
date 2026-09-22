# sim_middleware — RC arayüzü → vJoy köprüsü

Telefondaki RC kumanda web arayüzünü, bilgisayardaki uçuş simülatörleri
(RealFlight Evo, Velocidrone, FPV.SkyDive…) için **sanal bir USB joystick'e**
çevirir. Simülatör karşısında gerçek bir kumanda görür; senin arayüzün
olduğunu bilmez.

---

## 1. Amaç — bu şey ne işe yarıyor?

Uçağı kırmadan önce üç şeyi denemek gerekiyor:

**Kumanda arayüzünün kendisini denemek.** `controller_software` içindeki web
arayüzü klasik bir kumanda değil: telefon ekranında, bağlı (anchored) kol
modeliyle çalışıyor. Bu modelin gerçekten uçurulabilir olup olmadığı ancak
uçarken anlaşılır. Uçağı havaya atıp öğrenmek pahalı bir yol.

**Kas hafızası kurmak.** Expo, oran, gaz sınırı, mod seçimi — hepsi
arayüzde ayarlanabiliyor. Hangi değerlerin sana oturduğunu simülatörde
saatlerce, bedavaya deneyebilirsin.

**Arayüz değişikliklerini regresyona sokmak.** Kol eğrisini ya da gönderim
hızını değiştirdiğinde "daha mı iyi oldu" sorusunun cevabı laboratuvarda
değil, uçarken belli oluyor. Simülatör bunu tekrarlanabilir hale getiriyor.

Bu köprü olmadan arayüzü test etmenin tek yolu gerçek uçuş. Köprüyle
birlikte, **gerçek uçuşta kullanacağın arayüzün tam kendisiyle** simülatörde
uçuyorsun — ayrı bir "test arayüzü" yok, dolayısıyla test ettiğin şeyle
uçtuğun şey birebir aynı.

---

## 2. Mantık — nasıl çalışıyor?

### Veri akışı

```
  TELEFON                              BILGISAYAR
┌───────────────────────┐            ┌──────────────────────────────────┐
│  web arayuzu          │            │  app.py                          │
│  (slider / kol)       │            │                                  │
│         │             │            │   on_rc()                        │
│         ▼ oninput     │            │      │                           │
│  simSetAxes(...)      │            │      ▼                           │
│         │             │            │   us_to_vjoy()   1000-2000 us    │
│         ▼             │            │      │           →   0-32767     │
│  volatile.emit("rc",  │            │      ▼                           │
│    [ail,ele,thr,rud]) │            │   UpdateVJD()  tek HID raporu    │
└─────────┬─────────────┘            └──────┬───────────────────────────┘
          │ WebSocket                       │
          │ localhost:5000                  ▼
          │                            ┌─────────────┐
          └──── adb reverse ──USB──────│ vJoy surucu │
                                       └──────┬──────┘
                                              │ sanal USB joystick
                                              ▼
                                       ┌─────────────┐
                                       │ RealFlight  │
                                       └─────────────┘
```

### Dört tasarım kararı ve gerekçeleri

**Neden `adb reverse`, neden Wi-Fi değil?**
Wi-Fi üzerinden gitseydik araya router, kanal çakışması ve değişken gecikme
girerdi — üstelik ölçmesi zor bir gecikme. `adb reverse tcp:5000 tcp:5000`
telefonun `localhost:5000` portunu USB kablosu üzerinden PC'nin
`localhost:5000` portuna bağlıyor. Trafik hiç kablosuz ortama çıkmıyor.
Ölçülen gidiş-dönüş: **~2 ms**. Ayrıca uçuş alanında router yok; USB her
zaman var.

**Neden vJoy?**
Simülatörler HID joystick'i işletim sistemi seviyesinde okuyor. Simülatöre
"dışarıdan komut enjekte etme" gibi bir yol yok — olsa bile her simülatör
için ayrı iş olurdu. vJoy sahte bir HID cihazı yaratıyor; RealFlight,
Velocidrone, hepsi bunu normal bir kumanda sanıyor. Tek köprü, bütün
simülatörler.

**Neden 1000-2000 µs?**
Arayüz zaten RC dünyasının birimini üretiyor: servo darbe genişliği.
Gerçek uçuşta bu değer telsizden uçağa gidiyor. Simülatöre giderken de
aynı değeri kullanıyoruz ki **test ettiğin sinyal, uçtuğun sinyalle aynı
olsun**. Dönüşüm bu köprünün son adımında, tek bir fonksiyonda yapılıyor:

```
1000 us  →      0     (eksen dibi)
1500 us  →  16384     (notr)
2000 us  →  32767     (eksen tepesi)
```

Aralık dışı değerler **eşlemeden önce** kırpılıyor. Bozuk tek bir paket
simülatörde tam sapmaya yol açmasın diye.

**Neden değişim anında gönderim, neden sabit 50 Hz değil?**
Sabit periyotlu bir `setInterval`, kolun hareketiyle paketin çıkışı arasına
en kötü halde bir tam periyot koyar. Gönderimi dokunma olayının kendisi
tetikliyor; 10 ms'lik taban yalnızca 120 Hz örnekleyen bir dokunmatikte
bağlantıyı boğmamak için. Bekleyen bir değişiklik kalırsa bir sonraki tik
onu boşaltıyor. (Aynı model `web_ui.h` içindeki `kanalGonder()`'de de var.)

### Gecikme bütçesi

| Aşama | Süre |
|---|---|
| dokunma → `oninput` | tarayıcıya bağlı, ~8-16 ms |
| gönderim tabanı (`MIN_GAP_MS`) | 0-10 ms |
| WebSocket + adb + USB | ~1 ms |
| `on_rc` → `us_to_vjoy` → `UpdateVJD` | ~30 µs |
| vJoy sürücü → simülatör | 1 HID kare |

Yani toplam gecikmenin neredeyse tamamı ekranda ve tarayıcıda; köprü
ölçülebilir bir şey eklemiyor. Bunu korumak için yapılanlar:

- Paket geldiği anda vJoy'a yazılıyor; arada kuyruk, zamanlayıcı ya da
  "60 Hz'e düşür" katmanı **yok**.
- Sıcak yolda (`on_rc`) log, print, string formatlama, sözlük kopyalama
  yok. Sayaçlar biriktirilip watchdog thread'inde basılıyor.
- İstemciye ack dönülmüyor → paket başına fazladan TCP gidiş-dönüşü yok.
- Nagle kapalı (`disable_nagle_algorithm`); açık kalsa birkaç baytlık
  paketler için 40 ms'ye varan bekleme görülebiliyor.
- İstemci `transports:["websocket"]` ile bağlanıyor: uzun yoklama
  (polling) aşaması hiç yaşanmıyor, ilk paket bile WebSocket üzerinden.
- `volatile.emit`: bağlantı koptuğunda paketler kuyruğa girmiyor. RC'de
  en son değer dışındaki her şey çöp; kuyruk geri geldiğinde saniyeler
  öncesinin kol pozisyonlarını boşaltmasın.
- Werkzeug istek logu kapalı — Windows'ta konsola yazmak pahalı.
- `async_mode="threading"` + `simple-websocket`: eventlet/gevent
  monkey-patch'i olmadan gerçek WebSocket. (eventlet Python 3.11+ ile
  sorun çıkarıyor.)

### Failsafe

**500 ms** paket gelmezse — telefon kilitlendi, tarayıcı arka plana atıldı,
USB çıktı — gaz kesilir, diğer eksenler nötre döner. Bağlantı koptuğunda
(`disconnect`) aynısı anında uygulanır. Simülatörde tam gazda kalıp
duvara girmemek için.

Bunu yapan tek yardımcı thread `_watchdog()`; sıcak yoldan uzak duruyor,
50 ms'de bir uyanıyor ve ikinci işi olarak Hz sayacını basıyor.

---

## 3. Kurulum

### Bir kerelik

**a) Python paketleri**

```
pip install flask flask-socketio simple-websocket pyvjoy
```

`simple-websocket` şart: `async_mode="threading"` ile gerçek WebSocket'i o
sağlıyor.

> Bu klasörde hazır bir `.venv` var. Global Python'a bulaşmak istemezsen
> doğrudan onu kullan: `.venv\Scripts\python.exe app.py`

**b) vJoy sürücüsü**

[vJoy](https://sourceforge.net/projects/vjoystick/) kur — 64-bit Python
kullanıyorsan 64-bit sürüm. Sonra `vJoyConf`'u aç:

- Device 1 → **Enable**
- Eksenler: **X, Y, Z, Rz** işaretli olsun
- Buton/POV sayısı önemsiz

**c) Socket.IO istemci kütüphanesi**

```
python tools/fetch_client.py
```

CDN'den bir kez indirip `static/vendor/` içine koyar. Telefonda internet
olmayabilir (uçuş alanında genelde yok, sayfa da ESP32'den geliyor
olabilir) — kütüphane PC'de dursun, telefona USB üzerinden servis edilsin.

### Her seferinde

```
python app.py
```

```
adb reverse tcp:5000 tcp:5000
```

(Telefonda USB hata ayıklama açık olmalı. `adb reverse --list` ile
kontrol edebilirsin.)

---

## 4. Test — kademeli doğrulama

Sorun çıktığında hangi katmanda olduğunu bilmek için sırayla ilerle.
Her adım bir öncekini varsayar.

### Adım 1 — vJoy olmadan, köprünün mantığı

```
python app.py --dry-run          # bir konsolda
python tools/selftest.py         # baska bir konsolda
```

Eşleme değerlerini, 100 Hz akışı, gidiş-dönüş gecikmesini ve failsafe'i
kontrol eder. Beklenen çıktı:

```
1) esleme
  1000 us                      0            OK
  1500 us                      16384        OK
  2000 us                      32767        OK
  1250 us                      8192         OK
  900 us (kirpma)              0            OK
  2400 us (kirpma)             32767        OK
  1500 us ters                 16383        OK
  1000 us ters                 32767        OK

2) akis
  bagli, transport: websocket
  300 paket / 3.00 s  ->  100 Hz gonderildi
  sunucuya ulasan              300          OK

3) gidis-donus
  ortanca 2.23 ms   en kotu 3.09 ms

4) failsafe
  akis varken                  False        OK
  800 ms sessizlik sonrasi     True         OK

HEPSI GECTI
```

Burada takılıyorsan sorun Python tarafında; vJoy'a hiç bakma.

### Adım 2 — vJoy'a gerçekten yazıyor mu?

```
python app.py
```

Konsolda `vJoy cihaz 1 alindi (UpdateVJD yolu)` görmelisin. Sonra
tarayıcıda (PC'de, telefona gerek yok):

```
http://localhost:5000
```

4 slider'lı test paneli açılır. Yanına **vJoy Monitor**'ü aç (vJoy ile
birlikte gelir) ve slider'ları oynat. Eksenler kıpırdamalı:

| Slider | vJoy Monitor'da |
|---|---|
| AIL | X |
| ELE | Y |
| THR | Z |
| RUD | Rz |

Windows'un kendi `joy.cpl` penceresi de iş görür (Çalıştır → `joy.cpl` →
vJoy Device → Özellikler).

### Adım 3 — telefon bağlantısı

Telefonu USB ile bağla, `adb reverse tcp:5000 tcp:5000` çalıştır, telefonun
tarayıcısında `http://localhost:5000` aç. Aynı test paneli telefonda
açılmalı, slider'lar PC'deki vJoy eksenlerini oynatmalı.

Bağlantı durumu sayfanın üstünde: yeşil **bağlı** / kırmızı **kopuk**.
`simPing()` butonu konsola gidiş-dönüş gecikmesini yazar.

### Adım 4 — simülatör

1. `app.py` çalışırken RealFlight'ı aç (vJoy cihazı önceden görünsün).
2. **Simulation → Select Controller → vJoy Device**
3. **Calibrate**: her ekseni uçtan uca gezdir — test panelindeki
   slider'ları kullanmak en kolayı.
4. **Channel mapping**: Aileron→X, Elevator→Y, Throttle→Z, Rudder→Rz.
5. Uç.

### Sürekli izleme

Sunucu konsolu 2 saniyede bir paket hızını basıyor:

```
15:43:17  96 Hz  (191 paket / 2 s)
```

Anlık durum: `http://localhost:5000/health`

```json
{"vjoy":"device 1","failsafe":false,"packets":12480,"age_ms":11}
```

`age_ms` son paketin üstünden geçen süre. 500'ü aşarsa `failsafe` true olur.

---

## 5. Kullanım — kendi arayüzüne bağlama

Sayfaya iki script ekle:

```html
<script src="http://localhost:5000/socket.io.min.js"></script>
<script src="http://localhost:5000/static/rc_sim_client.js"></script>
```

Adresin mutlak olması şart: sayfa ESP32'den servis ediliyorsa `localhost`
telefonun kendisi, oradan da adb ile PC'ye düşüyor.

Slider'ın `oninput`'unda tek satır:

```js
simSetAxes({ ail: 1500, ele: 1500, thr: 1000, rud: 1500 });
```

Verilen kanallar güncellenir, verilmeyenler son değerinde kalır. Yani tek
bir slider için `simSetAxes({ ail: +s.value })` yeterli.

Diğer yardımcılar ([static/rc_sim_client.js](static/rc_sim_client.js)):

| | |
|---|---|
| `simPing()` | gidiş-dönüş gecikmesini konsola yazar |
| `simOnState(ok)` | sen tanımlarsan bağlantı durumu değişince çağrılır |
| `simFromStick(stick)` | aşağıya bak |

### Mevcut ESP32 kumanda arayüzü için

`controller_software/include/web_ui.h` içindeki kol modeli µs değil kendi
birimini tutuyor (`thr 0..1000`, `ele/rud -1000..1000`). O arayüze
simülatör desteği eklemek için `kanalGonder()` içine tek satır yeter:

```js
simFromStick(stick);          // 0..1000 / -1000..1000  ->  1000..2000 us
```

O arayüzde **aileron kanalı yok** (3 kanal uçak). Simülatörde kanadın da
eğilmesini istersen `app.py` içinde:

```python
MIRROR_RUDDER_TO_AILERON = True
```

Rudder aynı anda aileron eksenine de yazılır.

---

## 6. Eksen eşlemesi (Mode 2)

| Kanal | Kumanda | vJoy ekseni | pyvjoy sabiti | struct alanı |
|-------|---------|-------------|---------------|--------------|
| `ail` | sağ çubuk X | X | `HID_USAGE_X` | `wAxisX` |
| `ele` | sağ çubuk Y | Y | `HID_USAGE_Y` | `wAxisY` |
| `thr` | sol çubuk Y | Z | `HID_USAGE_Z` | `wAxisZ` |
| `rud` | sol çubuk X | RZ | `HID_USAGE_RZ` | `wAxisZRot` |
| `aux1`| — | RX | `HID_USAGE_RX` | `wAxisXRot` |
| `aux2`| — | RY | `HID_USAGE_RY` | `wAxisYRot` |

Eşleme `app.py` içinde `AXES` demetinde, tek yerde tanımlı.

**İki yazma yolu var.** Varsayılan `USE_BATCH_UPDATE = True`: dört ekseni
tek bir HID raporunda (`UpdateVJD`) yazar. Hem tek DLL çağrısı, hem de
eksenler aynı karede güncellendiği için aralarında yırtılma olmaz.
`False` yaparsan eksen başına `set_axis(HID_USAGE_X, …)` yoluna düşer.

---

## 7. Ayarlar — `app.py` başındaki blok

| Ayar | Varsayılan | Ne işe yarar |
|---|---|---|
| `HOST` / `PORT` | `127.0.0.1` / `5000` | adb reverse loopback'e düşürdüğü için 127.0.0.1 yeter. Wi-Fi'dan da erişmek istersen `0.0.0.0`. |
| `VJOY_DEVICE_ID` | `1` | vJoyConf'taki cihaz numarası |
| `US_MIN/MID/MAX` | `1000/1500/2000` | arayüzün ürettiği aralık |
| `REVERSE` | hepsi `False` | kanal başına ters çevirme |
| `MIRROR_RUDDER_TO_AILERON` | `False` | 3 kanal arayüz için |
| `FAILSAFE_MS` | `500` | bu süre paket gelmezse gaz kesilir |
| `USE_BATCH_UPDATE` | `True` | tek HID raporu / eksen başına SetAxis |

`HOST` ve `PORT` ortam değişkeninden de okunuyor: `SIM_HOST`, `SIM_PORT`,
`VJOY_ID`.

> **Ters yüzey?** Önce **simülatörün kendi kalibrasyonunu** dene. Orada
> çözülmüyorsa `REVERSE` sözlüğünü kullan. Sebebi: `REVERSE`'ü açarsan
> köprü artık uçakta olmayan bir davranış ekliyor demektir; test ettiğin
> şeyle uçtuğun şey ayrışır.

---

## 8. Sorun giderme

| Belirti | Sebep / çözüm |
|---|---|
| `pyvjoy bulunamadi` | `pip install pyvjoy` |
| `vJoyInterface.dll yuklenemedi` | 32/64-bit uyuşmazlığı. vJoy klasöründeki DLL'i `pyvjoy` paketinin içine kopyala. |
| `vJoy cihazi 1 alinamadi` | vJoyConf'ta cihaz kapalı, ya da başka bir program tutuyor. Simülatörü kapatıp `app.py`'yi önce başlat. |
| Telefonda sayfa açılmıyor | `adb reverse --list` boş mu? USB hata ayıklama açık mı? Kabloyu veri kablosuyla değiştir. |
| Sayfa açılıyor ama "kopuk" yazıyor | `app.py` çalışmıyor, ya da `SIM_URL` yanlış. Telefon tarayıcı konsolunu `chrome://inspect` ile aç. |
| Konsolda Hz görünüyor ama eksen kıpırdamıyor | vJoy tarafı. Adım 2'ye dön (vJoy Monitor). |
| vJoy Monitor'da kıpırdıyor ama simülatörde yok | RealFlight'ta kanal eşlemesi/kalibrasyon yapılmamış. Adım 4. |
| Uçak simülatörde durup duruyor donuyor | Failsafe tetikleniyor. Telefonun ekranı kilitleniyor olabilir — arayüzdeki Wake Lock devrede mi? |
| `code 400, Bad request syntax` | İstemci bağlantıyı sertçe kapattığında görülür, zararsız. |

---

## 9. Dosyalar

| | |
|---|---|
| [app.py](app.py) | Sunucu, eşleme, vJoy köprüsü, failsafe |
| [static/rc_sim_client.js](static/rc_sim_client.js) | Arayüze eklenecek istemci |
| [static/index.html](static/index.html) | 4 slider'lı test paneli |
| [tools/selftest.py](tools/selftest.py) | Uçtan uca test |
| [tools/fetch_client.py](tools/fetch_client.py) | socket.io.min.js indirici |
| [requirements.txt](requirements.txt) | Bağımlılıklar |
