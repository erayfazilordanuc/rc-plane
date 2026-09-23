"""docs/diagrams/*.svg ureticisi.

    python docs/diagrams/generate.py

Kaynak: firmware/*/docs/kablolama.html (pin ve kablo rengi) ve
docs/AIRFRAME.md (govde olculeri). Pin ya da olcu degisirse once kaynagi
guncelle, sonra bu betigi calistir - SVG'leri elle duzenleme.
SVG'ler GitHub'da <img> ile gosterildigi icin tum stil dosyanin icinde;
koyu tema prefers-color-scheme ile.
"""
import os, sys

OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.abspath(__file__))

STYLE = """
<style>
  .bg{fill:#f6f8fa} .card{fill:#ffffff;stroke:#d1d9e0;stroke-width:1}
  .sans{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI","Noto Sans",Helvetica,Arial,sans-serif}
  .mono{font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,"Liberation Mono",monospace}
  .ink{fill:#1f2328} .mute{fill:#59636e} .warn{fill:#9a4a00}
  .h1{font-size:22px;font-weight:700} .h2{font-size:14px;font-weight:600} .sub{font-size:14px}
  .s11{font-size:11px} .s12{font-size:12px} .s13{font-size:13px} .b{font-weight:700}
  .pcb{fill:#1f2a37} .pcb2{fill:#2a3848} .pad{fill:#c49a3c} .metal{fill:#b7c0ca}
  .t-on{fill:#e6edf3;font-weight:600} .t-off{fill:#7d8b9a} .t-flash{fill:#e0925a} .t-metal{fill:#2a3441}
  .rf{fill:#17332b} .t-rf{fill:#cfe3d8} .t-rfm{fill:#7fa594}
  .box{fill:#ffffff;stroke:#1f2328;stroke-width:1.5}
  .thin{fill:none;stroke:#1f2328;stroke-width:2}
  .plate{fill:none;stroke:#1f2328;stroke-width:3}
  .dot{fill:#1f2328}
  .cas{fill:none;stroke:#2b3542;stroke-width:7;stroke-linecap:round;stroke-linejoin:round}
  .w{fill:none;stroke-width:4;stroke-linecap:round;stroke-linejoin:round}
  .casp{fill:none;stroke:#2b3542;stroke-width:10;stroke-linecap:round;stroke-linejoin:round}
  .p{fill:none;stroke-width:7;stroke-linecap:round;stroke-linejoin:round}
  .lead{fill:none;stroke:#2b3542;stroke-width:12;stroke-linecap:round;stroke-linejoin:round}
  .dash{stroke-dasharray:9 7;stroke-linecap:butt}
  .red{stroke:#d93a2f} .black{stroke:#15181c} .white{stroke:#f3f3f1} .yellow{stroke:#e2b21b}
  .purple{stroke:#8456d4} .blue{stroke:#2b63d9} .mag{stroke:#cc36ad} .orange{stroke:#ec8a1a}
  .cyan{stroke:#1fb6cc} .green{stroke:#2ea24c}
  .antenna{fill:#1b1f24}
  .outline{fill:#ffffff;stroke:#1f2328;stroke-width:1.5}
  .fill2{fill:#eaeef2;stroke:#1f2328;stroke-width:1.5}
  .dim{fill:none;stroke:#59636e;stroke-width:1}
  .ext{fill:none;stroke:#8c959f;stroke-width:1;stroke-dasharray:3 3}
  .spar{fill:none;stroke:#9a4a00;stroke-width:1.5;stroke-dasharray:6 4}
  .brk{fill:none;stroke:#59636e;stroke-width:1.2;stroke-dasharray:4 3}
  .arrow{fill:#59636e}
  .rule{stroke:#d1d9e0;stroke-width:1}
  .prop{fill:none;stroke:#2ea24c;stroke-width:4;stroke-linecap:round}
  @media (prefers-color-scheme:dark){
    .bg{fill:#0d1117} .card{fill:#151b23;stroke:#3d444d}
    .ink{fill:#e6edf3} .mute{fill:#9198a1} .warn{fill:#f0a44b}
    .pcb{fill:#253344} .pcb2{fill:#314258} .metal{fill:#8e98a3} .t-metal{fill:#1a2129}
    .rf{fill:#1c3b32}
    .box{fill:#1b222c;stroke:#c9d1d9} .thin{stroke:#c9d1d9} .plate{stroke:#c9d1d9} .dot{fill:#c9d1d9}
    .cas,.casp,.lead{stroke:#b3bdc8} .black{stroke:#0b0d10}
    .antenna{fill:#3a4350}
    .outline{fill:#151b23;stroke:#c9d1d9} .fill2{fill:#222a35;stroke:#c9d1d9}
    .dim{stroke:#9198a1} .arrow{fill:#9198a1} .ext{stroke:#6e7681} .brk{stroke:#9198a1}
    .spar{stroke:#f0a44b} .rule{stroke:#3d444d}
  }
</style>
"""


