#include <Arduino.h>
#include <FastLED.h>
#include <math.h>
#include "config.h"
#include "settings.h"
#include "presets.h"
#include "web_ui.h"
#include "buttons.h"

// Per-drum runtime state (envelope follower + flash animation).
struct DrumState {
  float dcOffset = 2048.0f;  // starts near mid-scale of a 12-bit ADC
  float envelope = 0.0f;
  unsigned long lastHitMs = 0;
  unsigned long flashStartMs = 0;
  bool flashing = false;
  unsigned long prevAnimStart = (unsigned long)-1;  // last flashStartMs a chase render saw, to detect a fresh trigger
  unsigned long lastSparkleMs = 0;  // last time ANIM_SPARKLE spawned a twinkle
};

static DrumState states[NUM_DRUMS];

// One LED buffer per possible drum slot. FastLED needs the data pin as a
// compile-time template parameter, so slots (and their addLeds<> calls
// below) are fixed at 4 even if NUM_DRUMS is smaller.
static CRGB ledSlot0[MAX_LEDS_PER_STRIP];
static CRGB ledSlot1[MAX_LEDS_PER_STRIP];
static CRGB ledSlot2[MAX_LEDS_PER_STRIP];
static CRGB ledSlot3[MAX_LEDS_PER_STRIP];
static CRGB* const ledSlots[4] = {
  ledSlot0, ledSlot1, ledSlot2, ledSlot3
};

// Starts drum i's flash animation. Shared by real hits (mic threshold), the
// physical test button, and the web UI's "Test hit" button, so none of them
// are distinguishable downstream.
static void triggerHit(uint8_t i, unsigned long now) {
  DrumState& st = states[i];
  st.flashing = true;
  st.flashStartMs = now;
  st.lastHitMs = now;
  webUiNotifyHit(i);
}

// Bridges the web UI's "Test hit" button (which only knows a drum index)
// to triggerHit (which wants a timestamp too).
static void handleWebTestHit(uint8_t i) {
  triggerHit(i, millis());
}

// Fires every configured drum at once, for the physical "test all" button.
static void handleTestAll(unsigned long now) {
  for (uint8_t i = 0; i < NUM_DRUMS; i++) triggerHit(i, now);
}

void setup() {
  Serial.begin(115200);

  analogReadResolution(12);       // 0-4095
  analogSetAttenuation(ADC_11db); // ~0-3.3V input range

  settingsInit();
  presetsInit();

#if NUM_DRUMS > 0
  FastLED.addLeds<WS2812B, LED_PIN_0, GRB>(ledSlots[0], DRUMS[0].ledCount);
#endif
#if NUM_DRUMS > 1
  FastLED.addLeds<WS2812B, LED_PIN_1, GRB>(ledSlots[1], DRUMS[1].ledCount);
#endif
#if NUM_DRUMS > 2
  FastLED.addLeds<WS2812B, LED_PIN_2, GRB>(ledSlots[2], DRUMS[2].ledCount);
#endif
#if NUM_DRUMS > 3
  FastLED.addLeds<WS2812B, LED_PIN_3, GRB>(ledSlots[3], DRUMS[3].ledCount);
#endif

  FastLED.setMaxPowerInVoltsAndMilliamps(LED_SUPPLY_VOLTS, LED_SUPPLY_MILLIAMPS);
  FastLED.setBrightness(masterBrightness);
  FastLED.clear(true);

  for (uint8_t i = 0; i < NUM_DRUMS; i++) {
    pinMode(DRUMS[i].micPin, INPUT);
  }

  webUiInit(handleWebTestHit);
  buttonsInit(handleTestAll);
}

// Reads one drum's mic, updates its envelope follower, and triggers a hit
// if the envelope crosses threshold and the refractory period has elapsed.
// Non-blocking.
static void updateDetection(uint8_t i, unsigned long now) {
  const DrumConfig& cfg = DRUMS[i];
  DrumState& st = states[i];

  int raw = analogRead(cfg.micPin);

  // Slowly track the mic's resting DC bias, then rectify + smooth to get a
  // "loudness" envelope we can compare against a fixed threshold.
  st.dcOffset += DC_TRACK_ALPHA * (raw - st.dcOffset);
  float rectified = fabsf(raw - st.dcOffset);
  st.envelope += ENVELOPE_ALPHA * (rectified - st.envelope);

  webUiSetEnvelope(i, (int)st.envelope);

  int threshold;
  uint16_t refractoryMs;
  portENTER_CRITICAL(&settingsMux);
  threshold = settings[i].threshold;
  refractoryMs = settings[i].refractoryMs;
  portEXIT_CRITICAL(&settingsMux);

  if (st.envelope > threshold && (now - st.lastHitMs) > refractoryMs) {
    triggerHit(i, now);
  }
}

