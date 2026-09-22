# -*- coding: utf-8 -*-
"""Socket.IO istemci kutuphanesini bir kez indirip static/vendor icine koyar.

Telefonda internet olmayabilir (ucus alaninda genelde yok, sayfa da ESP32'den
geliyor olabilir). Kutuphane PC'de dursun, telefona adb reverse uzerinden
localhost:5000/socket.io.min.js olarak servis edilsin.

    python tools/fetch_client.py
"""

import os
import urllib.request

URL = "https://cdn.socket.io/4.7.5/socket.io.min.js"
DST = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                   "static", "vendor", "socket.io.min.js")

os.makedirs(os.path.dirname(DST), exist_ok=True)
print("indiriliyor:", URL)
urllib.request.urlretrieve(URL, DST)
print("kaydedildi:", DST, "(%d bayt)" % os.path.getsize(DST))
