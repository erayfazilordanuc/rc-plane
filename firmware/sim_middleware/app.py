# -*- coding: utf-8 -*-
"""
sim_middleware/app.py

RC web arayuzu  ->  vJoy sanal joystick koprusu.

Telefon USB ile bagli, "adb reverse tcp:5000 tcp:5000" ile telefonun
localhost:5000 adresi bu sunucuya dusuyor. Arayuz 1000-2000 us araligindaki
kanal degerlerini Socket.IO uzerinden yolluyor, burada 0-32767 araligina
cevrilip vJoy cihazina tek bir HID raporu olarak yaziliyor.

GECIKME NOTLARI (kodun her yerinde bunlara sadik kalindi):
  - Paket geldigi anda vJoy'a yaziliyor. Arada kuyruk, zamanlayici ya da
    "60 Hz'e dusur" gibi bir ara katman YOK.
  - Sicak yolda (on_rc) log, print, string formatlama, sozluk kopyalama yok.
    Istatistikler sayac olarak toplanip watchdog thread'inde basiliyor.
  - Istemciye ack donulmuyor: her paket icin ek bir TCP gidis-donusu olmuyor.
  - Nagle kapatiliyor (disable_nagle_algorithm). Acik kalirsa birkac baytlik
    paketler icin 40 ms'ye varan bekleme gorulebiliyor.
  - Werkzeug istek logu kapali; konsola yazmak Windows'ta pahali.

Calistirma:
    python app.py                 # vJoy'a yazar
    python app.py --dry-run       # vJoy olmadan, sadece sayac/log ile test
"""

import logging
import os
import sys
import threading
import time

from flask import Flask, jsonify, send_from_directory
from flask_socketio import SocketIO

# ============================================================
# AYARLAR
# ============================================================

HOST = os.environ.get("SIM_HOST", "127.0.0.1")   # adb reverse loopback'e dusurur
PORT = int(os.environ.get("SIM_PORT", "5000"))

VJOY_DEVICE_ID = int(os.environ.get("VJOY_ID", "1"))

# Arayuzun urettigi RC araligi (mikrosaniye) ve vJoy'un bekledigi aralik.
US_MIN, US_MID, US_MAX = 1000.0, 1500.0, 2000.0
VJOY_MIN, VJOY_MAX = 0, 32767

# Kanal basina ters cevirme. Simulatorde yuzey ters calisiyorsa once
# simulatorun kendi kalibrasyonunu dene; orada cozulmuyorsa burayi ac.
REVERSE = {
    "ail": False,
    "ele": False,
    "thr": False,
    "rud": False,
    "aux1": False,
    "aux2": False,
}

# Elindeki 3 kanal arayuzde aileron yok. True yapilirsa rudder ayni anda
# aileron eksenine de yazilir, boylece simulatorde kanat da eger.
MIRROR_RUDDER_TO_AILERON = False

# Bu sure boyunca paket gelmezse failsafe: gaz kesilir, diger eksenler notr.
FAILSAFE_MS = 500

# Tek HID raporu ile guncelleme (UpdateVJD): dort ekseni atomik olarak ayni
# karede yazar ve tek DLL cagrisidir. False yapilirsa eksen basina SetAxis
# (HID_USAGE_X / Y / Z / RZ) kullanilir.
USE_BATCH_UPDATE = True

STATS_PERIOD_S = 2.0
DRY_RUN = "--dry-run" in sys.argv

