# RC Plane — Geliştirme Yol Haritası

Sistem uçuyor: telsiz, arayüz, failsafe ve kumanda zinciri çalışıyor. Bu belge bundan
sonra ne yapılacağını, **neden** yapılacağını ve **ne zaman bitmiş sayılacağını** yazıyor.

Sıra, "en çok şeyi en az emekle değiştiren" işe göre dizildi. Emek tahminleri kabaca ve
tek kişilik çalışmaya göre.

| Sıra | İş | Emek | Neyi değiştirir |
|---|---|---|---|
| 1 | Depo ve belge temizliği | 1 gün | Projenin dışarıdan görünen hali |
| 2 | Link güvenliği (kimlik doğrulama) | 1–2 hafta | Sistemin en büyük açığı |
| 3 | Menzil ve link ölçümü | 2 gün + saha | "Verisi var mı" sorusunun cevabı |
| 4 | Telemetri ve uçuş kaydı | 1 hafta | Uçuşları analiz edebilmek |
| 5 | IMU ve stabilizasyon | 3–6 hafta | Projeyi İHA uçuş kontrolcüsüne çevirir |
| 6 | Donanımın olgunlaşması | 1–2 hafta | Güvenilirlik |
| 7 | Test altyapısı ve CI | 3–5 gün | Mühendislik süreci |
| 8 | 4 kanal (aileronlu kanat) | 1 hafta | Firmware zaten hazır |
| 9 | Frekans atlama (FHSS) | 2 hafta | Parazite ve karıştırmaya direnç |
| 10 | GPS ve eve dönüş | 4+ hafta | Otonomiye giriş |

---

## 1. Depo ve belge temizliği

Kısa işler, hepsi bugün bitebilir:

- [ ] `LICENSE` ekle (MIT önerisi). Lisanssız depo teknik olarak "tüm hakları saklı".
- [ ] Ana projedeki belge tutarsızlıkları: `RcPacket` sürüm 3'te **12 byte** ama
      `controller_software/README.md` ve `RF_PROTOKOL.md` hâlâ 10 byte diyor; protokol
      belgesinin başlığı "Sürüm 2".
- [ ] Ayarlar'daki **3 kanal / 4 kanal** seçimi hiçbir README'de anlatılmıyor.
- [x] Tartılmış ağırlık ve ölçülen CG → `AIRFRAME.md` tablosundaki tahminlerin yerine.

## 2. Link güvenliği — kimlik doğrulama ve tekrar koruması

**Sorun.** CRC-8 yalnızca bozulmayı yakalıyor, saldırıyı değil. Protokolü bilen biri aynı
kanaldan `"RCP01"` adresine geçerli paket üretirse uçağı kontrol edebilir; kaydedilmiş bir
paketi yeniden göndermek (replay) de mümkün. Hobi uçağı için kabul edilebilir, savunma
tarafında konuşulan ilk konu bu.

**Yapılacak.**
- Her iki karta NVS'te duran **paylaşılan anahtar** (ilk eşleşmede üretilip USB'den girilir).
- Her pakete kısaltılmış **AES-CMAC etiketi** (4 byte yeterli) — ESP32'de donanım AES var,
  `mbedtls` ile 50 Hz bütçesinde rahat kalır.
- `seq` alanını **monoton sayaç** olarak kullan; pencere dışındaki ve tekrar eden sayaçları
  reddet. Sayaç zaten var, yalnızca kuralı sıkılaştırmak gerekiyor.
- Paket 12 → 16 byte olur; `RC_PROTO_VER` artar, iki kart birden yüklenir.

**Bitti sayma ölçütü.**
- Üçüncü bir ESP32 ile üretilen sahte paket ve kaydedilmiş paketin tekrarı **reddediliyor**,
  seri logda red sayacı artıyor.
- Çerçeve süresi hâlâ 20 ms'nin altında; `[TX]` logunda gecikme artışı yok.
- Failsafe ve arm kilidi davranışı değişmiyor (aynı test senaryoları tekrar geçiyor).

**Neden değerli.** Mülakatta "linki nasıl güvene alırsın" sorusuna kodla cevap verebilmek.

## 3. Menzil ve link karakterizasyonu

**Yapılacak.** Açık alanda 50 / 100 / 200 / 400 m'de, her PA seviyesinde ve iki anten
yönünde ölçüm: `rxHz`, paket kaybı %, `kurt` / `kurtarma` sayaçları, failsafe'e düşme
sayısı. nRF24'te RSSI yok; RPD biti ve kayıp oranı kullanılır, ESP-NOW tarafında RSSI
okunabiliyor.

**Çıktı.** `docs/RANGE_TEST.md`: yöntem, ham tablo ve mesafeye karşı kayıp grafiği.
Grafiği README'ye koy — ölçülmüş veri, iddiadan daha ikna edici.

