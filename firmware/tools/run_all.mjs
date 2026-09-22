// Butun dogrulama kosumlarini sirayla calistirir ve tek bir sonuc doner.
//
//     node tools/run_all.mjs
//
// Cikis kodu 0 = hepsi gecti. CI'ya ya da bir commit oncesi kancaya
// dogrudan baglanabilir.
//
// Bu kosumlar donanim GEREKTIRMEZ: kaynak dosyalari okuyup arayuzun
// matematigini, temasini ve DOM tutarliligini dogrularlar. Gercek ucus
// davranisini test etmezler - onun yeri tezgah ve pist.
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const dizin = path.dirname(fileURLToPath(import.meta.url));

const kosumlar = [
  ['node',   'test_stick.mjs',    'Kol matematigi (bagli model, origin push, modlar)'],
  ['node',   'test_throttle.mjs', 'Gaz formulu: arayuz == firmware'],
  ['node',   'test_dom.mjs',      'Sayfa JS yukleme hatasi taramasi'],
  ['python', 'test_theme.py',     'Kontrast (iki tema) + ucus ekrani kisitlari'],
  ['python', 'test_ids.py',       'DOM id / sinif capraz kontrolu'],
];

let kalan = 0;
for (const [yorumlayici, betik, aciklama] of kosumlar) {
  const r = spawnSync(yorumlayici, [path.join(dizin, betik)], {
    encoding: 'utf8', cwd: dizin,
  });
  const ciktilar = ((r.stdout || '') + (r.stderr || '')).trimEnd().split('\n');
  const ozet = ciktilar[ciktilar.length - 1] || '(cikti yok)';
  const ok = r.status === 0;
  if (!ok) kalan++;
  console.log(`${ok ? 'GECTI ' : 'KALDI '} ${betik.padEnd(18)} ${aciklama}`);
  console.log(`        ${ozet}`);
  if (!ok) console.log(ciktilar.filter(l => l.startsWith('KALDI')).map(l => '        ' + l).join('\n'));
}

console.log(kalan === 0
  ? `\nButun kosumlar gecti (${kosumlar.length}/${kosumlar.length}).`
  : `\n${kalan} kosum KALDI.`);
process.exit(kalan ? 1 : 0);
