#pragma once
#include <FastLED.h>

// ---------------------------------------------------------------------------
// Per-drum configuration.
//
// To add a drum: bump NUM_DRUMS, add a row to DRUMS[], and make sure a
// LED_PIN_N / mic pin exists for the new slot (see below and main.cpp).
//
// Mic pins MUST be ADC1-capable pins so they keep working once WiFi is on
// (ADC2 shares hardware with the WiFi radio): GPIO32, 33, 34, 35, 36, 39.
// GPIO34/35/36/39 are input-only, which is fine for an analog mic input.
//
// color/threshold/refractoryMs/flashMs below are only the FIRST-BOOT
// defaults — once the device boots, those four are tunable live from the
// web UI and persisted to flash (see settings.h). Editing them here after
// the device has already saved settings has no effect; use the web UI (or
// wipe NVS) instead. name/pins/ledCount stay fixed here, since they describe
// physical wiring.
// ---------------------------------------------------------------------------

#define NUM_DRUMS 1  // 1-4 (max supported), must match the number of rows in DRUMS[] below
#if NUM_DRUMS > 4
#error "NUM_DRUMS supports at most 4 drums"
#endif

// Sentinel meaning "no pin wired for this button/input".
#define NO_PIN 255

// ---------------------------------------------------------------------------
// LED animation modes.
//
// FLASH is the original behavior: fully off between hits, solid color flash
// that decays linearly over flashMs.
//
// RAINBOW/COLORCYCLE run a continuous ambient pattern and layer a
// brightness "hit pulse" on top when the drum fires (strip is never fully
// dark); animSpeedMs is the hue-cycle period.
//
// The CHASE family is entirely hit-triggered: idle/off until a hit, then
// runs exactly one sweep and stops (a real chase never free-runs between
// hits). animSpeedMs is the sweep duration, animLength is the comet's
// trail width in pixels (non-persist variants only). "DOUBLE" launches two
// comets from the same point traveling opposite ways around the strip so
// they meet at the far side. "PERSIST" leaves every swept pixel lit solid
// (instead of a fading comet tail) until the sweep finishes, then the whole
// thing holds and fades out together over flashMs.
//
// The remaining four modes are "persistent low light + hit reaction" looks
// — the strip is never fully dark, and a hit adds a flare/burst/wave on top
// instead of the strip snapping from black to full brightness:
//   EMBER   - a dim resting glow that slowly breathes; a hit flares it to
//             full brightness and it decays back down to the glow (not to
//             black). animLength is repurposed as the glow's resting
//             brightness (percent), animSpeedMs is the breathe period.
//   SPARKLE - sparse random twinkles at rest; a hit triggers a denser,
//             brighter burst of sparkles that settles back to the idle
//             twinkle. animLength is repurposed as idle sparkle density
//             (percent), animSpeedMs is how often a new sparkle can spawn.
//   RIPPLE  - a dim solid base color; each hit sends a bright band out from
//             the center to both ends of the strip, fading to the base glow
//             behind it as it travels. animSpeedMs is travel time end-to-end,
//             animLength is the band's width in pixels, flashMs is how long
//             a pixel takes to fade from full brightness to the base glow
//             after the wave passes it.
//   STROBE  - off between hits; a hit fires a rapid multi-flash strobe burst
//             (on/off pulses) instead of one smooth decay. flashMs is the
//             total burst length, animSpeedMs is repurposed as the on/off
//             cycle period (lower = faster strobing).
// ---------------------------------------------------------------------------
enum AnimMode : uint8_t {
  ANIM_FLASH = 0,                // solid color, instant-on / linear decay (classic)
  ANIM_CHASE = 1,                // one comet sweeps once around the strip on a hit
  ANIM_RAINBOW = 2,              // continuous rotating rainbow across the strip
  ANIM_COLORCYCLE = 3,           // configured color slowly shifts hue, breathing
  ANIM_CHASE_DOUBLE = 4,         // two comets sweep from the same point, meet at the far side
  ANIM_CHASE_PERSIST = 5,        // single sweep, lights stay on solid, all release together
  ANIM_CHASE_DOUBLE_PERSIST = 6, // double sweep + persist, combined
  ANIM_EMBER = 7,                // dim breathing glow, flares on a hit, decays back to the glow
  ANIM_SPARKLE = 8,              // idle twinkle, bursts into denser sparkle on a hit
  ANIM_RIPPLE = 9,               // dim base color, hit sends a wave out from the center
  ANIM_STROBE = 10,              // off between hits, hit fires a rapid multi-flash burst
  ANIM_MODE_COUNT = 11
};