# ------------------------------------------------------------
# EKSEN ESLEMESI  (Mode 2 RC kumanda standardi)
#
#   ail  Aileron   sag cubuk X  ->  vJoy X      (HID_USAGE_X)
#   ele  Elevator  sag cubuk Y  ->  vJoy Y      (HID_USAGE_Y)
#   thr  Throttle  sol cubuk Y  ->  vJoy Z      (HID_USAGE_Z)
#   rud  Rudder    sol cubuk X  ->  vJoy RZ     (HID_USAGE_RZ)
#   aux1/aux2 istege bagli      ->  vJoy RX/RY
#
# Ikinci alan JOYSTICK_POSITION_V2 struct alani (toplu rapor yolu icin),
# ucuncusu pyvjoy HID_USAGE sabitinin adi (SetAxis yolu icin).
# ------------------------------------------------------------
AXES = (
    ("ail",  "wAxisX",    "HID_USAGE_X"),
    ("ele",  "wAxisY",    "HID_USAGE_Y"),
    ("thr",  "wAxisZ",    "HID_USAGE_Z"),
    ("rud",  "wAxisZRot", "HID_USAGE_RZ"),
    ("aux1", "wAxisXRot", "HID_USAGE_RX"),
    ("aux2", "wAxisYRot", "HID_USAGE_RY"),
)

# Arayuzden farkli isimlerle gelebilecek anahtarlar.
ALIASES = {
    "a": "ail", "roll": "ail", "aileron": "ail",
    "e": "ele", "pitch": "ele", "elevator": "ele",
    "t": "thr", "throttle": "thr",
    "r": "rud", "yaw": "rud", "rudder": "rud",
    "aux": "aux1",
}

# Dizi olarak gelirse sira bu.
ARRAY_ORDER = ("ail", "ele", "thr", "rud", "aux1", "aux2")

# Failsafe / acilis degerleri: gaz en altta, geri kalani notr.
NEUTRAL = {"ail": US_MID, "ele": US_MID, "thr": US_MIN,
           "rud": US_MID, "aux1": US_MID, "aux2": US_MID}


# ============================================================
# LOG
# ============================================================

logging.basicConfig(level=logging.INFO, format="%(asctime)s  %(message)s",
                    datefmt="%H:%M:%S")
log = logging.getLogger("sim")
logging.getLogger("werkzeug").setLevel(logging.ERROR)   # istek logu = gecikme


# ============================================================
# 1000-2000 us  ->  0-32767  DONUSUMU
# ============================================================

_SCALE = (VJOY_MAX - VJOY_MIN) / (US_MAX - US_MIN)   # 32767 / 1000 = 32.767


def us_to_vjoy(us, reverse=False):
    """1000-2000 us araligini vJoy'un bekledigi 0-32767 araligina tasir.

    Aralik disi degerler kirpilir: bozuk tek bir paket simulatorde tam
    sapmaya yol acmasin. Kirpma dogrusal eslemeden once yapilir.

        1000 us -> 0        (eksen dibi)
        1500 us -> 16384    (notr)
        2000 us -> 32767    (eksen tepesi)
    """
    if us < US_MIN:
        us = US_MIN
    elif us > US_MAX:
        us = US_MAX
    v = int((us - US_MIN) * _SCALE + 0.5) + VJOY_MIN
    if reverse:
        v = VJOY_MAX - v + VJOY_MIN
    return v


# ============================================================
# vJoy KOPRUSU
# ============================================================

