#include <WiFi.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "web_ui.h"
#include "settings.h"
#include "presets.h"
#include "buttons.h"

static AsyncWebServer server(80);
static AsyncWebSocket ws("/ws");
static DNSServer dnsServer;
static TestHitCallback testHitCb = nullptr;
static unsigned long lastTelemetryMs = 0;

// Envelope/hit counters are single aligned 32-bit words, so plain volatile
// reads/writes are safe across the loop task and the AsyncTCP task without a
// critical section. settings[] holds a CRGB (3 separate bytes) alongside the
// other fields, which isn't safe to read/write torn — that's guarded by
// settingsMux instead (see settings.h/.cpp).
static volatile int envelopeValues[NUM_DRUMS] = {0};
static volatile uint32_t hitSeqArr[NUM_DRUMS] = {0};

static CRGB parseHexColor(const char* hex) {
  if (!hex || hex[0] != '#' || strlen(hex) < 7) return CRGB::Black;
  auto hexVal = [](char c) -> uint8_t {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
  };
  uint8_t r = (hexVal(hex[1]) << 4) | hexVal(hex[2]);
  uint8_t g = (hexVal(hex[3]) << 4) | hexVal(hex[4]);
  uint8_t b = (hexVal(hex[5]) << 4) | hexVal(hex[6]);
  return CRGB(r, g, b);
}

static void appendPresetsArray(JsonDocument& doc) {
  JsonArray arr = doc["presets"].to<JsonArray>();
  PresetSummary list[MAX_PRESETS];
  int n = presetsList(list, MAX_PRESETS);
  for (int k = 0; k < n; k++) {
    JsonObject o = arr.add<JsonObject>();
    o["slot"] = list[k].slot;
    o["name"] = list[k].name;
  }
}

static void appendTemplatesArray(JsonDocument& doc) {
  JsonArray arr = doc["templates"].to<JsonArray>();
  for (uint8_t t = 0; t < NUM_PRESET_TEMPLATES; t++) {
    JsonObject o = arr.add<JsonObject>();
    o["idx"] = t;
    o["name"] = PRESET_TEMPLATES[t].name;
  }
}

static void appendPowerObject(JsonDocument& doc) {
  JsonObject o = doc["power"].to<JsonObject>();
  o["wired"] = POWER_SWITCH_PIN != NO_PIN;   // is a physical switch connected
  o["physOn"] = systemEnabled();             // physical switch's current state (always true if unwired)
  o["en"] = lightsEnabled;                   // software toggle from the web UI
}

static void appendButtonsArray(JsonDocument& doc) {
  JsonArray arr = doc["buttons"].to<JsonArray>();
  ButtonInfo list[MAX_PRESET_BUTTONS];
  int n = buttonsGetMapping(list, MAX_PRESET_BUTTONS);
  for (int k = 0; k < n; k++) {
    JsonObject o = arr.add<JsonObject>();
    o["index"] = list[k].index;
    o["wired"] = list[k].wired;
    o["kind"] = (uint8_t)list[k].kind;
    o["id"] = list[k].id;
  }
}

static String buildHello() {
  JsonDocument doc;
  doc["type"] = "hello";
  doc["maxAdc"] = 4095;
  doc["maxPresets"] = MAX_PRESETS;

  portENTER_CRITICAL(&settingsMux);
  doc["brightness"] = masterBrightness;
  JsonArray arr = doc["drums"].to<JsonArray>();
  for (uint8_t i = 0; i < NUM_DRUMS; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["i"] = i;
    o["n"] = DRUMS[i].name;
    o["lc"] = DRUMS[i].ledCount;
    char buf[8];
    snprintf(buf, sizeof(buf), "#%02x%02x%02x",
             settings[i].color.r, settings[i].color.g, settings[i].color.b);
    o["c"] = String(buf);
    o["th"] = settings[i].threshold;
    o["r"] = settings[i].refractoryMs;
    o["f"] = settings[i].flashMs;
    o["am"] = (uint8_t)settings[i].animMode;
    o["as"] = settings[i].animSpeedMs;
    o["al"] = settings[i].animLength;
  }
  portEXIT_CRITICAL(&settingsMux);

  JsonObject ap = doc["ap"].to<JsonObject>();
  ap["ssid"] = WIFI_AP_SSID;
  ap["ip"] = WiFi.softAPIP().toString();

  appendPresetsArray(doc);
  appendTemplatesArray(doc);
  appendButtonsArray(doc);
  appendPowerObject(doc);

  String out;
  serializeJson(doc, out);
  return out;
}

