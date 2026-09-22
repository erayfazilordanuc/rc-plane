/* ============================================================
 * rc_sim_client.js
 *
 * RC web arayuzu -> PC simulatoru koprusunun istemci tarafi.
 * Sayfaya once Socket.IO istemcisi, sonra bu dosya eklenir:
 *
 *   <script src="http://localhost:5000/socket.io.min.js"></script>
 *   <script src="http://localhost:5000/static/rc_sim_client.js"></script>
 *
 * Kullanim - slider'in oninput'unda tek satir:
 *
 *   simSetAxes({ ail: 1500, ele: 1500, thr: 1000, rud: 1500 });
 *
 * Verilen kanallar guncellenir, verilmeyenler son degerinde kalir.
 *
 * GONDERIM MODELI - zamanlayiciyla degil, DEGISIM aninda.
 * Sabit 50 Hz'lik bir setInterval, kolun hareketiyle paketin cikisi
 * arasina en kotu halde bir tam periyot koyar. Burada gonderimi
 * dokunma olayinin kendisi tetikliyor; MIN_GAP_MS yalnizca 120 Hz
 * ornekleyen bir dokunmatikte baglantiyi bogmamak icin taban koyuyor.
 * Bekleyen bir degisiklik kalirsa bir sonraki tik onu bosaltiyor.
 * ============================================================ */
"use strict";

/* Sayfa ESP32'den servis ediliyorsa mutlak adres sart. adb reverse
   telefonun localhost:5000 portunu PC'ye dusuruyor. */
const SIM_URL      = "http://localhost:5000";

const MIN_GAP_MS   = 10;    // iki paket arasi taban (~100 Hz tavan)
const KEEPALIVE_MS = 100;   // degisiklik olmasa da: sunucu watchdog'u 500 ms

/* Son gonderilen kanal degerleri (mikrosaniye). */
const simCh = { ail: 1500, ele: 1500, thr: 1000, rud: 1500 };

let simSock   = null;
let simOk     = false;
let simLastTx = 0;
let simDirty  = false;

/* ------------------------------------------------------------
 * BAGLANTI
 *
 * transports: ["websocket"] -> uzun yoklama (polling) ile baslayip
 * sonra yukseltme yapilmiyor; ilk paket bile WebSocket uzerinden
 * gidiyor. upgrade:false ayni sebeple.
 * ------------------------------------------------------------ */
function simConnect() {
  simSock = io(SIM_URL, {
    transports: ["websocket"],   // polling asamasini tumden atla
    upgrade: false,
    reconnection: true,
    reconnectionDelay: 300,
    reconnectionDelayMax: 1000,
    timeout: 2000,
    forceNew: true
  });

  simSock.on("connect", () => {
    simOk = true;
    simSend(true);
    if (typeof simOnState === "function") simOnState(true);
  });

  simSock.on("disconnect", () => {
    simOk = false;
    if (typeof simOnState === "function") simOnState(false);
  });

  simSock.on("connect_error", () => { simOk = false; });
}

/* ------------------------------------------------------------
 * KANAL YAZMA - slider'in oninput'undan cagrilir.
 * ------------------------------------------------------------ */
function simSetAxes(obj) {
  for (const k in obj) {
    if (k in simCh) simCh[k] = obj[k] | 0;
  }
  simSend(false);
}

/* ------------------------------------------------------------
 * GONDERIM
 *
 * volatile.emit: baglanti kopukken paket kuyruga girmiyor. Kuyruk
 * dolarsa baglanti geri geldiginde saniyeler oncesinin kol
 * pozisyonlari arka arkaya sunucuya bosaliyor - RC icin en son deger
 * disindaki her sey coptur.
 *
 * Dizi olarak yolluyoruz: [ail, ele, thr, rud]. Sunucu sozluk
 * bicimini de kabul ediyor, dizi sadece birkac bayt daha kucuk.
 * Ack beklenmiyor, geri donus paketi yok.
 * ------------------------------------------------------------ */
function simSend(force) {
  if (!simOk) { simDirty = true; return; }

  const t = performance.now();
  if (!force && t - simLastTx < MIN_GAP_MS) { simDirty = true; return; }

  simLastTx = t;
  simDirty  = false;
  simSock.volatile.emit("rc", [simCh.ail, simCh.ele, simCh.thr, simCh.rud]);
}

/* Bekleyeni bosaltan + sunucu watchdog'unu besleyen tik. */
setInterval(() => {
  if (simDirty || performance.now() - simLastTx >= KEEPALIVE_MS) simSend(true);
}, MIN_GAP_MS);

/* ------------------------------------------------------------
 * GECIKME OLCUMU - konsola yaz: simPing()
 * Sicak yolun disinda, ack ile doner.
 * ------------------------------------------------------------ */
function simPing() {
  if (!simOk) { console.log("bagli degil"); return; }
  const t0 = performance.now();
  simSock.emit("rtt", t0, () => {
    console.log("RTT " + (performance.now() - t0).toFixed(1) + " ms");
  });
}

/* ------------------------------------------------------------
 * MEVCUT KUMANDA ARAYUZUNDEN KOPRU  (istege bagli)
 *
 * controller_software/include/web_ui.h icindeki kol modeli us degil
 * kendi birimini tutuyor:  thr 0..1000,  ele/rud -1000..1000.
 * O arayuze simulator destegi eklemek icin kanalGonder() icinden
 * su satiri cagirmak yeterli:
 *
 *     simFromStick(stick);
 *
 * Aileron yok; sunucudaki MIRROR_RUDDER_TO_AILERON acilirsa rudder
 * ayni anda kanatlara da gider.
 * ------------------------------------------------------------ */
function simFromStick(stick) {
  simSetAxes({
    thr: 1000 + stick.thr,            // 0..1000     -> 1000..2000
    ele: 1500 + stick.ele * 0.5,      // -1000..1000 -> 1000..2000
    rud: 1500 + stick.rud * 0.5
  });
}

simConnect();
