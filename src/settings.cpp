#include <Preferences.h>
#include "settings.h"

DrumSettings settings[NUM_DRUMS];
uint8_t masterBrightness = DEFAULT_BRIGHTNESS;
bool lightsEnabled = true;
portMUX_TYPE settingsMux = portMUX_INITIALIZER_UNLOCKED;

static Preferences prefs;

// Keys are short (NVS keys are capped at 15 chars): "c0","t0","r0","f0","a0","s0","l0", "br".
static void keyFor(char field, uint8_t i, char* out) {
  out[0] = field;
  out[1] = '0' + i;
  out[2] = '\0';
}

void settingsInit() {
  prefs.begin("drumlight", false);

  for (uint8_t i = 0; i < NUM_DRUMS; i++) {
    const DrumConfig& cfg = DRUMS[i];
    char key[4];

    keyFor('c', i, key);
    uint32_t packed = ((uint32_t)cfg.defaultColor.r << 16) |
                       ((uint32_t)cfg.defaultColor.g << 8) |
                       (uint32_t)cfg.defaultColor.b;
    packed = prefs.getUInt(key, packed);
    settings[i].color = CRGB((packed >> 16) & 0xFF, (packed >> 8) & 0xFF, packed & 0xFF);

    keyFor('t', i, key);
    settings[i].threshold = prefs.getInt(key, cfg.defaultThreshold);

    keyFor('r', i, key);
    settings[i].refractoryMs = prefs.getUShort(key, cfg.defaultRefractoryMs);

    keyFor('f', i, key);
    settings[i].flashMs = prefs.getUShort(key, cfg.defaultFlashMs);

    keyFor('a', i, key);
    settings[i].animMode = (AnimMode)prefs.getUChar(key, (uint8_t)cfg.defaultAnimMode);

    keyFor('s', i, key);
    settings[i].animSpeedMs = prefs.getUShort(key, cfg.defaultAnimSpeedMs);

    keyFor('l', i, key);
    settings[i].animLength = prefs.getUShort(key, cfg.defaultAnimLength);
  }

  masterBrightness = prefs.getUChar("br", DEFAULT_BRIGHTNESS);
  lightsEnabled = prefs.getBool("en", true);
}

void settingsSaveDrum(uint8_t i) {
  if (i >= NUM_DRUMS) return;
  char key[4];
  CRGB c;
  int threshold;
  uint16_t refractoryMs, flashMs, animSpeedMs, animLength;
  AnimMode animMode;

  portENTER_CRITICAL(&settingsMux);
  c = settings[i].color;
  threshold = settings[i].threshold;
  refractoryMs = settings[i].refractoryMs;
  flashMs = settings[i].flashMs;
  animMode = settings[i].animMode;
  animSpeedMs = settings[i].animSpeedMs;
  animLength = settings[i].animLength;
  portEXIT_CRITICAL(&settingsMux);

  keyFor('c', i, key);
  prefs.putUInt(key, ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | c.b);
  keyFor('t', i, key);
  prefs.putInt(key, threshold);
  keyFor('r', i, key);
  prefs.putUShort(key, refractoryMs);
  keyFor('f', i, key);
  prefs.putUShort(key, flashMs);
  keyFor('a', i, key);
  prefs.putUChar(key, (uint8_t)animMode);
  keyFor('s', i, key);
  prefs.putUShort(key, animSpeedMs);
  keyFor('l', i, key);
  prefs.putUShort(key, animLength);
}

void settingsSaveBrightness() {
  uint8_t b;
  portENTER_CRITICAL(&settingsMux);
  b = masterBrightness;
  portEXIT_CRITICAL(&settingsMux);
  prefs.putUChar("br", b);
}

void settingsSetLightsEnabled(bool on) {
  portENTER_CRITICAL(&settingsMux);
  lightsEnabled = on;
  portEXIT_CRITICAL(&settingsMux);
  prefs.putBool("en", on);
}