// Returns a copy of c scaled by s (nscale8_video mutates in place, so this
// keeps callers from having to worry about that when composing colors).
static inline CRGB scaled(CRGB c, uint8_t s) {
  c.nscale8_video(s);
  return c;
}

static inline uint16_t wrapIndex(int idx, uint16_t count) {
  int c = (int)count;
  return (uint16_t)(((idx % c) + c) % c);
}

// Renders one hit-triggered chase sweep for drum i. Idle (off) until
// st.flashing goes true (set by triggerHit — same flag FLASH uses), then
// runs exactly one pass and stops; a new hit mid-sweep restarts it from the
// top, same as FLASH restarting its decay. Never free-runs between hits.
//
// doubleSided: two comets launch from pixel 0 and travel opposite ways
// around the strip, each covering half of it, meeting at the far side.
// persist: swept pixels are set solid and left on (instead of a fading
// comet tail) until the sweep completes, then the whole strip holds and
// fades out together over flashMs — "they all go away together".
static void renderChase(uint8_t i, unsigned long now, CRGB color, uint16_t flashMs,
                         uint16_t animSpeedMs, uint16_t animLength,
                         bool doubleSided, bool persist) {
  DrumState& st = states[i];
  CRGB* leds = ledSlots[i];
  uint16_t count = DRUMS[i].ledCount;
  if (count == 0) return;

  // A fresh trigger (flashStartMs just changed) always starts from a clean
  // buffer, so leftovers from a previous sweep/color never bleed in.
  if (st.prevAnimStart != st.flashStartMs) {
    fill_solid(leds, count, CRGB::Black);
    st.prevAnimStart = st.flashStartMs;
  }

  if (!st.flashing) {
    if (!persist) fadeToBlackBy(leds, count, 40); // let any residual comet tail keep decaying
    return;
  }

  unsigned long elapsed = now - st.flashStartMs;
  uint16_t travelMs = max((uint16_t)ANIM_SPEED_MIN_MS, animSpeedMs);
  uint16_t travelPixels = doubleSided ? (uint16_t)(count / 2) : count;
  uint16_t trailLen = (uint16_t)constrain((int)animLength, 1, (int)count);

  if (persist) {
    unsigned long totalMs = (unsigned long)travelMs + flashMs;
    if (elapsed >= totalMs) {
      st.flashing = false;
      fill_solid(leds, count, CRGB::Black);
      return;
    }
    float travelFrac = min(1.0f, (float)elapsed / (float)travelMs);
    uint16_t headOffset = (uint16_t)(travelFrac * travelPixels);
    for (uint16_t k = 0; k <= headOffset; k++) {
      leds[wrapIndex(k, count)] = color;
      if (doubleSided) leds[wrapIndex(-(int)k, count)] = color;
    }
    if (elapsed >= travelMs) {
      unsigned long holdElapsed = elapsed - travelMs;
      uint8_t level = 255 - (uint8_t)min((unsigned long)255, (255UL * holdElapsed) / max((uint16_t)1, flashMs));
      nscale8_video(leds, count, level);
    }
  } else {
    if (elapsed >= travelMs) {
      st.flashing = false; // sweep done; idle path above fades the tail out over subsequent frames
      fadeToBlackBy(leds, count, 40);
      return;
    }
    fadeToBlackBy(leds, count, 40);
    float travelFrac = (float)elapsed / (float)travelMs;
    int headOffset = (int)(travelFrac * travelPixels);
    for (uint16_t k = 0; k < trailLen; k++) {
      int off = headOffset - (int)k;
      if (off < 0) continue; // comet hasn't traveled this far yet
      leds[wrapIndex(off, count)] = color;
      if (doubleSided) leds[wrapIndex(-off, count)] = color;
    }
  }
}

