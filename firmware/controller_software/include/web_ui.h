// ============================================================
// web_ui.h - Kumanda web arayuzu (tek sayfa, PROGMEM'de)
//
// Ayri dosyada olmasinin sebebi main.cpp'yi okunur tutmak: sayfa ~50 KB
// ve ucus mantigi ile hicbir sekilde karismamasi gerekiyor.
//
// ============================================================
// A. OLCU TEK YERDEN GELIR  (--appW / --appH)
// ============================================================
// Mobil tarayicida "100%" ve "vh" GORUNEN yuksekligi vermez; adres/gezinme
// cubugu KAPALIYKEN ki yuksekligi verir. Sayfa overflow:hidden oldugu icin
// alta tasan kisma kaydirmak da mumkun degil - kollar cubugun arkasinda
// kayboluyordu. Bu yuzden gorunur alan JS ile visualViewport'tan olculur ve
// TEK kaynak olarak --appW/--appH degiskenlerine yazilir; butun boyutlar
// (kol yuvasi, sutun genisligi, esikler) bu ikisinden turer. dvh destegi
// varsa JS gelene kadar o kullanilir, yoksa vh'ye duser.
//
// Kirilim noktalari da @media yerine JS'in <html>'e yazdigi siniflardan
// okunur (dikey / kisa / cokKisa / dar): @media de ayni yanilan yukseklik
// degerini kullanir, olculen deger ise gercektir.
//
// ============================================================
// B. UCUS EKRANI - ALET PANELI KURALLARI
// ============================================================
// 1) SUS YOK. Gradyan, golge, gecis animasyonu yok. Hareket eden tek sey
//    veriyi temsil eder (kol tokmagi, cubuklar, basili-tut dolgusu).
//
// 2) RENK ANLAM TASIR, SUS DEGILDIR.
//      kirmizi = UYARI, hemen mudahale (baglanti yok, failsafe)
//      amber   = DIKKAT, ucusu keser ama acil degil (zayif link, trim)
//      yesil   = normal / hazir
//    Bu yuzden "telefonu yan cevir" gibi bilgi notlari NOTR renktedir;
//    amber'i bilgi icin harcamak gercek dikkat durumunu korlestirir.
//    Renk tek basina hicbir zaman tek isaret degildir - yaninda metin var.
//
// 3) DEGERLER YERINDEN OYNAMAZ. Tabular rakam + sabit genislik: 999 -> 1000
//    gecisinde satir titremez, goz sabit noktaya bakar.
//
// 4) KOL DUZENI - uc mod. Ucak 3 kanalliyken aileron ekseni bos kalir;
//    degisen sey BOS EKSENIN YERI:
//      MOD 1  sol: rudder + elevator   sag: gaz
//      MOD 2  sol: rudder + gaz        sag: elevator      (varsayilan)
//      MOD 3  sol: yalnizca gaz        sag: rudder + elevator
//    Tek eksenli yuva KOL (lever) olarak cizilir - dar, yuvarlak uclu.
//    Iki eksenli yuva yuvarlatilmis kare gimbal kapisi olarak cizilir.
//    Hangi yuvanin yana hareket ettigi bakisla anlasilir.
//
// 5) NEDEN TAM DAIRE DEGIL: gercek gimbal kapisi kare gibidir, cunku tam
//    gazdayken tam rudder verebilmek gerekir. Daire kapida bu ikisi ayni
//    anda %71'e duserdi - kalkis tirmanisinda yan ruzgar duzeltmesi icin
//    kabul edilemez. Bu yuzden KAPI yuvarlatilmis kare, sadece TOKMAK
//    yuvarlak.
//
// 6) BAGLI (anchored) KOL. Parmagin dokundugu an kolun O ANKI degeri o
//    noktaya sabitlenir; komut olan sey dokunustan SONRAKI harekettir.
//    Ayrintili gerekce makeGimbal'in ustunde.
//
// 7) KANAL SATIRLARI KOLLARI YANSITIR. Ust satir SOL kolun eksenleri, alt
//    satir SAG kolunkiler; "bu sayi hangi parmagimda" sorusu bakisla
//    cevaplanir. Yan faydasi: dort kanal iki satira siginca orta sutun
//    yariya iner, ACIL DURDURMA telefonda ekran disina tasmaz.
//
// 8) ACIL DURDURMA HER ZAMAN AYNI YERDE VE GORUNUR. Yer daralinca kanal
//    satirlari kayar (kendi kaydirma alani var), buton asla itilmez.
//
// 9) KAZARA DOKUNMAYA KARSI: bagli kol + ayni yuvada tek parmak + ARM ve
//    ACIL icin basili tutma + ARMED iken ayar kilidi + geri hareketi ve
//    uzun basma menusunun yutulmasi.
//
// 10) GECIKME. Kol verisi zamanlayiciyla degil dokunma olayinin kendisinde
//    gonderilir (en fazla 10 ms'de bir).
//
// 11) EKRAN KILIDI VE BILDIRIMLER UCUSU KESMEZ (Wake Lock; blur hicbir
//    kanali degistirmez).
//
// 12) TASIMA: WebSocket (port 81), kurulamazsa HTTP yoklamasi.
//
// ============================================================
// C. AYAR EKRANI - GORUNMESI GEREKEN KADARI
// ============================================================
// Ayarlar dort sekmeye ayrildi: KONTROL (kalkis oncesi liste, varsayilan
// sekme), UCAK, KOLLAR, KALIBRASYON. Sebep: on iki kart alt alta tek bir
// listede duruyordu; ucus oncesi bakilacak sey ile ayda bir dokunulacak
// servo ucu ayni gorsel agirliktaydi.
//
// GORUNURLUK UCAK TIPINE BAGLI. 3 kanal secince aileron oran/expo, aileron
// kalibrasyonu ve aileron trim'i EKRANDA YOKTUR - kapali degil, yok. 4 kanal
// secince de "3 Kanal" kol duzeni yok olur, cunku aileronlu ucakta bos eksen
// kalmaz. Ilgisiz ayarin gri gorunmesi bile "acaba bunu mu unuttum" diye
// zaman kaybettiriyor; havacilikta gosterge ya gecerlidir ya orada degildir.
//
// TEHLIKELI OLAN AYRI DURUR. Fabrika ayarlari ve ESC gaz araligi darbesi
// kendi kartinda, kirmizi kenarlikli; ESC darbesi ayrica "pervaneyi soktum"
// kutusu isaretlenmeden basilamaz.
// ============================================================
#pragma once
#include <Arduino.h>

static const char PAGE_INDEX[] PROGMEM = R"HTMLPAGE(
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no,viewport-fit=cover">
<meta name="theme-color" content="#0e1116">
<title>RC Plane Kumanda</title>
<!-- ANA EKRANA EKLENINCE TARAYICI CUBUGU HIC GELMEZ.
     Tam ekran API'si iPhone'da calismaz (Safari yalnizca videoya izin verir),
     Android'de de kullanici cikinca geri gelir. Kalici cozum sayfanin
     uygulama gibi acilmasi: asagidaki etiketler + /manifest.json bunu saglar.
     Ana ekrandaki kisayoldan acilinca adres cubugu ve sekme seridi yoktur,
     ~90 px yukseklik dogrudan kollara gider. -->
<meta name="mobile-web-app-capable" content="yes">
<meta name="apple-mobile-web-app-capable" content="yes">
<meta name="apple-mobile-web-app-status-bar-style" content="black-translucent">
<meta name="apple-mobile-web-app-title" content="Kumanda">
<link rel="manifest" href="/manifest.json">
<link rel="apple-touch-icon" href="/icon.svg">
<style>
/* ============================================================
   OLCU KAYNAGI
   JS her degisimde --appW/--appH'yi PIKSEL olarak yazar (visualViewport).
   Buradakiler yalnizca JS calisana kadarki taban degerler.
   ============================================================ */
:root{ --appW:100vw; --appH:100vh; }
@supports (height:100dvh){ :root{ --appH:100dvh; } }

/* ============================================================
   TEMA
   Acik tema varsayilan; cihaz koyu tercih ediyorsa ona doner. Ust seritteki
   tus ikisini de elle secmeye izin verir, secim tarayicida saklanir.
   Iki temada da en sonuk metin zemine karsi >= 4.5:1, degerler >= 7:1.
   ============================================================ */
:root{
  --bg:#f1f3f7; --surface:#ffffff; --surface2:#e9edf3; --line:#ccd3dc;
  --fg:#0e1319; --muted:#586472;
  /* YAZI/CIZGI tonlari - beyaz zeminde >= 4.5:1 olmak zorunda, o yuzden koyu */
  /* accent, kol tokmagini ve vurgulu yaziyi suruyor; iki zemini birden
     gormesi gerekiyor: beyaz kart (--surface) ve gimbal zemini (--gimbal).
     #2f74e8 beyazda 4.39, gimbalde 4.16 veriyordu - ikisi de AA esiginin
     (4.5) altinda. Bir ton koyultmak ikisini birden gecirdi:
     surface 4.98, gimbal 4.72. */
  --accent:#2a6cd6; --ok:#0d7a3c; --warn:#9a5400; --bad:#c1271d;
  /* GRAFIK tonu - yalnizca gaz cubugu ve tokmagi. Eskiden yazi tonuyla ayni
     koyu kahveydi (#9a5400) ve acik zeminde camur gibi duruyordu. */
  /* thr GRAFIK tonu (gaz cubugu + tokmagi), yazi degil. #e08600 gimbal
     zemininde yalnizca 2.63 veriyordu - grafik ogeler icin gereken 3:1'in
     bile altinda. Bir ton koyultmak 3.46'ya cikardi. Daha fazla
     koyultmadim: yazi esigi olan 4.5'e cikarmak icin ~#a85c00 gerekiyor ve
     o ton acik zeminde camur gibi duruyor (dosya gecmisinde ayni sebeple
     bir kez geri alinmis). Esik tartismasi tools/test_theme.py'de. */
  --thr:#c96f00; --gimbal:#f7f9fc; --grid:#dde4ec; --knobRing:#ffffff;
  --shade:rgba(0,0,0,.30);
  /* DOLGULU TUSLAR - dolgu / cizgi / uzerindeki yazi ucusu.
     Acik tema acik tonlarla calisir: solgun zemin, koyu yazi, ayni ailenin
     cizgisi. Doygun koyu dolgu + beyaz yazi acik temada agir duruyordu.
     Ucluler sayesinde ton acilirken okunaklik (>= 6:1) korunuyor ve iki
     temanin YAPISI ayni kaliyor - degisen yalnizca tonlar. */
  --okFill:#c1ecd4;   --okLine:#6cc79a;   --okInk:#07542a;
  --warnFill:#ffe0b3; --warnLine:#e8ab52; --warnInk:#7a3d00;
  --badFill:#ffcac2;  --badLine:#ea8578;  --badInk:#98180e;
  --accFill:#d8e6fd;  --accLine:#82b0f4;  --accInk:#0a4cb0;
}
@media (prefers-color-scheme: dark){
  :root:not([data-theme="light"]){
    --bg:#0e1116; --surface:#161a20; --surface2:#1d222a; --line:#2f3742;
    --fg:#f0f3f7; --muted:#9aa7b6;
    --accent:#69a6ff; --ok:#43d97f; --warn:#ffb545; --bad:#ff6257;
    --thr:#ff9f42; --gimbal:#0a0d11; --grid:#2b333d; --knobRing:#0e1116;
    --shade:rgba(0,0,0,.45);
    --okFill:#43d97f;   --okLine:#43d97f;   --okInk:#04250f;
    --warnFill:#ffb545; --warnLine:#ffb545; --warnInk:#3a2200;
    --badFill:#ff6257;  --badLine:#ff6257;  --badInk:#3d0803;
    --accFill:#69a6ff;  --accLine:#69a6ff;  --accInk:#04224a;
  }
}
:root[data-theme="dark"]{
  --bg:#0e1116; --surface:#161a20; --surface2:#1d222a; --line:#2f3742;
  --fg:#f0f3f7; --muted:#9aa7b6;
  --accent:#69a6ff; --ok:#43d97f; --warn:#ffb545; --bad:#ff6257;
  --thr:#ff9f42; --gimbal:#0a0d11; --grid:#2b333d; --knobRing:#0e1116;
  --shade:rgba(0,0,0,.45);
  /* Koyu temada dolgular zaten doygun ve zemin koyu: gorunum degismiyor.
     Cizgi = dolgunun kendisi, boylece kenarlik iki temada ayni yer kaplar. */
  --okFill:#43d97f;   --okLine:#43d97f;   --okInk:#04250f;
  --warnFill:#ffb545; --warnLine:#ffb545; --warnInk:#3a2200;
  --badFill:#ff6257;  --badLine:#ff6257;  --badInk:#3d0803;
  --accFill:#69a6ff;  --accLine:#69a6ff;  --accInk:#04224a;
}

/* ============================================================
   OLCEK
   Kol yuvasi iki siniri birden gozetir:
   1) GENISLIK - orta sutuna her zaman ~336 px kalmali; iki kanal karti yan
      yana ancak o zaman siginiyor. Yuzdeyle ("32vw") bu garanti edilemez:
      dar telefonda yuva buyuyup kanallari alt alta dusuruyor, genis ekranda
      gereksiz kucuk kaliyordu.
   2) YUKSEKLIK - gorunur yukseklikten ust serit + dolgu + eksen etiketi
      dusulur. Boylece yuva HICBIR kosulda ust seride ya da tarayici
      cubuguna tasamaz; formul zaten tasmayi mumkun kilmiyor.
   ============================================================ */
:root{
  --well:clamp(112px, min( calc((var(--appW) - 336px)/2),
                           calc(var(--appH) - 104px) ), 300px);
  --midMax:560px;
}
html.dikey{
  /* Dikeyde iki yuva yan yana ekranin altinda; sinir genislik. Yukseklik
     terimi de kanal satirlari + ACIL DURDURMA icin 296 px ayirir. */
  --well:clamp(112px, min( calc((var(--appW) - 36px)/2),
                           calc(var(--appH) - 296px) ), 280px);
}
/* Cok alcak ekranda yalnizca YUKSEKLIK payi kisilir (dolgular da kisildigi
   icin); genislik terimi aynen durur. Onceki hali genislik terimini tumden
   birakiyordu ve 580x244 gibi bir ekranda yuva buyuyup orta sutunu iki
   kartin altina dusuruyordu - tam da onlemesi gereken sey. */
html.cokKisa{ --well:clamp(104px, min( calc((var(--appW) - 336px)/2),
                                       calc(var(--appH) - 88px) ), 300px); }

*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
html,body{height:var(--appH);overflow:hidden;overscroll-behavior:none}
body{margin:0;background:var(--bg);color:var(--fg);
     font:400 13px/1.4 ui-sans-serif,system-ui,-apple-system,"SF Pro Text",
          "Segoe UI Variable Text","Segoe UI",Roboto,"Helvetica Neue",Arial,sans-serif;
     font-variant-numeric:tabular-nums;letter-spacing:-.005em;
     -webkit-font-smoothing:antialiased;text-rendering:optimizeLegibility;
     user-select:none;-webkit-user-select:none;-webkit-touch-callout:none;
     padding:env(safe-area-inset-top) env(safe-area-inset-right) env(safe-area-inset-bottom) env(safe-area-inset-left)}
