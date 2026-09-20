# DrumLight — Requirements & System Design

## 1. Overview

DrumLight makes a physical drum flash an addressable RGB LED strip when it is struck.
A microphone picks up the hit, firmware on an ESP32 detects the transient, and a WS2812B
LED strip wrapped around the drum shell flashes in response. The system is designed so a
single ESP32 can drive multiple independent drums (its own mic + its own LED strip per
drum), even though only one or two drums will be built first. The ESP32 also hosts its
own WiFi network and a browser-based control panel, so every drum's look is tunable live
from a phone with no app, router, or internet connection required.

## 2. Goals

- Low latency: visible flash within ~20 ms of a drum strike.
- One drum working end-to-end first; adding a second/third/fourth drum is a config
  change, not a redesign.
- Runs from VS Code + PlatformIO, flashed over USB.
- Power system that works today on USB for a single small test rig, with a documented,
  correct-from-the-start upgrade path to an external 5V supply once a full strip is wrapped
  around a real drum shell.
- Live tuning and control from a phone (color, sensitivity, timing, animation, presets)
  without a laptop, app install, or reflash — see §6.4.

## 3. Non-Goals (for v1)

- No music/BPM sync, no audio analysis beyond per-drum transient detection.
- No per-hit velocity-sensitive color mapping in v1 (just on/off flash), though the
  architecture doesn't preclude it later.
- No battery/portable power in v1 — mains-powered (wall adapter) is assumed.
- No internet connectivity or cloud control — the ESP32's WiFi is a local access point
  only (§6.4), by design (works at any venue, no dependency on outside network access).

## 4. Functional Requirements

