#include "wifi_link.h"

#include <WiFi.h>

#include "log.h"

namespace {

constexpr uint32_t kConnectTimeoutMs = 20000;
constexpr uint32_t kRetryPauseMs     = 1000;

const NodeConfig* g_cfg          = nullptr;
uint32_t          g_attempt      = 0;
uint32_t          g_attemptStart = 0;
uint32_t          g_pauseUntil   = 0;
bool              g_wasConnected = false;
volatile uint8_t  g_lastReason   = 0;

const char* reasonText(uint8_t reason) {
  switch (reason) {
    case 0:   return "none";
    case 8:   return "AP dropped us / hotspot off";
    case 15:  return "handshake timeout (wrong password?)";
    case 200: return "beacon timeout (signal lost)";
    case 201: return "SSID not found (hotspot off or 5 GHz?)";
    case 202: return "auth fail (wrong password)";
    case 204: return "handshake timeout (wrong password?)";
    default:  return "see esp_wifi_types.h";
  }
}

void onEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    g_lastReason = info.wifi_sta_disconnected.reason;
  }
}

void startAttempt() {
  ++g_attempt;
  g_attemptStart = millis();
  logf("WIFI connecting \"%s\" attempt=%lu\r\n", g_cfg->ssid, static_cast<unsigned long>(g_attempt));
  WiFi.disconnect(false, false);
  WiFi.begin(g_cfg->ssid, g_cfg->pass);
}

}  // namespace

void wifiBegin(const NodeConfig& cfg) {
  g_cfg = &cfg;
  WiFi.persistent(false);
  WiFi.setHostname(cfg.id);      // must precede mode(): applied on the STA-start event
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);          // USB powered: keep UDP/TCP latency low
  WiFi.setAutoReconnect(true);
  WiFi.onEvent(onEvent);
  startAttempt();
}

void wifiTick() {
  const uint32_t now = millis();
  if (WiFi.status() == WL_CONNECTED) {
    if (!g_wasConnected) {
      g_wasConnected = true;
      logf("WIFI ip=%s rssi=%d ssid=%s\r\n", WiFi.localIP().toString().c_str(), WiFi.RSSI(),
           WiFi.SSID().c_str());
    }
    return;
  }
  if (g_wasConnected) {
    g_wasConnected = false;
    logf("WIFI disconnected reason=%u (%s)\r\n", g_lastReason, reasonText(g_lastReason));
    startAttempt();
    return;
  }
  if (g_pauseUntil && now < g_pauseUntil) return;
  if (now - g_attemptStart >= kConnectTimeoutMs) {
    logf("WIFI attempt %lu failed (reason=%u: %s)\r\n", static_cast<unsigned long>(g_attempt),
         g_lastReason, reasonText(g_lastReason));
    g_pauseUntil = now + kRetryPauseMs;
    startAttempt();
  }
}

bool      wifiConnected() { return WiFi.status() == WL_CONNECTED; }
int       wifiRssi()      { return wifiConnected() ? WiFi.RSSI() : 0; }
IPAddress wifiIp()        { return WiFi.localIP(); }
String    wifiMac()       { return WiFi.macAddress(); }

void wifiScanPrint() {
  const int16_t n = WiFi.scanNetworks(false, true);
  if (n <= 0) {
    logf("OK 0 networks (scan result %d)\r\n", static_cast<int>(n));
    WiFi.scanDelete();
    return;
  }
  logf("OK %d networks\r\n", static_cast<int>(n));
  for (int16_t i = 0; i < n; ++i) {
    const String ssid = WiFi.SSID(i);
    logf("  \"%s\" ch=%d %d dBm %s\r\n", ssid.length() ? ssid.c_str() : "<hidden>", WiFi.channel(i),
         WiFi.RSSI(i), WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "open" : "secured");
  }
  WiFi.scanDelete();
}
