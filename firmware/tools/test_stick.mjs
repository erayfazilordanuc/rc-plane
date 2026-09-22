// web_ui.h icindeki GERCEK kol matematigini cikarip test eder.
// Fonksiyon govdeleri dosyadan kelimesi kelimesine aliniyor; burada
// yeniden yazilmis bir kopya yok.
//
// MODEL: bagli (anchored) kol. Dokunmak deger degistirmez; komut olan sey
// dokunustan sonraki harekettir. Yuvanin YARISI kadar parmak hareketi =
// tam sapma. Sinir asilinca taban kayar (origin push).
import fs from 'node:fs';

const src = fs.readFileSync(
  new URL('../controller_software/include/web_ui.h', import.meta.url), 'utf8');
const js = /<script>([\s\S]*?)<\/script>/.exec(
  /R"HTMLPAGE\(([\s\S]*)\)HTMLPAGE";/.exec(src)[1])[1];

function grab(name, kind) {
  // const'lar birden fazla satira yayilabiliyor (ornegin eksenAdi): ilk
  // satirda kesmek yarim bir ifade birakip SyntaxError veriyordu.
  const re = kind === 'const'
    ? new RegExp(`^const ${name} = [\\s\\S]*?;$`, 'm')
    : new RegExp(`^function ${name}\\([\\s\\S]*?\\n\\}`, 'm');
  const m = re.exec(js);
  if (!m) throw new Error('bulunamadi: ' + name);
  return m[0];
}

const parcalar = [
  grab('eksenAdi', 'const'),
  grab('axVal'),
  grab('setAxis'),
  grab('makeGimbal'),
  grab('applyMode'),
];

// --- kucuk DOM taklidi
const W = 200;                       // yuva kenari
// Kol hassasiyeti carpani - web_ui.h icindeki HASSASIYET ile AYNI olmali.
// Tam sapma icin gereken parmak yolu: (W / 2) / HASSASIYET.
// Bu sabit elle yazilmis piksel beklentilerini tek yerden turetiyor;
// hassasiyet degisince kosum de onunla birlikte gercegi soyluyor.
const HASSASIYET = 1.6;
const TAM_PX = (W / 2) / HASSASIYET;          // tam sapma icin parmak yolu
const YARIM_PX = TAM_PX / 2;                  // yarim sapma icin parmak yolu
function mkEl(id) {
  return {
    id, textContent: '', style: {}, _h: {}, disabled: false,
    classList: { _s: new Set(),
      add(c){this._s.add(c);}, remove(c){this._s.delete(c);},
      toggle(c,v){ v ? this._s.add(c) : this._s.delete(c); },
      contains(c){return this._s.has(c);} },
    addEventListener(t, f){ (this._h[t] ||= []).push(f); },
    setPointerCapture(){},
    getBoundingClientRect(){ return {left:0, top:0, width:W, height:W}; },
    fire(t, e){ (this._h[t]||[]).forEach(f => f(e)); },
  };
}
const els = {};
const $ = sel => (els[sel] ||= mkEl(sel));
const $$ = () => [];
// applyMode kanal kartlarini #grpL/#grpR arasinda dagitiyor; bu testin
// konusu kol matematigi, DOM duzeni degil - bos vekil yeterli.
const kanallariDiz = () => {};

const stick = { thr:0, ele:0, rud:0 };
const cfg = { mode:2 };
let gonderimSayisi = 0;
const kanalGonder = () => { gonderimSayisi++; };

const mk = (extra, ret) => new Function('$','$$','stick','cfg','kanalGonder','setTimeout','kanallariDiz',
  ...extra, 'let aktifMod = 0;\n' + parcalar.join('\n') + '\nreturn ' + ret + ';');

const api = mk([], '{makeGimbal, setAxis, axVal}')($, $$, stick, cfg, kanalGonder, setTimeout, kanallariDiz);
const gL = api.makeGimbal('#wellL', '#knobL');
const gR = api.makeGimbal('#wellR', '#knobR');
const api2 = mk(['gL','gR'], '{applyMode}')($, $$, stick, cfg, kanalGonder, setTimeout, kanallariDiz, gL, gR);

let gecti = 0, kaldi = 0;
function esit(ad, a, b) {
  const ok = a === b;
  console.log(`${ok ? 'GECTI ' : 'KALDI '} ${ad}: ${a} (beklenen ${b})`);
  ok ? gecti++ : kaldi++;
}

const down = (g, x, y, id=1) => g.well.fire('pointerdown', {pointerId:id, clientX:x, clientY:y, preventDefault(){}});
const move = (g, x, y, id=1) => g.well.fire('pointermove', {pointerId:id, clientX:x, clientY:y});
const up   = (g, id=1) => g.well.fire('pointerup', {pointerId:id});