class VJoyBridge(object):
    """vJoy cihazina yazan ince katman.

    Iki yazma yolu var: tek HID raporu (UpdateVJD) ya da eksen basina
    SetAxis. Ikisi de bloklamaz, DLL cagrisi mikrosaniyeler suruyor.
    """

    def __init__(self, device_id, dry_run=False):
        self.device_id = device_id
        self.dry_run = dry_run
        self.dev = None
        self.batch = USE_BATCH_UPDATE
        self._lock = threading.Lock()      # on_rc ile watchdog ayni anda yazabilir
        self._usage = {}                   # kanal adi -> HID_USAGE sabiti
        self._field = dict((name, field) for name, field, _ in AXES)
        self._pyvjoy = None

        if dry_run:
            log.warning("DRY-RUN: vJoy'a yazilmiyor, sadece sayac tutuluyor.")
            return

        try:
            import pyvjoy
        except ImportError:
            raise SystemExit(
                "pyvjoy bulunamadi.  ->  pip install pyvjoy\n"
                "vJoy surucusunun de kurulu olmasi gerekiyor "
                "(vJoy Device Driver, x64)."
            )
        except Exception as exc:
            # vJoyInterface.dll yuklenemedi: vJoy yok ya da mimari uyusmuyor.
            raise SystemExit(
                "vJoyInterface.dll yuklenemedi (%s).\n"
                "64-bit Python kullaniyorsan 64-bit vJoy kurulu olmali; "
                "gerekirse vJoy klasorundeki vJoyInterface.dll dosyasini "
                "pyvjoy paketinin icine kopyala." % exc
            )

        self._pyvjoy = pyvjoy
        for name, _field, usage in AXES:
            self._usage[name] = getattr(pyvjoy, usage)

        try:
            self.dev = pyvjoy.VJoyDevice(device_id)
        except Exception as exc:
            raise SystemExit(
                "vJoy cihazi %d alinamadi (%s).\n"
                "vJoyConf: cihaz etkin mi, X/Y/Z/RZ eksenleri isaretli mi, "
                "cihazi tutan baska bir program var mi?" % (device_id, exc)
            )

        log.info("vJoy cihaz %d alindi (%s yolu).",
                 device_id, "UpdateVJD" if self.batch else "SetAxis")

        # Kullanilmayan eksenler raporda 0 kalmasin: hepsini notre kur.
        self.write(NEUTRAL)

    def write(self, ch):
        """ch: kanal adi -> mikrosaniye. Verilmeyen kanala dokunulmaz."""
        if self.dry_run or self.dev is None:
            return

        if self.batch:
            data = self.dev.data
            for name, value in ch.items():
                field = self._field.get(name)
                if field is not None:
                    setattr(data, field,
                            us_to_vjoy(value, REVERSE.get(name, False)))
            with self._lock:
                self.dev.update()
        else:
            with self._lock:
                for name, value in ch.items():
                    usage = self._usage.get(name)
                    if usage is not None:
                        self.dev.set_axis(
                            usage, us_to_vjoy(value, REVERSE.get(name, False)))

    def failsafe(self):
        self.write(NEUTRAL)

    def close(self):
        if self.dev is not None:
            try:
                self.failsafe()
                self._pyvjoy._sdk.RelinquishVJD(self.device_id)
            except Exception:
                pass


# ============================================================
# SUNUCU
# ============================================================

app = Flask(__name__, static_folder="static", static_url_path="/static")

# async_mode="threading" + simple-websocket: eventlet/gevent monkey-patch
# gerektirmeden gercek WebSocket verir, Python 3.11'de sorunsuz calisir.
# ping araliklari kisa: telefon USB'den koparsa saniyeler icinde anlasilir.
socketio = SocketIO(
    app,
    async_mode="threading",
    cors_allowed_origins="*",      # sayfa ESP32'den servis ediliyorsa gerekli
    ping_interval=5,
    ping_timeout=5,
    logger=False,
    engineio_logger=False,
)

bridge = None

# Sicak yolda paylasilan durum. GIL altinda tek atama atomik oldugu icin
# burada kilit yok; kilit sadece vJoy yazmasinda.
_last_rx = 0.0        # perf_counter
_pkt_count = 0
_pkt_total = 0
_failsafe_on = True
_stop = threading.Event()


