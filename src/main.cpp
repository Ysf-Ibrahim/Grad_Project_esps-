/*
 * ESP32-S3-WROOM-1 N16R8  ->  Wi-Fi station  ->  phone hotspot
 *
 * Scope of this firmware: prove that the board joins the hotspot and gets an
 * IP address. Nothing else lives here yet (no PDM microphone).
 *
 * Serial output is mirrored to BOTH USB-C connectors of the DevKitC-1, so the
 * messages appear whichever connector the cable is plugged into:
 *   "USB"  connector -> native USB-Serial/JTAG  -> Serial   (ARDUINO_USB_CDC_ON_BOOT=1)
 *   "UART" connector -> CP2102N bridge on UART0 -> Serial0
 */

#include <Arduino.h>
#include <WiFi.h>

#include <cstdarg>
#include <cstring>

#include "wifi_credentials.h"

namespace {

constexpr uint32_t kSerialBaud       = 115200;
constexpr uint32_t kConnectTimeoutMs = 20000;  // give up on one attempt after this
constexpr uint32_t kProgressDotMs    = 500;    // print "." this often while connecting
constexpr uint32_t kStatusIntervalMs = 5000;   // periodic report while connected
constexpr uint32_t kRetryPauseMs     = 1000;   // pause between failed attempts

// ------------------------------------------------------------------ logging --

void logWrite(const char* text) {
  Serial.print(text);
  Serial0.print(text);
}

void logf(const char* fmt, ...) {
  char buf[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  logWrite(buf);
}

// -------------------------------------------------------------- Wi-Fi state --

uint32_t g_attempt      = 0;
uint32_t g_attemptStart = 0;
uint32_t g_lastDot      = 0;
uint32_t g_lastStatus   = 0;
bool     g_wasConnected = false;
volatile uint8_t g_lastDisconnectReason = 0;

const char* statusText(wl_status_t s) {
  switch (s) {
    case WL_IDLE_STATUS:     return "idle";
    case WL_NO_SSID_AVAIL:   return "SSID not found";
    case WL_SCAN_COMPLETED:  return "scan completed";
    case WL_CONNECTED:       return "connected";
    case WL_CONNECT_FAILED:  return "connect failed";
    case WL_CONNECTION_LOST: return "connection lost";
    case WL_DISCONNECTED:    return "disconnected";
    default:                 return "unknown";
  }
}

// esp_wifi_types.h WIFI_REASON_* codes, with plain-language hints.
const char* reasonText(uint8_t reason) {
  switch (reason) {
    case 0:   return "none";
    case 2:   return "AUTH_EXPIRE";
    case 3:   return "AUTH_LEAVE";
    case 4:   return "ASSOC_EXPIRE";
    case 8:   return "ASSOC_LEAVE (AP dropped us / hotspot turned off)";
    case 15:  return "4WAY_HANDSHAKE_TIMEOUT (usually wrong password)";
    case 200: return "BEACON_TIMEOUT (signal lost)";
    case 201: return "NO_AP_FOUND (SSID not visible - is the hotspot 2.4 GHz?)";
    case 202: return "AUTH_FAIL (wrong password)";
    case 203: return "ASSOC_FAIL";
    case 204: return "HANDSHAKE_TIMEOUT (usually wrong password)";
    case 205: return "CONNECTION_FAIL";
    default:  return "see esp_wifi_types.h";
  }
}

const char* rssiQuality(int rssi) {
  if (rssi >= -50) return "excellent";
  if (rssi >= -60) return "good";
  if (rssi >= -70) return "fair";
  return "weak";
}

const char* flashModeText() {
  switch (ESP.getFlashChipMode()) {
    case FM_QIO:  return "QIO";
    case FM_QOUT: return "QOUT";
    case FM_DIO:  return "DIO";
    case FM_DOUT: return "DOUT";
    default:      return "?";
  }
}

void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    g_lastDisconnectReason = info.wifi_sta_disconnected.reason;
  }
}

