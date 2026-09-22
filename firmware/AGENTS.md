# AGENTS.md — RC Plane yazılım deposu

Bu dosya, depoda çalışan kodlama ajanları içindir. Üç proje, ortak bir telsiz
protokolü ve **uçuş güvenliğini ilgilendiren bir kod yolu** barındırıyor.
Aşağıdaki kurallar tercih değil, kısıt.

---

## 1. Projeler

| Dizin | Ne | Dil / araç | Risk |
|---|---|---|---|
| `controller_software/` | Yer istasyonu (verici). WiFi AP, telefon arayüzü, 50 Hz çerçeve üretimi | C++ / PlatformIO / ESP32-WROOM-32 | **Yüksek** — gaz ve ARM kararı burada |
| `flight_software/` | Uçak (alıcı). Çift telsiz, failsafe, servo/ESC sürme | C++ / PlatformIO / ESP32 DevKitC (WROOM-32D) | **En yüksek** — motoru bu kart döndürüyor |
| `sim_middleware/` | Arayüz → vJoy köprüsü. Simülatörde uçuş denemesi | Python / Flask / Socket.IO | Düşük — yerde, masaüstünde |

`../rc-plane/` yayımlanan git deposudur ve `firmware/` altında bu iki
firmware'in **kopyasını** tutar. Burada bir değişiklik yaptıysan aynaya da
kopyala (bkz. §6).

---

## 2. Model yönlendirme

Görevi en ucuz **yeterli** modele ver. "Yeterli"nin ölçüsü hız değil, hatanın
bedeli: bu depoda bazı hataların bedeli kırılmış bir uçak.

### Opus 5 — `claude-opus-5`

Bir hatanın donanımı bozabileceği ya da sebebi belirsiz olan her iş.

- **Güvenlik zinciri**: ARM kapıları, failsafe, arm kilidi, gaz yolu,
  `escYaz()`, `kontrolUygula()`, `paketIsle()`
- **Protokol değişikliği** (`include/rc_protocol.h`): iki firmware'i birden
  kırar, boyut/`magic` ayrımı ve NVS imzaları buna bağlı
- **Eşzamanlılık ve zamanlama**: ESP-NOW geri çağrıları, `portENTER_CRITICAL`
  bölgeleri, nRF24 ACK payload FIFO'su, bloklamayan gönderim
- **Kol matematiği ve kalibrasyon eşlemesi**: bağlı kol modeli, origin push,
  end-point/travel adjust, expo eğrisi
- **Projeler arası değişiklik**: protokol + firmware + arayüz aynı anda
- **Tekrar üretilemeyen hata teşhisi**: "bazen oluyor", "telemetri gelmiyor",
  "servo titriyor" — bunların hiçbirini küçük modele verme

### Sonnet 5 — `claude-sonnet-5`

Sınırları belli, kalıbı kurulmuş iş.

- Tek proje içinde özellik ekleme (mevcut bir ayarın ikizini eklemek gibi)
- Arayüz düzeni, CSS, duyarlılık, tema, tipografi
- Uçtan uca ayar alanı ekleme (`TxAyar` → JSON → arayüz) — kalıp hazır
- `tools/` altındaki kosumları genişletme
- Mevcut yapıyı izleyen doküman güncellemesi
- Seri log satırlarına alan ekleme, teşhis çıktısı iyileştirme

### Haiku 4.5 — `claude-haiku-4-5-20251001`

Yargı gerektirmeyen, sonucu tek bakışta doğrulanabilen iş.

- Yeniden adlandırma, biçimlendirme, yorum düzeltmesi
- Derleme çalıştırıp çıktıyı raporlama
- Aynaya kopyalama ve `cmp` ile eşitlik doğrulama
- Envanter: sabitleri listeleme, `grep` taraması, pin haritası dökme
- README'nin mevcut bir bölümünü verilen metinle değiştirme

### Asla küçük modele verilmeyecek

Aşağıdakiler tek satırlık değişiklik gibi görünse de **Opus 5** ister:

- ESC'ye ulaşan değeri değiştiren her şey
- `RC_US_*`, `THROTTLE`, `ARMED`, `FAILSAFE` geçen her satır
- `rc_protocol.h`
- NVS imzaları (`NVS_IMZA`, `TX_IMZA`) — yanlış yönetilirse kalibrasyon
  sessizce çöp değerlerle yüklenir

---

## 3. Orkestrasyon kalıbı

**Tek ajan yeterliyse tek ajan kullan.** Alt ajan her seferinde bağlamı
sıfırdan kuruyor; bu depoda bağlam pahalı (protokol + iki firmware + arayüz).

Paralel alt ajan yalnızca şu üç durumda:

1. **Geniş arama** — "şu sabit nerelerde geçiyor" gibi çok dosyaya yayılan
   tarama. Sonuç tek satır dönerse alt ajan kazandırır.
2. **Bağımsız projeler** — `sim_middleware` işi ile firmware işi aynı anda
   yürütülebilir; ortak dosyaları yok.
3. **Bağımsız doğrulama** — bir değişikliği yazan ajan değil, ikinci bir ajan
   gözden geçirsin (güvenlik zinciri değişikliklerinde önerilir).

**Paralel yapma:** aynı dosyaya iki ajan yazamaz. `web_ui.h`, `main.cpp` ve
`rc_protocol.h` bu depoda en sık çakışan üç dosya.

