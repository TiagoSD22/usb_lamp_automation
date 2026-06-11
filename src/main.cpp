#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <esp_system.h>
#include <SinricPro.h>
#include <SinricProSwitch.h>

#include "config.h"

enum LampType { BUTTON, USB_SWITCH };

struct Lamp {
  const char* name;
  LampType    type;
  uint8_t     gpio;
};

const Lamp LAMPS[] = {
  { "color_cube",    BUTTON,     25 },
  { "portrait_1",    BUTTON,     26 },
  { "portrait_2",    BUTTON,     27 },
  { "sunset",        USB_SWITCH, 32 },
  { "drawing_board", USB_SWITCH, 33 },
};
const int LAMP_COUNT = sizeof(LAMPS) / sizeof(LAMPS[0]);

constexpr uint16_t PRESS_HOLD_MS = 250;
constexpr uint16_t PRESS_GAP_MS  = 200;

constexpr const char* NVS_NAMESPACE = "lamps";
constexpr const char* NVS_KEY_ON    = "on";

Preferences prefs;
bool allOn = false;

void loadState() {
  prefs.begin(NVS_NAMESPACE, true);
  allOn = prefs.getBool(NVS_KEY_ON, false);
  prefs.end();
}

void saveState() {
  prefs.begin(NVS_NAMESPACE, false);
  prefs.putBool(NVS_KEY_ON, allOn);
  prefs.end();
}

void clearState() {
  prefs.begin(NVS_NAMESPACE, false);
  prefs.clear();
  prefs.end();
  allOn = false;
}

bool isColdStart() {
  esp_reset_reason_t reason = esp_reset_reason();
  return reason == ESP_RST_POWERON || reason == ESP_RST_BROWNOUT;
}

void initGpios() {
  for (const Lamp& l : LAMPS) {
    digitalWrite(l.gpio, LOW);   // latch low before the driver is enabled
    pinMode(l.gpio, OUTPUT);
    digitalWrite(l.gpio, LOW);
  }
}

void pressButton(const Lamp& l) {
  digitalWrite(l.gpio, HIGH);
  delay(PRESS_HOLD_MS);
  digitalWrite(l.gpio, LOW);
  delay(PRESS_GAP_MS);
}

void setSwitch(const Lamp& l, bool on) {
  digitalWrite(l.gpio, on ? HIGH : LOW);
}

void applyLamp(const Lamp& l, bool on) {
  if (l.type == BUTTON) {
    pressButton(l);            // toggles whatever its current state is
  } else {
    setSwitch(l, on);
  }
}

// Idempotent: Group A buttons are toggles, so re-applying the current state
// would flip them off. Returns true only when the state actually changed.
bool setAllLamps(bool on) {
  if (on == allOn) return false;
  for (const Lamp& l : LAMPS) {
    applyLamp(l, on);
  }
  allOn = on;
  saveState();
  return true;
}

// After a soft reset the MOSFET gates dropped LOW; re-energize Group B to match
// the persisted "on" state. Group A kept its state (its power was never cut).
void reapplySwitches() {
  for (const Lamp& l : LAMPS) {
    if (l.type == USB_SWITCH) {
      setSwitch(l, true);
    }
  }
}

bool onPowerState(const String& deviceId, bool& state) {
  Serial.printf("[sinric] setPowerState %s\n", state ? "on" : "off");
  setAllLamps(state);
  return true;
}

void setupWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("[wifi] connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");
  }
  Serial.printf("\n[wifi] connected ip=%s\n", WiFi.localIP().toString().c_str());
}

void setupSinric() {
  SinricProSwitch& device = SinricPro[SINRIC_DEVICE_ID];
  device.onPowerState(onPowerState);
  SinricPro.onConnected([]() { Serial.println("[sinric] connected"); });
  SinricPro.onDisconnected([]() { Serial.println("[sinric] disconnected"); });
  SinricPro.begin(SINRIC_APP_KEY, SINRIC_APP_SECRET);
}

void printStatus() {
  Serial.printf("[state] all_on=%s wifi=%s sinric=%s\n",
                allOn ? "true" : "false",
                WiFi.status() == WL_CONNECTED ? "connected" : "down",
                SinricPro.isConnected() ? "connected" : "down");
}

void dispatch(const String& cmd) {
  if (cmd == "on") {
    setAllLamps(true);
  } else if (cmd == "off") {
    setAllLamps(false);
  } else if (cmd == "state") {
    printStatus();
  } else if (cmd == "reset") {
    clearState();
    Serial.println("[state] cleared");
  } else if (cmd.startsWith("on_") || cmd.startsWith("off_")) {
    bool on = cmd.startsWith("on_");
    int index = cmd.substring(cmd.indexOf('_') + 1).toInt();
    if (index >= 1 && index <= LAMP_COUNT) {
      applyLamp(LAMPS[index - 1], on);
    } else {
      Serial.printf("[serial] no lamp %d\n", index);
    }
  } else {
    Serial.printf("[serial] unknown command: %s\n", cmd.c_str());
  }
}

void handleSerial() {
  static String line;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (line.length() > 0) {
        dispatch(line);
        line = "";
      }
    } else {
      line += c;
    }
  }
}

void setup() {
  initGpios();
  Serial.begin(115200);
  delay(100);

  loadState();
  bool coldStart = isColdStart();
  if (coldStart) {
    allOn = false;
    saveState();
  }

  Serial.println("======================================");
  Serial.println(" Home Lamps Automation");
  Serial.printf(" boot=%s all_on=%s\n",
                coldStart ? "power-loss" : "soft", allOn ? "true" : "false");
  Serial.println("======================================");

  if (!coldStart && allOn) {
    reapplySwitches();
  }

  setupWiFi();
  setupSinric();
}

void loop() {
  SinricPro.handle();
  handleSerial();
}