def svg(w, h, body, label):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w} {h}" width="{w}" height="{h}" '
            f'role="img" aria-label="{label}">\n{STYLE}\n'
            f'<rect class="bg" x="0" y="0" width="{w}" height="{h}" rx="10"/>\n{body}\n</svg>\n')


def wire(d, color, dash=False):
    extra = " dash" if dash else ""
    return (f'<path class="cas{extra}" d="{d}"/><path class="w {color}{extra}" d="{d}"/>\n')


def pwr(d, color):
    return f'<path class="casp" d="{d}"/><path class="p {color}" d="{d}"/>\n'


def text(x, y, s, cls, anchor=None, rot=None):
    a = f' text-anchor="{anchor}"' if anchor else ""
    r = f' transform="rotate({rot} {x} {y})"' if rot is not None else ""
    return f'<text class="{cls}" x="{x}" y="{y}"{a}{r}>{s}</text>\n'


def dot(x, y, r=4.5):
    return f'<circle class="dot" cx="{x}" cy="{y}" r="{r}"/>\n'


def ground(x, y, up=False):
    s = -1 if up else 1
    return (f'<path class="thin" d="M{x-12} {y} H{x+12} M{x-8} {y+6*s} H{x+8} M{x-4} {y+12*s} H{x+4}"/>\n')


def cap(x, y_top, y_bot, plus_bottom, label=None, lx=None, ly=None, anchor="end"):
    """Dikey polarize kondansator: iki ucu (x,y_top) ve (x,y_bot)."""
    mid = (y_top + y_bot) / 2
    a, b = mid - 5, mid + 5
    out = f'<path class="thin" d="M{x} {y_top} V{a} M{x} {b} V{y_bot}"/>\n'
    if plus_bottom:
        out += f'<path class="plate" d="M{x-12} {a} Q{x} {a+5} {x+12} {a}"/><path class="plate" d="M{x-12} {b} H{x+12}"/>\n'
        out += text(x + 16, b + 12, "+", "sans s12 ink")
    else:
        out += f'<path class="plate" d="M{x-12} {a} H{x+12}"/><path class="plate" d="M{x-12} {b} Q{x} {b-5} {x+12} {b}"/>\n'
        out += text(x + 16, a - 2, "+", "sans s12 ink")
    if label:
        out += text(lx, ly, label, "sans s12 ink", anchor)
    return out


def board(x, y, w, h, left, right, pitch, y0, module_name, pin_dx=20):
    """ESP32 gelistirme karti. left/right: [(etiket, sinif)]"""
    out = f'<rect class="pcb" x="{x}" y="{y}" width="{w}" height="{h}" rx="8"/>\n'
    cx = x + w / 2
    out += f'<rect class="pcb2" x="{cx-40}" y="{y+8}" width="80" height="24" rx="2"/>\n'
    pts = []
    for i in range(6):
        xx = cx - 30 + i * 12
        pts += [f"{xx},{y+28}", f"{xx},{y+12}"] if i % 2 == 0 else [f"{xx},{y+12}", f"{xx},{y+28}"]
    out += f'<polyline fill="none" stroke="#c49a3c" stroke-width="2" points="{" ".join(pts)}"/>\n'
    out += f'<rect class="metal" x="{cx-35}" y="{y+36}" width="70" height="132" rx="3"/>\n'
    out += text(cx, y + 96, "ESP32", "mono s11 t-metal", "middle")
    out += text(cx, y + 110, module_name[0], "mono s11 t-metal", "middle")
    out += text(cx, y + 124, module_name[1], "mono s11 t-metal", "middle")
    lx, rx = x + pin_dx, x + w - pin_dx
    for i, (lab, cls) in enumerate(left):
        yy = y0 + i * pitch
        out += f'<circle class="pad" cx="{lx}" cy="{yy}" r="6"/>'
        out += text(lx + 14, yy + 4, lab, f"mono s12 {cls}")
    for i, (lab, cls) in enumerate(right):
        yy = y0 + i * pitch
        out += f'<circle class="pad" cx="{rx}" cy="{yy}" r="6"/>'
        out += text(rx - 14, yy + 4, lab, f"mono s12 {cls}", "end")
    out += f'<rect class="metal" x="{cx-20}" y="{y+h-10}" width="40" height="22" rx="3"/>\n'
    out += text(cx, y + h + 30, "USB", "mono s11 mute", "middle")
    return out