.app{display:flex;flex-direction:column;height:100%}

.lbl{font-size:9px;font-weight:650;letter-spacing:.11em;text-transform:uppercase;
     color:var(--muted)}
.num{font-variant-numeric:tabular-nums;font-feature-settings:"tnum" 1;letter-spacing:0}
button{font:inherit}
button:disabled{opacity:.38}
.ghost{background:var(--surface);border:1px solid var(--line);color:var(--fg);
     border-radius:9px;font:inherit;font-size:11.5px;font-weight:650;
     padding:7px 11px;touch-action:manipulation}
.ghost:active{background:var(--surface2)}

/* ============================================================
   UST SERIT
   Sabit duzen: ARM solda, gostergeler ortada, tuslar sagda. Konumu hicbir
   ekran boyunda degismez - kas hafizasi ancak boyle olusur.
   ============================================================ */
.hud{display:flex;align-items:center;gap:8px;padding:6px 8px;flex:0 0 auto;
     background:var(--surface);border-bottom:3px solid var(--line);
     position:relative;z-index:1}
/* Ana ikaz: uyari varken ust seridin alt cizgisi renklenir. Serit her zaman
   goz alaninda oldugu icin banner kaymis olsa bile durum belli olur.
   Cizgi KALICI olarak 3 px ve yalnizca RENGI degisiyor. Once 1 px kenarlik
   + inset box-shadow ile yapiliyordu; ucus ekraninda golge yasak (kosum
   bunu denetliyor) ve kenarlik kalinligini uyari aninda degistirmek 2 px
   reflow yaratirdi. Kalici kalinlik ikisini de cozuyor. */
body.dikkat .hud{border-bottom-color:var(--warn)}
body.ikaz   .hud{border-bottom-color:var(--bad)}

#armBtn{flex:0 0 auto;min-width:104px;border:1px solid transparent;
     border-radius:10px;padding:7px 12px;letter-spacing:.06em;text-align:center}
.b-arm  {background:var(--okFill);   border-color:var(--okLine);   color:var(--okInk)}
.b-armed{background:var(--warnFill); border-color:var(--warnLine); color:var(--warnInk)}

.stats{display:flex;flex:1 1 auto;min-width:0;gap:0}
.stat{flex:1 1 0;min-width:38px;text-align:center;line-height:1.15;padding:0 4px}
/* Ucak durumu en genis pay: digerleri en fazla 5 karakter ("7.84V"), bu ise
   "failsafe" yazabiliyor. Esit paylasimda dar telefonda tam da en kritik
   gosterge kirpiliyordu. */
.stats .stat:first-child{flex:1.5 1 0}
.stat + .stat{border-left:1px solid var(--line)}
.stat b{display:block;font-size:8.5px;font-weight:650;letter-spacing:.1em;
     text-transform:uppercase;color:var(--muted);
     white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.stat span{display:block;font-size:14px;font-weight:600;letter-spacing:-.01em;
     font-variant-numeric:tabular-nums}
/* Link kalitesi: sayinin altinda ince analog cubuk. Sayi kesin degeri,
   cubuk egilimi verir - ikisi birlikte tek bakista okunur. */
.qbar{display:block;height:2px;margin:2px 3px 0;background:var(--surface2);
     border-radius:1px;overflow:hidden}
.qbar i{display:block;height:100%;width:0;background:var(--ok)}
.qbar i.lo{background:var(--bad)}
.c-ok{color:var(--ok)} .c-bad{color:var(--bad)} .c-warn{color:var(--warn)}

.hudbtn{flex:0 0 auto;display:flex;gap:6px}
.ico{display:inline-flex;align-items:center;justify-content:center;
     width:34px;height:34px;padding:0}
.ico svg{width:17px;height:17px;display:block}

/* IKON DEGISIMI - hepsi ".ico svg.X" bicminde yazilir.
   DIKKAT: ".ico svg" kuralinin ozgullugu (0,1,1), tek sinifla yazilan bir
   ".ic-cmp{display:none}" (0,1,0) kuralindan YUKSEK. Tek sinifla yazilinca
   gizleme tutmuyor ve iki ikon ust uste biniyor. Tema ikonlarinda gozden
   kacmisti: orada [data-theme] kurallari ozgullugu yukselttigi icin sorun
   ancak betik calisana kadarki ilk karede goruluyordu.

   Tam ekran: disari bakan oklar = gir, iceri bakan oklar = cik. */
.ico svg.ic-cmp{display:none}
html.tamEkran .ico svg.ic-exp{display:none}
html.tamEkran .ico svg.ic-cmp{display:block}

/* TEMA TUSU SU ANKI TEMAYI GOSTERIR: koyu -> ay, acik -> gunes. Once tersi
   vardi (gidilecek temanin ikonu) ve ayar tusunun ikonu da isinli bir daire
   oldugu icin seritte iki tane gunes varmis gibi duruyordu. Ayar tusu artik
   sliderlarla cizilir. Sira: once sistem tercihi, sonra elle secim. */
.ico svg.ic-moon{display:none}
@media (prefers-color-scheme:dark){
  :root:not([data-theme="light"]) .ico svg.ic-sun{display:none}
  :root:not([data-theme="light"]) .ico svg.ic-moon{display:block}
}
:root[data-theme="light"] .ico svg.ic-sun{display:block}
:root[data-theme="light"] .ico svg.ic-moon{display:none}
:root[data-theme="dark"]  .ico svg.ic-sun{display:none}
:root[data-theme="dark"]  .ico svg.ic-moon{display:block}

/* Basili-tut onayi. Dolgu JS'ten surulur: ucus ekraninda CSS animasyonu
   yok, bu bir ilerleme gostergesi.

   touch-action:none ZORUNLU. Varsayilan (auto) ile tarayici 600 ms'lik
   basili tutmayi kaydirma/yakinlastirma jesti sanip dokunmayi sahipleniyor
   ve pointercancel firlatiyor; basiliTut() bunu iptal olarak okuyup
   vazgeciyor. Sonuc: pilot ARM'a basili tutuyor, tus doluyor ve hicbir sey
   olmuyor. Kol yuvalari (#wellL/#wellR) ayni sebeple none kullaniyor. */
.hold{position:relative;overflow:hidden;touch-action:none}
.hold > i{position:absolute;left:0;top:0;bottom:0;width:0;
     background:var(--shade);pointer-events:none}
.hold > b{position:relative;display:block;font-size:13.5px;font-weight:750}
.hold > small{position:relative;display:block;font-size:8.5px;font-weight:600;
     letter-spacing:.1em;opacity:.85}

/* Dar telefon: serit TEK SATIR kalir, ogeler kuculur. Iki katli serit
   dikeyde cok yer yiyordu. */
html.dar .hud{gap:5px;padding:5px 6px}
html.dar #armBtn{min-width:72px;padding:5px 8px}
html.dar .hold > b{font-size:12px}
html.dar .hold > small{font-size:7px;letter-spacing:.06em}
/* Ust seritte artik UC ikon tusu var (tam ekran + tema + ayar) ve 360 px'lik
   telefonda gostergelere ~170 px kaliyor. Bes gostergeyi sikistirmak
   basliklari "KA..." diye kirpiyordu: okunmayan gosterge zaten gosterge
   degil. Dar ekranda KAYIP gizleniyor - tek teshis degeri ve ayni bilgiyi
   LINK zaten veriyor; tam hali Ayarlar > Kontrol'de duruyor. Kalan dordu
   (ucak durumu, link, batarya, gaz) rahat okunacak genislige kavusuyor. */
html.dar .stat.teshis{display:none}
html.dar .stat{min-width:0;padding:0 1px;overflow:hidden}
html.dar .stat b{font-size:7.5px;letter-spacing:.02em}
html.dar .stat span{font-size:11.5px;white-space:nowrap}
html.dar .hudbtn{gap:4px}
html.dar .ico{width:29px;height:29px}
html.dar .ico svg{width:16px;height:16px}
html.cokKisa .hud{padding:4px 6px}

/* ============================================================
   UCUS ALANI
   ============================================================ */
.fly{flex:1 1 auto;display:flex;align-items:flex-end;justify-content:space-between;
     gap:12px;padding:10px 14px 12px;min-height:0}
.side{flex:0 0 auto;display:flex;flex-direction:column;align-items:center;gap:6px}

/* Orta sutun: uyari ustte, kanallar ortada, ACIL DURDURMA en altta.
   justify-content:flex-end butun kumeyi kollarin alt hizasina ceker; buton
   her ekranda ayni yerde, bas parmagin altinda kalir.
   overflow:hidden + kaydirilabilir .chwrap: yer daralinca KANALLAR kayar,
   ACIL DURDURMA asla ekran disina itilmez. */
.mid{flex:1 1 0;align-self:stretch;display:flex;flex-direction:column;
     justify-content:flex-end;gap:7px;min-width:0;min-height:0;overflow:hidden;
     max-width:var(--midMax);margin-inline:auto}

/* DIKEY: iki satirli izgara - ustte kanallar (kalan butun yer), altta iki
   kol yan yana. Once flex-wrap vardi; sarilan satirlarin yuksekligi
   denetlenemedigi icin tasan kisim kirpiliyordu. Izgarada ust satir
   minmax(0,1fr): once O kisalir, kollar ve buton yerinde kalir. */
html.dikey .fly{display:grid;grid-template-columns:1fr 1fr;
     grid-template-rows:minmax(0,1fr) auto;align-items:end;justify-items:center;
     gap:10px;padding:10px 12px 12px}
html.dikey .mid{grid-area:1/1/2/3;width:100%;align-self:stretch}
html.dikey #sideL{grid-area:2/1/3/2}
html.dikey #sideR{grid-area:2/2/3/3}
html.cokKisa .fly{padding:6px 10px 8px;gap:8px}

/* ---------- uyari seridi ----------
   Modal yok: pilot uyariyi okurken kollari kullanmaya devam edebilmeli. */
/* Uyari her duzende orta sutunun EN USTUNDE durur (margin-bottom:auto).
   Kokpitteki gibi: ust bolge ikaz alanidir, sakinken bos durur; kumanda
   takimi (kanallar + ACIL DURDURMA) altta, elin oldugu yerde kalir.
   Uyarinin kanallarin hemen ustunde belirip her seyi kaydirmasi, tam da
   dikkat gerektiren anda goz hedefini oynatiyordu. */
#alert{display:none;flex:0 0 auto;margin-bottom:auto;border-radius:10px;
       padding:9px 12px;text-align:center;border:1px solid var(--bad);
       background:var(--surface)}
#alert.on{display:block}
#alert.warn{border-color:var(--warn)}
#alert b{display:block;font-size:clamp(14px,2.6vw,19px);font-weight:700;
         line-height:1.15;color:var(--bad)}
#alert.warn b{color:var(--warn)}
#alert i{display:block;font-style:normal;font-size:10.5px;font-weight:500;
         color:var(--muted);margin-top:2px}
/* Kisa ekranda uyari uc satir yer kaplayip kanallari ekran disina itiyordu;
   tek satira iner. */
html.kisa #alert{padding:5px 9px;text-align:left}
html.kisa #alert.on{display:flex;align-items:baseline;gap:8px}
html.kisa #alert b{font-size:14px}
html.kisa #alert i{margin:0;flex:1 1 auto;min-width:0}

/* Bilgi notu - UYARI DEGIL, o yuzden notr renkte (bkz. tasarim notu B2). */
#rot{display:none;flex:0 0 auto;text-align:center;font-size:10px;font-weight:600;
     letter-spacing:.06em;color:var(--muted)}
html.dikey #rot{display:block}
html.dikey.kisa #rot{display:none}

/* ============================================================
   KANAL SATIRLARI
   Satirlar kol duzenini yansitir (tasarim notu B7). Satiri dolduran kanal
   sayisini JS belirler, kart iki duzenden birine girer:

     GENIS (satirda tek kanal)    ELEVATOR  [-] ======== [+]  1500
     KOMPAKT (satirda iki kanal)  ELEVATOR          1500
                                  [-] ========== [+]

   Trim tuslari cubugun IKI YANINDA: sol tus cubugu sola goturur. Eskiden
   ayri bir kume halindeydi - hem yer yiyor hem hangi tusun ne yaptigi ancak
   denenerek anlasiliyordu.
   ============================================================ */
.chwrap{flex:0 1 auto;min-height:0;overflow-y:auto;-webkit-overflow-scrolling:touch;
    display:flex;flex-direction:column;gap:6px}
/* flex-wrap + 122 px taban: kompakt kartin okunur kaldigi en dar olcu
   122 px. Daha dar bir orta sutun cikarsa kartlar kendiliginden alt alta
   gecer - kural hicbir ekranda okunakligi bozmaz. */
.chgrp{display:flex;flex-wrap:wrap;gap:6px}
.chgrp:empty{display:none}
.chgrp > .ch{flex:1 1 122px}

.ch{display:flex;align-items:center;gap:7px;min-width:0;background:var(--surface);
    border:1px solid var(--line);border-radius:10px;padding:4px 8px}
.ch > .lbl{flex:0 1 auto;min-width:0;white-space:nowrap;overflow:hidden;
    text-overflow:ellipsis}
.ch .val{flex:0 0 auto;min-width:38px;text-align:right;font-style:normal;
    font-size:13px;font-weight:600;font-variant-numeric:tabular-nums}
/* Trim rozeti: sifirken hic yer kaplamaz, sifir disinda amber. Farkedilmeden
   trim'li ucusa cikmak ilk ucuslarda en sik yapilan hata. */
.ch .val s{text-decoration:none;margin-left:3px;font-size:9.5px;font-weight:700;
    color:var(--warn);letter-spacing:0}
.ch .val s:empty{display:none}

.track{position:relative;flex:1 1 auto;min-width:34px;height:6px;border-radius:3px;
       background:var(--surface2);overflow:hidden}
.track i{position:absolute;top:0;bottom:0;background:var(--accent);border-radius:3px}
.track i.t{background:var(--thr)}
.track u{position:absolute;top:0;bottom:0;left:50%;width:1px;background:var(--line)}

.tb{flex:0 0 auto;width:26px;height:24px;padding:0;background:var(--surface2);
    border:1px solid var(--line);color:var(--fg);border-radius:7px;
    font-size:14px;font-weight:700;line-height:1;touch-action:manipulation}
.tb:active{background:var(--line)}

/* Kompakt duzen: ayni DOM, iki satir. Izgara alanlari kullanildigi icin DOM
   sirasina dokunmadan yerlesim degisiyor. */
.ch.cmp{display:grid;align-items:center;gap:3px 6px;padding:5px 8px 6px;
    grid-template-columns:auto minmax(0,1fr) auto;
    grid-template-areas:"lbl lbl val" "dec bar inc"}
.ch.cmp > .lbl{grid-area:lbl}
.ch.cmp .val{grid-area:val;text-align:right}
.ch.cmp .track{grid-area:bar}
.ch.cmp .tb.dec{grid-area:dec}
.ch.cmp .tb.inc{grid-area:inc}

html.dar .ch{gap:5px;padding:4px 6px}
html.dar .ch.cmp{padding:4px 6px 5px;gap:2px 5px}
html.dar .ch > .lbl{font-size:8.5px;letter-spacing:.08em}
html.dar .ch .val{min-width:34px;font-size:12.5px}
html.dar .tb{width:24px}
html.kisa .chwrap{gap:5px}
html.kisa .ch{padding:3px 8px}
html.kisa .ch.cmp{padding:4px 8px 5px}

