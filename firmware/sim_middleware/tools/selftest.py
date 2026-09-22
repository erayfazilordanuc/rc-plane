# -*- coding: utf-8 -*-
"""Kopruyu vJoy olmadan ucundan ucuna dogrular.

  1) us -> vJoy esleme degerleri
  2) sunucuya WebSocket ile baglanip 100 Hz kanal akisi surme
  3) gidis-donus gecikmesi
  4) akis kesilince failsafe

Sunucu ayri bir konsolda "python app.py --dry-run" ile calisiyor olmali.
"""

import json
import sys
import time
import urllib.request

sys.path.insert(0, __file__.rsplit("\\", 2)[0].rsplit("/", 2)[0])
sys.argv.append("--dry-run")          # app.py import edilirken vJoy aranmasin

import socketio as sio_client
from app import us_to_vjoy

URL = "http://127.0.0.1:5000"
fail = []


def check(name, got, want):
    ok = got == want
    print("  %-28s %-12s %s" % (name, got, "OK" if ok else "BEKLENEN " + str(want)))
    if not ok:
        fail.append(name)


print("1) esleme")
check("1000 us", us_to_vjoy(1000), 0)
check("1500 us", us_to_vjoy(1500), 16384)
check("2000 us", us_to_vjoy(2000), 32767)
check("1250 us", us_to_vjoy(1250), 8192)
check("900 us (kirpma)", us_to_vjoy(900), 0)
check("2400 us (kirpma)", us_to_vjoy(2400), 32767)
check("1500 us ters", us_to_vjoy(1500, True), 16383)
check("1000 us ters", us_to_vjoy(1000, True), 32767)


def health():
    return json.load(urllib.request.urlopen(URL + "/health", timeout=2))


print("\n2) akis")
sock = sio_client.Client(reconnection=False)
sock.connect(URL, transports=["websocket"])
print("  bagli, transport:", sock.transport())

base = health()["packets"]
N, HZ = 300, 100.0
t0 = time.perf_counter()
for i in range(N):
    sock.emit("rc", [1500 + i, 1500, 1000 + i, 1500])
    time.sleep(max(0, (i + 1) / HZ - (time.perf_counter() - t0)))
dt = time.perf_counter() - t0
time.sleep(0.2)

got = health()["packets"] - base
print("  %d paket / %.2f s  ->  %.0f Hz gonderildi" % (N, dt, N / dt))
check("sunucuya ulasan", got, N)

print("\n3) gidis-donus")
lat = []
for _ in range(20):
    t = time.perf_counter()
    sock.call("rtt", t, timeout=2)
    lat.append((time.perf_counter() - t) * 1000)
    time.sleep(0.01)
lat.sort()
print("  ortanca %.2f ms   en kotu %.2f ms" % (lat[len(lat) // 2], lat[-1]))

print("\n4) failsafe")
check("akis varken", health()["failsafe"], False)
time.sleep(0.8)
check("800 ms sessizlik sonrasi", health()["failsafe"], True)

sock.disconnect()
print("\n" + ("BASARISIZ: " + ", ".join(fail) if fail else "HEPSI GECTI"))
sys.exit(1 if fail else 0)
