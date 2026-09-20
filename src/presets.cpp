#include <Preferences.h>
#include <string.h>
#include "presets.h"
#include "settings.h"

static Preferences prefs;

static void keyFor(uint8_t slot, char* out) {
  snprintf(out, 4, "p%d", slot);
}

void presetsInit() {
  prefs.begin("dl_presets", false);
}

static bool presetsGet(uint8_t slot, Preset& out) {
  if (slot >= MAX_PRESETS) return false;
  char key[4];
  keyFor(slot, key);
  if (prefs.getBytesLength(key) != sizeof(Preset)) return false;
  prefs.getBytes(key, &out, sizeof(Preset));
  return true;
}

int presetsList(PresetSummary* out, uint8_t maxOut) {
  int count = 0;
  Preset p;
  for (uint8_t slot = 0; slot < MAX_PRESETS && count < maxOut; slot++) {
    if (!presetsGet(slot, p)) continue;
    out[count].slot = slot;
    strncpy(out[count].name, p.name, PRESET_NAME_LEN - 1);
    out[count].name[PRESET_NAME_LEN - 1] = '\0';
    count++;
  }
  return count;
}

int presetsFindFreeSlot() {
  Preset p;
  for (uint8_t slot = 0; slot < MAX_PRESETS; slot++) {
    if (!presetsGet(slot, p)) return slot;
  }
  return -1;
}

bool presetsSave(uint8_t slot, const char* name) {
  if (slot >= MAX_PRESETS) return false;

  Preset p;
  memset(&p, 0, sizeof(p));
  strncpy(p.name, name, PRESET_NAME_LEN - 1);

  portENTER_CRITICAL(&settingsMux);
  for (uint8_t i = 0; i < NUM_DRUMS; i++) {
    p.drums[i].r = settings[i].color.r;
    p.drums[i].g = settings[i].color.g;
    p.drums[i].b = settings[i].color.b;
    p.drums[i].animMode = (uint8_t)settings[i].animMode;
    p.drums[i].animSpeedMs = settings[i].animSpeedMs;
    p.drums[i].animLength = settings[i].animLength;
    p.drums[i].flashMs = settings[i].flashMs;
  }
  portEXIT_CRITICAL(&settingsMux);

  char key[4];
  keyFor(slot, key);
  return prefs.putBytes(key, &p, sizeof(p)) == sizeof(p);
}

bool presetsApply(uint8_t slot) {
  Preset p;
  if (!presetsGet(slot, p)) return false;

  portENTER_CRITICAL(&settingsMux);
  for (uint8_t i = 0; i < NUM_DRUMS; i++) {
    settings[i].color = CRGB(p.drums[i].r, p.drums[i].g, p.drums[i].b);
    settings[i].animMode = (AnimMode)p.drums[i].animMode;
    settings[i].animSpeedMs = p.drums[i].animSpeedMs;
    settings[i].animLength = p.drums[i].animLength;
    settings[i].flashMs = p.drums[i].flashMs;
  }
  portEXIT_CRITICAL(&settingsMux);

  for (uint8_t i = 0; i < NUM_DRUMS; i++) settingsSaveDrum(i);
  return true;
}

bool presetsApplyTemplate(uint8_t idx) {
  if (idx >= NUM_PRESET_TEMPLATES) return false;
  const PresetTemplate& tpl = PRESET_TEMPLATES[idx];

  portENTER_CRITICAL(&settingsMux);
  for (uint8_t i = 0; i < NUM_DRUMS; i++) {
    settings[i].color = tpl.drums[i].color;
    settings[i].animMode = tpl.drums[i].animMode;
    settings[i].animSpeedMs = tpl.drums[i].animSpeedMs;
    settings[i].animLength = tpl.drums[i].animLength;
    settings[i].flashMs = tpl.drums[i].flashMs;
  }
  portEXIT_CRITICAL(&settingsMux);

  for (uint8_t i = 0; i < NUM_DRUMS; i++) settingsSaveDrum(i);
  return true;
}

void presetsDelete(uint8_t slot) {
  if (slot >= MAX_PRESETS) return;
  char key[4];
  keyFor(slot, key);
  prefs.remove(key);
}