def nrf(x, y, pins, irq_y):
    """nRF24 modulu, sol kenarda tek sutun pin + ic kosede 2x4 header numaralandirmasi.
    pins: [(y, ad, fiziksel_no)]"""
    out = f'<rect class="rf" x="{x}" y="{y}" width="220" height="{irq_y - y + 26}" rx="6"/>\n'
    out += text(x, y - 12, "nRF24L01+ PA/LNA", "sans h2 ink")
    for (py, name, no) in pins:
        out += f'<circle class="pad" cx="{x}" cy="{py}" r="6"/>'
        out += text(x + 16, py + 4, name, "mono s13 t-rf")
        out += text(x + 66, py + 4, f"pin {no}", "mono s12 t-rfm")
    out += f'<circle class="pad" cx="{x}" cy="{irq_y}" r="6" opacity=".45"/>'
    out += text(x + 16, irq_y + 4, "IRQ", "mono s13 t-rfm")
    out += text(x + 66, irq_y + 4, "pin 8 · n/c", "mono s12 t-rfm")
    # 2x4 header, ustten bakis
    hx = x + 178
    out += text(hx, y + 30, "2×4 header", "sans s11 t-rf", "middle")
    for r in range(4):
        for c in range(2):
            n = r * 2 + c + 1
            px, py = hx - 18 + c * 36, y + 56 + r * 34
            if n == 1:
                out += f'<rect class="pad" x="{px-10}" y="{py-10}" width="20" height="20"/>'
            else:
                out += f'<circle class="pad" cx="{px}" cy="{py}" r="10"/>'
            out += text(px, py + 4, str(n), "mono s11 b t-metal", "middle")
    out += text(hx, y + 56 + 3 * 34 + 30, "square = pin 1", "sans s11 t-rf", "middle")
    # SMA + anten
    ax = x + 220
    ay = irq_y - 30
    out += f'<rect class="metal" x="{ax}" y="{ay}" width="14" height="16" rx="2"/>'
    out += f'<rect class="antenna" x="{ax+6}" y="{y+100}" width="14" height="{ay - y - 96}" rx="7"/>\n'
    return out


def notes(x, y, w, lines, title="Notes"):
    h = 44 + len(lines) * 24
    out = f'<rect class="card" x="{x}" y="{y}" width="{w}" height="{h}" rx="6"/>\n'
    out += text(x + 18, y + 28, title, "sans h2 ink")
    for i, ln in enumerate(lines):
        out += text(x + 18, y + 56 + i * 24, ln, "sans s13 ink")
    return out