/* ACIL DURDURMA da acik tonda ama kirmizi ailesinde ve ekrandaki TEK kirmizi
   dolgu: solgunlugu taninirligini bozmuyor, yazisi koyu kirmizi ve kalin.
   Basili-tut dolgusu (--shade) acik zeminde de gorunur. */
#stopBtn{flex:0 0 auto;border:1px solid var(--badLine);border-radius:10px;
     background:var(--badFill);color:var(--badInk);padding:9px 6px;
     letter-spacing:.06em;text-align:center}
#stopBtn:active{background:var(--badLine)}
html.kisa #stopBtn{padding:7px 6px}
/* Dikeyde (ve kisa olmayan ekranda) yer bol: kumanda takiminin dokunma
   hedefleri buyur. Ucus sirasinda iskalanan bir trim tusu bir sonraki
   denemeye mal olur; bos piksel bekletmenin anlami yok. */
html.dikey:not(.kisa) #stopBtn{padding:14px 6px}
html.dikey:not(.kisa) .tb{width:30px;height:28px}
html.dikey:not(.kisa) .ch{padding:6px 9px}
html.dikey:not(.kisa) .ch.cmp{padding:7px 9px 8px}

/* ============================================================
   GIMBAL VE KOL
   ============================================================ */
.well{position:relative;width:var(--well);height:var(--well);
      background:var(--gimbal);border:1px solid var(--line);
      border-radius:20%;touch-action:none}
.well.active{border-color:var(--accent)}
.well.limit{border-color:var(--warn)}
/* Tek eksenli yuva = KOL. Dar, yuvarlak uclu; yana hareket etmedigi
   sekilden anlasilsin diye bilerek gimbalden farkli cizilir. */
.well.lever{width:calc(var(--well)*.44);min-width:74px;border-radius:999px}

.grid{position:absolute;inset:0;pointer-events:none;overflow:hidden;
      border-radius:inherit}
.grid i{position:absolute;background:var(--grid)}
.grid i.h{left:12%;right:12%;top:50%;height:1px}
.grid i.v{top:12%;bottom:12%;left:50%;width:1px}
.grid u{position:absolute;left:50%;top:50%;width:34%;height:34%;
     margin:-17% 0 0 -17%;border:1px solid var(--grid);border-radius:50%}
.well.lever .grid i.h{display:none}
.well.lever .grid u{display:none}
.well.lever .grid i.v{top:7%;bottom:7%;width:3px;margin-left:-1px;
     border-radius:2px;background:var(--surface2)}

/* Tokmak ORTALAMASI transform ile yapilir, yuzde margin ile DEGIL. Yuzde
   margin hem yatayda hem dikeyde KAPSAYICININ GENISLIGINE gore cozulur; kol
   yuvasi kare olmadigi icin dikey ortalama kayiyor ve tokmak disari
   tasiyordu. translate(-50%,-50%) elemanin KENDI boyutuna gore calisir. */
.knob{position:absolute;left:50%;top:50%;width:30%;height:30%;
      border-radius:50%;background:var(--accent);
      border:2px solid var(--knobRing);pointer-events:none;will-change:transform}
.knob.thr{background:var(--thr)}
.well.lever .knob{width:74%;height:11%;border-radius:999px}

.axlbl{display:flex;justify-content:space-between;gap:10px;width:100%;padding:0 2px}
.axlbl s{text-decoration:none;font-size:9.5px;font-weight:700;
       letter-spacing:.08em;color:var(--muted)}
.axlbl s.off{opacity:.42}
.side.tek .axlbl{justify-content:center}
.side.tek .axlbl s.off{display:none}

/* ============================================================
   AYAR EKRANI
   position:fixed - body'nin guvenli alan dolgusundan ve overflow:hidden
   kirpmasindan bagimsiz; kendi payini kendi verir. Yuksekligi de olculen
   gorunur yukseklik: ucus ekraninda cozulen tasma sorunu burada da var.
   ============================================================ */
.cal{position:fixed;left:0;top:0;right:0;height:var(--appH);
     background:var(--bg);overflow-y:auto;-webkit-overflow-scrolling:touch;
     display:none;z-index:20;
     padding:0 calc(12px + env(safe-area-inset-right))
             calc(28px + env(safe-area-inset-bottom)) calc(12px + env(safe-area-inset-left))}
.cal.show{display:block}

.calhead{position:sticky;top:0;z-index:2;background:var(--bg);
     padding-top:env(safe-area-inset-top);margin-bottom:12px;
     border-bottom:1px solid var(--line)}
.calbar{display:flex;align-items:center;gap:8px;padding:9px 0;
     max-width:1180px;margin:0 auto}
.calbar h2{margin:0;font-size:13px;font-weight:700;letter-spacing:.06em}
.spacer{flex:1}
.tag{font-size:9.5px;font-weight:700;letter-spacing:.08em;text-transform:uppercase;
     padding:3px 8px;border-radius:999px;border:1px solid var(--line);color:var(--muted);
     white-space:nowrap}
.tag.on{color:var(--warn);border-color:var(--warn)}
.tag[hidden]{display:none}

/* Sekmeler: dort bolum, ayni anda biri gorunur. Ucus oncesi bakilacak liste
   ile ayda bir dokunulacak servo ucu ayni gorsel agirlikta olmasin diye. */
.tabs{display:flex;gap:3px;max-width:560px;margin:0 auto 10px;padding:3px;
     background:var(--surface2);border:1px solid var(--line);border-radius:11px}
.tabs button{flex:1 1 0;min-width:0;background:transparent;border:1px solid transparent;
     color:var(--muted);border-radius:8px;padding:8px 4px;font-size:11.5px;
     font-weight:650;touch-action:manipulation;white-space:nowrap;
     overflow:hidden;text-overflow:ellipsis}
.tabs button.on{background:var(--surface);border-color:var(--line);color:var(--fg)}

.panel{display:none}
.panel.on{display:grid;gap:10px;align-items:start;max-width:1180px;margin:0 auto;
     grid-template-columns:repeat(auto-fill,minmax(290px,1fr))}

.card{background:var(--surface);border:1px solid var(--line);border-radius:12px;
      padding:12px}
.card h3{margin:0 0 10px;font-size:10px;font-weight:700;letter-spacing:.1em;
      text-transform:uppercase;color:var(--muted);
      display:flex;align-items:center;gap:8px}
.card h3 em{margin-left:auto;font-style:normal;font-size:11px;font-weight:650;
      color:var(--fg);text-transform:none;letter-spacing:0}
.card.off{opacity:.5}
/* Geri donusu olmayan islemler kendi kartinda ve kirmizi kenarlikli durur. */
.card.tehlike{border-color:var(--bad)}
.card.tehlike h3{color:var(--bad)}
.card.genis{grid-column:1/-1}

.fld{display:flex;align-items:center;gap:10px;margin:9px 0}
.fld label{font-size:11px;color:var(--muted);width:82px;flex:0 0 auto;font-weight:600}
.fld em{font-style:normal;font-size:12px;font-weight:650;width:50px;text-align:right}
input[type=range]{flex:1;height:26px;-webkit-appearance:none;background:transparent}
input[type=range]::-webkit-slider-runnable-track{height:3px;border-radius:2px;background:var(--line)}
input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:20px;height:20px;
      margin-top:-8.5px;border-radius:50%;background:var(--accent);border:2px solid var(--surface)}
input[type=range]::-moz-range-track{height:3px;border-radius:2px;background:var(--line)}
input[type=range]::-moz-range-thumb{width:18px;height:18px;border-radius:50%;
      background:var(--accent);border:2px solid var(--surface)}

.btns{display:flex;gap:6px;flex-wrap:wrap;margin-top:10px}
.btns button{flex:1 1 auto;min-width:66px;background:var(--surface2);
      border:1px solid var(--line);color:var(--fg);border-radius:9px;padding:9px 6px;
      font-size:11.5px;font-weight:650;touch-action:manipulation}
.btns button:active{background:var(--line)}
.btns button.pri{background:var(--accFill);border-color:var(--accLine);color:var(--accInk)}
.btns button.dgr{border-color:var(--bad);color:var(--bad)}

.sw{display:flex;align-items:center;gap:9px;font-size:12px;margin:10px 0}
.sw input{width:18px;height:18px;accent-color:var(--accent)}
.note{font-size:11px;line-height:1.55;color:var(--muted);margin-top:9px}
.note b{color:var(--warn);font-weight:650}

.checks{list-style:none;margin:0;padding:0;font-size:12px}
.checks li{display:flex;align-items:center;gap:9px;padding:5px 0;color:var(--muted)}
.checks li::before{content:"";width:8px;height:8px;border-radius:50%;
      background:var(--bad);flex:0 0 auto}
.checks li.pass{color:var(--fg)}
.checks li.pass::before{background:var(--ok)}

/* Baglanti teshisi: etiket solda, deger sagda; hepsi ayni hizada. */
.rows{display:flex;flex-direction:column;gap:0;font-size:12px}
.rows div{display:flex;align-items:baseline;gap:10px;padding:5px 0}
.rows div + div{border-top:1px solid var(--line)}
.rows b{font-weight:600;color:var(--muted);font-size:11px}
.rows span{margin-left:auto;font-weight:650;font-variant-numeric:tabular-nums}

.seg{display:flex;gap:5px;flex-wrap:wrap}
.seg button{flex:1 1 0;min-width:110px;background:var(--surface2);
      border:1px solid var(--line);color:var(--muted);border-radius:9px;
      padding:9px 4px;font-size:11.5px;font-weight:650;line-height:1.35;
      touch-action:manipulation}
.seg button small{display:block;font-size:9px;font-weight:500;opacity:.85}
.seg button.on{background:var(--accFill);border-color:var(--accLine);color:var(--accInk)}
.seg button[hidden]{display:none}

/* Kol duzeni ozeti: hangi eksen hangi kolda, ayarin altinda yaziyla. */
.ozet{display:flex;gap:6px;margin-top:10px}
.ozet div{flex:1 1 0;border:1px solid var(--line);border-radius:9px;padding:8px}
.ozet b{display:block;font-size:9px;font-weight:700;letter-spacing:.1em;
      text-transform:uppercase;color:var(--muted);margin-bottom:3px}
.ozet span{font-size:12px;font-weight:650}

/* Dar ekran: etiket 82 px sabitken 360 px'lik ekranda slider'a 150 px
   kaliyordu. Sarmali duzende etiket ve deger ustte, slider kendi satirinda
   tam genislikte - dokunma hedefi buyuyor, deger okunur kaliyor. */
html.orta .card{padding:10px}
html.orta .fld{flex-wrap:wrap;gap:4px 10px;margin:12px 0}
html.orta .fld label{width:auto;flex:1 1 auto}
html.orta .fld em{width:auto;min-width:46px}
html.orta .fld input[type=range]{flex:1 0 100%;order:3;height:30px}
html.orta .seg button{flex:1 1 calc(50% - 3px);min-width:118px}
html.orta .btns button{min-width:calc(50% - 3px)}
html.orta .calbar h2{font-size:12px}
html.orta .tag{font-size:9px;padding:3px 6px}
html.orta .tabs button{font-size:11px;padding:8px 2px}
</style>

<div class="app">
  <div class="hud">
    <button id="armBtn" class="b-arm hold"><i></i><b>ARM</b><small>BASILI TUT</small></button>

    <div class="stats">
      <div class="stat"><b>Ucak</b><span id="hPlane" class="c-bad">yok</span></div>
      <div class="stat"><b id="hLinkYol">Link</b><span id="hLink">-</span>
        <u class="qbar"><i id="qLink"></i></u></div>
      <div class="stat teshis"><b>Kayip</b><span id="hLoss">-</span></div>
      <div class="stat"><b>Bat</b><span id="hVbat">-</span></div>
      <div class="stat"><b>Gaz</b><span id="hThr">0%</span></div>
    </div>

    <!-- Tema tusu: SU ANKI temanin ikonu. Ayar tusu: sliderlar. Ikisinin
         ikonu ayni aileden olmamali, yoksa iki tema tusu gibi duruyor. -->
    <div class="hudbtn">
      <button id="fsBtn" class="ghost ico" aria-label="Tam ekran" title="Tam ekran (yatay)"><svg class="ic-exp" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M8 3H5a2 2 0 0 0-2 2v3M16 3h3a2 2 0 0 1 2 2v3M8 21H5a2 2 0 0 1-2-2v-3M16 21h3a2 2 0 0 0 2-2v-3"/></svg><svg class="ic-cmp" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M8 3v3a2 2 0 0 1-2 2H3M16 3v3a2 2 0 0 0 2 2h3M8 21v-3a2 2 0 0 0-2-2H3M16 21v-3a2 2 0 0 1 2-2h3"/></svg></button>
      <button id="themeBtn" class="ghost ico" aria-label="Temayi degistir" title="Tema"><svg class="ic-sun" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><circle cx="12" cy="12" r="4"/><path d="M12 2v2M12 20v2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M2 12h2M20 12h2M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4"/></svg><svg class="ic-moon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M21 12.8A9 9 0 1 1 11.2 3a7 7 0 0 0 9.8 9.8z"/></svg></button>
      <button id="setBtn" class="ghost ico" aria-label="Ayarlar" title="Ayarlar"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><path d="M4 7h8M17 7h3M4 12h3M12 12h8M4 17h8M17 17h3"/><circle cx="14.5" cy="7" r="2.3"/><circle cx="9.5" cy="12" r="2.3"/><circle cx="14.5" cy="17" r="2.3"/></svg></button>
    </div>
  </div>

  <div class="fly">
    <div id="sideL" class="side">
      <div id="wellL" class="well">
        <div class="grid"><i class="h"></i><i class="v"></i><u></u></div>
        <div id="knobL" class="knob thr"></div>
      </div>
      <div class="axlbl"><s id="lblLX">Rudder</s><s id="lblLY">Gaz</s></div>
    </div>

    <div class="mid">
      <div id="alert"><b></b><i></i></div>
      <div id="rot">Telefonu yan cevirince kollar buyur</div>

      <!-- Kanal kartlari kol duzenine gore #grpL / #grpR arasinda dagitilir
           (kanallariDiz). Baslangicta hepsi ilk kutuda; applyMode acilista
           yerlerine koyar. -->
      <div class="chwrap">
        <div class="chgrp" id="grpL">
          <div class="ch" id="chThr">
            <span class="lbl">Gaz</span>
            <button class="tb dec" data-trim="Thr" data-d="-5" aria-label="Gaz trim eksi">&minus;</button>
            <div class="track"><i id="bThr" class="t" style="left:0;width:0"></i></div>
            <button class="tb inc" data-trim="Thr" data-d="5" aria-label="Gaz trim arti">+</button>
            <em class="val"><span id="vThr">1000</span><s id="trThr"></s></em>
          </div>
          <div class="ch" id="chEle">
            <span class="lbl">Elevator</span>
            <button class="tb dec" data-trim="Ele" data-d="-5" aria-label="Elevator trim eksi">&minus;</button>
            <div class="track"><u></u><i id="bEle" style="left:50%;width:0"></i></div>
            <button class="tb inc" data-trim="Ele" data-d="5" aria-label="Elevator trim arti">+</button>
            <em class="val"><span id="vEle">1500</span><s id="trEle"></s></em>
          </div>
          <div class="ch" id="chRud">
            <span class="lbl">Rudder</span>
            <button class="tb dec" data-trim="Rud" data-d="-5" aria-label="Rudder trim eksi">&minus;</button>
            <div class="track"><u></u><i id="bRud" style="left:50%;width:0"></i></div>
            <button class="tb inc" data-trim="Rud" data-d="5" aria-label="Rudder trim arti">+</button>
            <em class="val"><span id="vRud">1500</span><s id="trRud"></s></em>
          </div>
          <div class="ch" id="chAil" style="display:none">
            <span class="lbl">Aileron</span>
            <button class="tb dec" data-trim="Ail" data-d="-5" aria-label="Aileron trim eksi">&minus;</button>
            <div class="track"><u></u><i id="bAil" style="left:50%;width:0"></i></div>
            <button class="tb inc" data-trim="Ail" data-d="5" aria-label="Aileron trim arti">+</button>
            <em class="val"><span id="vAil">1500</span><s id="trAil"></s></em>
          </div>
        </div>
        <div class="chgrp" id="grpR"></div>
      </div>

      <button id="stopBtn" class="hold"><i></i><b>ACIL DURDURMA</b><small>BASILI TUT</small></button>
    </div>

    <div id="sideR" class="side">
      <div id="wellR" class="well">
        <div class="grid"><i class="h"></i><i class="v"></i><u></u></div>
        <div id="knobR" class="knob"></div>
      </div>
      <div class="axlbl"><s id="lblRX" class="off">Kilitli</s><s id="lblRY">Elevator</s></div>
    </div>
  </div>
