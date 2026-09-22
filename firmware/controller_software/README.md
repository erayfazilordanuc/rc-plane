# Ground Station — `controller_software`

The transmitter half of the RC Plane link. An **ESP32-WROOM-32** raises its own WiFi
access point, serves a phone flight interface, and pushes a 10-byte RC frame to the
aircraft **50 times a second** over two independent radios.

There is no commercial transmitter in this project. This board *is* the transmitter.

```
        phone (browser)                    this board                        air
   ┌────────────────────────┐      ┌──────────────────────────┐
   │  two touch gimbals     │  WS  │  expo · dual rate · trim │   nRF24 2.508 GHz
   │  bottom corners        │ ═══► │  arm interlocks          │ ═══════════════════►
   │  sends on touch event  │ :81  │  50 Hz frame builder     │   ESP-NOW 2.412 GHz
   └────────────────────────┘      └──────────────────────────┘ ◄═══ telemetry ═════
      RC-Plane-TX / 192.168.4.1            ESP32-WROOM-32
```

---

## What it does

| | |
|---|---|
| **WiFi AP** | `RC-Plane-TX` / `rcplane1234`, channel 1, one client, `http://192.168.4.1` |
| **Interface** | Single page, ~39 KB, served straight from PROGMEM. No SD card, no upload step |
| **Uplink** | `RcPacket` (10 B) at 50 Hz over nRF24 **and** ESP-NOW simultaneously |
| **Downlink** | Telemetry (8 B) and calibration reports (13 B), from either radio |
| **Stored on board** | Pilot settings — stick mode, trim, expo, dual rate, throttle limit |

