# DrumLight

Strike a drum, its LED strip flashes. ESP32 + MAX4466 mic(s) for hit detection,
WS2812B addressable LED strip(s) for the flash. Built to start with one drum and scale
to a few more on the same ESP32.

See [requirements.md](requirements.md) for the full design (architecture, wiring/power
plan, BOM, and why the drum count tops out around 6 on one board).

## Build & flash

1. Install the [PlatformIO extension](https://platformio.org/install/ide?install=vscode) for VS Code.
2. Open this folder in VS Code — PlatformIO will detect `platformio.ini` automatically.
3. Connect the ESP32 over USB.
4. PlatformIO toolbar: Build (checkmark), then Upload (arrow). Open the Serial Monitor at 115200 baud for debug output.

## Configuration

All tuning lives in [include/config.h](include/config.h): pins, LED counts, colors,
strike sensitivity (`threshold`), and timing. Add a second drum by bumping `NUM_DRUMS`
and adding a row to `DRUMS[]` — no other code changes needed.

Start with the default `threshold` and watch the Serial Monitor / LED behavior while
tapping the drum; raise it if it triggers on ambient noise, lower it if real hits are
missed. The MAX4466's onboard gain trimpot is the other sensitivity knob — start at
roughly midway.

### Fake-triggering a hit for testing

Each drum can have an optional test button: wire a momentary push button between its
`testButtonPin` (set in `config.h`, default `GPIO25` for Drum 1) and `GND` — the ESP32's
internal pull-up handles the rest, no resistor needed. Pressing it fires the exact same
flash animation a real mic hit would, which is handy for checking colors/timing or for
tuning `refractoryMs`/`flashMs` without needing to actually strike the drum. Set a drum's
`testButtonPin` to `NO_PIN` to leave it without one.
