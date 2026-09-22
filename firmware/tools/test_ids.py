# -*- coding: utf-8 -*-
# JS icinde $('#xxx') ile aranan her id, HTML'de gercekten var mi?
#
# Neden onemli: eksik bir id $() icin null doner ve .textContent erisimi
# TypeError firlatir. Bu hata requestAnimationFrame dongusunun icinde
# olursa dongu bir daha calismaz - kollar ekranda donar ve arayuz sessizce
# olur. Derleyici bunu yakalamaz, tarayici konsoluna bakmadan gorulmez.
import io, re, sys

import os
p = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                 '..', 'controller_software', 'include', 'web_ui.h')
src = io.open(p, encoding='utf-8').read()
html = re.search(r'R"HTMLPAGE\((.*)\)HTMLPAGE";', src, re.S).group(1)
js = re.search(r'<script>(.*?)</script>', html, re.S).group(1)
govde = html[:html.index('<script>')]

varolan = set(re.findall(r'id="([^"]+)"', govde))
aranan  = set(re.findall(r"\$\('#([\w-]+)'\)", js))
aranan |= set(re.findall(r"\$\('#'\s*\+\s*(\w+)\)", js))   # dinamik: $('#'+k)

# Dinamik olanlar: ['rateEle','expoEle',...].forEach(k => $('#'+k))
#
# Blogun govdesinde GERCEKTEN $('#'+k) olmasi sart. Eskiden her
# [..].forEach(k => ..) blogu DOM aramasi sayiliyordu; setCfg(k, 0) gibi
# yalnizca durumu degistiren bloklar da sayilinca kosum olmayan id'leri
# (#trimEle, #trimThr, ...) "eksik" diye bildiriyordu. Dort sahte hata,
# gercek bir eksigi de bunlarin arasinda kaybediyordu.
for m in re.finditer(r"\[([^\]]*?)\]\.forEach\(k\s*=>", js):
    kuyruk = js[m.end():m.end() + 300]
    if re.search(r"\$\('#'\s*\+\s*k\)", kuyruk):
        aranan |= set(re.findall(r"'([\w-]+)'", m.group(1)))
aranan.discard('k')

eksik = sorted(aranan - varolan)
kullanilmayan = sorted(varolan - aranan)

print("HTML'deki id sayisi :", len(varolan))
print("JS'in aradigi id    :", len(aranan))
print()
if eksik:
    print("!! JS'in aradigi ama HTML'de OLMAYAN id'ler:")
    for e in eksik: print("   #" + e)
else:
    print("GECTI  JS'in aradigi butun id'ler HTML'de var.")

# querySelector('#cal ...') gibi sinif tabanli erisimler ayri kontrol
sinif_arama = set(re.findall(r"querySelector\('\.([\w-]+)'\)", js))
sinif_arama |= set(re.findall(r"card\.querySelector\('\.([\w-]+)'\)", js))
html_sinif = set()
for c in re.findall(r'class="([^"]+)"', govde):
    html_sinif |= set(c.split())
eksik_sinif = sorted(s for s in sinif_arama if s not in html_sinif)
print()
if eksik_sinif:
    print("!! JS'in aradigi ama HTML'de OLMAYAN sinif:", eksik_sinif)
else:
    print("GECTI  JS'in aradigi butun siniflar HTML'de var.")

print()
print("Bilgi - HTML'de tanimli ama JS'te $ ile aranmayan id'ler")
print("(CSS/label hedefi olabilir, sorun degil):")
print("  ", ", ".join("#" + i for i in kullanilmayan) or "yok")

sys.exit(1 if (eksik or eksik_sinif) else 0)