// Renders ANIM_RIPPLE for drum i: a dim solid base glow at rest, and on a
// hit a bright band travels from the center out to both ends of the strip,
// fading back down to the base glow behind it as it goes. Never fully dark.
static void renderRipple(uint8_t i, unsigned long now, CRGB color, uint16_t flashMs,
                          uint16_t animSpeedMs, uint16_t animLength) {
  DrumState& st = states[i];
  CRGB* leds = ledSlots[i];
  uint16_t count = DRUMS[i].ledCount;
  if (count == 0) return;

  const uint8_t floorLevel = 22; // dim idle base glow, ~9% brightness
  fill_solid(leds, count, scaled(color, floorLevel));
  if (!st.flashing) return;

  uint16_t travelMs = max((uint16_t)ANIM_SPEED_MIN_MS, animSpeedMs);
  uint16_t waveWidth = (uint16_t)constrain((int)animLength, 1, (int)count);
  int center = count / 2;
  int maxDist = max(center, (int)count - center);

  unsigned long elapsed = now - st.flashStartMs;
  unsigned long totalMs = (unsigned long)travelMs + flashMs;
  if (elapsed >= totalMs) {
    st.flashing = false;
    return;
  }

  float travelFrac = min(1.0f, (float)elapsed / (float)travelMs);
  int front = (int)(travelFrac * maxDist);
  unsigned long fadeElapsed = elapsed > travelMs ? elapsed - travelMs : 0;
  uint8_t fadeLevel = 255 - (uint8_t)min((unsigned long)255, (255UL * fadeElapsed) / max((uint16_t)1, flashMs));

  for (int d = 0; d <= front; d++) {
    int distFromFront = front - d;
    uint8_t level = (distFromFront < (int)waveWidth) ? 255 : fadeLevel;
    level = max(level, floorLevel);
    CRGB c = scaled(color, level);
    int idxPos = center + d, idxNeg = center - d;
    if (idxPos >= 0 && idxPos < (int)count) leds[idxPos] = c;
    if (idxNeg >= 0 && idxNeg < (int)count) leds[idxNeg] = c;
  }
}

// Renders ANIM_STROBE for drum i: fully off at rest, a hit fires a rapid
// on/off strobe burst (period from animSpeedMs) for flashMs total, easing
// out over its last 30% instead of cutting off abruptly.
static void renderStrobe(uint8_t i, unsigned long now, CRGB color, uint16_t flashMs, uint16_t animSpeedMs) {
  DrumState& st = states[i];
  CRGB* leds = ledSlots[i];
  uint16_t count = DRUMS[i].ledCount;
  if (count == 0) return;

  if (!st.flashing) {
    fill_solid(leds, count, CRGB::Black);
    return;
  }

  unsigned long elapsed = now - st.flashStartMs;
  if (elapsed >= flashMs) {
    st.flashing = false;
    fill_solid(leds, count, CRGB::Black);
    return;
  }

  uint16_t periodMs = (uint16_t)constrain((int)animSpeedMs, 20, 500);
  bool on = (elapsed % periodMs) < (periodMs / 2);

  unsigned long fadeStart = (flashMs * 7) / 10;
  uint8_t level = 255;
  if (elapsed > fadeStart) {
    unsigned long fadeSpan = max((unsigned long)1, (unsigned long)flashMs - fadeStart);
    level = 255 - (uint8_t)min((unsigned long)255, (255UL * (elapsed - fadeStart)) / fadeSpan);
  }

  fill_solid(leds, count, on ? scaled(color, level) : CRGB::Black);
}