</div>

<!-- ================= AYARLAR ================= -->
<div id="cal" class="cal">
  <div class="calhead">
    <div class="calbar">
      <h2>Ayarlar</h2>
      <span id="calState" class="tag">Kalibrasyon kapali</span>
      <span id="calDirty" class="tag on" hidden>Kaydedilmedi</span>
      <span class="spacer"></span>
      <button id="calClose" class="ghost">Kapat</button>
    </div>
    <div class="tabs" id="tabs">
      <button data-tab="kontrol" class="on">Kontrol</button>
      <button data-tab="ucak">Ucak</button>
      <button data-tab="kol">Kollar</button>
      <button data-tab="kalib">Kalibrasyon</button>
    </div>
  </div>

  <!-- ---------- KONTROL: ucus oncesi ---------- -->
  <div class="panel on" data-panel="kontrol">
    <div class="card">
      <h3>Kalkis oncesi kontrol</h3>
      <ul class="checks">
        <li id="ck1">Telsiz yolu acik</li>
        <li id="ck2">Ucaktan telemetri geliyor</li>
        <li id="ck3">Link kalitesi %50 uzeri</li>
        <li id="ck4">Ucak failsafe'te degil</li>
        <li id="ck5">Gaz kolu minimumda</li>
        <li id="ck6">Kalibrasyon modu kapali</li>
        <li id="ck7">Kalibrasyon kaydedilmis</li>
      </ul>
      <div class="note">Yesil olmayan bir satirla kalkma. Liste ARM'i
        engellemez - engelleyen tek sey gaz kolunun minimumda olmasi ve
        kalibrasyon modu; gerisi senin kararin.</div>
    </div>

    <div class="card">
      <h3>Baglanti <em id="dPath">-</em></h3>
      <div class="rows">
        <div><b>Ucak durumu</b><span id="dState">-</span></div>
        <div><b>Link kalitesi</b><span id="dLink">-</span></div>
        <div><b>Paket kaybi</b><span id="dLoss">-</span></div>
        <div><b>Ucak bataryasi</b><span id="dVbat">-</span></div>
        <div><b>Gaz cikisi</b><span id="dThr">-</span></div>
      </div>
      <div class="note">Link %50'nin altina duserse anten yonunu, mesafeyi ve
        telsiz beslemesindeki 10uF kondansatoru kontrol et.</div>
    </div>

    <div class="card">
      <h3>Trim <em id="trimOzet">-</em></h3>
      <div class="note" style="margin-top:0">Trim ucus ekranindaki +/-
        tuslarindan, kanal basina &plusmn;%20. Sifir disindaki trim ucus
        ekraninda kanal degerinin yaninda amber rozet olarak gorunur.</div>
      <div class="btns"><button id="trimSifir">Butun trim'leri sifirla</button></div>
    </div>

    <div class="card">
      <h3>Sayfayi yenile</h3>
      <div class="note" style="margin-top:0">Arayuz takildiysa ya da kumanda
        yeniden yuklendiyse sayfayi yenilemek gerekir. Kazara basilmasin diye
        <b>1 saniye basili tutmak</b> sart; ayar ekrani ARMED iken zaten
        kilitli oldugu icin ucus sirasinda buraya erisilemez.</div>
      <div class="btns"><button id="yenileBtn" class="hold pri"><i></i>
        <b>SAYFAYI YENILE</b><small>BASILI TUT</small></button></div>
    </div>

    <div class="card" id="pwaKart">
      <h3>Ekrani buyut</h3>
      <div class="note" style="margin-top:0">Tarayicinin adres cubugu ekranin
        ustunden yer yiyor; o yer dogrudan kollarin boyundan kisiliyor.</div>
      <div class="btns"><button id="tamEkranBtn" class="pri">Tam ekrana gec</button></div>
      <div class="note"><b>Android:</b> ucus ekranindaki ust seritte, tema
        tusunun solundaki <b>tam ekran</b> tusu ayni isi yapar; dikeydeysen
        telefonu ayrica yatiya cevirir. Adres cubugu ve gezinme cubugu kalkar.
        Tam ekrandan cikarsan kumanda seni zorlamaz, geri donmek icin yine
        ayni tus.
        <br><b>iPhone:</b> tam ekran tusu Safari'de calismaz (Safari bunu
        yalnizca videoya veriyor). Kalicisi: <b>Paylas &gt; Ana Ekrana Ekle</b>.
        Ana ekrandaki kisayoldan acinca adres cubugu hic gelmez.
        <br>Ikisinde de sayfa uygulama gibi acilir; ekran kilidi zaten kapali
        tutuluyor.</div>
    </div>
  </div>

  <!-- ---------- UCAK ---------- -->
  <div class="panel" data-panel="ucak">
    <div class="card">
      <h3>Ucak tipi</h3>
      <div class="seg">
        <button data-ail="0">3 kanal<small>aileron yok</small></button>
        <button data-ail="1">4 kanal<small>aileronlu</small></button>
      </div>
      <div class="note">Aileron kanali ve ucaktaki iki cikis (GPIO 1 ve 21)
        firmware'de hazir. "3 kanal" secili iken aileron kanali <b>her zaman
        notr</b> gonderilir, ucaktaki cikislar bosta durur ve aileronla ilgili
        butun ayarlar bu ekrandan kalkar. Kanat degistirdiginde tek yapacagin
        sey servolari takip burayi "4 kanal" yapmak.</div>
    </div>

    <div class="card">
      <h3>Elevator kol yonu</h3>
      <div class="seg">
        <button data-ele="0">Standart<small>kol yukari = burun asagi</small></button>
        <button data-ele="1">Ekran yonu<small>kol yukari = burun yukari</small></button>
      </div>
      <div class="note">Gercek RC vericilerinde kol ileri itilince burun
        <b>asagi</b> gider, geri cekilince tirmanir. Butun egitim materyali ve
        simulatorler boyle; ters aliskanlik gercek bir kumandaya gecince
        tehlikeli olur. Dokunmatikte "ileri" karsiligi "yukari" oldugu icin
        varsayilan Standart.
        <br>Bu <b>kol</b> yonudur. Servo ters donuyorsa duzeltilecek yer
        Kalibrasyon sekmesindeki "yon ters" kutusu.</div>
    </div>
  </div>

  <!-- ---------- KOLLAR ---------- -->
  <div class="panel" data-panel="kol">
    <div class="card">
      <h3>Kol duzeni</h3>
      <div class="seg">
        <button data-mode="1">Mod 1<small>gaz sagda</small></button>
        <button data-mode="2">Mod 2<small>gaz + rudder solda</small></button>
        <button data-mode="3" id="mode3">3 Kanal<small>gaz solda, ikisi sagda</small></button>
      </div>
      <div class="ozet">
        <div><b>Sol kol</b><span id="ozetL">-</span></div>
        <div><b>Sag kol</b><span id="ozetR">-</span></div>
      </div>
      <div class="note" id="modeNote">Aileron olmadigi icin bir yatay eksen her
        modda bos kalir; hangi tarafta bos kalacagini sen seciyorsun. Tek
        eksenli yuva <b>kol</b> olarak cizilir, yana hareket etmedigi
        sekilden bellidir.</div>
    </div>

    <div class="card">
      <h3>Kol hissi <em id="stickOzet">-</em></h3>
      <div class="fld"><label>ELE oran</label><input type="range" id="rateEle" min="30" max="100" step="5"><em id="vRateEle">100%</em></div>
      <div class="fld"><label>ELE expo</label><input type="range" id="expoEle" min="0" max="50" step="5"><em id="vExpoEle">0%</em></div>
      <div class="fld"><label>RUD oran</label><input type="range" id="rateRud" min="30" max="100" step="5"><em id="vRateRud">100%</em></div>
      <div class="fld"><label>RUD expo</label><input type="range" id="expoRud" min="0" max="50" step="5"><em id="vExpoRud">0%</em></div>
      <div class="fld ailOnly"><label>AIL oran</label><input type="range" id="rateAil" min="30" max="100" step="5"><em id="vRateAil">100%</em></div>
      <div class="fld ailOnly"><label>AIL expo</label><input type="range" id="expoAil" min="0" max="50" step="5"><em id="vExpoAil">0%</em></div>
      <div class="note">Oran = kolun tam hareketinde yuzeyin ne kadar gidecegi.
        Expo = notrun etrafini yumusatir; ilk ucuslarda %25-30 cok yardim eder.</div>
    </div>

    <div class="card">
      <h3>Gaz siniri <em id="vThrLimit">100%</em></h3>
      <div class="fld"><label>Kumanda</label><input type="range" id="thrLimit" min="20" max="100" step="5"></div>
      <div class="note">Kol tam yukaridayken gonderilecek en yuksek gaz. Ilk
        ucuslarda %70 hem menzili hem hatayi kucultur. Ucagin kendi tavani
        Kalibrasyon sekmesinde ayri bir ayardir; ikisinden <b>dusuk olan</b>
        gecerlidir.</div>
    </div>
  </div>

  <!-- ---------- KALIBRASYON ---------- -->
  <div class="panel" data-panel="kalib">
    <div class="card genis">
      <h3>Servo kalibrasyonu <em id="calAck">-</em></h3>
      <div class="note" style="margin-top:0">Yon, notr ve uc noktalar <b>ucagin
        hafizasina</b> yazilir; telefonu degistirsen de kalir. Girmek icin ucak
        DISARM olmali. Bu moddayken motor kilitlidir ve kollar yuzeyleri
        surmez. Asagidaki kartlar yalnizca bu mod acikken calisir.</div>
      <div class="btns">
        <button id="calEnter" class="pri">Kalibrasyona gir</button>
        <button id="calExit">Cik</button>
      </div>
      <div id="calHint" class="note"></div>
    </div>

    <div class="card" data-surf="0">
      <h3>Elevator <em class="live">-</em></h3>
      <div class="sw"><input type="checkbox" class="rev" id="rev0"><label for="rev0">Yon ters</label></div>
      <div class="fld"><label>Alt uc</label><input type="range" class="mn" min="900" max="1450" step="5"><em class="vmn">1000</em></div>
      <div class="fld"><label>Notr</label><input type="range" class="md" min="1300" max="1700" step="1"><em class="vmd">1500</em></div>
      <div class="fld"><label>Ust uc</label><input type="range" class="mx" min="1550" max="2100" step="5"><em class="vmx">2000</em></div>
      <div class="btns">
        <button class="tmn">Alt uc</button><button class="tmd">Notr</button>
        <button class="tmx">Ust uc</button><button class="tsw">Tara</button>
      </div>
      <div class="note">Slider'i surukledikce servo <b>anlik</b> oraya gider -
        ayarin ne yaptigini gozunle gorursun.</div>
    </div>

    <div class="card" data-surf="1">
      <h3>Rudder <em class="live">-</em></h3>
      <div class="sw"><input type="checkbox" class="rev" id="rev1"><label for="rev1">Yon ters</label></div>
      <div class="fld"><label>Alt uc</label><input type="range" class="mn" min="900" max="1450" step="5"><em class="vmn">1000</em></div>
      <div class="fld"><label>Notr</label><input type="range" class="md" min="1300" max="1700" step="1"><em class="vmd">1500</em></div>
      <div class="fld"><label>Ust uc</label><input type="range" class="mx" min="1550" max="2100" step="5"><em class="vmx">2000</em></div>
      <div class="btns">
        <button class="tmn">Alt uc</button><button class="tmd">Notr</button>
        <button class="tmx">Ust uc</button><button class="tsw">Tara</button>
      </div>
    </div>

    <div class="card ailOnly" data-surf="3">
      <h3>Aileron <em class="live">-</em></h3>
      <div class="sw"><input type="checkbox" class="rev" id="rev3"><label for="rev3">Yon ters</label></div>
      <div class="fld"><label>Alt uc</label><input type="range" class="mn" min="900" max="1450" step="5"><em class="vmn">1000</em></div>
      <div class="fld"><label>Notr</label><input type="range" class="md" min="1300" max="1700" step="1"><em class="vmd">1500</em></div>
      <div class="fld"><label>Ust uc</label><input type="range" class="mx" min="1550" max="2100" step="5"><em class="vmx">2000</em></div>
      <div class="btns">
        <button class="tmn">Alt uc</button><button class="tmd">Notr</button>
        <button class="tmx">Ust uc</button><button class="tsw">Tara</button>
      </div>
      <div class="note">Tek kalibrasyon, <b>iki servo</b>: sag kanat kendi notru
        etrafinda aynalanir (biri kalkarken digeri iner). Sag servo ters
        calisiyorsa konnektoru cevir ya da linkaji ayarla.</div>
    </div>

    <div class="card" data-surf="2">
      <h3>ESC / Gaz <em class="live">-</em></h3>
      <div class="fld"><label>Stop</label><input type="range" class="mn" min="900" max="1200" step="5"><em class="vmn">1000</em></div>
      <div class="fld"><label>Ucak tavani</label><input type="range" class="md" min="1000" max="2100" step="10"><em class="vmd">2000</em></div>
      <div class="fld"><label>Tam gaz</label><input type="range" class="mx" min="1700" max="2100" step="5"><em class="vmx">2000</em></div>
      <div class="note">Ucak tavani ucagin kendi siniri, kumandadaki gaz
        sinirindan bagimsiz; ikisinden dusuk olan gecerli. Gaz kanalinda ters
        cevirme <b>bilerek yok</b> - ters gaz, kol asagidayken tam gaz demek
        olurdu. Gaz cikisi kalibrasyon modunda test edilemez; motor kilitli.</div>
    </div>

    <div class="card">
      <h3>Kaydet</h3>
      <div class="btns">
        <button id="cSave" class="pri">Ucaga kaydet</button>
        <button id="cLoad">Kayitliya don</button>
      </div>
      <div class="note">Kaydetmeden cikarsan degisiklikler ucagin RAM'inde kalir
        ve ilk resette silinir.</div>
    </div>

    <div class="card tehlike">
      <h3>Geri donusu yok</h3>
      <div class="note" style="margin-top:0">Motor "gaz veriyorum ama donmuyor"
        diyorsa ESC araligi bilmiyordur. <b>Pervaneyi sok.</b> Ucak karti
        USB'den beslenmeli, LiPo yalnizca ESC'ye gitmeli.</div>
      <div class="sw"><input type="checkbox" id="propOff"><label for="propOff">Pervaneyi soktum</label></div>
      <div class="btns">
        <button id="escHi" class="dgr" disabled>1) Tam gaz darbesi</button>
        <button id="escLo" disabled>2) Stop darbesi</button>
      </div>
      <div class="note">1'e bas, LiPo'yu tak, biplerini duy, sonra 2'ye bas.
        30 saniye icinde 2'ye basmazsan ucak kendiliginden stop'a doner.</div>
      <div class="btns"><button id="cDef" class="dgr">Fabrika ayarlarina don</button></div>
    </div>
  </div>