## 4. Telemetri ve uçuş kaydı

- **Batarya.** ADC1 pinine (ör. GPIO35) gerilim bölücü, `bataryaOku()` doldurulur, iki
  noktadan kalibrasyon. Arayüzdeki `BAT --` alanı kendiliğinden çalışmaya başlar.
- **Kayıt.** Yer istasyonu her çerçeveyi CSV'ye yazar (zaman, kanallar, `rxHz`, kayıp,
  voltaj, durum bitleri), arayüzden indirilir.
- **Bitti ölçütü.** Uçuş sonrası kaydı indirip grafiğe dökebiliyorsun; düşük voltajda
  arayüz uyarı veriyor.

Bu, 5. maddenin de önkoşulu: stabilizasyonun işe yarayıp yaramadığı ancak kayıtla anlaşılır.

## 5. IMU ve stabilizasyon — en büyük sıçrama

**Yapılacak.**
- IMU (MPU6050 başlangıç için yeterli, ICM-42688 daha iyi), titreşim yalıtımlı montaj.
- Tamamlayıcı ya da Mahony filtresiyle yatış/yunuslama tahmini; jiroskop sapması
  (drift) kalibrasyonu.
- 3 kanalda döngüler: yatış → rudder, yunuslama → elevator. PID, çıkış sınırları ve
  integral birikme (anti-windup) koruması.
- Modlar: **manuel** (bugünkü davranış), **stabilize**, ve en değerlisi
  **failsafe kurtarma**: link koptuğunda kanatları seviyeye getirip gazı kısarak süzülüş.
  Bugün failsafe uçağı yalnızca nötre alıyor; kurtarma gerçek bir emniyet özelliği olur.
- IMU arızası ya da tahmin sapması → otomatik manuele düşme.

**Test sırası.** Tezgahta (pervanesiz) kartı elde çevirip çıkışların doğru yöne gittiğini
gör → yüksek irtifada, geniş alanda, moda geçip hemen manuele dönerek dene → tam uçuş.

**Neden değerli.** İHA ekiplerinin çekirdek işi bu. Stabilizasyon eklendiğinde proje
"uzaktan kumanda" olmaktan çıkıp "uçuş kontrol yazılımı" oluyor.

## 6. Donanımın olgunlaşması

- Breadboard → delikli kart, sonra **KiCad** ile PCB (`hardware/kicad/`).
- Servolara ayrı UBEC (BEC hattındaki kondansatör bir yamadır, çözüm değil).
- Konnektörler (servo ve besleme ayrı), titreşim yalıtımı, anten yerleşimi ve gövde
  içinde kablo düzeni.
- Uçuş öncesi kontrol listesine "kablo çekiştirme testi" ekle.

## 7. Test altyapısı ve CI

- **Host üstünde birim testleri** (PlatformIO `native` ortamı): CRC, paket paketleme ve
  çözme, kalibrasyon eşlemesi, gaz eğrisi, failsafe durum makinesi.
- **GitHub Actions:** iki firmware derlemesi + `tools/run_all.mjs`. Depoya yeşil rozet.
- Küçük bir **gereksinim → test izlenebilirlik tablosu** (hangi güvenlik kuralını hangi
  test koruyor). Savunma sanayinde süreç bilinci, kodun kendisi kadar dikkat çeker.

## 8. Aileronlu kanat (4 kanal)

Firmware ve protokol hazır: aileron kanalı sürüm 3'te var, uçak GPIO 32/33'e aynalı çıkış
veriyor, arayüzde "4 kanal" seçeneği çalışıyor. Geriye yalnızca aileronlu bir kanat ve
iki servo kalıyor. Aynı uçakta iki kanadı karşılaştırmak (polihedral + rudder ile aileron)
güzel bir ölçüm yazısı olur.

## 9. Frekans atlama (FHSS)

Sabit kanal, hem WiFi parazitine hem kasıtlı karıştırmaya açık. Ortak bir atlama dizisi
(anahtardan türetilir), paket başına kanal değişimi ve yeniden senkron kuralı. 2. madde
bittikten sonra mantıklı: anahtar zaten paylaşılmış olur.

## 10. GPS ve otonomi

GPS modülü, konum telemetrisi, eve dönüş ve bağlantı kopmasında bekleme (loiter).
Bu noktada **ArduPilot / PX4 ve MAVLink**'i tanımak gerekir: kendi kodunu yazmaya devam
etsen bile sektörün ortak dili bu.

---

## Kapsam dışı bıraktıklarım

- **Gerçek zamanlı işletim sistemi (RTOS) görevlerine bölmek:** döngü bugün 20 ms
  bütçesini tutuyor, karmaşıklık kazancı karşılamıyor.
- **Kendi telsiz protokolünü LoRa'ya taşımak:** menzil artar ama gecikme kontrol için
  uygun değil.
