# Aircraft — `flight_software`

The airborne half of the RC Plane link. An **ESP32 DevKitC** (38-pin, ESP-WROOM-32 / 32D) listens on two radios at
once, validates every frame, runs it through a stored calibration curve, and drives the ESC
and two servos — with its own failsafe logic that does not trust the ground station.

**Three channels: throttle, elevator, rudder.** No ailerons — roll comes from rudder via
wing polyhedral, the classic 3-channel trainer layout. Which stick carries which surface is
a ground-station setting and does not reach this board: it always receives the same three
channels in the same order.

```
   nRF24 2.508 GHz  ═╗
                     ╠═► paketIsle() ─► magic + CRC-8 + version
   ESP-NOW 2.412 GHz ═╝        │        duplicate suppression
                               │        arm interlock
                               │        failsafe
                               ▼
                        calibration curve  ──► LEDC ──► ESC + 2 servos
                               │
                               └──► telemetry + calibration reports ──► ground
```

Either radio alone flies the aircraft. Both paths land in the same `paketIsle()`, so
validation, the arm lock and failsafe are defined exactly once.

---

## Wiring

Full diagram: [`docs/kablolama.html`](docs/kablolama.html) — open it in a browser.

| Function | GPIO | Note |
|---|---|---|
| **ESC signal** | **25** | 10 k from signal to GND — the pin floats during boot, the resistor keeps garbage out of the ESC |
| **Elevator servo** | **26** | |
| **Rudder servo** | **27** | |
| nRF24 SCK | 18 | VSPI |
| nRF24 MISO | 19 | VSPI |
| nRF24 MOSI | 23 | VSPI |
| nRF24 CSN | 5 | |
| nRF24 CE | 4 | |
| nRF24 VCC | 3V3 | top-left pin on the 38-pin board — **never 5 V** |

The radio uses the same five GPIOs as the ground station. GPIO 32/33 still carry the
mirrored aileron outputs in firmware; nothing is connected to them on this 3-channel airframe.

Avoided: **GPIO 0, 2, 12, 15** (strapping — a pulldown on GPIO0 drops the board into the
bootloader on every power-up, which is why the ESC moved off it), **14** (PWMs during boot),
**6–11** (internal flash, silkscreened `D0–D3`, `CMD`, `CLK` — `D2` is *not* GPIO2) and
**1/3** (UART0).

`SPI.begin(sck, miso, mosi, ss)` still names the pins explicitly, so a library that grabs SPI
first cannot move the radio. SPI runs at **4 MHz** — the same value the ground station
settled on, so the two cannot drift apart.

**Power.** No separate UBEC: the ESC's BEC output feeds the board's 5 V pin, and both servos
take their 5 V from there. A capacitor sits across 5 V–GND on that BEC line. nRF24 on 3.3 V,
with a 10 µF capacitor at its pins. Grounds common.

The boot log prints the reset reason. If you see `BROWNOUT` — especially *again* after
powering the motor — the problem is supply, not software: check the BEC-line capacitor and
the ground first, and give the servos their own UBEC if it persists.

---

## Outputs

All three outputs are driven by the chip's LEDC peripheral directly. `ESP32Servo` is not
used, for two concrete reasons: its pin allowlist rejects GPIO0, and its degree↔µs
conversion shifted the neutral point. Writing duty directly means the firmware speaks the
same unit as the wire protocol — microseconds, end to end, no conversion anywhere.

> **The 14-bit C3 trap.** This firmware started on an ESP32-C3, whose LEDC timer maxes out at
> 14 bits, while nearly every example online uses 16 — valid on the classic ESP32. On the C3
> that made `ledcSetup()` silently return 0: no PWM at all, 0 V on the pin. Resolution stays
> at 14 bits so the code keeps working if the board ever goes back, and setup still checks
> the return value and shouts if it is zero. 14 bits @ 50 Hz gives ~1.22 µs resolution.
>
> On the classic ESP32 LEDC channels share timers in pairs (0–1, 2–3, 4–5). Every output
> here runs at 50 Hz / 14 bit, so sharing is harmless — an output at a different frequency
> needs its own pair, or `ledcSetup()` silently retunes its neighbour.

---

## Calibration

Servo direction, sub-trim and endpoints are **not** compile-time constants. They live in
this board's NVS (`rcplane`), set over the air from the ground station's settings screen.