</div>
<script>
"use strict";
const $ = s => document.querySelector(s);
const $$ = s => Array.from(document.querySelectorAll(s));

// ============================================================
// GORUNUR ALAN OLCUMU - butun duzenin tek olcu kaynagi
//
// Mobil tarayicida CSS'in "100%" ve "vh" degeri GORUNEN yuksekligi vermez;
// adres/gezinme cubugu kapaliyken ki yuksekligi verir. Sayfa overflow:hidden
// oldugu icin alta tasan kisma kaydirmak da mumkun degildi: kol yuvalari
// cubugun arkasinda kaliyordu. visualViewport ise gercekten gorunen alani
// verir - olculen deger --appW/--appH'ye yazilir, butun boyutlar ve kirilim
// noktalari oradan turer.
//
// Kirilim siniflari da @media yerine buradan geliyor; @media ayni yanilan
// yukseklikle calisiyor, olculen deger ise gercek.
//   dikey   - portre
//   dar     - dar ekran (ucus ekrani sikisir)
//   orta    - ayar ekraninda etiket+slider alt alta insin
//   kisa    - alcak ekran (uyari tek satira iner)
//   cokKisa - cok alcak ekran (dolgular kisilir)
// ============================================================
let olcuW = 0, olcuH = 0;
function olcuAl(){
  const vv = window.visualViewport;
  const w = Math.round((vv && vv.width ) || window.innerWidth  || 0);
  const h = Math.round((vv && vv.height) || window.innerHeight || 0);
  if(!w || !h || (w === olcuW && h === olcuH)) return false;
  olcuW = w; olcuH = h;
  const r = document.documentElement;
  r.style.setProperty('--appW', w + 'px');
  r.style.setProperty('--appH', h + 'px');
  r.classList.toggle('dikey',   h > w);
  r.classList.toggle('dar',     w < 430);
  r.classList.toggle('orta',    w < 560);
  r.classList.toggle('kisa',    h < 470);
  r.classList.toggle('cokKisa', h < 380);
  return true;
}
olcuAl();

const stick = { thr:0, ele:0, rud:0, ail:0 };
let armed = false;                    // acilista HER ZAMAN disarm
let calibOn = false;
let cfg = {mode:2, eleDogrudan:0, aileron:0,
           trimEle:0, trimRud:0, trimThr:0, trimAil:0,
           expoEle:0, expoRud:0, expoAil:0,
           rateEle:100, rateRud:100, rateAil:100, thrLimit:100};
let armEpoch = 0;
let srvSon = {};                      // sunucudan gelen en son durum
const surf = [null,null,null,null];

function tit(p){ try{ if(navigator.vibrate) navigator.vibrate(p); }catch(_){} }

// ============================================================
// TEMA
// ============================================================
function temaUygula(t){
  document.documentElement.setAttribute('data-theme', t);
  try{ localStorage.setItem('rcTema', t); }catch(_){}
  // Ikon (gunes/ay) CSS'ten degisiyor - data-theme'e bagli. Burada yalnizca
  // erisilebilirlik etiketi ve tarayici serit rengi guncelleniyor: telefonda
  // adres cubugu acik temada beyaz zeminde kalinca ekranin ustu iki renkli
  // gorunuyordu.
  $('#themeBtn').setAttribute('aria-label',
    (t === 'dark') ? 'Acik temaya gec' : 'Koyu temaya gec');
  const m = document.querySelector('meta[name="theme-color"]');
  if(m) m.setAttribute('content', t === 'dark' ? '#0e1116' : '#f1f3f7');
}
(function temaBasla(){
  let t = null;
  try{ t = localStorage.getItem('rcTema'); }catch(_){}
  if(t !== 'dark' && t !== 'light')
    t = (window.matchMedia && matchMedia('(prefers-color-scheme: dark)').matches)
        ? 'dark' : 'light';
  temaUygula(t);
})();
$('#themeBtn').onclick = () =>
  temaUygula(document.documentElement.getAttribute('data-theme') === 'dark' ? 'light' : 'dark');

// ============================================================
// EKRAN KILIDI
// ============================================================
let wl = null;
async function ekraniAcikTut(){
  try{
    if(!('wakeLock' in navigator)) return;
    if(wl && !wl.released) return;
    wl = await navigator.wakeLock.request('screen');
    wl.addEventListener('release', () => { wl = null; });
  }catch(_){}
}
ekraniAcikTut();
document.addEventListener('visibilitychange', () => { if(!document.hidden) ekraniAcikTut(); });

// ============================================================
// Tasima: once WebSocket, olmazsa HTTP
// ============================================================
let ws = null, wsOk = false, httpBusy = false, httpSince = 0;
const HTTP_TIMEOUT = 400;

// Ayni anda YALNIZCA BIR yeniden baglanma denemesi olabilir.
//
// Eski kodda onerror soketi kapatiyor, onclose da yeni baglanti
// planliyordu; ikisi birden tetiklendiginde IKI soket aciliyordu. mini_ws
// tek istemci destekledigi icin sunucu birini atiyor ("yeni istemci geldi"),
// atilanin onclose'u bir yenisini planliyor ve dongu kendi kendini
// besliyordu.
//
// Olculen sonuc: saniyede bir kopus/baglanti cifti, her kopusta komut
// akisinda 1 sn'den uzun bosluk, kumandanin web watchdog'u devreye girip
// disarm + notrYuzeyler cagirmasi -> GAZ SIFIRA, YUZEYLER NOTRE, sonra
// sayfanin tekrar arm etmesiyle geri donme. Pilotun gordugu "gaz ve
// servolar saniyede bir git gel yapiyor" tam olarak buydu.
let wsBekleyen = false;

function wsPlanla(gecikme){
  if(wsBekleyen) return;
  wsBekleyen = true;
  setTimeout(() => { wsBekleyen = false; wsConnect(); }, gecikme);
}

function wsConnect(){
  // Acik ya da acilmakta olan bir soket varsa ikincisini ACMA.
  if(ws && (ws.readyState === 0 || ws.readyState === 1)) return;
  try{ ws = new WebSocket('ws://' + location.hostname + ':81/'); }
  catch(e){ ws = null; wsPlanla(1000); return; }
  ws.onopen    = () => { wsOk = true; send('G'); };
  ws.onclose   = () => { wsOk = false; ws = null; wsPlanla(800); };
  // onerror'dan SONRA her zaman onclose gelir (WebSocket sartnamesi).
  // Burada ne kapatma ne planlama yapiyoruz - cift baglanti tam buradan
  // doguyordu.
  ws.onerror   = () => {};
  ws.onmessage = ev => { let s; try{ s = JSON.parse(ev.data); }catch(e){ return; } apply(s); };
}
wsConnect();

function send(msg){
  if(wsOk && ws && ws.readyState === 1){ ws.send(msg); return true; }
  return false;
}

function httpControl(){
  const now = Date.now();
  if(httpBusy && now - httpSince < HTTP_TIMEOUT) return;
  httpBusy = true; httpSince = now;
  const ep = armEpoch;
  const ac = new AbortController();
  const to = setTimeout(() => ac.abort(), HTTP_TIMEOUT);
  fetch(`/c?t=${stick.thr|0}&e=${stick.ele|0}&r=${stick.rud|0}&a=${stick.ail|0}&arm=${armed?1:0}&ep=${armEpoch}`,
        {signal:ac.signal, cache:'no-store'})
    .then(r => r.json()).then(s => apply(s, ep))
    .catch(()=>{})
    .finally(()=>{ clearTimeout(to); httpBusy = false; });
}

// Kol verisi zamanlayiciyla degil DEGISIM aninda gider; 10 ms taban
// yalnizca 120 Hz dokunmatik ornekleme hizinda soketi bogmamak icin.
let sonGonderimMs = 0, bekleyen = false;
const KEEPALIVE_MS = 50;
function kanalGonder(zorla){
  const t = performance.now();
  if(!zorla && t - sonGonderimMs < 10){ bekleyen = true; return; }
  sonGonderimMs = t; bekleyen = false;
  if(!send(`C${stick.thr|0},${stick.ele|0},${stick.rud|0},${armed?1:0},${stick.ail|0},${armEpoch}`))
    httpControl();
}
setInterval(() => kanalGonder(true), KEEPALIVE_MS);

// ============================================================
// KOL
// ============================================================
function axVal(name){
  if(!name) return 0;
  return name === 'thr' ? (stick.thr/500 - 1) : stick[name]/1000;
}
function setAxis(name, n){
  if(name === 'thr') stick.thr = Math.round((n + 1) / 2 * 1000);
  else               stick[name] = Math.round(n * 1000);
}

// ------------------------------------------------------------
// BAGLI (anchored) KOL MODELI
//
// Parmagin DOKUNDUGU AN, kolun O ANKI degeri o noktaya sabitlenir. Dokunmak
// tek basina hicbir kanali degistirmez; komut olan sey dokunustan SONRAKI
// hareket. Parmagin yuvanin neresine indigi onemsiz.
//
// Neden mutlak eslemeden vazgecildi: kolu tutmak icin tam ortasina basmak
// gerekiyordu, isabet edemeyince kol parmagin dustugu yere ZIPLIYORDU. Ayni
// kusur ucusta daha tehlikeli: ele carpan bir parmak yuzeyi bir anda tam
// sapmaya goturuyordu. Bagli modelde carpma sadece referans belirler.
//
// ORIGIN PUSH: referans sabit kalsaydi yuvanin tepesinden tutup daha yukari
// cekmek mumkun olmaz, tam sapmaya tek hamlede ulasilamazdi. Sinir asilinca
// TABAN degeri kaydirilir; boylece nereden tutarsan tut tam sapmaya
// ulasirsin ve geri cekince deger ANINDA duser, doyuma yapismaz.
//
// Olcek: yuvanin yarisi kadar parmak hareketi = kanal genisliginin yarisi.
// ------------------------------------------------------------
function makeGimbal(wellSel, knobSel){
  const well = $(wellSel), knob = $(knobSel);
  const g = {well, knob, pid:null, ax:null, ay:null,
             ox:0, oy:0, bx:0, by:0, rx:1, ry:1, px:0, py:0};

  // Yuvarlatilmis kosede tokmak, kenarligi kesip disari tasiyor gibi
  // gorunuyor. Yolu biraz kisaltmak bunu tamamen bitiriyor; kanal degeri
  // etkilenmez, yalnizca cizim.
  const ICERI = 0.90;

  // KOL HASSASIYETI.
  //
  // Taban olcek yuvanin yarisi: yuvanin yarisi kadar parmak hareketi =
  // kanal genisliginin yarisi. HASSASIYET bunu boler, yani buyudukce daha
  // az parmak yolu tam sapmaya yetiyor.
  //
  // 1.6 SECILDI, cunku parmak-tokmak kopuklugunun olculebilir sebebi bu:
  // tokmagin gercek piksel yolu (yuva - tokmak)/2 * ICERI, taban olcek ise
  // yuva/2. 200 px yuva + 60 px tokmakta oran 100/63 ~ 1.6 - yani parmak
  // 100 px giderken tokmak 63 px gidiyor, tokmak parmagin GERISINDE
  // kaliyor ve tam cikis beklenenden fazla parmak yolu istiyor. Pilotun
  // tarifi birebir buydu: "parmagimi kaydirsam da gelmiyor, hareketi
  // parmagimla daha yakin olsun". 1.6 tokmagi parmagin altina getiriyor.
  //
  // NEDEN tokmagin yolundan HESAPLANMIYOR: tokmak yuva kadar buyuk
  // olculurse (yerlesim oturmamis, oge gizli) (yuva - tokmak)/2 sifira
  // duser ve olcek 1 px'e kirpilir - 1 px parmak hareketi TAM SAPMA demek.
  // Ucus kontrolunde bu kabul edilemez, bu yuzden olcek her zaman yuvanin
  // olcusune bagli ve sabit bir carpanla ayarlaniyor.
  //
  // Daha hassas isteniyorsa buyut, daha yumusak isteniyorsa kucult. Bagli
  // kol modeli, origin push ve carpma korumasi bundan etkilenmiyor.
  const HASSASIYET = 1.6;

  g.olc = () => {
    const r = well.getBoundingClientRect();
    const k = knob.getBoundingClientRect();
    g.rx = Math.max(1, r.width  / 2 / HASSASIYET);
    g.ry = Math.max(1, r.height / 2 / HASSASIYET);
    g.px = Math.max(0, (r.width  - k.width ) / 2) * ICERI;   // tokmagin piksel yolu
    g.py = Math.max(0, (r.height - k.height) / 2) * ICERI;
  };

  function put(e, ilk){
    if(ilk){
      g.olc();
      g.ox = e.clientX;  g.oy = e.clientY;
      g.bx = axVal(g.ax); g.by = axVal(g.ay);
      return;                       // DOKUNMAK KOMUT DEGIL
    }
    let vx = g.bx + (e.clientX - g.ox) / g.rx;
    let vy = g.by - (e.clientY - g.oy) / g.ry;

    if(vx >  1){ g.bx -= vx - 1; vx =  1; }
    if(vx < -1){ g.bx -= vx + 1; vx = -1; }
    if(vy >  1){ g.by -= vy - 1; vy =  1; }
    if(vy < -1){ g.by -= vy + 1; vy = -1; }

    if(g.ax) setAxis(g.ax, vx);
    if(g.ay) setAxis(g.ay, vy);

    const uc = (g.ax && Math.abs(vx) >= 1) || (g.ay && Math.abs(vy) >= 1);
    well.classList.toggle('limit', !!uc);
    kanalGonder();
  }

  well.addEventListener('pointerdown', e => {
    if(g.pid !== null || (!g.ax && !g.ay)) return;   // ayni yuvada 2. dokunus yok
    g.pid = e.pointerId;
    try{ well.setPointerCapture(e.pointerId); }catch(_){}
    well.classList.add('active');
    put(e, true); e.preventDefault();
  });
  well.addEventListener('pointermove', e => { if(e.pointerId === g.pid) put(e, false); });
  ['pointerup','pointercancel'].forEach(t => well.addEventListener(t, e => {
    if(e.pointerId !== g.pid) return;
    g.pid = null;
    well.classList.remove('active');
    well.classList.remove('limit');
    if(g.ax && g.ax !== 'thr') setAxis(g.ax, 0);   // yayli eksen notre
    if(g.ay && g.ay !== 'thr') setAxis(g.ay, 0);
    kanalGonder(true);
  }));
  return g;
}