struct DrumConfig {
  const char* name;
  uint8_t micPin;         // ADC1-capable pin, see note above
  uint16_t ledCount;      // LEDs in this drum's strip
  CRGB defaultColor;       // first-boot flash color (tunable afterwards)
  int defaultThreshold;    // first-boot envelope threshold (tunable afterwards)
  uint16_t defaultRefractoryMs;  // first-boot refractory period (tunable afterwards)
  uint16_t defaultFlashMs;       // first-boot flash decay duration (tunable afterwards)
  AnimMode defaultAnimMode;      // first-boot animation mode (tunable afterwards)
  uint16_t defaultAnimSpeedMs;   // first-boot animation speed (tunable afterwards)
  uint16_t defaultAnimLength;    // first-boot chase trail length in pixels (tunable afterwards)
};

// There is no per-drum test button — a single GLOBAL_TEST_BUTTON_PIN (see
// the control-board section below) fires every configured drum at once,
// through the same trigger path as a real mic hit.
static const DrumConfig DRUMS[NUM_DRUMS] = {
  //  name      micPin  ledCount  color        threshold  refractoryMs  flashMs  animMode     animSpeedMs  animLength
  { "Drum 1",   32,     92,       CRGB::Red,   700,       30,          200,     ANIM_FLASH,  1500,        10 },
  // { "Drum 2", 33,    60,       CRGB::Blue,  700,       120,          200,     ANIM_CHASE,  900,         14 },
  // { "Drum 3", 34,    60,       CRGB::Green, 700,       120,          200,     ANIM_RAINBOW, 4000,       10 },
  // { "Drum 4", 35,    60,       CRGB::Purple,700,       120,          200,     ANIM_CHASE,  900,         14 },
};

// ---------------------------------------------------------------------------
// Built-in preset "templates" — looks you fix at compile time so they're
// always available from the web UI's Presets panel, alongside whatever the
// user saves live from the browser. Unlike saved presets (stored in NVS,
// editable/deletable from the UI), these live in firmware and are read-only
// from the web UI — reflash to change them. One row of PresetTemplateDrum
// per entry in DRUMS[] above, same order.
// ---------------------------------------------------------------------------
struct PresetTemplateDrum {
  CRGB color;
  AnimMode animMode;
  uint16_t animSpeedMs;
  uint16_t animLength;
  uint16_t flashMs;
};

struct PresetTemplate {
  const char* name;
  PresetTemplateDrum drums[NUM_DRUMS];
};

#define NUM_PRESET_TEMPLATES 5
static const PresetTemplate PRESET_TEMPLATES[NUM_PRESET_TEMPLATES] = {
  //              name           color           animMode                animSpeedMs  animLength  flashMs
  { "Classic Flash",  { { CRGB::Red,       ANIM_FLASH,               1500,        10,         200 } } },
  { "Ring Chase",     { { CRGB::OrangeRed, ANIM_CHASE_DOUBLE_PERSIST, 350,        14,         150 } } },
  { "Rainbow Glow",   { { CRGB::White,     ANIM_RAINBOW,             4000,        10,         200 } } },
  { "Ember Pulse",    { { CRGB::OrangeRed, ANIM_EMBER,               2500,        15,         450 } } },
  { "Strobe Hit",     { { CRGB::White,     ANIM_STROBE,              60,          0,           250 } } },
};

// ---------------------------------------------------------------------------
// Physical control board.
//
// One maintained on/off switch (system power vs. light output — the board
// itself stays powered and connected either way), one momentary "test all"
// button, and up to MAX_PRESET_BUTTONS momentary buttons that each instantly
// recall a template or a saved preset — for swapping looks live between
// songs without touching the app. All are wired the same way: one leg to
// the pin, the other to GND. Internal pull-ups handle it, no external
// resistors needed. Set any pin to NO_PIN to leave that control unwired.
//
// PRESET_BUTTON_DEFAULTS is only a first-boot fallback for what each
// physical button loads — reassign any button to any template/preset from
// the web UI's Buttons panel afterwards, with no reflash needed (the
// reassignment persists to flash; see buttons.h).
// ---------------------------------------------------------------------------