const char* authText(wifi_auth_mode_t m) {
  switch (m) {
    case WIFI_AUTH_OPEN:            return "open";
    case WIFI_AUTH_WEP:             return "WEP";
    case WIFI_AUTH_WPA_PSK:         return "WPA";
    case WIFI_AUTH_WPA2_PSK:        return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:    return "WPA/WPA2";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-Enterprise";
    case WIFI_AUTH_WPA3_PSK:        return "WPA3-only";
    case WIFI_AUTH_WPA2_WPA3_PSK:   return "WPA2/WPA3";
    case WIFI_AUTH_WAPI_PSK:        return "WAPI";
    default:                        return "?";
  }
}

// One-off diagnostic: list every network the radio can see, so the exact SSID
// spelling / band / hidden status of the hotspot can be checked.
void printScan() {
  logWrite("\r\n[WiFi] Scanning for networks...\r\n");
  const int16_t n = WiFi.scanNetworks(false, true);  // blocking, include hidden
  if (n <= 0) {
    logf("[WiFi] Scan found nothing (result %d)\r\n", static_cast<int>(n));
    WiFi.scanDelete();
    return;
  }
  logf("[WiFi] %d network(s) visible:\r\n", static_cast<int>(n));
  for (int16_t i = 0; i < n; ++i) {
    const String ssid = WiFi.SSID(i);
    const bool match = (ssid == WIFI_SSID);
    logf("  %2d. \"%s\"  ch=%2d  %4d dBm  %s%s\r\n", static_cast<int>(i + 1),
         ssid.length() ? ssid.c_str() : "<hidden>", WiFi.channel(i), WiFi.RSSI(i),
         authText(WiFi.encryptionType(i)),
         match ? "   <== TARGET" : "");
    bool nonAscii = false;
    for (size_t k = 0; k < ssid.length(); ++k) {
      if (static_cast<uint8_t>(ssid[k]) > 0x7E) { nonAscii = true; break; }
    }
    if (nonAscii) {
      logWrite("      bytes:");
      for (size_t k = 0; k < ssid.length(); ++k) {
        logf(" %02X", static_cast<uint8_t>(ssid[k]));
      }
      logWrite("\r\n");
    }
  }
  WiFi.scanDelete();
}