const gL = makeGimbal('#wellL', '#knobL');
const gR = makeGimbal('#wellR', '#knobR');

let aktifMod = 0;
const eksenAdi = a => a === 'thr' ? 'Gaz' : a === 'ele' ? 'Elevator'
                    : a === 'rud' ? 'Rudder' : a === 'ail' ? 'Aileron' : 'Kilitli';

// ------------------------------------------------------------
// KANAL SATIRLARINI KOLLARA GORE DIZ
//
// Ust satir SOL kolun eksenleri, alt satir SAG kolunkiler; her satirda
// once dikey eksen (gaz/elevator), sonra yatay eksen. Iki fayda:
//   - "Bu sayi hangi parmagimda" sorusu bakisla cevaplaniyor.
//   - Dort kanal iki satira siginca orta sutun yariya iniyor; telefonda
//     ACIL DURDURMA'yi ekran disina iten yukseklik ortadan kalkiyor.
// Satirda iki kanal varsa kart kompakt (iki satirli) duzene geciyor -
// 130 px'lik yarim sutunda bile etiket, deger ve trim okunur kaliyor.
// ------------------------------------------------------------
const chSel = {thr:'#chThr', ele:'#chEle', rud:'#chRud', ail:'#chAil'};
function kanallariDiz(){
  const goster = new Set();
  [['#grpL', gL], ['#grpR', gR]].forEach(([sel, g]) => {
    const kutu = $(sel);
    const eksenler = [g.ay, g.ax].filter(Boolean);
    kutu.style.display = eksenler.length ? '' : 'none';
    eksenler.forEach(a => {
      const el = $(chSel[a]);
      el.classList.toggle('cmp', eksenler.length > 1);
      el.style.display = '';
      kutu.appendChild(el);                 // appendChild TASIR, kopyalamaz
      goster.add(a);
    });
  });
  // Kullanilmayan kanal (aileronsuz ucakta aileron) gizlenir.
  for(const a in chSel) if(!goster.has(a)) $(chSel[a]).style.display = 'none';
}

function applyMode(m){
  cfg.mode = (m === 1 || m === 3) ? m : 2;
  aktifMod = cfg.mode;

  // Aileron VARSA iki kol da tam gimbal olur - gercek 4 kanalli duzen.
  // Aileron YOKSA bir yatay eksen bos kalir; hangi tarafta oldugunu mod secer.
  if(cfg.aileron){
    if(cfg.mode === 1){ gL.ax='rud'; gL.ay='ele'; gR.ax='ail'; gR.ay='thr'; }
    else              { gL.ax='rud'; gL.ay='thr'; gR.ax='ail'; gR.ay='ele'; }
  }else if(cfg.mode === 1){
    gL.ax = 'rud';  gL.ay = 'ele';  gR.ax = null;   gR.ay = 'thr';
  }else if(cfg.mode === 3){
    gL.ax = null;   gL.ay = 'thr';  gR.ax = 'rud';  gR.ay = 'ele';
  }else{
    gL.ax = 'rud';  gL.ay = 'thr';  gR.ax = null;   gR.ay = 'ele';
  }

  $('#lblLX').textContent = eksenAdi(gL.ax);
  $('#lblLY').textContent = eksenAdi(gL.ay);
  $('#lblRX').textContent = eksenAdi(gR.ax);
  $('#lblRY').textContent = eksenAdi(gR.ay);
  $('#lblLX').classList.toggle('off', !gL.ax);
  $('#lblRX').classList.toggle('off', !gR.ax);

  $('#knobL').classList.toggle('thr', gL.ay === 'thr');
  $('#knobR').classList.toggle('thr', gR.ay === 'thr');

  // Tek eksenli yuva KOL olarak cizilir: dar, yuvarlak uclu, cubuk basli.
  // Yana hareket etmedigi denemeden once sekilden anlasilmali.
  gL.well.classList.toggle('lever', !gL.ax);
  gR.well.classList.toggle('lever', !gR.ax);
  $('#sideL').classList.toggle('tek', !gL.ax);
  $('#sideR').classList.toggle('tek', !gR.ax);

  $$('[data-mode]').forEach(b => b.classList.toggle('on', +b.dataset.mode === cfg.mode));

  // Hangi eksen hangi kolda - ayarin hemen altinda yaziyla. Mod tuslarinin
  // alt basligi ("gaz sagda") tek basina yetmiyordu; iki kolun tam listesi
  // secimi denemeden dogrulatiyor.
  const kolOzet = g => [g.ay, g.ax].filter(Boolean).map(eksenAdi).join(' + ');
  $('#ozetL').textContent = kolOzet(gL);
  $('#ozetR').textContent = kolOzet(gR);

  // ILGISIZ AYAR EKRANDA DURMAZ (bkz. baslik notu C).
  // Aileronlu ucakta "3 Kanal" duzeninin karsiligi yok - bos eksen kalmiyor,
  // o yuzden tus gri degil YOK. Aileronsuz ucakta da aileron oran/expo ve
  // aileron kalibrasyonu ekranda gorunmez.
  $('#mode3').disabled = !!cfg.aileron;
  $('#mode3').hidden   = !!cfg.aileron;
  $('#modeNote').textContent = cfg.aileron
    ? 'Aileronlu ucakta iki kol da tam gimbal: bos eksen kalmadigi icin uc kanalli duzen gecerli degil. Mod 1 ve Mod 2 arasindaki tek fark gazin hangi kolda oldugu.'
    : 'Aileron olmadigi icin bir yatay eksen her modda bos kalir; hangi tarafta bos kalacagini sen seciyorsun. Tek eksenli yuva kol olarak cizilir, yana hareket etmedigi sekilden bellidir.';
  $$('.ailOnly').forEach(el => el.style.display = cfg.aileron ? '' : 'none');
  kanallariDiz();
  gL.olc(); gR.olc();
}
applyMode(2);

// Gorunur alan degisince (donme, tarayici cubugunun acilip kapanmasi, tam
// ekrana gecis) hem CSS degiskenleri hem tokmak yolu yeniden olculur.
function ekranOlc(){ olcuAl(); gL.olc(); gR.olc(); }
addEventListener('resize', ekranOlc);
addEventListener('orientationchange', () => setTimeout(ekranOlc, 250));
document.addEventListener('fullscreenchange', () => setTimeout(ekranOlc, 150));
if(window.visualViewport){
  visualViewport.addEventListener('resize', ekranOlc);
  visualViewport.addEventListener('scroll', ekranOlc);
}
ekranOlc();

// ============================================================
// GAMEPAD / KLAVYE
// ============================================================
const DEAD = 0.08;
function dz(v){ return Math.abs(v) < DEAD ? 0 : (v - Math.sign(v) * DEAD) / (1 - DEAD); }
function pollPad(){
  const pads = navigator.getGamepads ? navigator.getGamepads() : [];
  let p = null;
  for(const q of pads) if(q && q.connected){ p = q; break; }
  if(!p || p.axes.length < 4) return false;
  const lx = dz(p.axes[0]), ly = dz(p.axes[1]);
  const rx = dz(p.axes[2]), ry = dz(p.axes[3]);
  if(cfg.aileron){
    if(cfg.mode === 1){
      if(gL.pid === null){ setAxis('rud', lx); setAxis('ele', -ly); }
      if(gR.pid === null){ setAxis('ail', rx); setAxis('thr', -ry); }
    }else{
      if(gL.pid === null){ setAxis('rud', lx); setAxis('thr', -ly); }
      if(gR.pid === null){ setAxis('ail', rx); setAxis('ele', -ry); }
    }
  }else if(cfg.mode === 1){
    if(gL.pid === null){ setAxis('rud', lx); setAxis('ele', -ly); }
    if(gR.pid === null){ setAxis('thr', -ry); }
  }else if(cfg.mode === 3){
    if(gL.pid === null){ setAxis('thr', -ly); }
    if(gR.pid === null){ setAxis('rud', rx); setAxis('ele', -ry); }
  }else{
    if(gL.pid === null){ setAxis('rud', lx); setAxis('thr', -ly); }
    if(gR.pid === null){ setAxis('ele', -ry); }
  }
  return true;
}

const keyStep = {'w':['thr',30],'s':['thr',-30],
                 'ArrowUp':['ele',40],'ArrowDown':['ele',-40],
                 'd':['rud',40],'a':['rud',-40]};
const keyHeld = {};
document.addEventListener('keydown', e => {
  if(e.key === ' '){ acilDurdur(); e.preventDefault(); return; }
  if(e.key === 'x' || e.key === 'X'){ stick.thr = 0; kanalGonder(true); e.preventDefault(); return; }
  if(!keyStep[e.key]) return;
  keyHeld[e.key] = true; e.preventDefault();
});
document.addEventListener('keyup', e => {
  const m = keyStep[e.key]; if(!m) return;
  keyHeld[e.key] = false;
  if(m[0] !== 'thr'){ stick[m[0]] = 0; kanalGonder(true); }
});
function pollKeys(){
  let d = false;
  for(const k in keyHeld){
    if(!keyHeld[k]) continue;
    const [ax, s] = keyStep[k];
    if(ax === 'thr') stick.thr = Math.max(0, Math.min(1000, stick.thr + s/3));
    else             stick[ax] = Math.max(-1000, Math.min(1000, stick[ax] + s));
    d = true;
  }
  return d;
}

// ============================================================
// Cizim
// ============================================================
function drawGimbal(g){
  if(!g.px && !g.py) g.olc();
  // Ortalama da transform icinde: yuzde margin en-boy orani kare olmayan
  // kol yuvasinda dikeyde kayiyordu (bkz. .knob CSS notu).
  g.knob.style.transform =
    `translate(calc(-50% + ${axVal(g.ax) * g.px}px), calc(-50% + ${-axVal(g.ay) * g.py}px))`;
}
function bar(el, v, bipolar){
  if(bipolar){
    const w = Math.abs(v) * 50;
    el.style.left  = (v >= 0 ? 50 : 50 - w) + '%';
    el.style.width = w + '%';
  }else{
    el.style.left = '0'; el.style.width = (v * 100) + '%';
  }
}
function loop(){
  const d1 = pollPad(), d2 = pollKeys();
  if(d1 || d2 || bekleyen) kanalGonder();
  drawGimbal(gL); drawGimbal(gR);
  bar($('#bThr'), stick.thr/1000, false);
  bar($('#bEle'), stick.ele/1000, true);
  bar($('#bRud'), stick.rud/1000, true);
  bar($('#bAil'), stick.ail/1000, true);
  $('#hThr').textContent = Math.round(stick.thr/10) + '%';
  requestAnimationFrame(loop);
}
requestAnimationFrame(loop);

setInterval(() => {
  if(wsOk) return;
  fetch('/status', {cache:'no-store'}).then(r => r.json()).then(s => apply(s)).catch(()=>{});
}, 1000);

// ============================================================
// Sunucudan gelen durum
// ============================================================
let sonFailsafe = false;

// Gecici, ENGELLEMEYEN uyari. Modal kutu yerine ust serit kullaniliyor:
// pilot ayni anda kollari kullanmaya devam edebilir.
let uyariBas = '', uyariAlt = '', uyariBitis = 0;
function uyar(bas, alt){
  uyariBas = bas; uyariAlt = alt || '';
  uyariBitis = Date.now() + 4500;
  tit([40,60,40]);
  bannerCiz(bas, uyariAlt, false);
}
function bannerCiz(bas, alt, ciddi){
  const al = $('#alert');
  al.querySelector('b').textContent = bas;
  al.querySelector('i').textContent = alt;
  al.className = (bas ? 'on' : '') + (bas && !ciddi ? ' warn' : '');
  // ANA IKAZ: ust seridin alt cizgisi. Banner kisa ekranda tek satira iner ya
  // da goz kollardayken kacabilir; ust serit her zaman goz alaninda.
  document.body.classList.toggle('ikaz',   !!bas &&  ciddi);
  document.body.classList.toggle('dikkat', !!bas && !ciddi);
}

// Kumandanin gaz kanalinda GERCEKTEN gonderecegi darbe.
// main.cpp'deki gazUs() ile ayni: Math.trunc C++ tamsayi bolmesini taklit
// eder; Math.round ile iki taraf tam ARM esiginde 1 us kayiyordu.
function gazCikisi(){
  const t = Math.max(0, Math.min(1000, stick.thr | 0));
  const g = Math.trunc(t * cfg.thrLimit / 100);
  return Math.max(1000, Math.min(2000, 1000 + g + cfg.trimThr));
}
function gazMinimumda(){ return gazCikisi() <= 1020; }

