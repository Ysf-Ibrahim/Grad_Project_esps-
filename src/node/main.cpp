/*
 * AcoustiScan node firmware  -  ESP32-S3-WROOM-1 N16R8 + SPH0641 PDM microphone
 *
 * One node = one board + one mic. Provisioned once over USB (ID, name, Wi-Fi
 * credentials in NVS), then runs from any 5 V supply:
 *   - joins the Wi-Fi network and reconnects on its own
 *   - answers the laptop's UDP beacons with a heartbeat (UDP 5005)
 *   - records on command and streams the PCM back (TCP 5010)
 *   - RGB LED shows the state
 * Protocol: PROTOCOL.md. Single superloop, no tasks, no locks.
 */

#include <Arduino.h>

#include "command_server.h"
#include "config.h"
#include "discovery.h"
#include "log.h"
#include "mic.h"
#include "provision.h"
#include "state.h"
#include "version.h"
#include "wifi_link.h"

namespace {

constexpr uint32_t kSerialBaud    = 115200;
constexpr int      kPinRgb        = 48;      // WS2812 on the DevKitC-1 (some clones: none / GPIO38)
constexpr uint8_t  kLedLevel      = 32;      // brightness cap
constexpr uint32_t kHealthEveryMs = 2000;
constexpr uint32_t kLedEveryMs    = 100;

NodeConfig g_cfg;
NodeConfig g_staged;
NodeState  g_state       = NodeState::Idle;
bool       g_micAlive    = false;
uint32_t   g_lastHealth  = 0;
uint32_t   g_lastLed     = 0;
bool       g_ledOverride = false;
bool       g_blink       = false;

void ledSet(uint8_t r, uint8_t g, uint8_t b) { neopixelWrite(kPinRgb, r, g, b); }
void ledOverride(bool on) { g_ledOverride = on; }

void ledTick() {
  const uint32_t now = millis();
  if (now - g_lastLed < kLedEveryMs) return;
  g_lastLed = now;
  if (g_ledOverride) return;
  g_blink = !g_blink;
  switch (g_state) {
    case NodeState::Recording: ledSet(g_blink ? kLedLevel : 0, g_blink ? kLedLevel : 0, g_blink ? kLedLevel : 0); return;
    case NodeState::Sending:   ledSet(0, kLedLevel, kLedLevel); return;
    default: break;
  }
  if (!wifiConnected())      { ledSet(0, 0, kLedLevel); return; }              // blue: connecting
  if (!micOk() || !g_micAlive) { ledSet(kLedLevel, 0, 0); return; }            // red: mic problem
  if (!g_cfg.provisioned)    { ledSet(kLedLevel, kLedLevel * 3 / 4, 0); return; } // yellow: unprovisioned
  ledSet(0, kLedLevel, 0);                                                      // green: online
}

// Keeps everything else alive; safe to call from inside a capture (every ~32 ms).
void tick() {
  provisionTick();
  wifiTick();
  const uint32_t now = millis();
  if (g_state == NodeState::Idle && now - g_lastHealth >= kHealthEveryMs) {
    g_lastHealth = now;
    g_micAlive = micProbe(20000).alive;
  }
  NodeStatus st = {&g_cfg, g_state, g_micAlive};
  discoveryTick(st);
  ledTick();
  if (g_state != NodeState::Idle) commandServerRejectNew();
}

}  // namespace

void setup() {
  Serial.begin(kSerialBaud);
  Serial.setTxTimeoutMs(20);
  Serial0.begin(kSerialBaud);
  delay(1200);

  configLoad(g_cfg);
  g_staged = g_cfg;

  logWrite("\r\n\r\n============================================\r\n");
  logf(" AcoustiScan node  fw %s\r\n", ACOUSTISCAN_FW_VERSION);
  logWrite("============================================\r\n");
  logf(" Id        : %s%s\r\n", g_cfg.id, g_cfg.provisioned ? "" : "  (unprovisioned, MAC-derived)");
  logf(" Name      : %s\r\n", g_cfg.name[0] ? g_cfg.name : "-");
  logf(" Network   : %s%s\r\n", g_cfg.ssid, g_cfg.credsFromHeader ? "  (compiled-in fallback)" : "  (from flash)");
  logf(" Chip      : %s rev %d, PSRAM %s %lu KB free\r\n", ESP.getChipModel(), ESP.getChipRevision(),
       psramFound() ? "found" : "NOT FOUND", static_cast<unsigned long>(ESP.getFreePsram() / 1024UL));

  ledSet(0, 0, kLedLevel);
  if (!micInit()) {
    logf("MIC INIT FAILED: %s\r\n", micFailReason());
  } else {
    logf("MIC INIT OK (%s)\r\n", micRoutingName());
  }
  g_micAlive = micProbe(20000).alive;

  ProvisionDeps pd = {&g_cfg, &g_staged, &g_state, &g_micAlive, ledSet, ledOverride};
  provisionBegin(pd);

  wifiBegin(g_cfg);
  discoveryBegin();
  commandServerBegin();
  logWrite("PROVISION READY\r\n");
}

void loop() {
  tick();
  CommandDeps cd = {&g_cfg, &g_state, &g_micAlive, tick};
  commandServerTick(cd);
  delay(2);
}
