#pragma once
#include <Arduino.h>
#include "config.h"

// Called when the web UI's "Test hit" button fires for drum i, so main.cpp
// can run it through the same trigger path as a real mic/button hit.
typedef void (*TestHitCallback)(uint8_t drumIndex);

// Mounts LittleFS, brings up the Wi-Fi AP + captive portal + mDNS, and
// starts the web server and WebSocket. Call once from setup(), after
// settingsInit().
void webUiInit(TestHitCallback onTestHit);

// Broadcasts telemetry (on its own throttled interval) and pumps the
// captive-portal DNS responder. Call every loop() iteration.
void webUiLoop(unsigned long now);

// Feeds drum i's latest envelope value into the telemetry stream. Call from
// the detection loop every iteration.
void webUiSetEnvelope(uint8_t i, int envelope);

// Flags that drum i just fired, so connected clients can show a hit marker.
// Call from triggerHit() for both real and test hits.
void webUiNotifyHit(uint8_t i);

// Re-syncs every connected browser with the full current state (drums,
// presets, button mapping, etc). Call after anything changes state outside
// of a browser's own request — e.g. a physical preset button being pressed.
void webUiNotifyStateChanged();