static void broadcastPower() {
  JsonDocument doc;
  doc["type"] = "power";
  appendPowerObject(doc);
  String out;
  serializeJson(doc, out);
  ws.textAll(out);
}

static void broadcastPresets() {
  JsonDocument doc;
  doc["type"] = "presets";
  appendPresetsArray(doc);
  String out;
  serializeJson(doc, out);
  ws.textAll(out);
}

static void broadcastButtons() {
  JsonDocument doc;
  doc["type"] = "buttons";
  appendButtonsArray(doc);
  String out;
  serializeJson(doc, out);
  ws.textAll(out);
}

static void broadcastTelemetry() {
  if (ws.count() == 0) return;

  JsonDocument doc;
  doc["type"] = "tm";
  doc["t"] = millis();
  doc["heap"] = ESP.getFreeHeap();
  doc["uptime"] = millis() / 1000;
  JsonArray arr = doc["d"].to<JsonArray>();
  for (uint8_t i = 0; i < NUM_DRUMS; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["i"] = i;
    o["e"] = envelopeValues[i];
    o["h"] = hitSeqArr[i];
  }

  String out;
  serializeJson(doc, out);
  ws.textAll(out);
}

static void sendAck(const char* type, int i) {
  JsonDocument ack;
  ack["type"] = type;
  if (i >= 0) ack["i"] = i;
  String out;
  serializeJson(ack, out);
  ws.textAll(out);
}