void startConnectAttempt() {
  ++g_attempt;
  g_attemptStart = millis();
  g_lastDot = g_attemptStart;
  logf("\r\n[WiFi] Connecting to \"%s\" (attempt %lu) ", WIFI_SSID,
       static_cast<unsigned long>(g_attempt));
  WiFi.disconnect(false, false);  // drop any half-open association, keep radio on
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

// ---------------------------------------------------------------- reporting --

void printBanner() {
  logWrite("\r\n\r\n============================================\r\n");
  logWrite(" ESP32-S3  Wi-Fi hotspot connection test\r\n");
  logWrite("============================================\r\n");
  logf(" Chip      : %s rev %d, %d core(s) @ %lu MHz\r\n", ESP.getChipModel(),
       ESP.getChipRevision(), ESP.getChipCores(),
       static_cast<unsigned long>(ESP.getCpuFreqMHz()));
  logf(" Flash     : %lu MB, %s mode\r\n",
       static_cast<unsigned long>(ESP.getFlashChipSize() / (1024UL * 1024UL)),
       flashModeText());
  logf(" PSRAM     : %s, %lu KB total, %lu KB free\r\n",
       psramFound() ? "found" : "NOT FOUND",
       static_cast<unsigned long>(ESP.getPsramSize() / 1024UL),
       static_cast<unsigned long>(ESP.getFreePsram() / 1024UL));
  logf(" SDK       : %s\r\n", ESP.getSdkVersion());
  logf(" STA MAC   : %s\r\n", WiFi.macAddress().c_str());
  logf(" Target AP : \"%s\"\r\n", WIFI_SSID);

  if (strcmp(WIFI_SSID, "PUT_MY_HOTSPOT_NAME_HERE") == 0 ||
      strcmp(WIFI_PASSWORD, "PUT_MY_PASSWORD_HERE") == 0) {
    logWrite(" !! WARNING: include/wifi_credentials.h still has placeholder values.\r\n");
    logWrite(" !!          Put the real hotspot SSID/password there, rebuild, upload.\r\n");
  }
}

void printConnected() {
  logWrite("\r\n\r\nCONNECTED!\r\n");
  logf(" SSID      : %s\r\n", WiFi.SSID().c_str());
  logf(" BSSID     : %s  (channel %d)\r\n", WiFi.BSSIDstr().c_str(), WiFi.channel());
  logf(" IP        : %s\r\n", WiFi.localIP().toString().c_str());
  logf(" Gateway   : %s\r\n", WiFi.gatewayIP().toString().c_str());
  logf(" Subnet    : %s\r\n", WiFi.subnetMask().toString().c_str());
  logf(" DNS       : %s\r\n", WiFi.dnsIP().toString().c_str());
  const int rssi = WiFi.RSSI();
  logf(" RSSI      : %d dBm (%s)\r\n", rssi, rssiQuality(rssi));
  logf(" Attempts  : %lu\r\n", static_cast<unsigned long>(g_attempt));
}

void printStatusLine() {
  const int rssi = WiFi.RSSI();
  logf("[WiFi] connected  IP=%s  RSSI=%d dBm (%s)  uptime=%lus\r\n",
       WiFi.localIP().toString().c_str(), rssi, rssiQuality(rssi),
       static_cast<unsigned long>(millis() / 1000UL));
}

}  // namespace

// ----------------------------------------------------------------- Arduino --

void setup() {
  Serial.begin(kSerialBaud);    // native USB CDC (baud is ignored, kept for clarity)
  Serial.setTxTimeoutMs(20);    // never stall if no terminal is open on the USB port
  Serial0.begin(kSerialBaud);   // UART0 -> CP2102N -> "UART" connector
  delay(1500);                  // short pause so a freshly opened terminal sees the banner

  WiFi.persistent(false);       // do not rewrite credentials to flash on every boot
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);  // core-level reconnect, backed up by loop() below
  WiFi.setHostname("esp32s3-wifi-test");
  WiFi.onEvent(onWiFiEvent);

  printBanner();
  printScan();
  startConnectAttempt();
}

void loop() {
  const uint32_t now = millis();
  const wl_status_t status = WiFi.status();

  if (status == WL_CONNECTED) {
    if (!g_wasConnected) {
      g_wasConnected = true;
      g_lastStatus = now;
      printConnected();
    } else if (now - g_lastStatus >= kStatusIntervalMs) {
      g_lastStatus = now;
      printStatusLine();
    }
    delay(10);
    return;
  }

  // ---- not connected ----
  if (g_wasConnected) {
    g_wasConnected = false;
    logf("\r\n[WiFi] DISCONNECTED (reason %u: %s) - reconnecting\r\n",
         g_lastDisconnectReason, reasonText(g_lastDisconnectReason));
    startConnectAttempt();
    return;
  }

  if (now - g_lastDot >= kProgressDotMs) {
    g_lastDot = now;
    logWrite(".");
  }

  if (now - g_attemptStart >= kConnectTimeoutMs) {
    logf("\r\n[WiFi] Attempt %lu failed after %lu s  (status: %s, last reason %u: %s)\r\n",
         static_cast<unsigned long>(g_attempt),
         static_cast<unsigned long>(kConnectTimeoutMs / 1000UL), statusText(status),
         g_lastDisconnectReason, reasonText(g_lastDisconnectReason));
    delay(kRetryPauseMs);
    startConnectAttempt();
  }

  delay(10);
}
