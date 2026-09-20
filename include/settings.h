#pragma once
#include <FastLED.h>
#include "config.h"

// Runtime-tunable per-drum settings (color/threshold/refractoryMs/flashMs).
// Seeded from DRUMS[]'s default* fields on first boot, then loaded from /
// persisted to NVS (flash) as the user tunes them from the web UI. Reading
// and writing these from outside the main loop task (e.g. the async web
// server's callback) must go through the settingsMux critical section below.
struct DrumSettings {
  CRGB color;
  int threshold;
  uint16_t refractoryMs;
  uint16_t flashMs;
  AnimMode animMode;
  uint16_t animSpeedMs;
  uint16_t animLength;
};

extern DrumSettings settings[NUM_DRUMS];
extern uint8_t masterBrightness;
// Software lighting on/off, independent of the physical POWER_SWITCH_PIN
// (see buttons.h's systemEnabled()). Both must be true for LEDs to light:
// this lets the web UI disable output even when no physical switch is
// wired, and the physical switch (if present) still force-overrides it.
extern bool lightsEnabled;
extern portMUX_TYPE settingsMux;

// Loads settings[], masterBrightness, and lightsEnabled from NVS, falling
// back to config.h defaults for anything not yet saved. Call once from
// setup().
void settingsInit();

// Persists drum i's current in-RAM settings[i] to NVS.
void settingsSaveDrum(uint8_t i);

// Persists the current in-RAM masterBrightness to NVS.
void settingsSaveBrightness();

// Sets lightsEnabled and persists it to NVS immediately (no separate
// commit step, since it's a discrete toggle rather than a dragged slider).
void settingsSetLightsEnabled(bool on);
