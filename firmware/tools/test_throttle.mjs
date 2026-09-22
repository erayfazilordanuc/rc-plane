// Arayuzdeki gazCikisi() ile firmware'deki gazUs() ayni sonucu vermeli.
// Ayrisirlarsa kontrol listesi yesil gorunurken ARM reddedilir - kullanicinin
// hicbir sekilde teshis edemeyecegi bir tutarsizlik.
//
// JS tarafi dosyadan cekiliyor; C++ tarafi da main.cpp'deki formulden
// birebir yeniden kuruluyor ve her iki kaynak da testte gosteriliyor.
import fs from 'node:fs';

const ui = fs.readFileSync(
  new URL('../controller_software/include/web_ui.h', import.meta.url), 'utf8');
const cpp = fs.readFileSync(
  new URL('../controller_software/src/main.cpp', import.meta.url), 'utf8');

const js = /<script>([\s\S]*?)<\/script>/.exec(/R"HTMLPAGE\(([\s\S]*)\)HTMLPAGE";/.exec(ui)[1])[1];
const gazCikisiSrc = /^function gazCikisi\(\)[\s\S]*?\n\}/m.exec(js)[0];

let cfg = { thrLimit: 100, trimThr: 0 };
let stick = { thr: 0 };
const gazCikisi = new Function('cfg', 'stick',
  gazCikisiSrc + '\nreturn gazCikisi;')(cfg, stick);

// --- C++ referansi (main.cpp'den):
//   const int32_t g = (int32_t)constrain(giris,0,1000) * tx.thrLimit / 100;
//   return rcClampUs(RC_US_MIN + g + tx.trimThr);
// C++ tamsayi bolmesi SIFIRA dogru kirpar; JS'te Math.round var.
const cppSat = /static uint16_t gazUs\(int16_t giris\) \{[\s\S]*?\n\}/.exec(cpp)[0];
console.log('--- firmware kaynagi ---');
console.log(cppSat.split('\n').filter(l => l.includes('int32_t g') || l.includes('return')).join('\n'));
console.log('--- arayuz kaynagi ---');
console.log(gazCikisiSrc.split('\n').filter(l => l.includes('const g') || l.includes('return')).join('\n'));
console.log();

function gazUsCpp(giris, thrLimit, trimThr) {
  const t = Math.max(0, Math.min(1000, giris));
  const g = Math.trunc(t * thrLimit / 100);      // C++ tamsayi bolmesi
  const v = 1000 + g + trimThr;
  return Math.max(1000, Math.min(2000, v));
}

let gecti = 0, kaldi = 0, ilkFark = null;
for (const limit of [20, 35, 50, 70, 85, 100]) {
  for (const trim of [-200, -105, -5, 0, 5, 105, 200]) {
    for (let thr = 0; thr <= 1000; thr += 1) {
      cfg.thrLimit = limit; cfg.trimThr = trim; stick.thr = thr;
      const a = gazCikisi();
      const b = gazUsCpp(thr, limit, trim);
      if (a === b) { gecti++; }
      else {
        kaldi++;
        if (!ilkFark) ilkFark = {limit, trim, thr, ui: a, fw: b};
      }
    }
  }
}
console.log(`Ortak nokta taramasi: ${gecti} esit, ${kaldi} farkli`);
if (ilkFark) console.log('ilk fark:', ilkFark);

// ARM kapisi ayni noktada mi kapaniyor?
let kapiFark = 0;
for (const limit of [20, 60, 100]) {
  for (const trim of [-200, -50, 0, 21, 50, 200]) {
    for (let thr = 0; thr <= 1000; thr += 1) {
      cfg.thrLimit = limit; cfg.trimThr = trim; stick.thr = thr;
      const uiIzin = gazCikisi() <= 1020;                       // arayuz
      const fwIzin = gazUsCpp(thr, limit, trim) <= 1000 + 20;   // firmware
      if (uiIzin !== fwIzin) kapiFark++;
    }
  }
}
console.log(`ARM kapisi uyusmazligi: ${kapiFark}`);

// Pozitif trim ARM'i gercekten kapatiyor mu?
cfg.thrLimit = 100; cfg.trimThr = 100; stick.thr = 0;
const pozBlok = gazCikisi() > 1020;
console.log(`Pozitif trim (+100) kol asagidayken ARM'i engelliyor: ${pozBlok} (cikis ${gazCikisi()} us)`);

// Negatif trim ARM'i engellemiyor olmali
cfg.trimThr = -100; stick.thr = 0;
const negIzin = gazCikisi() <= 1020;
console.log(`Negatif trim (-100) ARM'e izin veriyor: ${negIzin} (cikis ${gazCikisi()} us)`);

const ok = kaldi === 0 && kapiFark === 0 && pozBlok && negIzin;
console.log('\n' + (ok ? 'GECTI - arayuz ve firmware ayni' : 'KALDI'));
process.exit(ok ? 0 : 1);