# ---------------------------------------------------------------------------
# 1) Ucak: tam ucus devresi
# ---------------------------------------------------------------------------
def aircraft():
    B = []
    B.append(text(24, 36, "Aircraft — full flight circuit", "sans h1 ink"))
    B.append(text(24, 60, "ESP32 DevKitC 38-pin (WROOM-32D) · nRF24L01+ PA/LNA · throttle, elevator, rudder",
                  "sans sub mute"))
    b = ['<g transform="translate(0 40)">']
    b.append(text(540, 58, "pin labels as printed on the board", "sans s12 mute", "middle"))
    left = [("3V3", "t-on"), ("EN", "t-off"), ("VP", "t-off"), ("VN", "t-off"), ("34", "t-off"),
            ("35", "t-off"), ("32", "t-off"), ("33", "t-off"), ("25", "t-on"), ("26", "t-on"),
            ("27", "t-on"), ("14", "t-off"), ("12", "t-off"), ("GND", "t-on"), ("13", "t-off"),
            ("D2", "t-flash"), ("D3", "t-flash"), ("CMD", "t-flash"), ("5V", "t-on")]
    right = [("GND", "t-on"), ("23", "t-on"), ("22", "t-off"), ("TX", "t-off"), ("RX", "t-off"),
             ("21", "t-off"), ("GND", "t-off"), ("19", "t-on"), ("18", "t-on"), ("5", "t-on"),
             ("17", "t-off"), ("16", "t-off"), ("4", "t-on"), ("0", "t-off"), ("2", "t-off"),
             ("15", "t-off"), ("D1", "t-flash"), ("D0", "t-flash"), ("CLK", "t-flash")]

    # --- guc: raydan karta (kartin altinda kalsin diye once) ---
    b.append(pwr("M325 370 V670 H460", "red"))
    b.append(pwr("M300 370 V665", "black"))
    b.append(pwr("M300 520 H317 A8 8 0 0 1 333 520 H460", "black"))
    b.append(board(440, 70, 200, 640, left, right, 30, 130, ("WROOM", "-32D")))

    # --- LiPo -> ESC + UBEC (pil hatti asagi, UBEC'e) ---
    b.append(pwr("M140 126 V140 H70 V665 H100", "black"))
    b.append(pwr("M140 140 V170", "black"))
    b.append(pwr("M190 126 V155 H85 V645 H100", "red"))
    b.append(pwr("M190 155 V170", "red"))
    b.append(dot(140, 140))
    b.append(dot(190, 155))
    b.append('<rect class="box" x="100" y="80" width="130" height="46" rx="4"/>')
    b.append(text(165, 100, "LiPo 3S", "sans s13 b ink", "middle"))
    b.append(text(165, 117, "2200 mAh · 30C", "sans s12 mute", "middle"))
    b.append('<path class="thin" d="M100 178 H58 M100 190 H58 M100 202 H58"/>')
    b.append('<rect class="box" x="100" y="170" width="130" height="70" rx="4"/>')
    b.append(text(165, 200, "ESC 30 A", "sans s13 b ink", "middle"))
    b.append(text(165, 219, "BEC disabled", "sans s12 mute", "middle"))
    b.append('<circle class="box" cx="36" cy="190" r="22"/>')
    b.append(text(36, 195, "M", "sans s13 b ink", "middle"))
    b.append(text(36, 230, "A2212", "sans s11 mute", "middle"))
    b.append(text(36, 244, "1000 KV", "sans s11 mute", "middle"))
    b.append(text(36, 258, "10×4.5", "sans s11 mute", "middle"))
    # UBEC: karti ve servolari besleyen tek 5 V kaynagi
    b.append('<rect class="box" x="100" y="620" width="130" height="70" rx="4"/>')
    b.append(text(165, 650, "UBEC", "sans s13 b ink", "middle"))
    b.append(text(165, 669, "5 V · 3 A", "sans s12 mute", "middle"))
    b.append(pwr("M230 645 H292 A8 8 0 0 1 308 645 H325", "red"))
    b.append(pwr("M230 665 H300", "black"))
    b.append(dot(325, 645))
    b.append(dot(300, 665))

    # --- 3 telli kablolar: cihaz -> servo rayi ---
    for d, c in (("M286 370 H240 V205 H230", "white"),
                 ("M286 400 H258 V495 H230", "yellow"),
                 ("M286 430 H278 V585 H230", "purple")):
        b.append(f'<path class="lead" d="{d}"/><path class="w {c}" d="{d}"/>')
    for yy, name in ((470, "Elevator"), (560, "Rudder")):
        b.append(f'<rect class="box" x="100" y="{yy}" width="130" height="50" rx="4"/>')
        b.append(text(165, yy + 22, name, "sans s13 b ink", "middle"))
        b.append(text(165, yy + 39, "servo · 9 g", "sans s12 mute", "middle"))

    # --- servo rayi ---
    b.append(text(325, 330, "servo rail", "sans s12 mute", "middle"))
    for xx, s in ((300, "−"), (325, "+"), (350, "S")):
        b.append(text(xx, 347, s, "mono s12 mute", "middle"))
    b.append('<rect class="pcb" x="286" y="354" width="78" height="92" rx="4"/>')
    for yy in (370, 400, 430):
        for xx in (300, 325, 350):
            b.append(f'<circle class="pad" cx="{xx}" cy="{yy}" r="6"/>')

    # --- sinyaller ---
    b.append(wire("M460 370 H350", "white"))
    b.append(wire("M460 400 H350", "yellow"))
    b.append(wire("M460 430 H350", "purple"))
    b.append(text(370, 363, "ESC", "sans s11 ink"))
    b.append(text(370, 393, "ELEV", "sans s11 ink"))
    b.append(text(370, 423, "RUD", "sans s11 ink"))

    # 10k: ESC sinyali -> GND
    b.append('<path class="thin" d="M425 370 V352 M425 312 V300"/>')
    b.append('<rect class="box" x="419" y="312" width="12" height="40" rx="2"/>')
    b.append(ground(425, 300, up=True))
    b.append(dot(425, 370, 4))
    b.append(text(412, 337, "10k", "sans s12 ink", "end"))

    # BEC hatti kondansatoru
    b.append('<path class="thin" d="M325 560 H385 V574"/>')
    b.append(cap(385, 574, 598, plus_bottom=False))
    b.append(ground(385, 598))
    b.append(dot(325, 560))
    b.append(text(385, 632, "bulk cap", "sans s11 mute", "middle"))
    b.append(text(385, 646, "on 5 V rail", "sans s11 mute", "middle"))
    b.append(text(430, 662, "UBEC 5 V", "sans s11 mute", "end"))
    b.append(text(430, 512, "GND", "sans s11 mute", "end"))

    # --- nRF24 ---
    b.append(text(650, 88, "3V3 → VCC, routed under the board", "sans s11 mute"))
    b.append(wire("M460 130 V96 H640", "red", dash=True))
    b.append(wire("M640 96 H700 V150 H900", "red"))
    b.append(wire("M620 130 H680 V190 H900", "black"))
    b.append(wire("M620 160 H660 V230 H900", "cyan"))
    b.append(wire("M620 340 H740 V270 H900", "green"))
    b.append(wire("M620 370 H770 V310 H900", "orange"))
    b.append(wire("M620 400 H800 V350 H900", "mag"))
    b.append(wire("M620 490 H830 V390 H900", "blue"))
    b.append(cap(875, 150, 190, plus_bottom=False, label="10–100 µF", lx=858, ly=175))
    b.append(dot(875, 150, 4))
    b.append(dot(875, 190, 4))
    b.append(nrf(900, 118, [(150, "VCC", 2), (190, "GND", 1), (230, "MOSI", 6), (270, "MISO", 7),
                            (310, "SCK", 5), (350, "CSN", 4), (390, "CE", 3)], 430))

    b.append(notes(670, 515, 490, [
        "nRF24 VCC → 3V3 (top-left pin). Never 5 V.",
        "Board and servos run from a separate 5 V / 3 A UBEC; the ESC's BEC is disabled.",
        "10k on GPIO25 holds the ESC line low while the board boots.",
        "Keep off GPIO 0, 2, 12, 15, 14 and the flash pins D0–D3 / CMD / CLK.",
        "Unplug the LiPo before connecting USB to flash.",
    ]))

    # lejant (notlarin altinda, tek satir)
    b.append(pwr("M684 735 H712", "red"))
    b.append(text(722, 740, "5 V · UBEC", "sans s12 ink"))
    b.append(pwr("M812 735 H840", "black"))
    b.append(text(850, 740, "GND", "sans s12 ink"))
    b.append(wire("M902 735 H930", "red", dash=True))
    b.append(text(940, 740, "3.3 V, under the board", "sans s12 ink"))
    b.append("</g>")
    body = "\n".join(B + b)
    return svg(1180, 800, body,
               "Aircraft wiring. ESP32 DevKitC: GPIO25 to ESC signal with a 10k pull-down, GPIO26 elevator "
               "servo, GPIO27 rudder servo, all through a servo rail. The 3S LiPo feeds both a 30 A ESC "
               "driving the A2212 motor and a separate 5 V 3 A UBEC, which powers the servo rail and the "
               "board 5V pin through a bulk capacitor; the ESC's own BEC is disabled. nRF24L01+ on 3V3 "
               "with a 10-100 uF capacitor: MOSI 23, MISO 19, SCK 18, CSN 5, CE 4.")


