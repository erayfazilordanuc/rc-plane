"""Ucagin basit performans modeli: itki / surukleme egrileri ve el atisi simulasyonu.

    python docs/diagrams/flight_model.py      # ozet tablo
    python docs/diagrams/generate.py          # flight_performance.svg de uretilir

Olcum degil, MODEL. Varsayimlar asagida tek yerde; biri olculunce (statik itki,
devir, hiz) buraya yazilir ve grafik yeniden uretilir.

Testler tam gazda yapildi (gaz siniri varsayilan %100, ESC kalibre). Bu yuzden
egriler gaz yuzdesine gore degil, TAM GAZDAKI STATIK ITKIYE gore cizilir: olculmemis
tek buyuk girdi o.
"""
import math

# --- ucak (docs/AIRFRAME.md) ---
M = 1.106                 # kg, ucusa hazir, tartildi
S = 0.28                  # m2, 1400 x 200
AR = 1.4 ** 2 / S         # 7.0
G = 9.81
W = M * G

# --- varsayimlar ---
RHO = 1.20                # kg/m3, deniz seviyesi ~25 C
CL_MAX = 0.9              # KFm-2, Re ~130k, dusuk tahmin
E_OSW = 0.75              # Oswald, dikdortgen kanat + dihedral
CD0 = 0.05                # govde kutu, basamakli profil, acik linkajlar
CD0_BAND = (0.04, 0.07)   # belirsizlik bandi
K = 1 / (math.pi * E_OSW * AR)

# Dinamik itki (G. Staples yaklasik denklemi): T = a * rpm * (b * rpm - V).
# Pervane capi / adimi inc, V m/s, T newton.
PROP_D, PROP_P = 10.0, 4.5

# eCalc propCalc (kullanici calistirdi): A2212 1000 KV, 10x4.5, 3S 2200 mAh, 1105 g.
# Tam gaz statik itki buradan alinir; hiza gore dususu Staples denklemi verir.
# OLCUM DEGIL - terazi sonucu gelince STATIC_G_FULL onunla degistirilir.
ECALC = {
    "static_g": 912, "rpm": 8680, "pitch_kmh": 60, "current_a": 15.14, "power_w": 161.1,
    "c_load": 6.88, "stall_kmh": 30, "level_kmh": 68, "climb_ms": 4.5, "tw": 0.83,
    "flight_min": (7.4, 10.6), "eff_max": 82.5,
}
STATIC_G_FULL = ECALC["static_g"]


def _prop(rpm, d, p):
    a = 4.392e-8 * d ** 3.5 / math.sqrt(p)
    return a * rpm, 4.23e-4 * rpm * p          # (egim, adim hizi m/s)


def _rpm_for(grams, d=PROP_D, p=PROP_P):
    """Staples denkleminde statik itki = a*b*rpm^2."""
    a, b = _prop(1.0, d, p)
    return math.sqrt(grams / 1000 * G / (a * b))


RPM_FULL = _rpm_for(STATIC_G_FULL)   # ~8470; eCalc 8680 der, fark itki denklemlerinin farki


def thrust(v, rpm=None, d=PROP_D, p=PROP_P):
    k, vp = _prop(RPM_FULL if rpm is None else rpm, d, p)
    return max(0.0, k * (vp - v))


def pitch_speed(rpm=None, d=PROP_D, p=PROP_P):
    return _prop(RPM_FULL if rpm is None else rpm, d, p)[1]


def static_g(rpm=None):
    return thrust(0, rpm) / G * 1000


def rpm_for_static(grams):
    """Ayni pervanede statik itki devrin karesiyle olcekleniyor."""
    return RPM_FULL * math.sqrt(grams / static_g())


def drag(v, cd0=None):
    cd0 = CD0 if cd0 is None else cd0
    q = 0.5 * RHO * v * v
    cl = W / (q * S)
    return q * S * (cd0 + K * cl * cl)


def stall_speed(cl_max=None):
    return math.sqrt(2 * W / (RHO * S * (CL_MAX if cl_max is None else cl_max)))


def min_drag_speed(cd0=None):
    cl = math.sqrt((CD0 if cd0 is None else cd0) / K)
    return math.sqrt(2 * W / (RHO * S * cl))


def climb_rate(v, rpm=None, cd0=None):
    return (thrust(v, rpm) - drag(v, cd0)) * v / W


