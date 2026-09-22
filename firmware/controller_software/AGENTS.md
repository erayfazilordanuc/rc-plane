# AGENTS.md — controller_software (yer istasyonu)

Depo kökündeki `AGENTS.md` dosyasındaki model yönlendirme, değişmezler ve doğrulama
kuralları burada da geçerlidir. Bu dosya yalnızca bu projeye özel olanı anlatır.

---

## Ne yapıyor

ESP32-WROOM-32. Kendi WiFi erişim noktasını açıyor, telefona uçuş arayüzünü
sunuyor, kol konumlarını expo/oran/trim'den geçirip **50 Hz**'de uçağa
gönderiyor, telemetri ve kalibrasyon raporlarını geri okuyor.

Bu kartta commercial bir verici yok — **verici bu kart**.

## Dosyalar

| Dosya | İçerik | Kim dokunmalı |
|---|---|---|
| `src/main.cpp` | Telsiz, güvenlik zinciri, HTTP + WebSocket uçları, NVS | Güvenlik zinciri → Opus 5; log/JSON alanı → Sonnet 5 |
| `include/rc_protocol.h` | Telsiz formatı — **uçakla byte-byte aynı** | Yalnız Opus 5 |
| `include/mini_ws.h` | Kütüphanesiz WebSocket sunucusu (SHA-1 + base64 dahil) | Opus 5 — çerçeve ayrıştırma ve tampon sınırları |
| `include/web_ui.h` | Telefon arayüzü, tek `PROGMEM` dizgi (~58 KB) | Düzen/tema → Sonnet 5; kol matematiği → Opus 5 |
| `diag/rf_diag.cpp` | Ham SPI nRF24 teşhisi, ayrı ortam | Sonnet 5 |

## Bu projeye özel tuzaklar

- **`web_ui.h` bir C++ ham dizgisidir** (`R"HTMLPAGE( ... )HTMLPAGE"`).
  İçinde `)HTMLPAGE"` geçemez. Dosya saf ASCII olmalı.
- **Arayüzdeki gaz hesabı firmware ile birebir aynı olmak zorunda.**
  `gazCikisi()` (JS) ve `gazUs()` (C++) ayrışırsa kalkış öncesi kontrol yeşil
  görünürken ARM reddedilir. `tools/test_throttle.mjs` bunu 42 000 noktada
  denetliyor — JS'te `Math.trunc`, C++'ta tamsayı bölmesi.
- **`WiFiClient::write()` tıkalı sokette ~10 sn bloklar.** `mini_ws.h` bu
  yüzden yazmadan önce sıfır zaman aşımlı `select()` ile yokluyor. Bu kontrolü
  kaldırma.
- **`WebServer` her cevaba `Connection: close` koyar.** Kol verisi bu yüzden
  WebSocket'ten gidiyor; HTTP yalnızca yedek yol.
- **NVS ad alanı `rctx`.** `TxAyar` yapısına alan eklersen `TX_IMZA`'yı artır,
  yoksa eski kayıt yeni alanlara oturur.
- **Ayarlar ekranı ARMED iken açılmaz** — tüm ekranı kaplıyor, kolları
  gizlerdi.

## Derleme

```bash
pio run                 # kumanda firmware'i (varsayilan ortam)
pio run -e rfdiag       # ham SPI nRF24 teshisi - WiFi ACMAZ
pio run -t upload       # COM5'e yukler
```

Port `platformio.ini` içinde **COM5** olarak sabit. Kaldırma — PlatformIO ilk
bulduğu portu seçer ve firmware'i uçak kartına atar.