# ---------------------------------------------------------------------------
# 2) Yer istasyonu: telsiz baglantisi
# ---------------------------------------------------------------------------
def ground_station():
    B = [text(24, 36, "Ground station — radio wiring", "sans h1 ink"),
         text(24, 60, "ESP32 DevKit V1 30-pin (WROOM-32) · nRF24L01+ PA/LNA · same five GPIOs as the aircraft",
              "sans sub mute")]
    b = ['<g transform="translate(0 40)">']
    b.append(text(180, 58, "pin labels as printed on the board", "sans s12 mute", "middle"))
    off = "t-off"
    left = [(s, off) for s in ("EN", "VP", "VN", "D34", "D35", "D32", "D33", "D25", "D26", "D27",
                               "D14", "D12", "D13", "GND", "VIN")]
    right = [("D23", "t-on"), ("D22", off), ("TX0", off), ("RX0", off), ("D21", off), ("D19", "t-on"),
             ("D18", "t-on"), ("D5", "t-on"), ("TX2", off), ("RX2", off), ("D4", "t-on"), ("D2", off),
             ("D15", off), ("GND", "t-on"), ("3V3", "t-on")]
    b.append(board(80, 70, 200, 520, left, right, 30, 130, ("WROOM", "-32")))
    b.append(wire("M260 130 H300 V150 H560", "cyan"))
    b.append(wire("M260 280 H330 V190 H560", "green"))
    b.append(wire("M260 310 H360 V230 H560", "orange"))
    b.append(wire("M260 340 H390 V270 H560", "mag"))
    b.append(wire("M260 430 H420 V310 H560", "blue"))
    b.append(wire("M260 520 H450 V350 H560", "black"))
    b.append(wire("M260 550 H480 V390 H560", "red"))
    b.append(cap(530, 350, 390, plus_bottom=True, label="10–100 µF", lx=552, ly=418))
    b.append(dot(530, 350, 4))
    b.append(dot(530, 390, 4))
    b.append(nrf(560, 118, [(150, "MOSI", 6), (190, "MISO", 7), (230, "SCK", 5), (270, "CSN", 4),
                            (310, "CE", 3), (350, "GND", 1), (390, "VCC", 2)], 430))
    b.append(notes(500, 478, 380, [
        "VCC → 3V3, never 5 V. One row off on the",
        "2×4 header reverses the supply.",
        "10–100 µF right at the module pins, or",
        "radio.begin() succeeds only sometimes.",
        "SPI runs at 4 MHz — dupont wire is not",
        "reliable at the library's 10 MHz default.",
    ]))
    b.append("</g>")
    return svg(900, 720, "\n".join(B + b),
               "Ground station wiring. ESP32 DevKit V1 to nRF24L01+: D23 MOSI, D19 MISO, D18 SCK, D5 CSN, "
               "D4 CE, GND, 3V3 to VCC with a 10-100 uF capacitor at the module pins.")


