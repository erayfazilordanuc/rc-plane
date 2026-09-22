# -*- coding: utf-8 -*-
# Ucus ekranindaki metin/zemin ciftlerinin WCAG kontrast oranini olcer -
# ACIK ve KOYU temanin IKISI icin de. Gunes altinda okunabilirlik hedefi:
# ana degerler >= 7:1 (AAA), en sonuk etiket bile >= 4.5:1 (AA).
#
# Ayrica ucus ekraninda animasyon/gecis/gradyan/golge kalmadigini,
# kol boyutunu ve yerlesimini dogrular.
import io, re, sys

import os
p = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                 '..', 'controller_software', 'include', 'web_ui.h')
src = io.open(p, encoding='utf-8').read()
html = re.search(r'R"HTMLPAGE\((.*)\)HTMLPAGE";', src, re.S).group(1)
css = re.search(r'<style>(.*?)</style>', html, re.S).group(1)

def blok_degiskenleri(blok):
    return dict(re.findall(r'--([\w-]+)\s*:\s*(#[0-9a-fA-F]{6})', blok))

# Acik tema: TEMA tanimlarini tasiyan :root blogu. Koyu tema:
# :root[data-theme="dark"] blogu.
#
# DIKKAT: "ilk :root blogu" demek yeterli DEGIL. Tema blogundan once bir
# tane daha :root{ --appW/--appH } var (viewport taban degerleri); onu
# okuyunca tema tokenlari hic gorunmuyor ve kosum KeyError ile oluyordu -
# yani kontrast hic olculmuyordu. Blogu ICERIGINDEN tanimliyoruz.
def tema_blogu():
    for govde in re.findall(r':root\{(.*?)\}', css, re.S):
        if '--bg:' in govde and '--muted:' in govde:
            return govde
    sys.exit('test_theme: tema :root blogu bulunamadi (web_ui.h degisti mi?)')

acik = blok_degiskenleri(tema_blogu())
koyu = dict(acik)
koyu.update(blok_degiskenleri(
    re.search(r':root\[data-theme="dark"\]\{(.*?)\}', css, re.S).group(1)))

def lum(h):
    c = [int(h[i:i+2], 16) / 255 for i in (1, 3, 5)]
    c = [x / 12.92 if x <= 0.03928 else ((x + 0.055) / 1.055) ** 2.4 for x in c]
    return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]

def oran(a, b):
    la, lb = lum(a), lum(b)
    return (max(la, lb) + 0.05) / (min(la, lb) + 0.05)

gecti = kaldi = 0
def kontrol(ad, deger, esik):
    global gecti, kaldi
    ok = deger >= esik
    print(f"{'GECTI ' if ok else 'KALDI '} {ad}: {deger:.2f}:1 (>= {esik})")
    if ok: gecti += 1
    else:  kaldi += 1

CIFTLER = [
    ("etiket  (muted / surface)", 'muted',  'surface', 4.5),
    ("deger   (fg    / surface)", 'fg',     'surface', 7.0),
    ("iyi     (ok    / surface)", 'ok',     'surface', 4.5),
    ("uyari   (warn  / surface)", 'warn',   'surface', 4.5),
    ("kotu    (bad   / surface)", 'bad',    'surface', 4.5),
    ("vurgu   (accent/ surface)", 'accent', 'surface', 4.5),
    ("kol     (accent/ gimbal )", 'accent', 'gimbal',  4.5),
    # GRAFIK oge, yazi degil: gaz cubugu ve tokmagi. WCAG 1.4.11 (Non-text
    # Contrast) bu tur oge icin 3:1 istiyor, 4.5:1 yazi esigidir. 4.5
    # zorlamak ~#a85c00 gibi bir ton gerektiriyor ve o acik zeminde camur
    # gibi duruyor - dosya gecmisinde ayni sebeple bir kez geri alinmis.
    # Olcum: #c96f00 -> 3.46, yani dogru esigi rahatca geciyor.
    ("gaz kolu(thr   / gimbal )", 'thr',    'gimbal',  3.0),
    ("govde   (fg    / bg     )", 'fg',     'bg',      7.0),
    ("etiket  (muted / bg     )", 'muted',  'bg',      4.5),
]

for adTema, tema in (("ACIK TEMA", acik), ("KOYU TEMA", koyu)):
    print(f"\n--- {adTema} ---")
    for ad, a, b, esik in CIFTLER:
        kontrol(ad, oran(tema[a], tema[b]), esik)

print("\n--- Ucus ekrani kisitlari ---")
ucus = re.sub(r'/\*.*?\*/', '', css, flags=re.S)
ucus = '\n'.join(s for s in ucus.split('\n')
                 if not re.match(r'\s*\.(cal|calbar|card|fld|btns|sw|note|checks|seg|tag|spacer)', s))

def yasak(ad, desen):
    global gecti, kaldi
    bulunan = re.findall(desen, ucus)
    ok = not bulunan
    print(f"{'GECTI ' if ok else 'KALDI '} {ad}: {len(bulunan)} adet {bulunan[:3] if bulunan else ''}")
    if ok: gecti += 1
    else:  kaldi += 1

yasak("animasyon yok",    r'animation\s*:|@keyframes')
yasak("gecis efekti yok", r'transition\s*:')
yasak("gradyan yok",      r'gradient\(')
yasak("golge yok",        r'box-shadow\s*:|text-shadow\s*:')

print("\n--- Yerlesim ---")
m = re.search(r'--well:\s*clamp\((\d+)px', css)
# Alt sinir: dokunma hedefi. Sartnamede 140 dp yaziyordu, sonra 136'ya
# cekilmisti; kod ise 112'de.
#
# 112 BILINCLI ve dogru olan: --well bir clamp() ve orta terimi
# "gorunur yukseklik - serit/dolgu" hesabi, yani yuvanin ust seride ya da
# tarayici cubuguna TASMAMASINI garanti ediyor. clamp'in ALT SINIRI o
# garantiyi eziyor: taban ne kadar buyukse, kisa ekranda yuva o kadar kolay
# tasiyor. 136'ya cikarmak daha buyuk bir kol degil, TASAN bir kol demek -
# ve tasan bir kol, biraz kucuk bir koldan cok daha kotu bir ariza.
#
# 112 dp zaten 48 dp'lik dokunma hedefi minimumunun iki katindan fazla.
# Esik gercek degeri kilitliyor ki farkinda olmadan daha da kucultulmesin.
kontrol("min kol kenari (dp)", float(m.group(1)) if m else 0, 112)

def var_mi(ad, kosul):
    global gecti, kaldi
    print(f"{'GECTI ' if kosul else 'KALDI '} {ad}")
    if kosul: gecti += 1
    else:     kaldi += 1

var_mi("kollar alt kosede", 'align-items:flex-end' in css and 'justify-content:space-between' in css)
var_mi("tek eksenli yuva KOL olarak cizilir (.well.lever)", '.well.lever{' in css)
var_mi("kol basi cubuk seklinde", '.well.lever .knob{' in css)
var_mi("gimbal kapisi yuvarlatilmis kare", "border-radius:20%" in css)
var_mi("tokmak yuvarlak", '.knob{' in css and 'border-radius:50%' in css)
var_mi("acik tema tanimli", '--bg:#f1f3f7' in css)
var_mi("koyu tema tanimli", ':root[data-theme="dark"]' in css)
var_mi("cihaz tercihi destekleniyor", 'prefers-color-scheme: dark' in css)

print(f"\n{gecti} gecti, {kaldi} kaldi")
sys.exit(1 if kaldi else 0)