| ID | Requirement |
|----|-------------|
| F1 | System shall continuously monitor each drum's microphone for a strike. |
| F2 | On a strike, the corresponding drum's LED strip shall flash within ≤20 ms. |
| F3 | Flash shall decay back to off (or idle color) over a configurable duration (default 150–250 ms). |
| F4 | A single strike shall not cause multiple flashes (debounce/refractory period, default 120 ms). |
| F5 | Strike sensitivity (threshold) shall be configurable per drum without rewriting logic. |
| F6 | Flash color and LED count shall be configurable per drum. |
| F7 | System shall support 1–4 drums on a single ESP32 (see §6.3 for why 4 is the ceiling), each with independent mic input and independent LED output. |
| F8 | Each drum's detection and animation shall run independently — a hit on drum A must not delay or block drum B's detection or animation. |
| F9 | Firmware shall cap total LED current draw (via brightness scaling) so it never exceeds the power supply's rated current, regardless of how many pixels/drums flash simultaneously. |
| F10 | System shall support multiple animation modes per drum (flash, chase variants, rainbow, color-cycle, ember, sparkle, ripple, strobe), selectable per drum and swappable without reflashing. |
| F11 | The ESP32 shall host its own WiFi access point and serve a browser-based control panel, reachable with no internet connection or external router. |
| F12 | The control panel shall allow live, per-drum tuning of color, strike threshold, refractory period, flash duration, and animation mode/speed, with changes visible immediately. |
| F13 | The control panel shall display a live per-drum envelope reading so threshold can be tuned by watching real values, not guesswork. |
| F14 | System shall support saving/loading named presets (a snapshot of every drum's color + animation) — a fixed set of built-in templates plus at least 8 user-saved slots. |
| F15 | System shall support an optional physical control board: a maintained power switch (kills LED output without powering down detection/WiFi), a momentary "test all drums" button, and momentary buttons that recall a preset/template — reassignable from the control panel without a reflash. |
| F16 | All tunable settings (per-drum tuning, master brightness, lights on/off, presets, button mapping) shall persist across power cycles. |

## 5. Non-Functional Requirements

| ID | Requirement |
|----|-------------|
| N1 | Firmware written in C++ using PlatformIO (Arduino framework for ESP32). |
| N2 | No blocking `delay()` in the main loop — detection and animation are both non-blocking state machines, so timing stays correct as drum count grows. |
| N3 | All per-drum wiring (pins, LED counts) and first-boot tuning defaults live in one config file/struct, not scattered through logic; live-tuned values (thresholds, colors, timing, animation) persist to flash (NVS) and take over from those defaults once set. |
| N4 | Wiring shall tie all grounds (ESP32, mic boards, LED PSU) together — a floating ground is the #1 cause of noisy/ghost triggers and corrupted LED data. |
| N5 | System shall be safely expandable: adding a drum means adding one config entry + wiring, no protocol/architecture changes. |

## 6. System Architecture

### 6.1 Block diagram (per drum, replicated N times)

```
   [Drum shell + head]
          |  (acoustic vibration)
          v
   [MAX4466 mic module] --analog audio--> [ESP32 ADC1 channel]
                                                 |
                                     [envelope follower + threshold
                                      + refractory period, in firmware]
                                                 |
                                          hit detected?
                                                 |
                                                 v
                                   [non-blocking animation state
                                    machine — mode selected per drum,
                                    see §6.4]
                                                 |
                                                 v
                                   [ESP32 GPIO --(level shifter)-->
                                    WS2812B data in, via FastLED/RMT]
                                                 |
                                                 v
                                     [LED strip around drum shell]

   Power: 5V rail (USB or external PSU) -> ESP32 5V + LED strip 5V,
          all grounds common.

   Alongside all drums: ESP32 hosts a WiFi AP + web control panel
   (§6.4) and reads an optional physical control board (§6.5) — both
   read/write the same live per-drum settings the animation loop uses.
```

### 6.2 Firmware loop model

- `loop()` round-robins over all configured drums every iteration (no per-drum delay).
- Per drum: read ADC → update DC-offset tracking → rectify → low-pass into an
  "envelope" value → compare envelope to threshold → if above threshold and refractory
  period has elapsed, trigger a hit. The current envelope is also pushed to the web UI's
  telemetry stream (§6.4) for live threshold tuning.
- A hit sets that drum's animation state to "flashing" with a start timestamp; every loop
  iteration recomputes the animation purely from elapsed time (each mode has its own
  attack/decay/sweep curve, see §6.4's animation modes) — no `delay()`, so multiple drums
  animate concurrently and independently.
- Reading/writing a drum's live-tuned settings (color, threshold, refractory, flash
  duration, animation mode/speed/length) from outside the main loop (e.g. an incoming web
  UI message) goes through a critical section, since the main loop reads the same fields
  every iteration.
- `webUiLoop()` and the physical control board's polling also run once per `loop()`
  iteration, non-blocking, alongside per-drum detection/animation.
- `FastLED.show()` is called once per loop iteration after all strips are updated, using
  FastLED's built-in power-limiting (`setMaxPowerInVoltsAndMilliamps`) to enforce N F9. A
  master on/off (physical switch AND/OR the web UI's software toggle) forces the strips
  dark at this final step without stopping detection/animation/WiFi underneath.

### 6.3 Why max 4 drums per ESP32

- Mic inputs could go higher: the ESP32 has two ADC units, and since **ADC2 shares
  hardware with WiFi** (unusable/unreliable once WiFi is active — and this design always
  has WiFi on for the control panel, §6.4), all mics must go on **ADC1** pins only. A
  typical ESP32 dev board exposes 6 ADC1-capable pins (`GPIO32, 33, 34, 35, 36, 39`), which
  alone wouldn't force a cap below 6.
- The actual ceiling is the LED side of the implementation: FastLED needs each strip's
  data pin as a compile-time template parameter, so the firmware declares a fixed 4 LED
  buffers/pin slots (`LED_PIN_0`–`LED_PIN_3`) rather than a dynamically-sized array, and
  `NUM_DRUMS` is capped at 4 accordingly (`config.h` enforces this at compile time).
  Supporting more would mean adding more fixed slots, not a config change.
- Conclusion: build 1–2 drums now; the same firmware/wiring pattern scales to 4 without
  redesign; beyond 4, use a second ESP32.

### 6.4 Web control panel

- The ESP32 runs as its own WiFi access point (`WIFI_AP_SSID`/`WIFI_AP_PASSWORD` in
  `config.h`) — no router or internet needed — with a DNS captive-portal responder so
  phones auto-prompt the control page on connect, and mDNS (`drumlight.local`) as a
  fallback to the AP's fixed IP (`192.168.4.1`).
- `data/index.html` is a single-file browser UI served from the ESP32's LittleFS
  filesystem (flashed separately from firmware — see README's "Upload Filesystem Image"
  step) and driven live over a WebSocket (JSON messages): per-drum color/threshold/
  refractory/flash/animation editing, a live envelope graph fed by the detection loop's
  telemetry, master brightness, "test hit" per drum, save/load/delete presets, load a
  built-in template, and reassign physical preset buttons.
- Any change from any source (a browser, a physical preset button, a template load)
  re-syncs every connected browser, so multiple simultaneous clients stay consistent.
- Animation modes (`AnimMode` in `config.h`), selectable per drum from the panel:
  `FLASH` (classic instant-on/linear-decay), four `CHASE` variants (single/double comet
  sweep, with an optional "persist" look where swept pixels hold solid until release),
  `RAINBOW` and `COLORCYCLE` (continuous ambient patterns that pulse brighter on a hit),
  and four "idle glow + hit reaction" modes that are never fully dark between hits —
  `EMBER` (breathing glow that flares on a hit), `SPARKLE` (idle twinkle that bursts
  denser), `RIPPLE` (a wave travels out from center to both ends), and `STROBE` (rapid
  on/off burst instead of a smooth decay).
- Presets (`presets.h`) snapshot every drum's color + animation (not mic tuning, which is
  per-hardware) into one named slot: 5 read-only built-in templates baked into firmware
  (`PRESET_TEMPLATES` in `config.h`), plus up to `MAX_PRESETS` (8) user-saved slots in
  NVS, editable/deletable from the panel.

### 6.5 Physical control board (optional)

- A maintained power switch (`POWER_SWITCH_PIN`) that forces every strip dark while
  leaving detection, animation state, and WiFi/UI running underneath — flipping it back
  on resumes instantly rather than rebooting anything.
- A momentary "test all" button (`GLOBAL_TEST_BUTTON_PIN`) that fires every configured
  drum through the same trigger path as a real hit.
- Up to `MAX_PRESET_BUTTONS` (5) momentary buttons, each mapped to a built-in template or
  a saved preset. The mapping starts from a compile-time default (`PRESET_BUTTON_DEFAULTS`)
  but is reassignable live from the web UI's Buttons panel and persists to NVS, so a
  reflash never undoes a live reassignment.
- All inputs are active-low with the ESP32's internal pull-ups (no external resistors)
  and software-debounced (30 ms). Any pin can be set to `NO_PIN` to leave that control
  unwired without affecting the rest of the board.

## 7. Hardware & Bill of Materials

![DrumLight wiring diagram — ESP32, MAX4466 mic, level shifter, WS2812B strip, and the optional control board's buttons](docs/diagrams/wiring-diagram.png)

Reference wiring for Drum 1 plus the optional physical control board, matching
`config.h`'s pin assignments. SVG source: [docs/diagrams/wiring-diagram.svg](docs/diagrams/wiring-diagram.svg).

| Component | Notes |
|-----------|-------|
| ESP32 dev board (e.g. ESP32-DevKitC / NodeMCU-32S) | 1 per group of up to 4 drums |
| MAX4466 electret mic amp module (with adjustable gain pot) | 1 per drum |
| WS2812B addressable RGB LED strip | length per drum = however much wraps the shell |
| External 5V PSU (5V, current per §8) | shared, once beyond USB-safe current (see §8) |
| Level shifter, 74AHCT125 or 74HCT14 (3.3V→5V) | 1 per LED data line (recommended, see §8.4) |
| 300–500 Ω resistor | 1 per LED strip, in series on data line, near strip input |
| 1000 µF electrolytic capacitor, ≥6.3V rating | 1 per LED strip, across 5V/GND at strip input |
| Wire (mic leads thin ok; LED power leads thick, see §8.3) | |
| Common ground bus / terminal block | ties ESP32 GND, mic GNDs, PSU GND together |
| Maintained SPST switch (optional) | power/lighting on-off, see §6.5 |
| Momentary push buttons, up to 6 (optional) | 1 global test-all + up to 5 preset-recall, see §6.5 |

## 8. Power Design

### 8.1 The constraint

A WS2812B pixel draws up to ~60 mA at full white (20 mA × 3 channels). "Full strip per
drum" wrapped around a shell easily means 30–60+ LEDs per drum:

| LEDs per drum | Worst-case current @ full white | @ 2 drums simultaneously |
|---|---|---|
| 30 | 1.8 A | 3.6 A |
| 60 | 3.6 A | 7.2 A |

A USB port/cable is good for ~500 mA–1 A. **USB alone cannot safely drive a full strip at
real brightness** — this is the main reason to plan an external supply now, even though a
single bare LED or two for bench testing will run fine on USB.

### 8.2 Power architecture

**Option A — single shared 5V supply (current setup).** One 5V source feeds the LED
strip's 5V/GND directly, and the same rail also feeds the ESP32's `5V` pin. This is the
simplest wiring and gets the common-ground requirement (N4) for free, since there's only
one ground net. Use this as the default for bench testing and for a single modest drum.

- Size the source's rated current for your real LED load *plus* ~800 mA of headroom for
  the ESP32 (its own draw, worst case, during WiFi TX bursts) — don't size it to the LEDs
  alone.
- Set `LED_SUPPLY_MILLIAMPS` in `config.h` to that source's rated current **minus** the
  ~800 mA ESP32 headroom, not the full rating. FastLED's brightness limiter (F9) only
  throttles the LEDs; it doesn't know the ESP32 is drawing from the same rail, so if the
  number is too generous the LEDs can still sag the rail enough to reset the ESP32 right
  when a drum is hit — the worst possible moment.
- A basic PC USB port (~500 mA) is not enough once real brightness is involved; use a
  proper 5V USB power adapter/brick rated at least 2 A for anything beyond a handful of
  test LEDs.
- Leaving the board's own USB port connected (e.g. for PlatformIO flashing) at the same
  time as the external 5V pin is normally safe — most ESP32 devkits diode-OR the two
  inputs — but confirm on your specific board rather than assuming it.

**Option B — split supplies.** LEDs on their own dedicated 5V PSU, ESP32 still on USB,
grounds tied together (a shared ground bus/terminal strip is fine). More parts, but the
ESP32 is electrically isolated from LED current spikes — no shared-rail brownout risk.

**When to switch from A to B:** once a drum's strip is long/bright enough that its
worst-case current (see §8.1 table) starts to eat most of a single 2 A–ish supply's
headroom, or once you add a second drum on the same supply. At that point isolating the
ESP32 onto its own source removes a whole failure mode instead of just budgeting around it.

Either way: firmware's brightness limiter (F9) is a software safety net, not a substitute
for sizing the supply correctly for realistic peak use.

### 8.3 Wiring gauge / injection

- Use ≥20 AWG (thicker for longer runs) for LED 5V/GND power wiring — signal-gauge wire
  from a breadboard kit will overheat/drop voltage under amps of current.
- For strips longer than ~2 m or >60 LEDs, inject power at both ends of the strip (a second
  5V/GND wire pair from the PSU to the far end), not just the start, to avoid visible
  brightness/color droop along the strip.

### 8.4 Logic level

The ESP32 outputs 3.3V logic; WS2812B data-in wants ≥0.7×Vdd = 3.5V when run at 5V. 3.3V is
technically under spec — it often works on a short strip with a fresh ESP32 output, but
gets less reliable with longer strips, longer data wires, or a strip that's dropped below
5V from cable resistance. Recommended: a cheap 74AHCT125 (or 74HCT14) level shifter between
the ESP32 GPIO and each strip's data-in, one per drum. Combined with the series resistor
and input capacitor (§7), this is the standard "make WS2812B reliable" recipe.

## 9. Future Expansion (out of scope now, but designed for)

WiFi control, live web tuning, NVS-persisted settings, and per-drum animation modes
beyond flash+decay were originally future work here — all shipped, see §6.4. Remaining
ideas, out of scope for now but not precluded by the current design:

- Velocity-sensitive color/brightness mapping from envelope peak amplitude.
- BLE control as an alternative to the WiFi AP (e.g. for venues with WiFi congestion).
- Authentication/PIN on the web UI (currently open once connected to the AP — fine for
  a private/trusted event WiFi, not for a network anyone can join).
- Scaling past 4 drums by adding a second ESP32 rather than redesigning this one (see
  §6.3 for why 4 is this board's ceiling).

## 10. Risks / Open Questions

- Real-world threshold tuning (drum resonance, room noise, mic placement) will need
  on-drum testing — the MAX4466's onboard gain pot plus firmware threshold are the two
  knobs.
- Cross-talk: a loud hit on drum A may register faintly on drum B's mic if drums are close
  together. Mitigated by per-drum threshold tuning and mic placement (closer to the head,
  gain not maxed out); revisit if it's a real problem once built.
- Exact LED count per drum isn't finalized — PSU sizing in §8.1 should be revisited once
  actual strip lengths are known.
- `WIFI_AP_PASSWORD` defaults to a weak placeholder (`"drumlight"`) and the control panel
  has no login once connected — fine for a bench/rehearsal WiFi, but change the password
  (see `config.h`) before using this anywhere the network is reachable by untrusted people.