api2.applyMode(2);
// 3 kanalda her zaman BIR yuva gimbal (iki eksen), digeri KOL (tek eksen).
// Mod 2'de sag kol yalnizca elevator tasir - o da yana hareket etmiyor,
// dolayisiyla o da kol olarak cizilir. Kural moda ozel degil, eksen sayisina bagli.
esit('mod2 sol gimbal (iki eksen)', gL.well.classList.contains('lever'), false);
esit('mod2 sag KOL (tek eksen)',    gR.well.classList.contains('lever'), true);
esit('mod2 sol yatay = rudder',      gL.ax, 'rud');
esit('mod2 sol dikey = gaz',         gL.ay, 'thr');
esit('mod2 sag yatay kullanilmiyor', gR.ax, null);
esit('mod2 sag dikey = elevator',    gR.ay, 'ele');

// ============================================================
// 1) DOKUNMAK KOMUT DEGIL  -- kazara carpma korumasi
// ============================================================
stick.thr = 400; stick.rud = 0;
down(gL, 0, 0);                       // yuvanin sol-ust kosesine carp
esit('carpma: gaz degismez',    stick.thr, 400);
esit('carpma: rudder degismez', stick.rud, 0);
up(gL);
esit('carpma sonrasi gaz yerinde', stick.thr, 400);

stick.ele = 0;
down(gR, W, 0);                       // sag kolun tepesine carp
esit('carpma: elevator degismez', stick.ele, 0);
up(gR);
esit('carpma sonrasi elevator notr', stick.ele, 0);

// ============================================================
// 2) REFERANS = DOKUNULAN NOKTA  -- nereden tutarsan tut ayni his
// ============================================================
stick.rud = 0;
down(gL, 30, 170);                    // kolun ortasindan cok uzak bir nokta
esit('uzaktan tutmak rudder`i bozmaz', stick.rud, 0);
move(gL, 30 + TAM_PX, 170);           // tam sapma icin gereken parmak yolu
esit('tam yol saga = tam sag', stick.rud, 1000);
move(gL, 30 + YARIM_PX, 170);         // yarim yola geri
esit('yarim yol = yarim sag', stick.rud, 500);
up(gL);
esit('birakinca rudder notr', stick.rud, 0);

// Ayni hareket yuvanin BASKA bir yerinden ayni sonucu vermeli
stick.rud = 0;
down(gL, 150, 40);
move(gL, 150 + YARIM_PX, 40);         // yarim yol saga
esit('baska noktadan yarim yol = yarim sag', stick.rud, 500);
up(gL);

// ============================================================
// 3) ORIGIN PUSH  -- tam sapmaya her yerden ulasilir, geri donus ani
// ============================================================
stick.rud = 0;
down(gL, 190, 100);                   // sag kenara yakin tut
move(gL, 400, 100);                   // yuvanin cok disina, saga
esit('disari tasma: tam sapmada doyar', stick.rud, 1000);
esit('doyumda kenarlik isareti', gL.well.classList.contains('limit'), true);
move(gL, 400 - YARIM_PX, 100);        // yarim yol geri
esit('doyumdan geri donus ANINDA', stick.rud, 500);
esit('doyumdan cikinca isaret kalkar', gL.well.classList.contains('limit'), false);
move(gL, 400 - YARIM_PX - TAM_PX, 100);   // bir tam yol daha geri
esit('geri donus dogrusal', stick.rud, -500);
up(gL);

// ============================================================
// 4) GAZ  -- yay yok, referans mevcut degere baglanir
//
// OLCEK: gaz araligi (0..1000) normalize edilince -1..+1, yani sapma
// eksenleriyle AYNI genislikte. Fiziksel gimbalde de oyle - gaz kolunun
// uctan uca yolu elevator kolununkiyle ayni. Pratikte:
//   yuzey : ortadan kenara TAM_PX = tam sapma      (uctan uca 2*TAM_PX)
//   gaz   : dipten tepeye 2*TAM_PX = %0 -> %100     (uctan uca 2*TAM_PX)
// Yani TAM_PX kadar hareket her zaman kanal genisliginin yarisi kadar.
// TAM_PX hassasiyet carpanindan turetiliyor (bkz. dosya basi).
// ============================================================
stick.thr = 0;
down(gL, 100, 20);                    // gaz 0 iken yuvanin TEPESINDEN tut
esit('gaz: tepeden tutmak gazi acmaz', stick.thr, 0);
move(gL, 100, 20 - YARIM_PX);         // yarim yol yukari
esit('gaz: yarim yol yukari = %25', stick.thr, 250);
move(gL, 100, 20 - TAM_PX);           // bir tam yol
esit('gaz: tam yol yukari = %50', stick.thr, 500);
move(gL, 100, 20 - 2 * TAM_PX);       // iki tam yol
esit('gaz: iki tam yol yukari = tam gaz', stick.thr, 1000);
move(gL, 100, 20 - 4 * TAM_PX);       // cok daha yukari - doyum
esit('gaz: doyumda kalir', stick.thr, 1000);
move(gL, 100, 20 - 3 * TAM_PX);       // bir tam yol geri
esit('gaz: doyumdan geri donus ANINDA', stick.thr, 500);
up(gL);
esit('gaz: birakinca YERINDE kalir', stick.thr, 500);