That placement is deliberate: endpoints belong to the airframe's linkage geometry, not to
the pilot. They survive a reset, they can be changed with the receiver buried in a fuselage
and no USB attached, and — the part that actually matters — **failsafe neutral becomes the
calibrated neutral** rather than a hardcoded 1500 µs. If a linkage sits off-centre, so would
a hardcoded failsafe.

### The mapping

Standard **end point / travel adjust** behaviour, with independent scaling either side of
neutral:

```
input > 1500  →  scaled into  midUs .. maxUs
input < 1500  →  scaled into  minUs .. midUs
```

You can give one direction 100 % travel and the other 70 %; mechanical linkages are never
symmetric. Reversal happens *before* scaling, mirrored around neutral, so flipping direction
does not swap your endpoints.

Throttle reinterprets the same three fields: `minUs` is the ESC stop pulse, `maxUs` is full
throttle, and `midUs` is this aircraft's **own throttle ceiling** — independent of the
transmitter's limit, lower of the two wins.

### Why calibration cannot hurt the aircraft

| Guard | Where |
|---|---|
| Entering calibration is refused while armed | `RC_CFG_ENTER` |
| Arming is impossible while in calibration | `armed = ... && !kalibModu` |
| ESC pinned to its stop pulse for the whole mode | `escYaz()` |
| `TEST` and `SWEEP` never apply to the throttle output | `konfigIsle()` |
| Every write clamped to 900–2100 µs | `cikisYaz()` → `rcClampHard()` |
| Ordering and ≥50 µs of travel enforced before accepting | `ayarGecerli()` |
| `TEST` returns to neutral after 4 s | `kalibTick()` |
| Link loss exits calibration | `failsafeUygula()` |
| Corrupt NVS record falls back to factory values | `ayarYukle()` — signature + validation |

The `TEST` timeout matters more than it looks: a servo parked against a linkage stop draws
current and heats up. Every new `TEST` resets the timer, so the servo holds wherever you
want it while you drag a slider, and lets go when you walk away.

`SWEEP` runs a triangle wave min→max→min over 3 s, three times, then re-centres — enough to
hear a binding linkage before it strips a gear in the air.

**Throttle has no reverse.** Reversed throttle means full power with the stick down; the
setting that is useful on a control surface is a failure mode on a motor.

### ESC throttle range

An uncalibrated ESC can still read 1200 µs as stop — the usual cause of *"throttle is going
out but the motor won't turn"*. Two ways to teach it:

* **Serial** — press `k` within 5 s of boot, then confirm each step with `e`. The window
  opens on every boot (the classic ESP32 cannot tell whether USB is attached, unlike the C3's
  native USB), so a stray byte on the UART must never be enough to reach full throttle.
* **Over the air** — the settings screen, behind a prop-removed checkbox. This board also
  requires the `RC_CFGF_PROP_OFF` bit, calibration mode, and disarmed, then drops back to
  stop by itself after 30 s if step 2 never arrives.

Both routines deliberately exceed the throttle ceiling, which is why neither is reachable
from flight code. **Remove the propeller.**

---

## Failsafe and the arm chain

| Condition | Result |
|---|---|
| No valid frame for 500 ms | ESC to stop, surfaces to **calibrated** neutral, disarmed, arm lock re-armed |
| Bad magic / CRC / protocol version | Frame dropped silently, consecutive-valid counter reset |
| Link returns | **10 consecutive** valid frames required before leaving failsafe |
| Boot, and after every failsafe | **Arm-locked** — will not arm until it has seen a frame with `ARMED = 0` |

The arm lock is the piece worth explaining. A recovering link must never spin the motor on
its own, so the lock only releases when the ground station explicitly reports disarmed —
meaning the operator has to cycle DISARM → ARM by hand.

The relink hysteresis exists because recovering on a single frame made the motor stutter
every time the link twitched.

> **Ground station state ≠ aircraft state.** The interface shows both. If the transmitter
> says ARMED while this board says DISARM, the arm lock has not released — press DISARM and
> ARM again.

---

## Telemetry

An 8-byte frame carrying battery millivolts, packet loss, `rxHz`, and status bits (armed /
failsafe / calibration / unsaved-config). It rides the nRF24 ACK payload and goes out as an
ESP-NOW broadcast, **and refreshes every 100 ms independently of packet arrival**.