static void handleClientMessage(JsonDocument& doc) {
  const char* type = doc["type"] | "";

  if (strcmp(type, "set") == 0) {
    int i = doc["i"] | -1;
    if (i < 0 || i >= NUM_DRUMS) return;
    const char* k = doc["k"] | "";

    portENTER_CRITICAL(&settingsMux);
    if (strcmp(k, "th") == 0) settings[i].threshold = doc["v"] | settings[i].threshold;
    else if (strcmp(k, "r") == 0) settings[i].refractoryMs = doc["v"] | settings[i].refractoryMs;
    else if (strcmp(k, "f") == 0) settings[i].flashMs = doc["v"] | settings[i].flashMs;
    else if (strcmp(k, "c") == 0) settings[i].color = parseHexColor(doc["v"] | "");
    else if (strcmp(k, "am") == 0) {
      int v = doc["v"] | (int)settings[i].animMode;
      if (v >= 0 && v < ANIM_MODE_COUNT) settings[i].animMode = (AnimMode)v;
    } else if (strcmp(k, "as") == 0) settings[i].animSpeedMs = doc["v"] | settings[i].animSpeedMs;
    else if (strcmp(k, "al") == 0) settings[i].animLength = doc["v"] | settings[i].animLength;
    portEXIT_CRITICAL(&settingsMux);

  } else if (strcmp(type, "commit") == 0) {
    int i = doc["i"] | -1;
    if (i < 0 || i >= NUM_DRUMS) return;
    settingsSaveDrum((uint8_t)i);
    sendAck("saved", i);

  } else if (strcmp(type, "hit") == 0) {
    int i = doc["i"] | -1;
    if (i < 0 || i >= NUM_DRUMS) return;
    if (testHitCb) testHitCb((uint8_t)i);

  } else if (strcmp(type, "setBrightness") == 0) {
    int v = constrain((int)(doc["v"] | (int)masterBrightness), 0, 255);
    portENTER_CRITICAL(&settingsMux);
    masterBrightness = (uint8_t)v;
    portEXIT_CRITICAL(&settingsMux);
    FastLED.setBrightness((uint8_t)v);

  } else if (strcmp(type, "commitBrightness") == 0) {
    settingsSaveBrightness();
    sendAck("brightSaved", -1);

  } else if (strcmp(type, "savePreset") == 0) {
    const char* name = doc["name"] | "";
    if (!name[0]) return;
    int slot = doc["slot"] | -1;
    if (slot < 0 || slot >= MAX_PRESETS) {
      slot = presetsFindFreeSlot();
      if (slot < 0) { sendAck("presetFull", -1); return; }
    }
    presetsSave((uint8_t)slot, name);
    broadcastPresets();

  } else if (strcmp(type, "loadPreset") == 0) {
    int slot = doc["slot"] | -1;
    if (slot < 0 || slot >= MAX_PRESETS) return;
    // Re-syncs every connected browser (all drums, one click) rather than
    // acking just the requester — that's the whole point of a preset.
    if (presetsApply((uint8_t)slot)) ws.textAll(buildHello());

  } else if (strcmp(type, "deletePreset") == 0) {
    int slot = doc["slot"] | -1;
    if (slot < 0 || slot >= MAX_PRESETS) return;
    presetsDelete((uint8_t)slot);
    broadcastPresets();

  } else if (strcmp(type, "loadTemplate") == 0) {
    int idx = doc["idx"] | -1;
    if (idx < 0 || idx >= NUM_PRESET_TEMPLATES) return;
    // Same as loadPreset: templates persist into live settings once applied
    // and every connected browser re-syncs, not just the requester.
    if (presetsApplyTemplate((uint8_t)idx)) ws.textAll(buildHello());

  } else if (strcmp(type, "setButtonTarget") == 0) {
    int index = doc["index"] | -1;
    if (index < 0 || index >= MAX_PRESET_BUTTONS) return;
    int kind = doc["kind"] | (int)BTN_TARGET_NONE;
    int id = doc["id"] | 0;
    if (kind < 0 || kind > BTN_TARGET_PRESET) return;
    if (buttonsSetMapping((uint8_t)index, (ButtonTargetKind)kind, (uint8_t)id)) {
      broadcastButtons();
    }

  } else if (strcmp(type, "setEnabled") == 0) {
    bool on = doc["v"] | true;
    settingsSetLightsEnabled(on);
    broadcastPower();
  }
}

static void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                       AwsEventType type, void* arg, uint8_t* data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    client->text(buildHello());
  } else if (type == WS_EVT_DATA) {
    AwsFrameInfo* info = (AwsFrameInfo*)arg;
    if (!info->final || info->index != 0 || info->len != len || info->opcode != WS_TEXT) return;
    JsonDocument doc;
    if (deserializeJson(doc, data, len)) return;
    handleClientMessage(doc);
  }
}

void webUiInit(TestHitCallback onTestHit) {
  testHitCb = onTestHit;

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount failed");
  }

  WiFi.mode(WIFI_AP);
  WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD);
  IPAddress apIP = WiFi.softAPIP();
  Serial.print("DrumLight AP up, connect and open http://");
  Serial.println(apIP);

  // Captive portal: answer every DNS query with our own IP so phones/laptops
  // auto-prompt "Sign in to network" straight to the control page.
  dnsServer.start(53, "*", apIP);

  MDNS.begin("drumlight");  // also reachable at http://drumlight.local

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
  server.onNotFound([](AsyncWebServerRequest* request) {
    request->send(LittleFS, "/index.html", "text/html");
  });
  server.begin();
}

void webUiLoop(unsigned long now) {
  dnsServer.processNextRequest();
  ws.cleanupClients();
  if (now - lastTelemetryMs >= TELEMETRY_INTERVAL_MS) {
    lastTelemetryMs = now;
    broadcastTelemetry();
  }
}

void webUiSetEnvelope(uint8_t i, int envelope) {
  if (i < NUM_DRUMS) envelopeValues[i] = envelope;
}

void webUiNotifyHit(uint8_t i) {
  if (i < NUM_DRUMS) hitSeqArr[i]++;
}

void webUiNotifyStateChanged() {
  ws.textAll(buildHello());
}
