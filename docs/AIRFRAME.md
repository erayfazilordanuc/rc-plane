# RC Plane — Gövde Tasarımı ve Yapım Planı

3 kanallı (gaz, elevator, rudder) eğitim uçağı. Fotoblok levha ve ahşap çıtadan,
elde kesilerek yapıldı. Aileron yok: yatış, kanadın polihedral kırımı ve rudder ile
sağlanıyor.

![Gövde ölçüleri](diagrams/airframe_layout.svg)

> Bu belgedeki değerler **tasarım** değerleridir. Uçağın tartılmış ağırlığı ve ölçülmüş
> ağırlık merkezi henüz buraya işlenmedi.

Tüm ölçüler mm. Sıfır noktası firewall (motor perdesi).

---

## 1. Ana ölçüler

| | Değer |
|---|---|
| Kanat açıklığı | 1400 |
| Veter | 200 (dikdörtgen) |
| Kanat alanı | 28 dm² |
| Panel bölünmesi | 380 + 640 + 380 |
| Polihedral kırım | ±320 |
| Kırım açısı / uç takozu | 8° / 53 mm |
| Profil | KFm-2, basamak hücum kenarından 100 mm geride |
| Profil kalınlığı | ön yarı 10, arka yarı 5 |
| Kiriş (çıta) ekseni | hücum kenarından 50 mm geride |
| Gövde uzunluğu | 1050 |
| Gövde kesiti | 75 en × 80 yükseklik (kuyrukta 45 yükseklik) |
| Kanat hücum kenarı | firewall'dan 285 |
| **Ağırlık merkezi (CG)** | **hücum kenarından 50 mm geride** (veterin %25'i, çıta hizası) |
| Yatay dengeleyici | 400 × 150 |
| Dikey dengeleyici | 180 yükseklik, kök 140 / uç 100 |
| Motor / pervane | A2212 1000 KV / 10×4.5 |
| Tahmini toplam ağırlık | ~976 g → 35 g/dm² |

Levha boyutu 50×70 cm olduğu için tek parça 700 mm'lik orta panel mümkün değildi
(kesim payı sıfır kalıyordu). Bu yüzden orta panel 640, uç paneller 380 yapıldı;
kırım ±320'ye kaydı. Daha içeriden kırılan kanat biraz daha fazla yatış kararlılığı
veriyor, yani bu kayma aleyhe değil.

---

## 2. Kanat kesiti ve kiriş

Üst katman iki şerit halinde kesilir, aradaki boşluğa çıta konur. Aşağıdaki ölçüler
**10 mm genişliğinde çıta** içindir. Çıta farklıysa: ön şerit = arka şerit = 50 − G/2,
boşluk = G.

---

## 3. Kesim listesi

Hiçbir parça 700 mm'yi geçmiyor. Gövdenin uzun parçaları eklenerek yapılıyor, ek yerleri
kaydırılmış.

| Parça | Ölçü | Adet |
|---|---|---|
| Kanat tabanı — orta | 640 × 200 | 1 |
| Kanat tabanı — uç | 380 × 200 | 2 |
| KFm ön şerit — orta | 640 × 45 | 1 |
| KFm arka şerit — orta | 640 × 45 | 1 |
| KFm ön şerit — uç | 380 × 45 | 2 |
| KFm arka şerit — uç | 380 × 45 | 2 |
| Kırım takviyesi | 160 × 80 | 4 |
| Yan duvar — ön | 400 × 80 | 2 |
| Yan duvar — arka | 650 × 80 (45'e daralan) | 2 |
| Üst güverte — ön | 600 × 75 | 1 |
| Üst güverte — arka | 450 × 75 | 1 |
| Alt panel — ön | 485 × 75 | 1 |
| Alt panel — arka | 570 × 75 | 1 |
| Kanat doubler | 200 × 80 | 4 |
| Ara bölme | 75 × 80 | 3 |
| Ara bölme — arka | 75 × 55 | 1 |
| Yatay dengeleyici | 400 × 150 | 1 |
| Dikey dengeleyici | 180 × 140 | 1 |

**Ek yeri kuralı:** yan duvarlar x=400'de, üst güverte x=600'de ekleniyor. İkisi aynı
istasyona denk gelmemeli, yoksa gövdede zayıf bir kesit oluşur. Her ek yerinin iç
yüzüne 100 mm bindirme parçası.

Firewall'un 3B baskı modeli: [`cad/print_files/Firewall.stl`](../cad/print_files/Firewall.stl)
(63 × 95 × 22 mm).

---

## 4. Levha yerleşimi — 4 levha

| Levha | İçerik |
|---|---|
| 1 | Kanat tabanı orta (640×200) + KFm orta şeritler + kırım takviyeleri + ara bölmeler |
| 2 | Kanat tabanı uçlar (2× 380×200) + KFm uç şeritler + yan duvar ön parçalar |
| 3 | Yan duvar arka (2× 650×80) + üst güverte (600 + 450) + alt paneller (485 + 570) |
| 4 | Yatay + dikey dengeleyici + kanat doubler'ları + yedek |

Levha 3'teki parçaların hiçbiri 650 mm'yi geçmiyor, toplam genişlik 460 mm; 500'lük
levhaya sığıyor. **5 levha al**, biri fire payı.

---

## 5. Çıta planı — 50 cm'lik çıtalar

| Bölge | Uzunluk | Çıta |
|---|---|---|
| Orta panel — üst | 640 | 2× 500, kökte ortalanmış 360 bindirme |
| Orta panel — alt | 640 | 2× 500, aynı şekilde |
| Sol uç panel — üst | 330 | 1× 500'den kesim |
| Sağ uç panel — üst | 330 | 1× 500'den kesim |

**Toplam 6 adet 50 cm çıta.**

Bindirme kanat kökünde ortalanıyor: bir çıta −320'den +40'a, diğeri −40'tan +320'ye.
Eğilme momentinin en büyük olduğu yer kök, çift kalınlık tam gerektiği yere denk geliyor.

Bindirme bölgesinde iki çıta **yan yana** duruyor, üst üste değil; 5 mm'lik boşluğa
10 mm sığmaz. O bölgede kanal genişliği 20 mm.

Uç panellerde alt çıta yok; oradaki eğilme momenti düşük, bant yeterli.

---

## 6. Ağırlık ve denge (tasarım tahmini)

| Kalem | g |
|---|---|
| Fotoblok — kanat | 235 |
| Fotoblok — gövde | 190 |
| Fotoblok — kuyruk | 43 |
| Ahşap çıta (6 adet) | 35 |
| Kontrplak firewall | 25 |
| Tutkal + bant | 90 |
| Motor + ESC + pervane | 95 |
| Batarya 3S 2200 | 180 |
| Servo × 2 (9 g) | 18 |
| ESP32 DevKitC + nRF24 PA/LNA + kablo | 45 |
| Pushrod, yeke, dowel | 20 |
| **Toplam** | **976** |

Kanat yükü 35 g/dm². İtki/ağırlık ≈ 0.97.

> İlk planda uçuş kartı ESP32-C3 SuperMini ve ayrı bir UBEC vardı. Uçan uçakta
> ESP32 DevKitC var ve UBEC yok (servolar ESC'nin BEC'inden besleniyor). Elektronik
> satırı bu yüzden yeniden tartılmalı.

**CG kontrolü:** uçak tam donanımlı ve bataryası takılıyken, kanadın altından iki
parmakla hücum kenarından 50 mm geride tut. Yatay durmalı ya da burnu hafifçe aşağı
eğilmeli. Kuyruk düşüyorsa bataryayı öne kaydır; kurşun ekleme.

---

## 7. Yapım sırası

1. **Test parçası:** 220×300 mm. 45° eğik kesim, 2/3 ve 1/3 derinlikte çizik, bant menteşe
2. 1:1 kağıt şablonlar
3. Kanat tabanı ve KFm şeritlerini kes
4. Ön şerit → çıta → arka şerit sırasıyla yapıştır, hücum kenarını 45° tıraşla
5. Polihedral kırım (53 mm takoz, ağırlık altında, en az 30 dk)
6. Kırım takviyeleri + bant
7. Alt çıta, sonra alt yüz bandı
8. Gövde: yan duvarlar → ekler → doubler'lar → firewall (epoksi) → ara bölmeler → alt panel
9. **BEC hattını masada test et:** ESC → kart 5V → servolar; seri logda `BROWNOUT` görülmemeli
10. Elektronik montaj, üst güverte
11. Kuyruk yüzeyleri, hizalayarak
12. Pushrod ve mekanik nötr ayarı; ardından arayüzden servo kalibrasyonu
13. Son CG ölçümü

Alt çıtayı 7. adımdan önce takma: kanadı masaya düz bastıramazsın ve burulma kalıcı olur.

Elektronik bağlantı şeması: [`diagrams/aircraft_wiring.svg`](diagrams/aircraft_wiring.svg)
ve [`firmware/flight_software/docs/kablolama.html`](../firmware/flight_software/docs/kablolama.html).