// Advances drum i's animation and writes the result into its LED buffer.
// Non-blocking — every mode is purely a function of elapsed time (plus the
// decaying "hit pulse"/chase-sweep state below), so nothing here needs to
// persist across frames except the pixel buffer itself (used as a trail).
//
// ANIM_FLASH and the whole CHASE family have no ambient pattern — fully off
// until a hit. RAINBOW/COLORCYCLE run a continuous ambient pattern and
// brighten on top of it when the drum fires.
static void updateAnimation(uint8_t i, unsigned long now) {
  const DrumConfig& cfg = DRUMS[i];
  DrumState& st = states[i];
  CRGB* leds = ledSlots[i];
  uint16_t count = cfg.ledCount;

  CRGB color;
  uint16_t flashMs, animSpeedMs, animLength;
  AnimMode mode;
  portENTER_CRITICAL(&settingsMux);
  color = settings[i].color;
  flashMs = settings[i].flashMs;
  mode = settings[i].animMode;
  animSpeedMs = settings[i].animSpeedMs;
  animLength = settings[i].animLength;
  portEXIT_CRITICAL(&settingsMux);

  switch (mode) {
    case ANIM_CHASE:
      renderChase(i, now, color, flashMs, animSpeedMs, animLength, false, false);
      return;
    case ANIM_CHASE_DOUBLE:
      renderChase(i, now, color, flashMs, animSpeedMs, animLength, true, false);
      return;
    case ANIM_CHASE_PERSIST:
      renderChase(i, now, color, flashMs, animSpeedMs, animLength, false, true);
      return;
    case ANIM_CHASE_DOUBLE_PERSIST:
      renderChase(i, now, color, flashMs, animSpeedMs, animLength, true, true);
      return;
    case ANIM_RIPPLE:
      renderRipple(i, now, color, flashMs, animSpeedMs, animLength);
      return;
    case ANIM_STROBE:
      renderStrobe(i, now, color, flashMs, animSpeedMs);
      return;
    default:
      break;
  }

  uint16_t speedMs = max((uint16_t)ANIM_SPEED_MIN_MS, animSpeedMs); // guard against 0 -> div by zero

  uint8_t pulse = 0; // 0-255, decays from 255 over flashMs after a hit
  if (st.flashing) {
    unsigned long elapsed = now - st.flashStartMs;
    if (elapsed >= flashMs) {
      st.flashing = false;
    } else {
      float t = (float)elapsed / (float)flashMs;   // 0 -> 1 over the flash
      pulse = (uint8_t)(255.0f * (1.0f - t));       // instant on, linear decay
    }
  }

  switch (mode) {
    case ANIM_RAINBOW: {
      // Continuous rotating rainbow band; a hit briefly boosts brightness.
      uint8_t hue = (uint8_t)(((now % speedMs) * 255UL) / speedMs);
      fill_rainbow(leds, count, hue, count ? (uint8_t)(255 / count + 1) : 0);
      nscale8_video(leds, count, qadd8(140, pulse));
      break;
    }
    case ANIM_COLORCYCLE: {
      // The configured color slowly shifts hue, breathing; a hit flashes it.
      CHSV base = rgb2hsv_approximate(color);
      uint8_t hueShift = (uint8_t)(((now % speedMs) * 255UL) / speedMs);
      fill_solid(leds, count, CHSV(base.hue + hueShift, base.sat, 255));
      nscale8_video(leds, count, qadd8(110, pulse));
      break;
    }
    case ANIM_EMBER: {
      // Dim breathing resting glow; a hit flares it to full brightness and
      // it decays back down to the glow (never to black). animLength is
      // repurposed as the glow's resting brightness (percent), animSpeedMs
      // as the breathe period.
      uint8_t floorPct = (uint8_t)constrain((int)animLength, 1, 100);
      float breathePhase = (float)(now % speedMs) / (float)speedMs;
      float tri = breathePhase < 0.5f ? (breathePhase * 2.0f) : (2.0f - breathePhase * 2.0f);
      uint8_t floorLevel = (uint8_t)(255.0f * (floorPct / 100.0f) * (0.7f + 0.3f * tri));
      fill_solid(leds, count, scaled(color, qadd8(floorLevel, pulse)));
      break;
    }
    case ANIM_SPARKLE: {
      // Sparse idle twinkle; a hit bursts into denser, brighter sparkles
      // that settle back to the idle twinkle. animLength is repurposed as
      // idle sparkle density (percent), animSpeedMs as the spawn interval.
      uint8_t densityPct = (uint8_t)constrain((int)animLength, 1, 100);
      uint16_t spawnMs = max((uint16_t)ANIM_SPEED_MIN_MS, animSpeedMs);
      fadeToBlackBy(leds, count, 24);
      if (count > 0 && now - st.lastSparkleMs >= spawnMs) {
        st.lastSparkleMs = now;
        uint8_t spawns = 1 + (pulse / 60); // a hit spawns extra sparkles this tick
        for (uint8_t k = 0; k < spawns; k++) {
          if (pulse > 0 || random8(100) < densityPct) {
            leds[random16(count)] = scaled(color, qadd8(180, pulse));
          }
        }
      }
      break;
    }
    case ANIM_FLASH:
    default: {
      fill_solid(leds, count, scaled(color, pulse));
      break;
    }
  }
}

void loop() {
  unsigned long now = millis();

  buttonsUpdate(now);

  for (uint8_t i = 0; i < NUM_DRUMS; i++) {
    updateDetection(i, now);
    updateAnimation(i, now);
  }

  webUiLoop(now);

  // Two independent on/off gates, both suppressing only the final LED
  // output (detection/animation state keeps running underneath either
  // way, so re-enabling resumes exactly where things would already be):
  // the physical POWER_SWITCH_PIN (systemEnabled(), always true if unwired)
  // and the software toggle from the web UI (lightsEnabled), which lets
  // lighting be disabled even with no physical switch connected.
  if (!systemEnabled() || !lightsEnabled) {
    FastLED.clear();
  }
  FastLED.show();
}