def best_climb(rpm=None, cd0=None):
    vs = stall_speed()
    return max((climb_rate(vs + i * 0.05, rpm, cd0), vs + i * 0.05) for i in range(200))


def max_level_speed(rpm=None, cd0=None):
    v = stall_speed()
    if thrust(v, rpm) < drag(v, cd0):
        return None
    while thrust(v, rpm) >= drag(v, cd0):
        v += 0.01
    return v


def static_to_hold(cd0=None):
    """Tam gazda, stall ustunde herhangi bir hizda irtifa tutmaya yeten en dusuk statik itki (g)."""
    g = static_g()
    while best_climb(rpm_for_static(g), cd0)[0] > 0:
        g -= 5
    return g


def simulate(rpm=None, v0=7.5, h0=2.0, cl_trim=0.6, dt=0.005, t_end=20.0, cd0=None):
    """Noktasal kutle, boyuna duzlem. Elevator sabit (trim), yani CL sabit:
    statik kararli ucak hucum acisini tutar. Pilot mudahalesi yok.
    Donus: (t listesi, h listesi) - yere degince ya da t_end'de biter."""
    cd0 = CD0 if cd0 is None else cd0
    v, gam, h, t = v0, 0.0, h0, 0.0
    ts, hs = [0.0], [h0]
    while t < t_end and h > 0:
        q = 0.5 * RHO * v * v
        lift = q * S * cl_trim
        dr = q * S * (cd0 + K * cl_trim ** 2)
        dv = (thrust(v, rpm) - dr - W * math.sin(gam)) / M
        dg = (lift - W * math.cos(gam)) / (M * v)
        v, gam = v + dv * dt, gam + dg * dt
        h += v * math.sin(gam) * dt
        t += dt
        ts.append(t); hs.append(max(h, 0.0))
    return ts, hs


def summary():
    vs = stall_speed()
    rows = [
        ("Stall hizi (CLmax %.1f)" % CL_MAX, "%.1f m/s" % vs),
        ("En az surukleme hizi", "%.1f m/s" % min_drag_speed()),
        ("Adim hizi, tam gaz", "%.1f m/s" % pitch_speed()),
        ("Statik itki, tam gaz (eCalc)", "%.0f g (%.0f d/d)" % (static_g(), RPM_FULL)),
        ("10 m/s'de itki / statik", "%%%.0f" % (thrust(10) / thrust(0) * 100)),
    ]
    for g in (static_g(), 650, 500):
        rpm = rpm_for_static(g)
        rc, v = best_climb(rpm)
        vmax = max_level_speed(rpm)
        rows.append(("Statik %.0f g: en iyi tirmanis" % g, "%+.2f m/s @ %.1f m/s" % (rc, v)))
        rows.append(("Statik %.0f g: yatay hiz araligi" % g,
                     "%.1f-%.1f m/s" % (vs, vmax) if vmax else "yok (irtifa tutamaz)"))
    for cd0 in (CD0_BAND[0], CD0, CD0_BAND[1]):
        rows.append(("Irtifa tutmaya yeten statik itki (CD0 %.2f)" % cd0, "%.0f g" % static_to_hold(cd0)))
    c = CD0
    while best_climb(None, c)[0] > 0:
        c += 0.005
    rows.append(("Varsayilan itkiyle irtifa tutulamayan CD0", ">= %.2f" % c))
    rpm9 = 8700
    rc9 = max((thrust(v, rpm9, 9, 6) - drag(v)) * v / W for v in [vs + i * 0.05 for i in range(200)])
    rows.append(("9x6 pervane (8700 d/d): adim hizi / en iyi tirmanis",
                 "%.1f m/s / %+.2f m/s" % (pitch_speed(rpm9, 9, 6), rc9)))
    for g in (static_g(), 650, 500, 0):
        ts, hs = simulate(rpm_for_static(g) if g else 0, v0=8.5, cl_trim=0.75)
        rows.append(("Atis sim. 8.5 m/s, statik %.0f g" % g,
                     "%.1f s sonra yerde" % ts[-1] if hs[-1] <= 0 else "20 s'de h=%.1f m" % hs[-1]))
    return rows


if __name__ == "__main__":
    for k, v in summary():
        print(f"{k:52s} {v}")