function apply(s, epoch){
  if(s.cfg){ Object.assign(cfg, s.cfg); syncCfgUi(); }
  if(s.rep){ surf[s.rep.s] = s.rep; syncSurfUi(s.rep.s); }
  if(s.reps) s.reps.forEach(r => { surf[r.s] = r; syncSurfUi(r.s); });
  if(!s.hasOwnProperty('armed')) return;
  srvSon = s;

  // BAYAT DURUM MESAJI YEREL ARM NIYETINI EZMESIN.
  //
  // Kumanda artik sayfanin epoch'unu geri yankiliyor (s.ep). Uyusmuyorsa o
  // mesaj bizim son ARM/DISARM niyetimizden ONCE uretilmistir ve "armed"
  // alani gecersizdir.
  //
  // Eskiden bu kontrol yalnizca HTTP yolunda vardi: WebSocket yolu apply(s)
  // cagiriyor, epoch undefined geliyor ve koruma tumden atlaniyordu. Asil
  // tasima WebSocket oldugu icin koruma pratikte hic calismiyordu. Sonuc:
  // ARM'a basildiginda sunucunun komuttan ONCE urettigi armed:false mesaji
  // sayfayi geri disarm ediyor, sayfa arm=0 gonderiyor, kumanda sessizce
  // disarm olup GAZI KESIYOR, sonra eski bir armed:true mesaji tekrar arm
  // ediyor. Salinim "gaz saniyede bir kesiliyor" olarak gorunuyordu ve
  // kendiliginden ya da elle disarm/arm ile hizalandiginda geciyordu.
  const ep = (s.ep !== undefined) ? s.ep : epoch;
  if(ep === undefined || ep === armEpoch) armed = s.armed;

  // Tus, UZLASTIRILMIS durumu gostersin - bayat mesajda titremesin.
  $('#armBtn').querySelector('b').textContent = armed ? 'DISARM' : 'ARM';
  $('#armBtn').className = (armed ? 'b-armed' : 'b-arm') + ' hold';
  $('#setBtn').disabled = armed;   // ayar ekrani kollari kapatir

  // ACK varken "yok" yazmak yaniltici: paketler ucaga ULASIYOR, yalnizca
  // ucagin durumunu okuyamiyoruz. Kirmizi degil amber - ucusu keser ama
  // kontrol kaybi degil.
  const pTxt = !s.rx ? (s.ack ? 'tlm yok' : 'yok')
             : s.pcal ? 'kalib' : s.pfs ? 'failsafe'
             : s.parmed ? 'armed' : 'hazir';
  $('#hPlane').textContent = pTxt;
  $('#hPlane').className   = !s.rx ? (s.ack ? 'c-warn' : 'c-bad')
                           : (s.pcal || s.pfs) ? 'c-warn'
                           : s.parmed ? 'c-ok' : '';

  $('#hLink').textContent = s.link + '%';
  $('#hLink').className   = s.link >= 50 ? '' : 'c-bad';

  // BAGLANTI NE ILE SAGLANIYOR - etiketin kendisi soyluyor.
  // Yapilandirilmis yol degil, son saniyede GERCEKTEN veri akan yol.
  // Ikisi birden akiyorsa yedeklilik var; biri dusunce etiket hemen degisir.
  const yol = (s.nrfa ? 'nRF24' : '') + (s.nrfa && s.nowa ? '+' : '') + (s.nowa ? 'NOW' : '');
  $('#hLinkYol').textContent = yol || 'Link';
  $('#hLinkYol').className   = yol ? '' : 'c-bad';
  $('#hLoss').textContent = s.rx ? ('%' + s.loss) : '--';
  $('#hLoss').className   = (s.rx && s.loss > 10) ? 'c-warn' : '';
  // Dar telefonda pil 0.1 V cozunurlukle: "7.84V" bes karakter ve o genislikte
  // kirpiliyor. Kesin deger zaten Ayarlar > Kontrol'de tam yaziyor.
  const dar = document.documentElement.classList.contains('dar');
  $('#hVbat').textContent = s.vbat
    ? (s.vbat/1000).toFixed(dar ? 1 : 2) + 'V' : '--';

  // Link: sayi kesin degeri, altindaki ince cubuk egilimi verir.
  const q = Math.max(0, Math.min(100, s.link | 0));
  $('#qLink').style.width = q + '%';
  $('#qLink').className   = q >= 50 ? '' : 'lo';

  // Kontrol sekmesindeki teshis karti - ayni veriler, okunacak bicimde.
  // Acik olan yollar ve bunlardan hangisinin GERCEKTEN tasidigi ayri ayri:
  // "acik ama tasimiyor" durumu (ornegin ESP-NOW geri yolu) burada gorunur.
  const acik  = (s.nrf ? 'nRF24' : '') + (s.nrf && s.now ? ' + ' : '') + (s.now ? 'ESP-NOW' : '');
  const akan  = (s.nrfa ? 'nRF24' : '') + (s.nrfa && s.nowa ? ' + ' : '') + (s.nowa ? 'ESP-NOW' : '');
  $('#dPath').textContent  = !acik ? 'yol yok'
                           : akan === acik ? acik
                           : (akan || 'hicbiri') + ' (acik: ' + acik + ')';
  $('#dState').textContent = pTxt;
  $('#dLink').textContent  = s.link + '%';
  $('#dLoss').textContent  = s.rx ? ('%' + s.loss) : '--';
  $('#dVbat').textContent  = s.vbat ? (s.vbat/1000).toFixed(2) + 'V' : '--';
  $('#dThr').textContent   = gazCikisi() + ' us';
  $('#calDirty').hidden    = !s.pdirty;
  $('#vThr').textContent  = s.uthr;
  $('#vEle').textContent  = s.uele;
  $('#vRud').textContent  = s.urud;
  if(s.uail !== undefined) $('#vAil').textContent = s.uail;

  const oncekiCalib = calibOn;
  calibOn = !!s.pcal;
  if(calibOn && !oncekiCalib)
    for(let k = 0; k < 3; k++) setTimeout(() => cfgCmd(OP.READ, k, 0,0,0, 0), k * 150);
  calDurum(s);

  let bas = '', alt = '', ciddi = true;
  if(Date.now() < uyariBitis) { bas = uyariBas; alt = uyariAlt; ciddi = false; }
  // Sunucunun ARM'i NEDEN reddettigi. Her seyin onunde: "Baglanti yok"
  // gibi genel bir satirdan daha kesin ve dogrudan yapilacak seyi soyluyor.
  // Kartta 4 sn'lik omru var, kendiliginden kayboluyor.
  else if(s.armred)          { bas = 'ARM reddedildi'; alt = s.armred; }
  else if(s.tez)             { bas = 'Tezgah modu';   alt = 'Telemetri olmadan ARM ediliyor. Ucustan once TEZGAH_MODU = false yap.'; }
  else if(!s.rf)        { bas = 'Telsiz yok';    alt = 'Hicbir telsiz yolu acik degil - ucaga veri gitmiyor.'; }
  // ILERI yol ayakta (ACK geliyor), yalnizca GERI yol olu. Bunu "baglanti
  // yok" diye yazmak yaniltici ve tehlikeli: pilot kontrolu kaybettigini
  // sanip panikliyor, oysa ucak tam kumanda edilebilir. Gaz da kesilmiyor.
  else if(!s.rx && s.ack)
                        { bas = 'Telemetri yok'; alt = 'Paketler ucaga ULASIYOR (ACK var), donus yolu olu. Kontrol VAR, gaz kesilmedi - gostergelere guvenme, ucagi getir.'; ciddi = false; }
  else if(!s.rx)        { bas = 'Baglanti yok';  alt = 'Ucaktan cevap gelmiyor. Gaz kesildi, yuzeyler notrde.'; }
  else if(s.pfs)        { bas = 'Failsafe';      alt = 'Ucak baglantiyi kaybetti - motor durdu, yuzeyler notrde.'; }
  else if(s.pcal)       { bas = 'Kalibrasyon';   alt = 'Motor kilitli, kollar yuzeyleri surmuyor.'; ciddi = false; }
  else if(s.armed && !s.parmed)
                        { bas = 'Ucak arm olmadi'; alt = 'Ucakta arm kilidi acik. DISARM basip tekrar ARM et.'; }
  else if(s.pdirty)     { bas = 'Kaydedilmedi';  alt = 'Ucakta kaydedilmemis kalibrasyon var - reset atarsa kaybolur.'; ciddi = false; }
  else if(s.link < 50)  { bas = 'Link zayif';    alt = 'Anten yonu, mesafe, 10uF kondansator.'; ciddi = false; }

  bannerCiz(bas, alt, ciddi);

  // ACK akiyorken bu titresimi calmiyoruz: pilot ucus ortasinda gereksiz
  // panige sokuluyordu, oysa ucak kontrol altinda.
  const fs = !!(s.pfs || (!s.rx && !s.ack));
  if(fs && !sonFailsafe) tit([120,80,120,80,120]);
  sonFailsafe = fs;

  const set = (id, ok) => $(id).classList.toggle('pass', !!ok);
  set('#ck1', s.rf);
  set('#ck2', s.rx);
  set('#ck3', s.link >= 50);
  set('#ck4', s.rx && !s.pfs);
  set('#ck5', gazMinimumda());
  set('#ck6', !s.pcal);
  set('#ck7', s.rx && !s.pdirty);
}

// ============================================================
// BASILI TUT ONAYI
// Ucusu kesebilecek iki buton tek dokunusla calismaz. Ele carpan bir parmak
// motoru kesip zorunlu inise sokabilirdi; gercek bir acil durumda 400 ms
// hicbir sey kaybettirmez. Parmak 44 px kayarsa onay iptal olur.
// ============================================================
function basiliTut(btn, sure, onKosul, fn){
  let t0 = 0, raf = 0, aktif = false, sx = 0, sy = 0;
  const dolgu = btn.querySelector('i');
  function bitir(tamam){
    if(!aktif) return;
    aktif = false; cancelAnimationFrame(raf);
    dolgu.style.width = '0%';
    if(tamam) fn();
  }
  function tik(){
    if(!aktif) return;
    const o = Math.min(1, (performance.now() - t0) / sure);
    dolgu.style.width = (o * 100) + '%';
    if(o >= 1){ tit(35); bitir(true); return; }
    raf = requestAnimationFrame(tik);
  }
  btn.addEventListener('pointerdown', e => {
    if(btn.disabled) return;
    if(onKosul && !onKosul()) return;
    aktif = true; t0 = performance.now(); sx = e.clientX; sy = e.clientY;
    try{ btn.setPointerCapture(e.pointerId); }catch(_){}
    tik(); e.preventDefault();
  });
  btn.addEventListener('pointermove', e => {
    if(aktif && Math.hypot(e.clientX - sx, e.clientY - sy) > 44) bitir(false);
  });
  ['pointerup','pointercancel'].forEach(t => btn.addEventListener(t, () => bitir(false)));
}

function acilDurdur(){
  armed = false; armEpoch = (armEpoch + 1) & 255;
  stick.thr = 0; stick.ele = 0; stick.rud = 0;
  tit([30,70,30]);
  if(!send('X')) fetch('/stop', {cache:'no-store'}).then(r=>r.json()).then(s=>apply(s)).catch(()=>{});
}

// ARM on kosulu saglanmadiginda MODAL uyari YOK.
//
// Once alert() vardi ve tam olarak yapilmamasi gereken seyi yapiyordu:
// gaz yukaridayken ARM'a basilinca modal aciliyor, modal dokunma dizisini
// bozdugu icin pilot gaz kolunu indiremiyordu. Yani uyari, uyardigi
// tehlikeyi gidermeyi engelliyordu. Simdi engellemeyen bir serit + titresim.
basiliTut($('#armBtn'), 600, () => {
  if(armed) return true;
  if(!gazMinimumda()){
    uyar('Gaz minimumda degil',
         stick.thr > 20
           ? 'Gaz kolunu tam asagi cek, sonra ARM.'
           : 'Gaz trim\'i +' + cfg.trimThr + ' us: kol asagidayken bile cikis ' +
             gazCikisi() + ' us. Trim\'i sifira yaklastir.');
    return false;
  }
  if(calibOn){ uyar('Kalibrasyon acik', 'Kalibrasyon modundayken ARM edilemez. Once cik.'); return false; }
  return true;
}, () => {
  tit(armed ? [30,70,30] : 45);
  armed = !armed; armEpoch = (armEpoch + 1) & 255;
  kanalGonder(true);
});

basiliTut($('#stopBtn'), 400, null, acilDurdur);

// Sayfa yenileme. Ucusu kesebilecek her tus gibi basili-tut ile calisiyor;
// ustune ARMED iken ayar ekrani kilitli oldugu icin ucusta erisilemez.
// Yenilemeden once ACIL DURDURMA gonderiliyor: sayfa giderken kumandanin
// elinde bayat bir gaz degeri kalmasin.
basiliTut($('#yenileBtn'), 1000, () => !srvSon.armed, () => {
  try{ send('X'); }catch(_){}
  setTimeout(() => location.reload(), 120);
});

$('#setBtn').onclick = () => {
  $('#cal').classList.add('show');
  for(let s = 0; s < 3; s++) setTimeout(() => cfgCmd(OP.READ, s, 0,0,0, 0), s * 120);
};
$('#calClose').onclick = () => $('#cal').classList.remove('show');

// Sekmeler. Ucus oncesi bakilacak liste ile ayda bir dokunulacak servo ucu
// ayni gorsel agirlikta olmasin diye ayarlar dorde bolundu.
$$('#tabs button').forEach(b => b.onclick = () => {
  $$('#tabs button').forEach(x => x.classList.toggle('on', x === b));
  $$('#cal .panel').forEach(p => p.classList.toggle('on', p.dataset.panel === b.dataset.tab));
  $('#cal').scrollTop = 0;
});

// Trim'i sifirlamak tek tek +/- tuslamaktan cok daha guvenli: ayar ekrani
// ARMED iken zaten kilitli oldugu icin ucus sirasinda basilamaz.
$('#trimSifir').onclick = () => {
  ['trimEle','trimRud','trimThr','trimAil'].forEach(k => setCfg(k, 0));
};

// ---- pilot ayarlari (kumanda kartinda saklanir) ----
function setCfg(k, v){
  cfg[k] = v;
  if(!send(`S${k}=${v}`)) fetch(`/set?k=${k}&v=${v}`).catch(()=>{});
  syncCfgUi();
}
$$('[data-trim]').forEach(b => b.onclick = () => {
  const k = 'trim' + b.dataset.trim;
  setCfg(k, Math.max(-200, Math.min(200, cfg[k] + (+b.dataset.d))));
});
$$('[data-mode]').forEach(b => b.onclick = () => { applyMode(+b.dataset.mode); setCfg('mode', cfg.mode); });
$$('[data-ele]').forEach(b => b.onclick = () => setCfg('eleDogrudan', +b.dataset.ele));
$$('[data-ail]').forEach(b => b.onclick = () => { setCfg('aileron', +b.dataset.ail); applyMode(cfg.mode); });
['rateEle','expoEle','rateRud','expoRud','rateAil','expoAil','thrLimit'].forEach(k => {
  $('#'+k).addEventListener('input', e => setCfg(k, +e.target.value));
});

function syncCfgUi(){
  // Trim SIFIR ise rozet bos birakilir; CSS onu tamamen gizler. Sifir olmayan
  // trim uyari renginde durur - farkedilmeden ucusa cikilmasin.
  const im = v => v ? ((v > 0 ? '+' : '') + v) : '';
  $('#trEle').textContent = im(cfg.trimEle);
  $('#trRud').textContent = im(cfg.trimRud);
  $('#trThr').textContent = im(cfg.trimThr);
  $('#trAil').textContent = im(cfg.trimAil);
  $('#rateAil').value = cfg.rateAil; $('#vRateAil').textContent = cfg.rateAil+'%';
  $('#expoAil').value = cfg.expoAil; $('#vExpoAil').textContent = cfg.expoAil+'%';
  $$('[data-ail]').forEach(b => b.classList.toggle('on', +b.dataset.ail === (cfg.aileron|0)));
  $('#rateEle').value = cfg.rateEle; $('#vRateEle').textContent = cfg.rateEle+'%';
  $('#expoEle').value = cfg.expoEle; $('#vExpoEle').textContent = cfg.expoEle+'%';
  $('#rateRud').value = cfg.rateRud; $('#vRateRud').textContent = cfg.rateRud+'%';
  $('#expoRud').value = cfg.expoRud; $('#vExpoRud').textContent = cfg.expoRud+'%';
  $('#thrLimit').value = cfg.thrLimit; $('#vThrLimit').textContent = cfg.thrLimit+'%';
  $('#stickOzet').textContent = 'expo ' + cfg.expoEle + '/' + cfg.expoRud +
                                ' - oran ' + cfg.rateEle + '/' + cfg.rateRud;
  // Trim ozeti: yalnizca sifir OLMAYANLAR yazilir. Dort tane "0" okumak
  // "hangisi sifir degildi" sorusunu cevaplamiyor.
  const tl = [['ELE',cfg.trimEle],['RUD',cfg.trimRud],['GAZ',cfg.trimThr]]
             .concat(cfg.aileron ? [['AIL',cfg.trimAil]] : [])
             .filter(x => x[1]).map(x => x[0] + ' ' + im(x[1]));
  $('#trimOzet').textContent = tl.length ? tl.join('  ') : 'hepsi sifir';
  $('#trimSifir').disabled = !tl.length;
  $$('[data-ele]').forEach(b => b.classList.toggle('on', +b.dataset.ele === (cfg.eleDogrudan|0)));
  if(cfg.mode !== aktifMod) applyMode(cfg.mode);
}