Protocol details live in [`docs/RF_PROTOCOL.md`](https://github.com/erayfazilordanuc/rc-plane/blob/main/docs/RF_PROTOCOL.md). The wire format
itself is in [`include/rc_protocol.h`](include/rc_protocol.h), which must stay **byte-identical**
to the copy in `flight_software`.

---

## Wiring

nRF24L01+ on VSPI. `SPI.begin()` names the pins explicitly — partly as documentation,
partly to override any library that grabbed SPI first.

| nRF24L01 | ESP32 | Wire colour |
|---|---|---|
| VCC | **3V3** — never 5 V | red |
| GND | GND | black |
| CE | GPIO4 | blue |
| CSN | GPIO5 | magenta |
| SCK | GPIO18 | orange |
| MOSI | GPIO23 | cyan |
| MISO | GPIO19 | green |
| IRQ | not connected | — |

SPI runs at **4 MHz**, not the library's 10 MHz default — dupont wire and breadboard are
not reliable at 10 MHz.

> **A 10–100 µF capacitor across VCC–GND, as close to the module pins as possible, is not
> optional.** The module draws a current spike on transmit; without the cap it browns out
> and `radio.begin()` succeeds only sometimes. That intermittency is the single most
> misleading failure in this build.
>
> The header is 2×4 with **GND on pin 1 and VCC on pin 2**. Being one row off reverses the
> supply and kills the module. Touch it after powering up — if it is warm, pull it.

---

## The interface

Designed as an instrument panel, not a web page. Landscape, gimbals in the **bottom
corners**, at least 140 dp on a side (`clamp(140px, min(34vw,46vh), 300px)`).

**Three stick layouts.** A 3-channel airframe has no ailerons, so one horizontal axis is
always spare — what changes between modes is *where* the spare axis sits. It is drawn
locked and greyed rather than left looking broken.

| | Left stick | Right stick |
|---|---|---|
| **Mode 1** | rudder + elevator | throttle |
| **Mode 2** *(default)* | rudder + throttle | elevator |
| **3 CH** | throttle only | rudder + elevator |

Mode 2 is the simulator and world standard. **3 CH** is the layout you get on a real radio
by assigning rudder to the aileron channel and gathering both surfaces on the right stick,
which leaves the left stick as a pure throttle lever — a common preference on 3-channel
trainers. The mode is stored on the board, so it survives a phone change.

Elevator and rudder spring back to centre on release. Throttle stays where you left it,
like a ratcheted stick.

### Anchored sticks: touching is not a command

The moment a finger lands, the stick's *current* value is anchored to that point. What moves
a channel is the motion **after** touchdown — where in the well you landed is irrelevant.

Absolute mapping came first, and it had two faults. The obvious one: grabbing a stick meant
hitting its exact centre, and missing made the stick **jump** to wherever the finger landed.
The one that matters in the air: a hand brushing the well threw a surface to full deflection
instantly.

Anchoring removes both. A knock only sets a reference point; nothing moves.

**Origin push** is the known trap of this model and its fix. With a fixed reference you could
not grab near the top of the well and keep pulling up, so full deflection was unreachable in
one gesture. When the limit is hit the *base value* shifts instead, keeping the finger
meaningful: full deflection is reachable from any grab point, and pulling back responds
**immediately** rather than sticking at saturation. The well border turns amber at full
deflection.

**Scale:** 100 px of finger travel is half a channel's width. Surfaces run centre-to-edge for
full deflection, throttle bottom-to-top for 0→100 % — the same total travel either way, like a
physical gimbal. Lifting and re-grabbing accumulates.

Multi-touch is per gimbal via pointer capture, so *released* and *dragged outside* never get
confused and both sticks work at once. A second finger in the same well is ignored.

### Guarding against knocks

A hand will hit the screen mid-flight. The decisions:

| Risk | Decision |
|---|---|
| A knock throws a surface | Anchored sticks — touching changes nothing |
| Palm and thumb in one well | Second `pointerId` in that well is ignored |
| Touch outside the wells | Drives no channel at all |
| ARM / emergency hit by accident | **Press-and-hold** (600 ms / 400 ms); sliding 44 px cancels |
| Settings opening over the sticks | AYAR is locked while armed |
| Back gesture, long-press menu | Swallowed (`popstate`, `contextmenu`) |
| Accidental reload | Browser confirms while armed (`beforeunload`) |
| System edge gesture reaching a stick | Wells sit 16 px in from the edge |
| URL bar and edge swipes | Fullscreen + landscape lock attempted on first touch |

Requiring a hold on the emergency stop is deliberate: a stray finger cutting the motor into a
forced landing costs far more than 400 ms does in a real emergency.

**Stick data is sent on the touch event itself**, not on a timer — 10 ms floor only so a
120 Hz digitiser cannot flood the socket, plus a 50 ms keepalive that doubles as the
watchdog feed. The interface adds no buffering of its own; end-to-end latency is
essentially the radio's own 20 ms frame.

Also on the flight screen: a pre-arm checklist that names whatever is blocking ARM, live
link / packet-loss / battery / throttle readouts, in-flight trim rockers, and a large
failsafe banner that never covers the sticks.

**Deliberately absent from the flight screen:** animation, transitions, gradients, shadows.
Every colour pair on it clears WCAG AAA against its background, so it stays readable in
direct sun. `navigator.vibrate()` marks arm, disarm and failsafe; a screen Wake Lock keeps
the display on, and pulling down the notification shade does not disturb a single channel.

A USB gamepad works too, through the Gamepad API. Keyboard, for bench work: `W`/`S`
throttle, arrows elevator, `A`/`D` rudder, `Space` emergency stop, `X` throttle cut.

---

## Safety

Every guard here is duplicated independently on the aircraft. Trusting one side alone means
a corrupted packet or a stale browser tab can spin a propeller.

| Condition | Result |
|---|---|
| Boot | Always disarmed |
| Throttle **output** above 1020 µs | ARM refused — checks the pulse, not the stick, so positive throttle trim cannot sneak past |
| No fresh telemetry from the aircraft | ARM refused, and auto-disarm after 1 s |
| Aircraft in calibration mode | ARM refused; auto-disarm if it enters one |
| Browser silent for 1 s | Auto-disarm, surfaces neutral |
| Disarmed | Throttle channel forced to 1000 µs on the wire |
| Emergency button / `Space` | Immediate disarm, all channels neutral |

`TEZGAH_MODU` (bench mode) relaxes only the telemetry requirement, for diagnosing a broken
downlink on the bench. **It ships as `false`.** When true it shouts about it in the boot log,
in every `[TX]` line, and across the top of the interface.

---

## Stick feel

Applied on this board, not in the browser, so a different phone cannot lose your trim.
Stored in NVS under `rctx`.

| Setting | Range | Default |
|---|---|---|
| Stick mode | 1 / 2 / 3 (3 CH) | 2 |
| Trim — throttle / elevator / rudder | ±200 µs (**±20 %** of channel width) | 0 |
| Expo — elevator / rudder | 0–50 % | 25 |
| Dual rate — elevator / rudder | 30–100 % | 100 |
| Throttle limit | 20–100 % | 100 |

Expo uses the industry-standard curve `y = x·((1−e) + e·x²)`: it softens the middle without
giving up any authority at the endpoints. 25 % keeps a first flight from feeling twitchy.

Throttle is limited twice — once here, once against the aircraft's own stored ceiling. The
two are independent and the lower one wins.

Servo direction, sub-trim and endpoints are **not** here. Those belong to the airframe and
live in the aircraft's flash; this board just drives the calibration screen.

---

## How it works

* **A dependency-free WebSocket server** — [`include/mini_ws.h`](include/mini_ws.h), SHA-1 and
  base64 included. `WebServer` stamps `Connection: close` on every response, so 20 Hz stick
  updates meant a fresh TCP handshake per frame and visibly jittery surfaces. A flight-ready
  firmware also should not need the internet to compile, which ruled out pulling in a
  library. The handshake accumulates across `loop()` passes and never blocks.

* **Sends are gated behind a zero-timeout `select()`.** `WiFiClient::write()` retries a
  stalled socket ten times with a one-second timeout each — up to ~10 s of blocking. The
  flight loop must never be the thing that waits, and a skipped status frame costs nothing.

* **Non-blocking radio transmit.** `startWrite()` plus STATUS polling instead of
  `radio.write()`, which blocks until ACK or until all retries are exhausted — about 12 ms
  of a 20 ms budget when the link drops, i.e. the controller gets sluggish exactly when
  control matters most. A transmit over 15 ms is aborted and the FIFO flushed; the count
  shows up as `takilan=N`.

* **Broadcast telemetry, and why not unicast.** Unicast was tried and measured out:
  `ack=0 / nack=57` — not one of 57 sends per second acknowledged, then
  `ESP_ERR_ESPNOW_NO_MEM`. The aircraft's STA is not associated with this AP, so its unicast
  frames get no MAC-layer ACK; ESP-NOW retries, the queue fills, telemetry stops completely.
  Broadcast frames expect no ACK and never fill the queue, and the payload carries its own
  magic byte and CRC-8.

* **Calibration commands are retransmitted** every 60 ms until the aircraft echoes the
  matching sequence number, giving up at 1500 ms with a visible error. A config frame takes
  its own 20 ms slot; skipping one control frame is harmless, losing a `SAVE` is not.

* **Self-healing radio.** `radio.begin()` is retried every 3 s and link state comes from a
  live register read, so a wire reseated mid-session recovers without a reset.

* **HTTP fallback stays wired.** If the WebSocket cannot be established the page falls back
  to polling `/c`, and the calibration screen keeps working through `/status`.

---

## Build & flash

```bash
pio run -t upload -t monitor
```

`platformio.ini` pins **COM5** (the CP210x bridge on this board) explicitly. Without it
PlatformIO takes the first port it finds and flashes the aircraft instead. Check yours with
`pio device list`.

The default environment builds only the flight firmware, so a bare `pio run -t upload` can
never leave the diagnostic image on the board.

Roughly 65 % of the app partition, 14 % of RAM.

### Diagnostics

```bash
pio run -e rfdiag -t upload -t monitor      # raw-SPI nRF24 diagnosis
pio run -t upload -t monitor                # back to the transmitter
```

[`diag/rf_diag.cpp`](diag/rf_diag.cpp) bypasses the RF24 library entirely and drives SPI by
hand: shorts, MISO drive, all 12 SCK/MISO/MOSI and CE/CSN permutations, then a speed sweep.
It reports the correct wiring order if the wires got swapped. **It does not bring up WiFi.**

If the registers it reads are bit-shifted copies of each other — `0x08, 0x10, 0x21, 0x42,
0x84` — the MISO line is floating and the module is not answering at all.

### Reading the log

```
[TX] nrf=BAGLI now=BAGLI ws=VAR | web=20/s | kumanda=DISARM ucak=DISARM
     thr=1000 ele=1500 rud=1500 | link=98% | rx=OK 50Hz kayip=%0 vbat=0mV
```

`web=0/s` means the problem is the browser, not the radio — the page is not sending.
`ws=yok` means the WebSocket never came up and the HTTP fallback is carrying control.

---

## Layout

```
src/main.cpp            radio, safety chain, HTTP + WebSocket endpoints, NVS
include/rc_protocol.h    wire format — byte-identical copy in flight_software
include/mini_ws.h        WebSocket server, no external dependency
include/web_ui.h         the phone interface, one PROGMEM string
diag/rf_diag.cpp         raw-SPI nRF24 diagnosis (separate environment)
```
