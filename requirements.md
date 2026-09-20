# DrumLight — Requirements & System Design

## 1. Overview

DrumLight makes a physical drum flash an addressable RGB LED strip when it is struck.
A microphone picks up the hit, firmware on an ESP32 detects the transient, and a WS2812B
LED strip wrapped around the drum shell flashes in response. The system is designed so a
single ESP32 can drive multiple independent drums (its own mic + its own LED strip per
drum), even though only one or two drums will be built first.

## 2. Goals

- Low latency: visible flash within ~20 ms of a drum strike.
- One drum working end-to-end first; adding a second/third drum is a config change, not a
  redesign.
- Runs from VS Code + PlatformIO, flashed over USB.
- Power system that works today on USB for a single small test rig, with a documented,
  correct-from-the-start upgrade path to an external 5V supply once a full strip is wrapped
  around a real drum shell.

## 3. Non-Goals (for v1)

- No app/web UI, no WiFi control, no music/BPM sync. (Noted as future work, see §9.)
- No per-hit velocity-sensitive color mapping in v1 (just on/off flash), though the
  architecture doesn't preclude it later.
- No battery/portable power in v1 — mains-powered (wall adapter) is assumed.

## 4. Functional Requirements

| ID | Requirement |
|----|-------------|
| F1 | System shall continuously monitor each drum's microphone for a strike. |
| F2 | On a strike, the corresponding drum's LED strip shall flash within ≤20 ms. |
| F3 | Flash shall decay back to off (or idle color) over a configurable duration (default 150–250 ms). |
| F4 | A single strike shall not cause multiple flashes (debounce/refractory period, default 120 ms). |
| F5 | Strike sensitivity (threshold) shall be configurable per drum without rewriting logic. |
| F6 | Flash color and LED count shall be configurable per drum. |
| F7 | System shall support 1–6 drums on a single ESP32 (see §6.3 for why 6 is the practical ceiling), each with independent mic input and independent LED output. |
| F8 | Each drum's detection and animation shall run independently — a hit on drum A must not delay or block drum B's detection or animation. |
| F9 | Firmware shall cap total LED current draw (via brightness scaling) so it never exceeds the power supply's rated current, regardless of how many pixels/drums flash simultaneously. |

## 5. Non-Functional Requirements

| ID | Requirement |
|----|-------------|
| N1 | Firmware written in C++ using PlatformIO (Arduino framework for ESP32). |
| N2 | No blocking `delay()` in the main loop — detection and animation are both non-blocking state machines, so timing stays correct as drum count grows. |
| N3 | All per-drum tuning (pins, thresholds, colors, LED counts) lives in one config file/struct, not scattered through logic. |
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
                                   [non-blocking flash/decay
                                    animation state machine]
                                                 |
                                                 v
                                   [ESP32 GPIO --(level shifter)-->
                                    WS2812B data in, via FastLED/RMT]
                                                 |
                                                 v
                                     [LED strip around drum shell]

   Power: 5V rail (USB or external PSU) -> ESP32 5V + LED strip 5V,
          all grounds common.
```

### 6.2 Firmware loop model

- `loop()` round-robins over all configured drums every iteration (no per-drum delay).
- Per drum: read ADC → update DC-offset tracking → rectify → low-pass into an
  "envelope" value → compare envelope to threshold → if above threshold and refractory
  period has elapsed, trigger a hit.
- A hit sets that drum's animation state to "flashing" with a start timestamp; every loop
  iteration recomputes brightness from elapsed time (attack + decay curve) — no `delay()`,
  so multiple drums animate concurrently and independently.
- `FastLED.show()` is called once per loop iteration after all strips are updated, using
  FastLED's built-in power-limiting (`setMaxPowerInVoltsAndMilliamps`) to enforce N F9.

### 6.3 Why max ~6 drums per ESP32

- LED outputs: FastLED's ESP32 RMT driver supports up to 8 parallel strips on 8 different
  GPIOs — not the bottleneck.
- Mic inputs are the real limit. The ESP32 has two ADC units: **ADC2 shares hardware with
  WiFi** and becomes unusable/unreliable whenever WiFi is active, so all mics must go on
  **ADC1** pins only. A typical ESP32 dev board exposes 6 ADC1-capable pins:
  `GPIO32, 33, 34, 35, 36, 39`. That caps this design at **6 simultaneous drums** on one
  ESP32 if WiFi is ever turned on. (If WiFi is permanently off, ADC2 pins could be added
  for more, but that's not recommended — keep the door open for WiFi-based control later.)
- Conclusion: build 1–2 drums now; the same firmware/wiring pattern scales to 6 without
  redesign; beyond 6, use a second ESP32.

## 7. Hardware & Bill of Materials

| Component | Notes |
|-----------|-------|
| ESP32 dev board (e.g. ESP32-DevKitC / NodeMCU-32S) | 1 per group of up to 6 drums |
| MAX4466 electret mic amp module (with adjustable gain pot) | 1 per drum |
| WS2812B addressable RGB LED strip | length per drum = however much wraps the shell |
| External 5V PSU (5V, current per §8) | shared, once beyond USB-safe current (see §8) |
| Level shifter, 74AHCT125 or 74HCT14 (3.3V→5V) | 1 per LED data line (recommended, see §8.4) |
| 300–500 Ω resistor | 1 per LED strip, in series on data line, near strip input |
| 1000 µF electrolytic capacitor, ≥6.3V rating | 1 per LED strip, across 5V/GND at strip input |
| Wire (mic leads thin ok; LED power leads thick, see §8.3) | |
| Common ground bus / terminal block | ties ESP32 GND, mic GNDs, PSU GND together |

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

- WiFi/BLE control of thresholds/colors from a phone (would need to keep drums on ADC1 as
  designed, see §6.3).
- Velocity-sensitive color/brightness mapping from envelope peak amplitude.
- Per-drum animation patterns beyond flash+decay (chase, rainbow, etc.) — the animation
  state machine is already isolated per drum so this is additive.
- Persisting per-drum config (thresholds/colors) to flash (NVS/Preferences) instead of
  hardcoded constants.

## 10. Risks / Open Questions

- Real-world threshold tuning (drum resonance, room noise, mic placement) will need
  on-drum testing — the MAX4466's onboard gain pot plus firmware threshold are the two
  knobs.
- Cross-talk: a loud hit on drum A may register faintly on drum B's mic if drums are close
  together. Mitigated by per-drum threshold tuning and mic placement (closer to the head,
  gain not maxed out); revisit if it's a real problem once built.
- Exact LED count per drum isn't finalized — PSU sizing in §8.1 should be revisited once
  actual strip lengths are known.