Görev devretmeden önce ajana şunları ver: hangi projede çalıştığı, §4'teki
değişmezler, §5'teki doğrulama komutu.

---

## 4. Değişmezler

Bunlar tartışmaya açık değil; bozulursa uçak düşer.

1. **`rc_protocol.h` iki projede byte-byte aynıdır.** Birini değiştirdiysen
   diğerine kopyala ve `cmp` ile doğrula. Farklı sürümler linki sessizce
   öldürür — iki kart da "bağlı" görünür.
2. **Protokol değişince `RC_PROTO_VER` artar** ve **iki kart birden yüklenir.**
   Sürüm nibble'ı uyuşmayan paket atılır.
3. **Yapı değişince NVS imzası artar** (`NVS_IMZA` / `TX_IMZA`). Artırılmazsa
   eski flash kaydı yeni alanlara oturur ve servo uçuşta anlamsız bir yere
   gider.
4. **Gaz her zaman iki taraflı sınırlanır.** Kumandadaki `thrLimit` ve uçaktaki
   tavan bağımsızdır; birine güvenip diğerini kaldırma.
5. **ARM kararı kol konumuna değil, gönderilecek darbeye bakar**
   (`gazUs(ctrl.thrIn) > RC_US_MIN + 20`). Trim'i atlayan bir kontrol yazma.
6. **Uçuş ekranında modal yok.** `alert()` / `confirm()` bir pointer olayının
   içinden çağrılırsa dokunma dizisini bozar ve pilotun gazı indirmesini
   engeller. Uyarı `uyar()` ile üst şeride yazılır.
7. **`TEZGAH_MODU` false ile teslim edilir.** Değiştirdiysen geri al.
8. **Uçuş ekranında animasyon / geçiş / gradyan / gölge yok.** `tools/test_theme.py`
   bunu kaynak üzerinden denetliyor.

---

## 5. Doğrulama

Değişikliği bitirmeden önce ikisi de çalışmalı:

```bash
# 1) Derleme - dort ortam
cd controller_software && pio run && pio run -e rfdiag
cd ../flight_software  && pio run && pio run -e bench

# 2) Arayuz kosumlari - donanim gerektirmez
node tools/run_all.mjs
```

`tools/run_all.mjs` beş kontrolü çalıştırır:

| Kosum | Ne doğruluyor |
|---|---|
| `test_stick.mjs` | Kol matematiği: bağlı model, origin push, üç mod, aileron, çarpma koruması, tokmağın yuvadan taşmaması |
| `test_throttle.mjs` | Arayüzdeki `gazCikisi()` ile firmware'deki `gazUs()` 42 000 noktada aynı sonucu veriyor mu (ARM eşiği buna bağlı) |
| `test_dom.mjs` | Sayfa JS'i sahte DOM üzerinde baştan sona hatasız çalışıyor mu — yükleme hatası bütün olay bağlayıcılarını sessizce iptal eder |
| `test_theme.py` | İki temada WCAG kontrastı + uçuş ekranı kısıtları + yerleşim |
| `test_ids.py` | JS'in aradığı her `#id` ve `.sınıf` HTML'de var mı — eksiği rAF döngüsünü öldürür |

Kosumlar **kaynak dosyaları okur**, elle kopyalanmış sürüm tutmaz. Arayüzü
değiştirdiğinde kosum da onunla birlikte gerçeği söyler.

**Bu kosumlar uçuş davranışını test etmez.** Donanım doğrulaması ayrı:
pervanesiz tezgah testi (`pio run -e bench`), sonra pist.

---

## 6. Ayna

`../rc-plane/` yayımlanan depodur. Firmware dosyaları orada **kopyadır**:

```
Flight Software/controller_software/  →  rc-plane/firmware/controller_software/
Flight Software/flight_software/      →  rc-plane/firmware/flight_software/
Flight Software/controller_software/docs/RF_PROTOKOL.md → rc-plane/docs/RF_PROTOCOL.md
```

Kopyaladıktan sonra `cmp -s` ile doğrula. **Commit atma** — kullanıcı istemeden
`git commit` çalıştırma.

`sim_middleware/` aynada yok; yalnız bu çalışma ağacında duruyor.

---

## 7. Dil ve üslup

- Kod yorumları, seri log ve arayüz metni **Türkçe, ASCII** (Türkçe karakter
  yok). Firmware'de `LC_ALL=C grep -n '[^ -~\t]'` boş dönmeli.
- Yorumlar **neden**i anlatır, neyi değil. "Bu değer 4 MHz" değil, "10 MHz'de
  dupont kabloyla güvenilir değil".
- Kullanıcıya dönen metin ve README'ler: kök `README.md` ve proje README'leri
  İngilizce (GitHub vitrini), teknik dokümanlar Türkçe.
- Yeni dosya eklerken mevcut dosyanın yorum yoğunluğunu ve adlandırma
  biçimini taklit et.

---

## 8. Ortam

- PlatformIO: `C:\.platformio\penv\Scripts\pio.exe`
- Portlar `platformio.ini` içinde sabit: **COM5 = kumanda**, **COM6 = uçak**.
  Sabitlenmezse yanlış karta yükleme yapılır.
- Node 24+ ve Python 3.11 `tools/` kosumları için gerekli.
- Kabuk: PowerShell birincil, Bash mevcut. Heredoc içinde ters bölü ve
  Türkçe karakterler bozulabiliyor; büyük dosyaları dosyaya yazıp kopyala.
