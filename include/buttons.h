#pragma once
#include <Arduino.h>
#include "config.h"

// Physical control board: a maintained on/off power switch, one momentary
// "test all" button, and up to MAX_PRESET_BUTTONS momentary buttons that
// each instantly recall a template or a saved preset. See config.h for pin
// assignments and the compile-time default mapping.
//
// The preset-button -> target mapping starts from PRESET_BUTTON_DEFAULTS in
// config.h, but is then persisted to NVS once reassigned from the web UI's
// Buttons panel, so a firmware reflash doesn't undo a live reassignment.

// Called when the global test button is pressed, so main.cpp can fire every
// configured drum through the normal trigger path.
typedef void (*TestAllCallback)(unsigned long now);

// Configures pins and loads the button->target mapping from NVS (falling
// back to PRESET_BUTTON_DEFAULTS). Call once from setup(), after
// presetsInit().
void buttonsInit(TestAllCallback onTestAll);

// Polls the power switch, test button, and preset buttons (all debounced).
// A preset button press applies its mapped target directly (via presets.h)
// and syncs every connected browser. Non-blocking. Call every loop()
// iteration.
void buttonsUpdate(unsigned long now);

// True when the master power switch allows LED output (always true if
// POWER_SWITCH_PIN is NO_PIN).
bool systemEnabled();

// One preset button's current mapping, for the web UI's Buttons panel.
struct ButtonInfo {
  uint8_t index;
  bool wired;
  ButtonTargetKind kind;
  uint8_t id;
};

// Fills `out` (capacity maxOut, should be >= MAX_PRESET_BUTTONS) with every
// preset button's current mapping. Returns how many were written.
int buttonsGetMapping(ButtonInfo* out, uint8_t maxOut);

// Reassigns preset button `index` to load template/preset `id`, and
// persists it to NVS. Returns false if index is out of range.
bool buttonsSetMapping(uint8_t index, ButtonTargetKind kind, uint8_t id);