// ============================================================
// Kalibrasyon - op kodlari rc_protocol.h ile ayni
// ============================================================
const OP = {ENTER:1, EXIT:2, SET:3, TEST:4, SWEEP:5, CENTER:6,
            SAVE:7, LOAD:8, DEFAULT:9, READ:10, ESC_HI:11, ESC_LO:12};
const OPAD = {1:'gir', 2:'cik', 3:'ayar', 4:'test', 5:'tara', 6:'notr',
              7:'kaydet', 8:'geri yukle', 9:'fabrika', 10:'oku',
              11:'ESC tam gaz', 12:'ESC stop'};

let sonKomut = null;

function cfgCmd(op, s, mn, md, mx, fl){
  sonKomut = {op:op, t:Date.now()};
  const m = `K${op},${s|0},${mn|0},${md|0},${mx|0},${fl|0}`;
  if(!send(m)) fetch(`/cfg?op=${op}&s=${s|0}&mn=${mn|0}&md=${md|0}&mx=${mx|0}&f=${fl|0}`)
                 .then(r=>r.json()).then(x=>apply(x)).catch(()=>{});
}

// Kalibrasyon ekraninin durumu TEK yerden yazilir. Sessiz basarisizlik en
// kotusuydu: butona basiliyor, hicbir sey olmuyor, sebep gorunmuyordu.
// Artik hem neden girilemedigi hem son komutun sonucu ekranda.
function calDurum(s){
  s = s || {};
  // Telemetri varsa GERCEK durum, yoksa gonderdigimiz komuttan cikan varsayim.
  const varsayim = !s.rx && kalibVarsayim;
  const aktif = calibOn || varsayim;

  const t = $('#calState');
  t.textContent = calibOn ? 'Kalibrasyon acik'
                : varsayim ? 'Kalibrasyon (dogrulanmadi)'
                : 'Kalibrasyon kapali';
  t.className = 'tag' + (aktif ? ' on' : '');

  $('#calEnter').disabled = aktif || !!s.armed;
  $('#calExit').disabled  = !aktif;
  $$('#cal .card[data-surf]').forEach(c => {
    c.classList.toggle('off', !aktif);
    Array.from(c.querySelectorAll('input,button')).forEach(el => el.disabled = !aktif);
  });
  const p = $('#propOff').checked && aktif;
  $('#escHi').disabled = !p;
  $('#escLo').disabled = !p;

  let h = '';
  if(s.rf === false) h = 'Telsiz yolu yok - komut gonderilemiyor.';
  else if(s.armed)   h = 'Once DISARM et - kalibrasyona ARMED iken girilemez.';
  else if(!s.rx)     h = (varsayim ? 'Kalibrasyonda oldugu VARSAYILIYOR. ' : '')
                       + 'Telemetri yok: komutlar gidiyor ama ucagin ne uyguladigi '
                       + 'geri okunamiyor. Degerler ekranda guncellenmez, '
                       + 'servoya bakarak ayarla. (Yol: '
                       + (s.nrf ? 'nRF24 ' : '') + (s.now ? 'ESP-NOW' : '')
                       + (s.nrf || s.now ? '' : 'yok') + ')';
  else if(s.cfgerr)  h = 'Son komut (' + (sonKomut ? (OPAD[sonKomut.op] || sonKomut.op) : '?') +
                         ') ucaga ULASMADI ya da ucak REDDETTI. Link kalitesine bak, tekrar dene.';
  else if(!calibOn)  h = 'Hazir. "Kalibrasyona gir" ile baslat.';
  else               h = 'Acik. Slider suruklendikce servo anlik takip eder.';
  $('#calHint').textContent = h;

  $('#calAck').textContent = s.cfgerr ? 'komut basarisiz'
                           : sonKomut ? (OPAD[sonKomut.op] || sonKomut.op) + ' onaylandi'
                           : '-';
}

// Telemetri olmadan kalibrasyon: komut UPLINK'ten gidiyor ve ucak kabul
// ediyor, ama onay DOWNLINK'ten gelmedigi icin arayuz bunu ogrenemiyordu -
// butun kalibrasyon denetimleri sonsuza kadar kilitli kaliyordu.
// Cozum: ENTER gonderildikten sonra kalibrasyonda oldugumuzu VARSAY.
// Telemetri varsa gercek durum zaten bunu ezer.
let kalibVarsayim = false;
$('#calEnter').onclick = () => { kalibVarsayim = true;  cfgCmd(OP.ENTER, 0, 0,0,0, 0); calDurum(srvSon); };
$('#calExit').onclick  = () => { kalibVarsayim = false; cfgCmd(OP.EXIT,  0, 0,0,0, 0); calDurum(srvSon); };
$('#cSave').onclick    = () => cfgCmd(OP.SAVE,  0, 0,0,0, 0);
$('#cLoad').onclick    = () => cfgCmd(OP.LOAD,  0, 0,0,0, 0);
$('#cDef').onclick     = () => { if(confirm('Butun kalibrasyon fabrika degerine donsun mu?')) cfgCmd(OP.DEFAULT,0,0,0,0,0); };

$('#propOff').onchange = e => {
  $('#escHi').disabled = !e.target.checked || !calibOn;
  $('#escLo').disabled = !e.target.checked || !calibOn;
};
$('#escHi').onclick = () => {
  if(!confirm('Cikisa TAM GAZ darbesi verilecek. Pervane sokulu mu?')) return;
  cfgCmd(OP.ESC_HI, 2, 0,0,0, 2);          // 2 = RC_CFGF_PROP_OFF
};
$('#escLo').onclick = () => cfgCmd(OP.ESC_LO, 2, 0,0,0, 0);

let sonDokunma = 0;
document.addEventListener('pointerdown', () => sonDokunma = Date.now(), true);

$$('#cal .card[data-surf]').forEach(card => {
  const s   = +card.dataset.surf;
  const mn  = card.querySelector('.mn'), md = card.querySelector('.md'),
        mx  = card.querySelector('.mx'), rev = card.querySelector('.rev');

  function push(canli){
    sonDokunma = Date.now();
    let a = +mn.value, b = +md.value, c = +mx.value;
    if(s !== 2){
      if(b < a + 50) b = a + 50;
      if(c < b + 50) c = b + 50;
      md.value = b; mx.value = c;
    }else{
      if(b < a) b = a;
      if(b > c) b = c;
      md.value = b;
    }
    card.querySelector('.vmn').textContent = a;
    card.querySelector('.vmd').textContent = b;
    card.querySelector('.vmx').textContent = c;
    cfgCmd(OP.SET, s, a, b, c, (rev && rev.checked) ? 1 : 0);
    // CANLI TEST: uc noktayi surukledikce servo oraya gitsin. Ayarin ne
    // yaptigini gormeden kalibre etmek korlemesine is. Gaz cikisi haric -
    // motor kalibrasyon modunda kilitli.
    if(canli !== undefined && s !== 2) cfgCmd(OP.TEST, s, 0, canli, 0, 0);
  }
  if(mn) mn.addEventListener('input', () => push(+mn.value));
  if(mx) mx.addEventListener('input', () => push(+mx.value));
  if(md) md.addEventListener('input', () => push());     // SET zaten notre goturur
  if(rev) rev.addEventListener('change', () => push());

  const test = v => cfgCmd(OP.TEST, s, 0, v, 0, 0);
  const q = sel => card.querySelector(sel);
  if(q('.tmn')) q('.tmn').onclick = () => test(+mn.value);
  if(q('.tmd')) q('.tmd').onclick = () => test(+md.value);
  if(q('.tmx')) q('.tmx').onclick = () => test(+mx.value);
  if(q('.tsw')) q('.tsw').onclick = () => cfgCmd(OP.SWEEP, s, 0,0,0, 0);
});

// Ucaktan gelen rapor slider'lari gunceller. Ekranda gorunen deger her
// zaman UCAGIN uyguladigi degerdir - gonderdigimiz degil.
function syncSurfUi(s){
  const card = document.querySelector(`#cal .card[data-surf="${s}"]`);
  if(!card) return;
  const r = surf[s];
  card.querySelector('.live').textContent = r.live + ' us';
  if(Date.now() - sonDokunma < 700) return;
  const set = (sel, v) => { const el = card.querySelector(sel); if(el && +el.value !== v) el.value = v; };
  set('.mn', r.mn); set('.md', r.md); set('.mx', r.mx);
  card.querySelector('.vmn').textContent = r.mn;
  card.querySelector('.vmd').textContent = r.md;
  card.querySelector('.vmx').textContent = r.mx;
  const rev = card.querySelector('.rev');
  if(rev) rev.checked = !!r.rev;
}

// ============================================================
// KAZARA GEZINME KORUMASI
// ============================================================
document.addEventListener('contextmenu', e => e.preventDefault());

history.pushState(null, '', location.href);
addEventListener('popstate', () => {
  history.pushState(null, '', location.href);
  tit([25,50,25]);
});

addEventListener('beforeunload', e => {
  if(!armed) return;
  e.preventDefault(); e.returnValue = '';
});

// Tam ekran: URL cubugu kaybolur, sistem kenar hareketleri zorlasir ve
// yatay kilit ANCAK tam ekranda calisir. Ikisi de kullanici hareketi
// gerektirdigi icin ilk dokunusa bagli; reddedilirse hicbir sey bozulmaz.
// TAM EKRAN + YATAY KILIT
//
// SIRA ONEMLI: yatay kilit ANCAK tam ekrandayken calisir. Once ikisi yan yana
// cagriliyordu, yani kilit istegi tam ekran daha acilmadan gidiyor ve tarayici
// tarafindan her seferinde reddediliyordu - telefon dikeyde kaliyordu.
// Artik kilit, tam ekran sozu COZULDUKTEN sonra isteniyor.
//
// TAM EKRANA YALNIZCA ELLE GIRILIR.
//
// Eskiden ekrana her dokunusta kendiliginden tam ekran deneniyordu. Iki
// bedeli vardi:
//   1. Tam ekran gecisi viewport'u degistirdigi icin tarayici o anda aktif
//      olan dokunma dizisine pointercancel firlatiyor. ARM ve ACIL DURDURMA
//      tam olarak basili-tut jestiyle calisiyor, yani ilk dokunus o tuslara
//      geldiginde jest sessizce iptal oluyordu.
//   2. Pilot istemedigi bir anda ekran duzeni degisiyordu.
//
// Artik tek giris yolu ust seritteki tus ve Ayarlar > Kontrol'deki kart.
function yatayaKilitle(){
  try{
    if(screen.orientation && screen.orientation.lock)
      screen.orientation.lock('landscape').catch(()=>{});
  }catch(_){}
}
function tamEkranaGec(){
  ekraniAcikTut();
  const el = document.documentElement;
  if(document.fullscreenElement){ yatayaKilitle(); return; }
  if(!el.requestFullscreen){ yatayaKilitle(); return; }   // iPhone: yalnizca dene
  try{
    el.requestFullscreen({navigationUI:'hide'}).then(yatayaKilitle, () => {});
  }catch(_){}
}
function tamEkranCik(){
  try{ if(screen.orientation && screen.orientation.unlock) screen.orientation.unlock(); }catch(_){}
  try{ if(document.exitFullscreen) document.exitFullscreen().catch(()=>{}); }catch(_){}
}
// Ekrana dokunmak YALNIZCA ekran kilidini tazeler - gorunumu degistirmez.
// Ekran kilidi tarayici tarafindan zaman zaman birakiliyor; kullanici
// etkilesimi onu yenilemek icin dogru an.
addEventListener('pointerdown', () => { ekraniAcikTut(); });
document.addEventListener('fullscreenchange', () => {
  const acik = !!document.fullscreenElement;
  document.documentElement.classList.toggle('tamEkran', acik);
  $('#fsBtn').setAttribute('aria-label', acik ? 'Tam ekrandan cik' : 'Tam ekran');
  $('#fsBtn').title = acik ? 'Tam ekrandan cik' : 'Tam ekran (yatay)';
});

// Ust seritteki tus. Dikeydeyken basmak hem tam ekrana alir hem telefonu
// yatiya cevirir - kollar dikeyde genislik siniri yuzunden kucuk kaliyor.
$('#fsBtn').onclick = () => {
  if(document.fullscreenElement){ tamEkranCik(); return; }
  tamEkranaGec();
};
// iPhone'da ne tam ekran ne yatay kilit var: bos is yapan tus "bozuk" hissi
// verir, o yuzden hic gosterilmiyor. Dogru yol Ayarlar > Kontrol'de yazili.
if(!document.documentElement.requestFullscreen &&
   !(window.screen && screen.orientation && screen.orientation.lock))
  $('#fsBtn').style.display = 'none';

// Ana ekrandan (uygulama gibi) acildiysa tarayici cubugu zaten yok - kart
// gereksiz. iOS'ta navigator.standalone, digerlerinde display-mode sorulur.
(function pwaKartiAyarla(){
  let bagimsiz = false;
  try{
    bagimsiz = !!navigator.standalone ||
      (matchMedia('(display-mode: standalone)').matches ||
       matchMedia('(display-mode: fullscreen)').matches);
  }catch(_){}
  const kart = $('#pwaKart');
  if(bagimsiz){ kart.style.display = 'none'; return; }
  // Tam ekran API'si yoksa (iPhone) tusu hic gosterme: basilip hicbir sey
  // olmamasi "bozuk" hissi verir, dogru yol zaten altta yaziyor.
  if(!document.documentElement.requestFullscreen)
    $('#tamEkranBtn').parentNode.style.display = 'none';
  $('#tamEkranBtn').onclick = () => {
    tamEkranaGec();
    $('#cal').classList.remove('show');   // tam ekrani ucus ekraninda gor
  };
})();

syncCfgUi();
</script>
)HTMLPAGE";

// ============================================================
// PWA MANIFESTOSU
// "Ana ekrana ekle" ile acildiginda sayfa uygulama gibi calisir: adres
// cubugu, sekme seridi ve (Android'de) durum cubugu yok. Kumanda ekraninda
// kazanilan ~90 px dogrudan kol yuvasinin boyuna gidiyor.
// display_override: Android'de once tam ekran denenir, olmazsa standalone.
// iOS manifestoyu kismen okur; oradaki isi apple-* meta etiketleri yapar.
// ============================================================
static const char PAGE_MANIFEST[] PROGMEM = R"MANIFEST({
"name":"RC Plane Kumanda","short_name":"Kumanda",
"start_url":"/","scope":"/","id":"/",
"display":"fullscreen","display_override":["fullscreen","standalone"],
"orientation":"landscape",
"background_color":"#0e1116","theme_color":"#0e1116",
"icons":[{"src":"/icon.svg","sizes":"any","type":"image/svg+xml","purpose":"any"}]
})MANIFEST";

// Tek ikon, SVG: PROGMEM'de 300 bayt tutar. PNG olsaydi her boyut icin ayri
// bir ikili blok gerekirdi ve flash'ta yeri yok.
static const char PAGE_ICON[] PROGMEM =
  R"ICON(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 192 192">)ICON"
  R"ICON(<rect width="192" height="192" rx="34" fill="#0e1116"/>)ICON"
  R"ICON(<path fill="#69a6ff" d="M96 22l8 56 68 26v14l-68-10-4 38 24 16v10l-28-8)ICON"
  R"ICON(-28 8v-10l24-16-4-38-68 10v-14l68-26z"/></svg>)ICON";
