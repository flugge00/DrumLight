#pragma once
#include <Arduino.h>
#include "config.h"

// Named "looks" — a saved bundle of every drum's color + animation
// (mode/speed/flash duration), so the whole kit can be switched at once
// instead of tuning each drum's card individually. Mic tuning (threshold/
// refractoryMs) is per-hardware and intentionally NOT part of a preset.
//
// Stored as fixed-size blobs in NVS, one per slot. A slot is "free" when no
// blob of the current size exists there yet (see presetsGet).
#define MAX_PRESETS 8
#define PRESET_NAME_LEN 20

struct PresetDrum {
  uint8_t r, g, b;
  uint8_t animMode;
  uint16_t animSpeedMs;
  uint16_t animLength;
  uint16_t flashMs;
};

struct Preset {
  char name[PRESET_NAME_LEN];
  PresetDrum drums[NUM_DRUMS];
};

struct PresetSummary {
  uint8_t slot;
  char name[PRESET_NAME_LEN];
};

// Opens the presets NVS namespace. Call once from setup(), after settingsInit().
void presetsInit();

// Fills `out` (capacity maxOut) with a summary of every occupied slot.
// Returns how many were written.
int presetsList(PresetSummary* out, uint8_t maxOut);

// Snapshots the current in-RAM settings[] (color/animMode/animSpeedMs/
// flashMs, all drums) into `slot` under `name`, overwriting whatever was
// there. Returns false if slot is out of range.
bool presetsSave(uint8_t slot, const char* name);

// Loads `slot` into settings[] and persists every drum to NVS. Returns
// false if the slot is empty or out of range.
bool presetsApply(uint8_t slot);

// Loads built-in PRESET_TEMPLATES[idx] into settings[] and persists every
// drum to NVS, same as presetsApply but for a read-only firmware template
// instead of a saved slot. Returns false if idx is out of range.
bool presetsApplyTemplate(uint8_t idx);

// Clears `slot`.
void presetsDelete(uint8_t slot);

// First unoccupied slot, or -1 if all MAX_PRESETS are taken.
int presetsFindFreeSlot();