# ---------------------------------------------------------------------------
# 3) Govde olculeri
# ---------------------------------------------------------------------------
def arrow_h(x1, x2, y):
    return (f'<path class="dim" d="M{x1} {y} H{x2}"/>'
            f'<path class="arrow" d="M{x1} {y} l7 -3.5 v7 z"/><path class="arrow" d="M{x2} {y} l-7 -3.5 v7 z"/>\n')


def arrow_v(x, y1, y2):
    return (f'<path class="dim" d="M{x} {y1} V{y2}"/>'
            f'<path class="arrow" d="M{x} {y1} l-3.5 7 h7 z"/><path class="arrow" d="M{x} {y2} l-3.5 -7 h7 z"/>\n')


def cg(x, y, r=10):
    return (f'<circle class="outline" cx="{x}" cy="{y}" r="{r}"/>'
            f'<path class="dot" d="M{x} {y} V{y-r} A{r} {r} 0 0 1 {x+r} {y} Z"/>'
            f'<path class="dot" d="M{x} {y} V{y+r} A{r} {r} 0 0 1 {x-r} {y} Z"/>\n')


def airframe():
    """Ucagin YAPILMIS hali. Tasarim planindan farklari: polihedral yerine ortadan
    tek dihedral kirimi, kanat HK 285 yerine 230, agirlik 1105 g."""
    s = 0.42
    fx, cy = 110, 370                     # firewall, govde ekseni
    X = lambda mm: round(fx + mm * s, 1)  # istasyon -> px
    Y = lambda mm: round(cy + mm * s, 1)  # aciklik -> px
    le, te = X(230), X(430)
    b = []
    b.append(text(24, 30, "TOP VIEW", "sans s12 b mute"))
    # govde + motor + pervane
    b.append(f'<rect class="fill2" x="{fx}" y="{Y(-37.5)}" width="{round(1050*s,1)}" height="{round(75*s,1)}"/>')
    b.append(f'<rect class="box" x="{fx-18}" y="{cy-8}" width="18" height="16" rx="2"/>')
    b.append(f'<path class="prop" d="M{fx-22} {Y(-127)} V{Y(127)}"/>')
    # kanat: dikdortgen, sivrilme yok
    b.append(f'<rect class="outline" x="{le}" y="{Y(-700)}" width="{te-le}" height="{round(1400*s,1)}"/>')
    # aileron bolgesi: uctan 250, firar kenarindan 45 - isaretli, kesilmemis
    for k in (-700, 450):
        b.append(f'<rect class="brk" x="{X(385)}" y="{Y(k)}" width="{round(45*s,1)}" '
                 f'height="{round(250*s,1)}" fill="none"/>')
    b.append(text(te + 10, Y(-600), "aileron area", "sans s11 mute"))
    b.append(text(te + 10, Y(-600) + 13, "250 × 45, not cut", "sans s11 mute"))
    b.append(f'<path class="spar" d="M{X(280)} {Y(-700)} V{Y(700)}"/>')
    # ara bolmeler
    for st in (150, 285, 485, 750):
        b.append(f'<path class="ext" d="M{X(st)} {Y(-37.5)} V{Y(37.5)}"/>')
    b.append(text(X(760), Y(37.5) + 16, "bulkheads 150 / 285 / 485 / 750", "sans s11 mute", "end"))
    # yatay + dikey dengeleyici
    b.append(f'<rect class="outline" x="{X(900)}" y="{Y(-200)}" width="{round(150*s,1)}" height="{round(400*s,1)}"/>')
    b.append(f'<rect class="dot" x="{X(910)}" y="{cy-1.5}" width="{round(140*s,1)}" height="3"/>')
    b.append(cg(X(280), cy))
    b.append(text(X(280) + 14, cy - 20, "CG", "sans s12 b ink"))
    # olculer
    b.append(f'<path class="ext" d="M{le} {Y(-700)} H44 M{le} {Y(700)} H44"/>')
    b.append(arrow_v(50, Y(-700), Y(700)))
    b.append(text(42, cy, "1400", "mono s13 ink", "middle", -90))
    b.append(f'<path class="ext" d="M{le} {Y(-700)} V44 M{te} {Y(-700)} V44"/>')
    b.append(arrow_h(le, te, 50))
    b.append(text((le + te) / 2, 42, "200", "mono s13 ink", "middle"))
    b.append(text(X(280) + 6, Y(-700) + 18, "spar · 50 mm behind LE", "sans s11 warn", None, 90))
    b.append(f'<path class="ext" d="M{fx} {cy+16} V706 M{X(1050)} {cy} V706 M{le} {Y(700)} V688"/>')
    b.append(arrow_h(fx, le, 682))
    b.append(text((fx + le) / 2, 676, "230", "mono s12 ink", "middle"))
    b.append(arrow_h(fx, X(1050), 700))
    b.append(text((fx + X(1050)) / 2, 718, "1050 · firewall to tail", "mono s12 ink", "middle"))
    b.append(f'<path class="ext" d="M{X(900)} {Y(-200)} V264 M{X(1050)} {Y(-200)} V264"/>')
    b.append(arrow_h(X(900), X(1050), 270))
    b.append(text((X(900) + X(1050)) / 2, 262, "150", "mono s12 ink", "middle"))
    b.append(f'<path class="ext" d="M{X(1050)} {Y(-200)} H581 M{X(1050)} {Y(200)} H581"/>')
    b.append(arrow_v(575, Y(-200), Y(200)))
    b.append(text(590, cy, "400", "mono s12 ink", "middle", -90))

    # --- sag sutun ---
    R = 640
    b.append(text(R, 40, "Airframe", "sans h1 ink"))
    b.append(text(R, 62, "as built, in mm · 5 mm foam board + wooden spar", "sans s13 mute"))
    # yan gorunus
    b.append(text(R, 98, "SIDE VIEW", "sans s12 b mute"))
    t = 0.28
    sx, base = 680, 190
    SX = lambda mm: round(sx + mm * t, 1)
    # ust kenar duz; govde kuyruga dogru alttan daraliyor (80 -> 45)
    top_f = top_t = round(base - 80 * t, 1)
    bot_t = round(top_f + 45 * t, 1)
    b.append(f'<path class="fill2" d="M{sx} {top_f} H{SX(1050)} V{bot_t} L{SX(400)} {base} H{sx} Z"/>')
    # KFm-2: on yari kalin, arka yari ince
    b.append(f'<path class="outline" d="M{SX(230)} {top_f} V{top_f-5.6} H{SX(330)} V{top_f-2.8} H{SX(430)} V{top_f} Z"/>')
    b.append(f'<rect class="outline" x="{SX(900)}" y="{top_t-2}" width="{round(150*t,1)}" height="2"/>')
    fin_top = round(top_t - 2 - 180 * t, 1)
    b.append(f'<path class="outline" d="M{SX(910)} {top_t-2} H{SX(1050)} V{fin_top} H{SX(950)} Z"/>')
    b.append(f'<rect class="box" x="{sx-14}" y="{base-18}" width="14" height="14" rx="2"/>')
    b.append(f'<path class="prop" d="M{sx-18} {base-11-35.6} V{base-11+35.6}"/>')
    # burun altinda kurban seridi
    b.append(f'<path class="thin" style="stroke-width:3" d="M{sx} {base+3} H{SX(200)}"/>')
    b.append(text(SX(100), base + 20, "sacrificial strip", "sans s11 mute", "middle"))
    b.append(cg(SX(280), round(base - 40 * t, 1), 7))
    b.append(text(SX(280) + 12, base + 20, "CG", "sans s12 b ink"))
    b.append(text(SX(330), top_f - 14, "high wing · +1.4° incidence", "sans s11 mute", "middle"))
    b.append(text(SX(1050), fin_top - 10, "stab 0° · decalage 1.4°", "sans s11 mute", "end"))
    # on gorunus
    b.append(text(R, 258, "FRONT VIEW", "sans s12 b mute"))
    f = 0.22
    c0, wy = 820, 318
    FX = lambda mm: round(c0 + mm * f, 1)
    tip = round(wy - 123 * f, 1)   # 10 derece, uc yukselmesi 700*tan10
    tail_top = wy   # ust kenar duz: kuyruk yuzeyleri govde ustu hizasinda
    b.append(f'<circle cx="{c0}" cy="{round(wy+40*f,1)}" r="{round(127*f,1)}" fill="none" class="ext"/>')
    b.append(f'<path class="thin" d="M{c0} {tail_top} V{round(tail_top-180*f,1)}"/>')
    b.append(f'<path class="thin" d="M{FX(-200)} {tail_top} H{FX(200)}"/>')
    b.append(f'<rect class="fill2" x="{FX(-37.5)}" y="{wy}" width="{round(75*f,1)}" height="{round(80*f,1)}"/>')
    b.append(f'<polyline fill="none" class="thin" style="stroke-width:5;stroke-linejoin:round" '
             f'points="{FX(-700)},{tip} {c0},{wy} {FX(700)},{tip}"/>')
    b.append(text(c0, 368, "10° dihedral, one break at the centre · roll comes from rudder",
                  "sans s12 mute", "middle"))

    # tablo
    rows = [("Wingspan", "1400"), ("Chord", "200, constant"), ("Wing area", "28 dm²"),
            ("Airfoil", "KFm-2, step 100 from LE"), ("Dihedral", "10° · one break at centre"),
            ("Fuselage", "1050 × 75 × 80"), ("Wing LE", "230 from firewall"),
            ("CG", "280 · 50 behind LE (25 %)"), ("Incidence", "wing +1.4° · stab 0°"),
            ("Horizontal stab", "400 × 150 · Vh 0.70"), ("Fin", "180 high · Vv 0.036"),
            ("Ready to fly", "1105 g · 39.5 g/dm²")]
    b.append(text(R, 398, "KEY NUMBERS", "sans s12 b mute"))
    for i, (k, v) in enumerate(rows):
        yy = 422 + i * 24
        b.append(text(R, yy, k, "sans s13 ink"))
        b.append(text(980, yy, v, "mono s13 ink", "end"))
        b.append(f'<path class="rule" d="M{R} {yy+7} H980"/>')
    b.append(text(R, 706, "Motor 2° down, 2° right · elevator ±14 mm, rudder ±20 mm.", "sans s11 mute"))
    b.append(text(R, 720, "No landing gear: hand launch, belly landing.", "sans s11 mute"))
    return svg(1000, 730, "\n".join(b),
               "Airframe top, side and front views as built. Wingspan 1400 mm, constant 200 mm chord, "
               "10 degree dihedral with a single break at the centre, fuselage 1050 mm, wing leading edge 230 mm "
               "behind the firewall, CG 280 mm on the spar line at 25 percent chord, stabiliser 400 by "
               "150 mm, ready to fly 1105 g.")


os.makedirs(OUT, exist_ok=True)
for name, fn in (("aircraft_wiring.svg", aircraft), ("ground_station_wiring.svg", ground_station),
                 ("airframe_layout.svg", airframe)):
    with open(os.path.join(OUT, name), "w", encoding="utf-8", newline="\n") as fh:
        fh.write(fn())
    print("yazildi", name)
