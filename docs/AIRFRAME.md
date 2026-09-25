# RC Plane — Mekanik Tasarım Kararları

3 kanallı (gaz, elevator, rudder) eğitim uçağı. 5 mm fotobloktan elde kesildi.
Aileron yok: yatış, kanadın dihedral kırımı ve rudder ile sağlanıyor.

Aşağıdaki değerler **yapılmış uçağın** değerleri. İlk tasarım planından üç önemli fark var:
polihedral (640/380 bölünmesi) terk edildi ve dihedral tam ortaya alındı, kanat hücum kenarı
montajda 285 yerine 230'a geldi, uçuşa hazır ağırlık 976 g tahmini yerine **1106 g** çıktı.

![Gövde ölçüleri](diagrams/airframe_layout.svg)

Tüm ölçüler mm. Sıfır noktası firewall.

---

## 1. Kanat

| | |
|---|---|
| Açıklık / veter | 1400 / 200 sabit |
| Alan | 28 dm² |
| Plan | Dikdörtgen, sivrilme yok |
| Dihedral | **10°**, tam ortadan tek kırım — uçta ≈ 123 mm yükselme |
| Profil | KFm-2, basamak hücum kenarından 100 mm (%50 veter) |
| Kalınlık | Ön yarı 10 mm, arka yarı 5 mm |
| Kiriş | Hücum kenarından 50 mm geride |

- Üst katman iki şeride bölünüp **çıta aralarına gömüldü**; üst ve alt çıta çifti birlikte
  I-kiriş gibi çalışıyor.
- Kökte kiriş bindirmesi en az 200 mm, alt ve üstte takviye plakası. Eğilme momenti en çok
  kökte, çift kalınlık oraya denk geliyor.
- **Alt yüz baştan sona şeffaf bantlı:** hem nem bariyeri hem burulma rijitliği.
- Aileron bölgesi (uçtan 250 × 45 mm) işaretlendi ama **kesilmedi**. Firmware ve protokol
  4 kanalı zaten destekliyor; kanat sonradan aileronlu hale getirilebilir.

## 2. Gövde

| | |
|---|---|
| Uzunluk | 1050 |
| Kesit | 75 × 80 |
| Kuyruk | Üst kenar düz; yükseklik alttan daralarak 45 mm'ye iniyor, genişlik sabit 75 |
| Firewall | 3 mm, 3D baskı ([`Firewall.stl`](../cad/print_files/Firewall.stl)), epoksi, 20 mm yuvaya oturuyor |

- Yan duvar eki **x=400**, üst güverte eki **x=600**. İkisi bilerek aynı istasyona
  getirilmedi; aynı kesitte buluşurlarsa gövde orada zayıflar.
- Kanat bölgesine (x=285–485) iç yüzden **çift kat doubler**.
- Ara bölmeler: **x=150, 285, 485, 750**.
- **İniş takımı yok:** elden fırlatma, karın inişi. Burun altına kurban şeridi ve bant.

## 3. Yerleşim ve denge

