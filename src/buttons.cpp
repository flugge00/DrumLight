#include <Preferences.h>
#include "buttons.h"
#include "presets.h"
#include "web_ui.h"

static Preferences prefs;
static TestAllCallback testAllCb = nullptr;
static ButtonTarget mapping[MAX_PRESET_BUTTONS];
static bool powerEnabled = true;

// Debounce state, one instance per physical button/switch.
struct ButtonState {
  bool rawPrev = false;
  bool stable = false;
  unsigned long lastChangeMs = 0;
};

static ButtonState powerState;
static ButtonState testState;
static ButtonState presetStates[MAX_PRESET_BUTTONS];

static const unsigned long DEBOUNCE_MS = 30;

static void keyFor(uint8_t idx, char* out) {
  snprintf(out, 4, "b%d", idx);
}

void buttonsInit(TestAllCallback onTestAll) {
  testAllCb = onTestAll;
  prefs.begin("dl_buttons", false);

  for (uint8_t i = 0; i < MAX_PRESET_BUTTONS; i++) {
    char key[4];
    keyFor(i, key);
    const ButtonTarget& def = PRESET_BUTTON_DEFAULTS[i];
    uint16_t packed = prefs.getUShort(key, ((uint16_t)def.kind << 8) | def.id);
    mapping[i].kind = (ButtonTargetKind)(packed >> 8);
    mapping[i].id = packed & 0xFF;

    if (PRESET_BUTTON_PINS[i] != NO_PIN) {
      pinMode(PRESET_BUTTON_PINS[i], INPUT_PULLUP);
    }
  }

  if (POWER_SWITCH_PIN != NO_PIN) {
    pinMode(POWER_SWITCH_PIN, INPUT_PULLUP);
    // Read the switch's resting state immediately so the system doesn't
    // glitch "off" for one debounce window right after boot.
    powerEnabled = digitalRead(POWER_SWITCH_PIN) == LOW;
    powerState.rawPrev = powerState.stable = powerEnabled;
  }

  if (GLOBAL_TEST_BUTTON_PIN != NO_PIN) {
    pinMode(GLOBAL_TEST_BUTTON_PIN, INPUT_PULLUP);
  }
}

// Debounces one active-low pin. Returns true on the frame the stabilized
// reading changes; the new value is written to stableOut either way.
static bool debounceChanged(uint8_t pin, ButtonState& st, unsigned long now, bool& stableOut) {
  bool reading = digitalRead(pin) == LOW;
  if (reading != st.rawPrev) {
    st.lastChangeMs = now;
    st.rawPrev = reading;
  }
  bool changed = false;
  if ((now - st.lastChangeMs) > DEBOUNCE_MS && reading != st.stable) {
    st.stable = reading;
    changed = true;
  }
  stableOut = st.stable;
  return changed;
}

static void applyTarget(const ButtonTarget& t) {
  bool applied = false;
  if (t.kind == BTN_TARGET_TEMPLATE) {
    applied = presetsApplyTemplate(t.id);
  } else if (t.kind == BTN_TARGET_PRESET) {
    applied = presetsApply(t.id);
  }
  if (applied) webUiNotifyStateChanged();
}

void buttonsUpdate(unsigned long now) {
  if (POWER_SWITCH_PIN != NO_PIN) {
    bool stable;
    if (debounceChanged(POWER_SWITCH_PIN, powerState, now, stable)) {
      webUiNotifyStateChanged();
    }
    powerEnabled = stable;
  }

  if (GLOBAL_TEST_BUTTON_PIN != NO_PIN) {
    bool stable;
    if (debounceChanged(GLOBAL_TEST_BUTTON_PIN, testState, now, stable) && stable) {
      if (testAllCb) testAllCb(now);
    }
  }

  for (uint8_t i = 0; i < MAX_PRESET_BUTTONS; i++) {
    if (PRESET_BUTTON_PINS[i] == NO_PIN) continue;
    bool stable;
    if (debounceChanged(PRESET_BUTTON_PINS[i], presetStates[i], now, stable) && stable) {
      applyTarget(mapping[i]);
    }
  }
}

bool systemEnabled() {
  return powerEnabled;
}

int buttonsGetMapping(ButtonInfo* out, uint8_t maxOut) {
  int count = 0;
  for (uint8_t i = 0; i < MAX_PRESET_BUTTONS && count < maxOut; i++) {
    out[count].index = i;
    out[count].wired = PRESET_BUTTON_PINS[i] != NO_PIN;
    out[count].kind = mapping[i].kind;
    out[count].id = mapping[i].id;
    count++;
  }
  return count;
}

bool buttonsSetMapping(uint8_t index, ButtonTargetKind kind, uint8_t id) {
  if (index >= MAX_PRESET_BUTTONS) return false;
  mapping[index].kind = kind;
  mapping[index].id = id;
  char key[4];
  keyFor(index, key);
  prefs.putUShort(key, ((uint16_t)kind << 8) | id);
  return true;
}