// Tam gazdan asagi cekmek - uctan uca yine iki tam yol
stick.thr = 1000;
down(gL, 100, 60);
move(gL, 100, 60 + 2 * TAM_PX);       // iki tam yol asagi
esit('gaz: tam gazdan tam kesmeye uctan uca yol', stick.thr, 0);
up(gL);

// Kaldirip yeniden tutmak birikimlidir - bagli modelin dogal jesti
stick.thr = 0;
down(gL, 100, 180); move(gL, 100, 180 - TAM_PX); up(gL);   // +bir tam yol
esit('gaz: birinci hamle %50', stick.thr, 500);
down(gL, 100, 180); move(gL, 100, 180 - TAM_PX); up(gL);   // yeniden tut, +bir tam yol
esit('gaz: ikinci hamle birikir -> tam gaz', stick.thr, 1000);

// 5) SAG KOL  -- elevator yayli, yatay eksen olu
// ============================================================
stick.ele = 0; stick.rud = 0;
down(gR, 100, 100);
move(gR, 100, 0);                     // 100 px yukari
esit('elevator: yukari tam', stick.ele, 1000);
move(gR, 300, 0);                     // yatay hareket
esit('olu yatay eksen rudder`i bozmaz', stick.rud, 0);
up(gR);
esit('elevator: birakinca notr', stick.ele, 0);

// ============================================================
// 6) COKLU DOKUNMA
// ============================================================
stick.thr = 0; stick.rud = 0; stick.ele = 0;
down(gL, 100, 100, 7);
down(gR, 100, 100, 9);
move(gL, 200, 100, 7);                // sol: saga tam
move(gR, 100, 0, 9);                  // sag: yukari tam
esit('coklu: sol rudder saga',      stick.rud, 1000);
esit('coklu: sag elevator yukari',  stick.ele, 1000);
up(gL, 7);
esit('coklu: sol birakildi -> rudder notr', stick.rud, 0);
esit('coklu: sag hala basili',              stick.ele, 1000);
up(gR, 9);

// Ayni yuvada ikinci parmak yok sayilir (avuc + basparmak)
stick.rud = 0;
down(gL, 100, 100, 3);
down(gL, 10, 190, 4);                 // ikinci parmak - yok sayilmali
move(gL, 200, 100, 3);
esit('ayni yuvada 2. parmak yok sayilir', stick.rud, 1000);
move(gL, 10, 190, 4);                 // 2. parmagin hareketi de etkisiz
esit('2. parmagin hareketi etkisiz', stick.rud, 1000);
up(gL, 3);

// ============================================================
// 7) HER DEGISIM GONDERIM TETIKLER (gecikme sarti)
// ============================================================
const oncekiSayac = gonderimSayisi;
down(gL, 100, 100); move(gL, 100, 80); move(gL, 100, 60); up(gL);
esit('her hareket gonderim tetikler (>=3)', gonderimSayisi - oncekiSayac >= 3, true);

// ============================================================
// 8) MOD 1 ve MOD 3
// ============================================================
api2.applyMode(1);
esit('mod1 sol yatay = rudder',   gL.ax, 'rud');
esit('mod1 sol dikey = elevator', gL.ay, 'ele');
esit('mod1 sag yatay bos',        gR.ax, null);
esit('mod1 sag dikey = gaz',      gR.ay, 'thr');

api2.applyMode(3);
esit('mod3 sol yatay BOS',        gL.ax, null);
esit('mod3 sol dikey = gaz',      gL.ay, 'thr');
esit('mod3 sag yatay = rudder',   gR.ax, 'rud');
esit('mod3 sag dikey = elevator', gR.ay, 'ele');
// Tek eksenli yuva KOL olarak cizilir - yana hareket etmedigi sekilden belli
esit('mod3 sol yuva KOL olarak cizilir', gL.well.classList.contains('lever'), true);
esit('mod3 sag yuva gimbal kalir',       gR.well.classList.contains('lever'), false);