| | |
|---|---|
| Kanat hücum kenarı | x=230 *(tasarımda 285 idi, montajda 230'a geldi)* |
| **Ağırlık merkezi** | **x=278** (ölçülen) — hücum kenarından 48 mm, veterin %24'ü; hedef x=280 (%25) |

- **Batarya cırt bantla ayarlanabilir.** CG'nin tek düzeltme aracı bu; kurşun eklenmiyor.
- **Kanat gövdeye yapışık değil:** iki ahşap pim + kauçuk lastik. Sert bir inişte kanat
  yerinden çıkıyor — tasarlanmış kırılma noktası.

## 4. Açılar

| | |
|---|---|
| Kanat hücum açısı | +1,4° (5 mm takoz) |
| Yatay dengeleyici | 0° — dekalaj 1,4° |
| Motor | 2° aşağı, 2° sağ |

Firewall dik kesildi; motor açıları montaj pulu ile veriliyor.

## 5. Kuyruk

| | |
|---|---|
| Yatay | 400 × 150 (kanadın %21'i), **Vh = 0,70** |
| Elevator | Veterin %35'i = 52 mm |
| Dikey | 180 yükseklik, kök 140 / uç 100, **Vv = 0,036** |
| Rudder | 50 mm sabit genişlik, firar kenarı dik |
| Kuyruk momenti | ~630 mm = 3,1 × veter |

- **Menteşe:** iki yüzden 45° tıraş, tek taraftan bant.
- **Sapmalar:** elevator ±14 mm, rudder ±20 mm. Mekanik nötr ayarlı; ince ayar
  arayüzdeki servo kalibrasyonundan yapılıyor (yön, nötr, uç noktalar uçağın NVS'inde).

## 6. Güç sistemi

| | |
|---|---|
| Motor / pervane | A2212 1000 KV / 10×4.5 |
| ESC | 30 A |
| Batarya | 3S 2200 mAh 30C |
| 5 V | **Ayrı UBEC, 5 V / 3 A** — ESC'nin lineer BEC'i devre dışı |
| Akım hedefi | Tam gazda 25 A altı |
| Tam gaz (eCalc) | ≈ 15,1 A, 161 W, 8680 d/d, ≈ 912 g statik itki — ölçülmedi |

ESC'nin lineer BEC'i devre dışı bırakıldı; kart ve iki servo ayrı UBEC'ten besleniyor.
Sebebi yaşandı: servolara sinyal gidince kart çöküyordu — önce ESP32-C3'te, DevKitC'ye
geçince yine. Sorun yazılım değil beslemeydi; 9 g servo ani harekette ~700 mA çekiyor ve
12 V'u 5 V'a düşüren lineer BEC hattı tutamıyordu. Ayrı UBEC'ten sonra çökme bitti.
Bağlantı şeması: [`diagrams/aircraft_wiring.svg`](diagrams/aircraft_wiring.svg).

## 7. Malzeme ve yapım

- **5 mm kraft fotoblok**, 50 × 70 cm levha. Hiçbir parça 700 mm'yi geçmiyor; gövdenin uzun
  parçaları ekleme yapılarak elde edildi.
- **Sıcak silikon** ana yapıştırıcı; epoksi yalnızca firewall'da.
- **Ahşap çıta kiriş** (karbon yerine).
- Tutkal **köpük yüzeye** sürülüyor, kağıda değil.

Yapım sırası: test parçası (45° eğik kesim, çizik derinlikleri, bant menteşe) → 1:1 şablonlar
→ kanat panelleri ve çıta → ortadan dihedral kırımı → kök takviyesi → gövde yan duvarları,
ekler, doubler'lar, firewall → ara bölmeler → **UBEC hattını masada dene** → elektronik ve
üst güverte → kuyruk yüzeyleri → pushrod ve mekanik nötr → son CG ölçümü.

## 8. Kütle

| | |
|---|---|
| Uçuşa hazır | **1106 g** |
| Kanat yükü | 39,5 g/dm² |
| İtki/ağırlık | ≈ 0,83 *(eCalc, ölçülmedi)* |

İtki/ağırlık ≈ 0,83 olduğu için dik tırmanış yok: elden fırlatmada burnu fazla kaldırmamak,
hızı toplamasını beklemek gerekiyor. 0,83 **statik** bir oran; uçuş hızındaki itki bunun çok
altında — bkz. §9.

**CG kontrolü:** uçak bataryalı ve tam donanımlıyken, kanadın altından iki parmakla hücum
kenarından 50 mm geriden tut. Yatay durmalı ya da burnu hafif aşağı bakmalı. Kuyruk
düşüyorsa bataryayı öne kaydır.

## 9. Uçuş testleri ve performans analizi

### Ne oldu

Sürülmüş tarlada elden atış; ilk oturum 21 Eylül 2026, sonra bir akşam oturumu. Telsiz her
seferinde çalıştı, uçak havada kalmadı. Her atış **tam kolda**, ayarlar varsayılanda (gaz
sınırı %100, uçak tavanı yok) ve ESC kalibre edilmişken yapıldı. Kodda bu, arada başka
kısıt olmadan 2000 µs darbe demek (kumandada `gazUs()`, uçakta `escYaz()`). Süreler
videolardan kare sayılarak çıkarıldı (30/60 fps).

| Atış | Videoda görünen |
|---|---|
| En iyi (21 Eylül) | Elden çıkıyor, biraz tırmanıyor, sonra yavaş yavaş irtifa kaybediyor, kurtarılamıyor — ~10 s. Pilotun yorumu: rüzgâr yardım etti |
| Yumuşak, burun yukarı | Yavaş ve burnu kalkık bırakılıyor, bir kanat düşüyor, bırakıştan **1,2 s** sonra yerde |
| Yukarı fırlatma | Pilot tek elle dik yukarı atıyor, telefon öbür elde; birkaç metre çıkıyor, geri çöküyor, **~4,5 s** sonra yerde |

Kamera her klipte hareketli olduğu için hız ve alçalma oranı videodan güvenilir ölçülemiyor.

### Model

![Uçuş performansı](diagrams/flight_performance.svg)

[`diagrams/flight_model.py`](diagrams/flight_model.py): noktasal kütle, boyuna düzlem. Ölçüm
değil, model — varsayımlar tek yerde, ölçüldükçe değiştirilecek. Testler tam gazda yapıldığı
için eğriler gaz yüzdesine göre değil, **tam gazdaki statik itkiye** göre çiziliyor: ölçülmemiş
en büyük girdi o.

| Varsayım | Değer | Nasıl doğrulanır |
|---|---|---|
| Statik itki, tam gaz | ≈ 912 g (eCalc) — **ölçülmedi** | El kantarı / mutfak terazisi |
| CLmax | 0,9 (KFm-2, Re ≈ 130 000) | XFLR5 polarları |
| CD0 | 0,05 (bant 0,04–0,07) | XFLR5 + sabit kameradan süzülüş |
| Oswald e | 0,75 | XFLR5 |
| Hıza göre itki düşüşü | Staples yaklaşık denklemi | Ses spektrumundan devir (phyphox) |
| Hava yoğunluğu | 1,20 kg/m³ | — |

| Sonuç | Değer |
|---|---|
| Stall hızı | ≈ 8,5 m/s |
| En az sürükleme hızı | ≈ 8,4 m/s — stall ile aynı |
| Adım hızı (10×4.5) | ≈ 16 m/s; 10 m/s'de itki statiğin %38'i |
| Tam gaz, ≈ 912 g statik (eCalc) | Yatay uçuş 8,5–13,1 m/s, en iyi tırmanış ≈ 2,4 m/s |
| Tam gaz, 650 g statik | Yatay uçuş 8,5–10,8 m/s, tırmanış ≈ 1,0 m/s |
| İrtifa tutulamayan statik itki | ≈ 440–500 g altı (itki/ağırlık 0,4–0,45) |
| ≈ 912 g ile irtifa tutulamayan CD0 | ≈ 0,3 — bağlı akışta olanaksız, ancak stall'da |
| Motorsuz, 2 m'den | 1,6 s'de yerde |

Atış simülasyonu: 2 m yükseklik, 8,5 m/s, elevator sabit (9,3 m/s'ye trimli), pilot
müdahalesi yok. ≈ 912 ve 650 g tırmanıyor, 450 g yere iniyor (4,3 s).

### eCalc karşılaştırması

Aynı güç sistemi [eCalc propCalc](https://www.ecalc.ch/)'ta çalıştırıldı (A2212 1000 KV,
10×4.5, 3S 2200 mAh 30C, 1105 g). Modelin statik itkisi buradan geliyor.

| | eCalc |
|---|---|
| Tam gaz | 15,14 A, 10,52 V, 159 W elektrik / 131 W mekanik, 8680 d/d, verim %82,5 |
| En iyi verim noktası | 7,26 A, 78,5 W, 9766 d/d, verim %86,6 |
| Batarya | 6,88 C yük, 10,64 V; tam gazda 7,4 dk, karışık uçuşta 10,6 dk |
| Motor sıcaklığı | ~42 °C (15 s tam gazda) |
| Statik itki | ≈ 912 g, özgül itki 5,73 g/W, itki/ağırlık 0,83 |
| Adım hızı / uç hızı | 60 km/h (16,7 m/s) / 416 km/h |
| Stall / yatay hız | 30 km/h (8,3 m/s) / 68 km/h (18,9 m/s) |
| Tırmanma | 4,5 m/s (~25–30°) |

eCalc stall ve adım hızında modelle uyuşuyor, gerisinde daha iyimser (tırmanış 4,5'e
karşı 2,4 m/s). Sonuç hangisinin doğru olduğuna bağlı değil: kağıt üzerinde bu güç
sistemi tırmanıyor.

### Yorum

- **Kağıt ile saha çelişiyor — bulgu bu.** eCalc'ın ≈ 912 g'ı ile iki hesap da tam gazda
  tırmanış veriyor; uçak tırmanmadı. Model yavaş düşüşü yalnızca iki durumda üretiyor: motor ~500 g'ın altında
  itki veriyorsa, ya da kanat stall'un ötesinde uçuyorsa (orada sürükleme bu modelin çok
  üstünde).
- **Güç mü, hız mı?** Düşüş düşük hızda oluyor: uçak stall hızından hiç kurtulamıyor. Onu
  orada tutan ya itkinin hızlanmaya yetmemesi, ya da kanadın fazla yüksek açıda tutulması
  (burun yukarı bırakış, çekili elevator). Tek ölçüm ikisini ayırıyor: statik itki ~800 g ve
  üstüyse güç zinciri temiz, sorun atış ve trim; ~500 g ve altıysa sorun güç zinciri.
- **Hız payı yok.** En az sürükleme hızı stall'un üstüne oturuyor. Uçağı yavaşlatan her şey
  (yumuşak atış, burun yukarı bırakış, düşüşü durdurmak için elevator çekmek) sürüklemeyi
  artırıp düşüşü hızlandırıyor.
- **Rüzgâr — en iyi uçuş şans olabilir.** Karşı rüzgâra atılan uçak elden atış hızı *artı*
  rüzgâr hızıyla ayrılır; bir hamle (gust) de bir anlığına hız ekler. Stall ile en az
  sürükleme hızı üst üste olduğu için rüzgârın kısa bir kesilmesi uçağı yeniden düşüşe sokar.
  Rüzgârın yapamayacağı: sabit rüzgâr, uçak havadayken hava hızını değiştirmez; rüzgâr
  yönüne dönmek yer hızını değiştirir, hava hızını değil. Düşen uçağı yalnızca yükselen hava
  taşır. Rüzgâr ölçülmedi, bu yüzden hipotez olarak kalıyor.
- **Süzülerek kurtulamaz.** 10 saniye havada kalması motorun çektiğini gösteriyor; eksik olan
  güç **payı**.

### Nerede yanıldım

- Tasarımı statik sayılarla doğruladım (itki/ağırlık, kuyruk hacimleri, CG); uçuş hızındaki
  itki ile sürüklemeyi hiç karşılaştırmadım.
- İtkiyi hiç ölçmedim. 0,83 oranı eCalc tahmini; "yerde ilerlemeye başladı" yalnızca itkinin
  yer sürtünmesini yendiğini gösterir — ağırlığın küçük bir kesri.
- Ağırlık %13 arttı (976 → 1106 g), kanat yükü 35 → 39,5 g/dm²; payı yeniden hesaplamadım.
- Atışlar yumuşak ya da burun yukarıydı, biri tek elle.
- Belgeler ilk uçuş için %70–80 gaz sınırı öneriyor — bu pay için yanlış (testler %100'de
  yapıldı).
- Veri olmadan uçtum: uçuş kaydı, sabit kamera, ölçülmüş itki, rüzgâr ölçümü yok.

### Dışarıdan gelen öneriler

| Öneri | Değerlendirme |
|---|---|
| 10×4.5 yerine 9×6 pervane | **İşe yarar** — adım hızı 16 → 22 m/s, en iyi tırmanış 2,4 → 2,7 m/s (8700 d/d varsayımıyla — eCalc'ta denenecek). Akım kontrolü |
| 2200 yerine 1300–1500 mAh batarya | Küçük etki — ~−80 g, stall −%4; CG yeniden ayarlanmalı |
| Kağıdı soyulmuş fotoblok / XPS ile 750–800 g'da yeniden yapım | **En büyük kaldıraç** — stall 7,0–7,2 m/s, kanat yükü 27–29 g/dm² |
| KFm-2 yerine Clark Y / NACA 4412 | Makul, daha az sürükleme; yeni kanat demek, ucuz çözümlerden sonra |
| Spiral dalışa karşı dikey kuyruk +%15–20 | **Desteklenmiyor.** Vv 0,036 olağan aralıkta (0,02–0,05). Büyük dikey kuyruk spiral ıraksamayı *artırır*; güçlü dihedral + küçük kuyruk daha çok Dutch roll'a eğilimlidir. XFLR5 kararlılık analizi gerekir |
| Dokunmatik rudder'a expo / ölü bölge | Expo zaten var (varsayılan %25); ölü bölge yok, eklemesi ucuz |

Parça almadan yapılacak ölçümler ve yazılım işleri: kök `README.md` → *Next Steps*.

---

## İlk plandan neler değişti

| Konu | Plan | Yapılan |
|---|---|---|
| Kanat kırımı | Polihedral, 640 + 380 panel, ±320'de kırım | 10° dihedral, ortadan tek kırım |
| Kanat hücum kenarı | x=285 | x=230 |
| 5 V | ESC'nin BEC'i | Ayrı UBEC 5 V / 3 A, ESC BEC devre dışı |
| Ağırlık | 976 g tahmini | 1106 g ölçülen |
| Kanat yükü | 35 g/dm² | 39,5 g/dm² |