That independence is not decoration. The ground station refuses to arm without fresh
telemetry, and duplicate-suppression gaps would otherwise read as *"the aircraft is not
answering"*.

`rxHz` counts unique frames per second. Link quality as a percentage never answered "how
many frames actually arrived"; at 50 Hz you expect 50, and any shortfall is visible directly.

In calibration mode the same return path alternates telemetry with configuration reports,
cycling through all three outputs at about 5 Hz. Each report carries the value **actually
being written** to that output — so the settings screen shows what the aircraft applied, not
what was sent to it.

Battery measurement is stubbed at `0`, which the interface renders as `--`. Add a divider to
an **ADC1** pin (GPIO 32–39, e.g. 35 — ADC2 is unusable while ESP-NOW holds the radio) and
fill in `bataryaOku()`; the gauge starts working on its own.

---

## Build & flash

```bash
pio run -t upload -t monitor
```

`platformio.ini` pins **COM6** explicitly. That number belonged to the old C3's native USB;
the DevKitC enumerates through its USB bridge and may get another one — run
`pio device list` with only this board attached and update both environments. Without a
pinned port PlatformIO takes the first one it finds — COM5, the ground station — and flashes
the wrong board.

Roughly 58 % of the app partition, 13 % of RAM.

### Bench testing

```bash
pio run -e bench -t upload -t monitor    # no radio at all
pio run -t upload -t monitor             # back to flight firmware
```

[`hardware_test/servo_bench_ledc.cpp`](hardware_test/servo_bench_ledc.cpp) drives the
outputs from the serial port with no radio involved, which splits "is the software wrong" from
"is it power, ground or wiring". If the servos move here, the software and pin mapping are
fine.

It also carries two diagnostics for hand-soldered boards, where the silkscreen number and
the actual GPIO do not always agree:

* `t` — pulls every candidate pin to 3.3 V DC at once. Every wire you soldered should read
  3.3 V at its far end; 0 V means it is not on the board, or it is a cold joint.
* `p` — walks the candidates one at a time and prints which pin it is driving, so you can
  find which GPIO a mystery wire actually lands on.
* `v` — 50 % duty on the selected servo pin for 5 s. A normal servo pulse is 5–10 % duty and
  reads 0.16–0.33 V on a cheap multimeter, indistinguishable from nothing. 50 % reads
  ~1.65 V and leaves no doubt.

**Disconnect the ESC, servos and nRF24 for `t` and `p`, and power the board from USB** — they
drive plain 3.3 V DC, and pushing that into a line tied to 5 V damages the chip.

`hardware_test/hardware_test.ino` is an Arduino IDE archive of the same idea, kept only for
a quick try. Nothing builds it; the LEDC version above supersedes it.

### Reading the log

```
[RX] nrf=BAGLI now=BAGLI | DISARM rxarm=0 kilit=ACIK relink=0/10 |
     thr=1000<-1000(disarm) ele=1500->1500 rud=1500->1500 |
     50 paket, tekrar 50, kayip %0, bozuk 0 | tlm nrf=50/red0 now=50/hata0
```

| Field | Meaning |
|---|---|
| `rxarm` | The raw ARMED bit as received |
| `kilit` | Arm lock. `KAPALI` means ARMED is ignored no matter what arrives |
| `thr=A<-B(reason)` | A applied, B requested, and why it was clipped |
| `ele=A->B` | A received, B written after the calibration curve |
| `tekrar` | Copies arriving via the second radio — normal, not loss |

*"Throttle is going out but the motor won't turn"* with `thr=1000<-1600(disarm)` in the log
means the problem is the arm gate, not the transmitter.

`tekrar` sitting near the unique-frame count is what a healthy redundant link looks like:
every frame arrived twice, and the second copy was discarded before it touched an output.

---

## Layout

```
src/main.cpp                        radios, safety chain, calibration, LEDC outputs, NVS
include/rc_protocol.h                wire format — byte-identical copy in controller_software
hardware_test/servo_bench_ledc.cpp   bench test + solder diagnostics (separate environment)
hardware_test/hardware_test.ino      Arduino IDE archive, not built
docs/kablolama.html                  wiring diagram, open in a browser
```

Protocol details: [`docs/RF_PROTOCOL.md`](https://github.com/erayfazilordanuc/rc-plane/blob/main/docs/RF_PROTOCOL.md).