# ------------------------------------------------------------
# SICAK YOL: kanal paketi
#
# Arayuz iki bicimde yollayabilir:
#   {"ail":1500,"ele":1500,"thr":1000,"rud":1500}     okunakli
#   [1500,1500,1000,1500]                             birkac us daha ucuz
# ------------------------------------------------------------
@socketio.on("rc")
def on_rc(msg):
    global _last_rx, _pkt_count, _pkt_total, _failsafe_on

    ch = {}
    if type(msg) is dict:
        for key, value in msg.items():
            name = ALIASES.get(key, key)
            if name in NEUTRAL:
                ch[name] = value
    elif type(msg) is list:
        for i, value in enumerate(msg):
            if i < 6:
                ch[ARRAY_ORDER[i]] = value
    else:
        return

    if MIRROR_RUDDER_TO_AILERON and "rud" in ch and "ail" not in ch:
        ch["ail"] = ch["rud"]

    bridge.write(ch)

    _last_rx = time.perf_counter()
    _pkt_count += 1
    _pkt_total += 1
    _failsafe_on = False
    # Bilerek return yok: ack paketi = fazladan gidis-donus.


@socketio.on("rtt")
def on_rtt(t):
    """Istemcinin gecikme olcmesi icin. Sicak yolun disinda, ack ile doner."""
    return t


@socketio.on("connect")
def on_connect():
    global _last_rx, _failsafe_on
    _last_rx = time.perf_counter()
    _failsafe_on = False
    log.info("arayuz baglandi")


@socketio.on("disconnect")
def on_disconnect():
    global _failsafe_on
    bridge.failsafe()
    _failsafe_on = True
    log.info("arayuz koptu -> failsafe")


@app.route("/")
def index():
    return send_from_directory("static", "index.html")


@app.route("/socket.io.min.js")
def socketio_client():
    """Socket.IO istemcisi. Telefonda internet olmayabilir; kutuphanenin
    yerelden servis edilmesi gerekiyor (bkz. tools/fetch_client.py)."""
    return send_from_directory("static/vendor", "socket.io.min.js")


@app.route("/health")
def health():
    return jsonify(
        vjoy=("dry-run" if DRY_RUN else "device %d" % VJOY_DEVICE_ID),
        failsafe=_failsafe_on,
        packets=_pkt_total,
        age_ms=int((time.perf_counter() - _last_rx) * 1000) if _last_rx else None,
    )


# ------------------------------------------------------------
# WATCHDOG + ISTATISTIK
#
# Sicak yoldan uzak duran tek yardimci thread. Iki isi var: paket akisi
# kesilince failsafe uygulamak ve birikmis sayaci basmak.
# ------------------------------------------------------------
def _watchdog():
    global _pkt_count, _failsafe_on
    fs = FAILSAFE_MS / 1000.0
    next_stats = time.perf_counter() + STATS_PERIOD_S

    while not _stop.wait(0.05):
        now = time.perf_counter()

        if not _failsafe_on and _last_rx and (now - _last_rx) > fs:
            bridge.failsafe()
            _failsafe_on = True
            log.warning("%d ms paket yok -> failsafe (gaz kesildi)", FAILSAFE_MS)

        if now >= next_stats:
            n, _pkt_count = _pkt_count, 0
            if n:
                log.info("%.0f Hz  (%d paket / %.0f s)",
                         n / STATS_PERIOD_S, n, STATS_PERIOD_S)
            next_stats = now + STATS_PERIOD_S


def main():
    global bridge
    bridge = VJoyBridge(VJOY_DEVICE_ID, dry_run=DRY_RUN)

    threading.Thread(target=_watchdog, name="watchdog", daemon=True).start()

    # Nagle kapali: birkac baytlik kanal paketleri TCP tamponunda beklemesin.
    from werkzeug.serving import WSGIRequestHandler
    WSGIRequestHandler.disable_nagle_algorithm = True
    WSGIRequestHandler.protocol_version = "HTTP/1.1"

    log.info("http://%s:%d  --  telefon icin: adb reverse tcp:%d tcp:%d",
             HOST, PORT, PORT, PORT)
    try:
        socketio.run(app, host=HOST, port=PORT,
                     allow_unsafe_werkzeug=True, use_reloader=False)
    except KeyboardInterrupt:
        pass
    finally:
        _stop.set()
        bridge.close()
        log.info("kapandi")


if __name__ == "__main__":
    main()
