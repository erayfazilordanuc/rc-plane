# AGENTS.md — sim_middleware (simülatör köprüsü)

Depo kökündeki `AGENTS.md` dosyasındaki model yönlendirme ve dil kuralları burada da
geçerlidir. Bu proje **yerde çalışır ve uçuşa dokunmaz** — deponun en düşük
riskli parçası. Firmware'e uygulanan katılıkta bir gözden geçirme gerekmez.

---

## Ne yapıyor

Telefondaki RC arayüzünü, bilgisayardaki uçuş simülatörleri için **sanal bir
USB joystick'e** çeviriyor. Simülatör gerçek bir kumanda gördüğünü sanıyor.

```
telefon (web arayuzu) --WebSocket--> adb reverse --USB--> app.py --> vJoy --> simulator
```

Amaç: gerçek uçuşta kullanılacak **arayüzün tam kendisiyle** simülatörde
uçmak. Ayrı bir "test arayüzü" yok, dolayısıyla test edilen şey uçulan şeyle
birebir aynı.

## Dosyalar

| Dosya | İçerik |
|---|---|
| `app.py` | Flask + Socket.IO sunucu, `us_to_vjoy()` eşlemesi, watchdog |
| `static/rc_sim_client.js` | Arayüze enjekte edilen istemci |
| `static/index.html` | Yerel deneme sayfası |
| `tools/selftest.py` | vJoy olmadan köprüyü doğrular |
| `tools/fetch_client.py` | socket.io istemcisini yerelleştirir |

## Model yönlendirme

- **Sonnet 5** — varsayılan. Özellik, eşleme, arayüz, doküman.
- **Haiku 4.5** — `requirements.txt`, biçimlendirme, `selftest.py` çalıştırıp
  raporlama.
- **Opus 5** — yalnızca gecikme bütçesine dokunan değişiklikler (sıcak yol,
  thread'ler, Nagle/transport ayarları) ya da failsafe mantığı.

## Bu projeye özel kurallar

- **Sıcak yolda (`on_rc`) log, print, string formatlama, sözlük kopyalama
  yok.** Sayaçlar biriktirilip watchdog thread'inde basılıyor. Bu kural
  ölçülmüş bir gecikme bütçesine dayanıyor (~30 µs), bozma.
- **Arada kuyruk / zamanlayıcı / "60 Hz'e düşür" katmanı yok.** Paket geldiği
  anda vJoy'a yazılıyor.
- **`volatile.emit` bilinçli**: bağlantı koptuğunda paketler kuyruğa girmiyor.
  RC'de en son değer dışındaki her şey çöp.
- **500 ms failsafe**: paket gelmezse gaz kesilir, diğer eksenler nötre döner.
  `disconnect` anında aynısı uygulanır.
- **`1000–2000 µs` aralığı korunur.** Firmware ile aynı birim; dönüşüm tek bir
  fonksiyonda (`us_to_vjoy`) ve aralık dışı değerler **eşlemeden önce**
  kırpılıyor.
- `async_mode="threading"` + `simple-websocket` bilinçli tercih — eventlet
  Python 3.11+ ile sorun çıkarıyor.

## Çalıştırma

```bash
python -m venv .venv && .venv\Scripts\activate
pip install -r requirements.txt

python app.py               # vJoy'a yazar
python app.py --dry-run     # vJoy olmadan, sayac/log ile test
python tools/selftest.py    # kopruyu dogrular

adb reverse tcp:5000 tcp:5000   # telefonu bagla
```

`.venv/` ve `__pycache__/` `.gitignore` içinde; commit etme.
