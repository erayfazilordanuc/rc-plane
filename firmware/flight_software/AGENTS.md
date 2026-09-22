# AGENTS.md — flight_software (uçak)

Depo kökündeki `AGENTS.md` dosyasındaki model yönlendirme, değişmezler ve doğrulama
kuralları burada da geçerlidir. Bu dosya yalnızca bu projeye özel olanı anlatır.

**Depodaki en riskli kod burada.** Motoru bu kart döndürüyor ve yerdeki
operatörün ulaşamadığı tek yer burası. Şüphedeysen Opus 5 kullan.

---

## Ne yapıyor

ESP32 DevKitC 38 pin (ESP-WROOM-32 / 32D). İki telsizi aynı anda dinliyor, her çerçeveyi doğruluyor,
saklanan kalibrasyon eğrisinden geçirip ESC ve servolara yazıyor — ve yer
istasyonuna güvenmeyen kendi failsafe mantığını çalıştırıyor.

## Pin haritası

Şema: `docs/kablolama.html`.

| İşlev | GPIO | Not |
|---|---|---|
| ESC | 25 | Sinyal–GND arası 10k; boot sırasında ESC'ye çöp darbe gitmiyor |
| Elevator | 26 | |
| Rudder | 27 | |
| Aileron sol / sağ | 32 / 33 | Firmware'de hazır; bu uçakta bağlı değil, nötrde bekler |
| nRF24 SCK / MISO / MOSI | 18 / 19 / 23 | Kumandayla aynı (VSPI) |
| nRF24 CSN / CE | 5 / 4 | Kumandayla aynı |
| Besleme | 5V pini | Ayrı UBEC yok: ESC BEC → kart 5V → servolar, hatta kondansatör |

Kaçınılan pinler: **0, 2, 12, 15** (strapping), **14** (boot'ta PWM), **6–11**
(flash — kartta `D0–D3`, `CMD`, `CLK`; `D2` GPIO2 değil), **1, 3** (UART0).
**Pin değiştirmek donanım değişikliğidir** — kullanıcı açıkça istemeden dokunma.

## Bu projeye özel tuzaklar

- **`LEDC_BIT` 14'te kalır.** Klasik ESP32 daha fazlasına izin veriyor ama
  firmware C3'ten geldi ve C3'te 16 bit `ledcSetup()`'ı sessizce 0 döndürüp
  pinde hiç PWM bırakmıyordu. Kanallar zamanlayıcıyı çiftler halinde
  paylaşıyor (0–1, 2–3, 4–5); farklı frekanslı çıkış eklersen ayrı çifte koy.
- **GPIO0'a pulldown koyma.** C3'te ESC oradaydı; klasik ESP32'de GPIO0 boot
  modu pini, pulldown kartı her açılışta yükleme moduna sokar.
- **Seri ESC kalibrasyonu `k` + her adımda `e` ister.** Klasik ESP32'de
  `if (Serial)` her zaman true — pencere her boot'ta açılıyor. "Herhangi bir
  tuş" onayına geri dönme: UART'taki tek bir gürültü baytı tam gaz darbesi
  demek.
- **Aileron aynalaması `cikisYaz()` içinde**, çağrı yerlerinde değil. Böylece
  failsafe / TEST / TARA dahil her yol iki servoyu birden sürüyor. Aynalamayı
  yukarı taşıma — bir yol unutulur.
- **Failsafe nötrü 1500 değil, `ayar[].midUs`.** Kalibre edilmiş nötr. Sabit
  1500 yazarsan linkajı kaymış bir uçak failsafe'te yamuk durur.
- **Arm kilidi**: boot'ta ve her failsafe'ten sonra takılı; kumandadan
  `ARMED=0` görülmeden açılmaz. Link geri geldiğinde motorun kendiliğinden
  dönmesini bu engelliyor. Kaldırma.
- **Paket tipi `magic`'ten ayırt edilir, boyuttan değil.** Sürüm 3'te
  `RcPacket` ve `RcConfigPacket` ikisi de 12 byte.
- **Telemetri yayın (broadcast) ile gider.** Unicast ölçülüp elendi:
  `ack=0 / nack=57`, ardından `ESP_ERR_ESPNOW_NO_MEM`. Uçağın STA'sı kumandanın
  AP'sine ilişkilendirilmiş değil. Unicast'e geri dönme.
- **NVS ad alanı `rcplane`.** `CikisAyar` dizisi büyürse `NVS_IMZA`'yı artır.
- **Kalibrasyon modunda motor kilitli**, `TEST`/`SWEEP` gaz çıkışına hiç
  uygulanmaz, her yazım `rcClampHard()` (900–2100 µs) ile kırpılır.

## Derleme

```bash
pio run                 # ucus firmware'i (varsayilan ortam)
pio run -e bench        # telsiz YOK - cikislari seri porttan sur
pio run -t upload       # COM6'ya yukler
```

Port **COM6** olarak sabit. Bu numara eski C3'ün yerel USB'sindendi; DevKitC USB
köprüsüyle bağlanıyor ve farklı numara alabilir — kullanıcıya `pio device list`
ile kontrol ettir. Portu kaldırma: yazılmazsa PlatformIO COM5'teki kumandaya yükler.

## Donanım doğrulaması

Kod kosumları uçuş davranışını test etmez. Sırayla:

1. **Pervaneyi sök.**
2. `pio run -e bench -t upload -t monitor` — servolar seri porttan hareket
   ediyorsa yazılım ve pin haritası sağlam. `t` / `p` / `v` komutları elle
   lehimlenmiş kartta hangi telin hangi GPIO'da olduğunu söyler.
3. Ucus firmware'i, `[RX]` satırında `nrf=BAGLI` ve `rxarm` / `kilit` alanlarını
   oku.
4. `BROWNOUT` görüyorsan sorun besleme, yazılım değil — kod düzeltemez.