enum ButtonTargetKind : uint8_t {
  BTN_TARGET_NONE = 0,
  BTN_TARGET_TEMPLATE = 1,  // id = index into PRESET_TEMPLATES
  BTN_TARGET_PRESET = 2,    // id = saved-preset slot
};

struct ButtonTarget {
  ButtonTargetKind kind;
  uint8_t id;
};

#define MAX_PRESET_BUTTONS 5

static const uint8_t PRESET_BUTTON_PINS[MAX_PRESET_BUTTONS] = { 26, 27, 14, 13, 4 };

static const ButtonTarget PRESET_BUTTON_DEFAULTS[MAX_PRESET_BUTTONS] = {
  { BTN_TARGET_TEMPLATE, 0 },  // Classic Flash
  { BTN_TARGET_TEMPLATE, 1 },  // Ring Chase
  { BTN_TARGET_TEMPLATE, 2 },  // Rainbow Glow
  { BTN_TARGET_NONE,     0 },
  { BTN_TARGET_NONE,     0 },
};

// Maintained toggle switch: closed to GND = lighting enabled. Open = the
// board stays powered, Wi-Fi/UI/mic detection keep running, but every strip
// is forced dark. Set to NO_PIN to always leave lighting enabled. PIN 23 is good later.
#define POWER_SWITCH_PIN 23 

// Momentary button: one press fires every configured drum at once (the same
// trigger path as a real hit), for testing the whole rig without playing.
#define GLOBAL_TEST_BUTTON_PIN 25

// ---------------------------------------------------------------------------
// Wi-Fi access point. The ESP32 hosts its own network (no router needed) so
// the control page is always reachable at http://192.168.4.1 regardless of
// venue Wi-Fi. Change WIFI_AP_PASSWORD before using this anywhere untrusted
// (8-63 chars, or "" for an open network).
// ---------------------------------------------------------------------------
#define WIFI_AP_SSID "DrumLight"
#define WIFI_AP_PASSWORD "drumlight"

// How often telemetry (live mic envelope, for the web UI's tuning graph) is
// broadcast to connected web clients.
#define TELEMETRY_INTERVAL_MS 40  // ~25 Hz

// Master brightness applied on top of each drum's flash color (0-255),
// first-boot default; tunable afterwards from the web UI.
#define DEFAULT_BRIGHTNESS 180

// LED data pins, one per drum slot in DRUMS[] above, same order. Only 4
// slots exist since NUM_DRUMS is capped at 4.
// Any GPIO works for LED data (unlike the mic pins) — just avoid strapping
// pins (0, 2, 12, 15) and the input-only pins (34-39, already used for mics).
#define LED_PIN_0 16
#define LED_PIN_1 17
#define LED_PIN_2 18
#define LED_PIN_3 19

// Longest strip any single drum slot may use — buffers are sized for this.
#define MAX_LEDS_PER_STRIP 150

// Power budget for FastLED's built-in brightness limiter. Set this to match
// your actual 5V LED power supply so firmware can never ask for more current
// than the supply provides, no matter how many drums flash at once.
//
// If the ESP32 is powered from the SAME rail as the LEDs (5V pin fed from the
// same source as the strip, rather than a separate PSU) — set this to that
// source's rated current MINUS ~800mA of headroom for the ESP32's own draw.
// This limiter only throttles the LEDs; it has no idea the ESP32 is drawing
// from the same rail, so an over-generous number here can sag the shared
// rail enough to reset the ESP32 right when a drum is hit. e.g. a 2A shared
// USB supply -> budget ~1200mA here, not 2000.
#define LED_SUPPLY_VOLTS 5
#define LED_SUPPLY_MILLIAMPS 1200

                                                                                                                                                                                                                                                                                                                                                                                                   // Envelope-follower tuning, shared across all drums (per-drum threshold is
// set above). DC_TRACK_ALPHA tracks the mic's slow resting bias; ENVELOPE_ALPHA
// smooths the rectified signal into a "loudness" value compared to threshold.
#define DC_TRACK_ALPHA 0.001f
#define ENVELOPE_ALPHA 0.3f

// Floor for animSpeedMs (the "Anim speed" slider) — how fast a chase sweep
// or a rainbow/color-cycle hue rotation is allowed to run. Chase sweeps in
// particular need to be able to finish well under a drum's hit spacing
// (often <200ms in a fast passage) to read as "triggered by this hit"
// rather than still catching up from the last one.
#define ANIM_SPEED_MIN_MS 40