stick.rud = 0; stick.ele = 0;
down(gR, 100, 100);
move(gR, 200, 0);                     // saga + yukari tam
esit('mod3 sag kol: rudder saga',     stick.rud, 1000);
esit('mod3 sag kol: elevator yukari', stick.ele, 1000);
up(gR);
esit('mod3 birakinca rudder notr',    stick.rud, 0);
esit('mod3 birakinca elevator notr',  stick.ele, 0);

stick.thr = 300;
down(gL, 20, 20);                     // mod3 sol kol: yalnizca gaz
esit('mod3 sol: carpma gazi bozmaz', stick.thr, 300);
move(gL, 200, 20);                    // yatay hareket - olu eksen
esit('mod3 sol: yatay eksen olu',    stick.rud, 0);
esit('mod3 sol: yatay hareket gazi bozmaz', stick.thr, 300);
move(gL, 20, 20 - 0.7 * TAM_PX);      // tam yolun %70'i yukari: +%35 -> 300+350
esit('mod3 sol: dikey hareket gaz verir', stick.thr, 650);
up(gL);

api2.applyMode(7);
esit('gecersiz mod -> 2', cfg.mode, 2);


// ============================================================
// 9) AILERON (4 kanal)
// ============================================================
cfg.aileron = 1;
api2.applyMode(2);
esit('ail+mod2 sol yatay = rudder',   gL.ax, 'rud');
esit('ail+mod2 sol dikey = gaz',      gL.ay, 'thr');
esit('ail+mod2 sag yatay = aileron',  gR.ax, 'ail');
esit('ail+mod2 sag dikey = elevator', gR.ay, 'ele');
esit('ail+mod2 iki kol da gimbal (L)', gL.well.classList.contains('lever'), false);
esit('ail+mod2 iki kol da gimbal (R)', gR.well.classList.contains('lever'), false);

api2.applyMode(1);
esit('ail+mod1 sol dikey = elevator', gL.ay, 'ele');
esit('ail+mod1 sag yatay = aileron',  gR.ax, 'ail');
esit('ail+mod1 sag dikey = gaz',      gR.ay, 'thr');

// aileron ekseni bagli kol kurallarina uyuyor mu
stick.ail = 0; stick.ele = 0;
api2.applyMode(2);
down(gR, 40, 160);                    // kosesinden tut - carpma
esit('aileron: carpma kanali bozmaz', stick.ail, 0);
move(gR, 140, 160);                   // 100 px saga
esit('aileron: 100 px saga = tam sag', stick.ail, 1000);
up(gR);
esit('aileron: birakinca notr (yayli)', stick.ail, 0);

// 3 kanala donunce aileron ekseni kaybolur
cfg.aileron = 0;
api2.applyMode(2);
esit('3 kanalda sag yatay eksen yok', gR.ax, null);
esit('3 kanalda sag yuva KOL',        gR.well.classList.contains('lever'), true);

// ============================================================
// 10) TOKMAK YUVANIN DISINA TASMAZ
// Cizim yolu her iki eksende de (yuva - tokmak)/2 * ICERI ile sinirli.
// ============================================================
cfg.aileron = 0;
api2.applyMode(2);
gL.olc(); gR.olc();
// mkEl her elemana 200x200 rect veriyor; tokmak da 200 -> yol 0 cikar.
// Gercek olculeri taklit etmek icin tokmaga kucuk bir rect ver.
gR.knob.getBoundingClientRect = () => ({left:0,top:0,width:60,height:60});
gR.olc();
esit('tokmak yolu = (yuva-tokmak)/2 * 0.90', Math.round(gR.px), Math.round((200-60)/2*0.90));
esit('yatay ve dikey yol esit (kare yuva)',  Math.round(gR.px), Math.round(gR.py));
// En uc noktada tokmagin kenari yuvanin icinde mi?
const uc = gR.px + 30;                // yol + tokmak yaricapi
esit('tam sapmada tokmak yuvanin icinde', uc <= 100, true);

// Kol (lever) yuvasi: kare DEGIL, dikey yol yuksekligi kullanmali
gL.well.getBoundingClientRect = () => ({left:0,top:0,width:88,height:200});
gL.knob.getBoundingClientRect = () => ({left:0,top:0,width:65,height:22});
gL.olc();
esit('kol: dikey yol yukseklikten hesaplanir', Math.round(gL.py), Math.round((200-22)/2*0.90));
esit('kol: tam sapmada bas yuvanin icinde', gL.py + 11 <= 100, true);

console.log(`\n${gecti} gecti, ${kaldi} kaldi`);
process.exit(kaldi ? 1 : 0);
