#include "discovery.h"

#include <WiFi.h>
#include <WiFiUdp.h>

#include <cstring>

#include "version.h"
#include "wifi_link.h"

namespace {

constexpr uint16_t kPort           = 5005;
constexpr uint32_t kHeartbeatMs    = 2000;
constexpr uint32_t kBeaconStaleMs  = 10000;
const char*        kBeaconMagic    = "ACOUSTISCAN-SERVER";

WiFiUDP   g_udp;
bool      g_begun      = false;
bool      g_haveServer = false;
IPAddress g_server;
uint32_t  g_lastBeacon = 0;
uint32_t  g_lastSent   = 0;

void sendHeartbeat(const NodeStatus& st, const IPAddress& to) {
  char json[400];
  const size_t n = statusJson(json, sizeof(json), st);
  g_udp.beginPacket(to, kPort);
  g_udp.write(reinterpret_cast<const uint8_t*>(json), n);
  g_udp.endPacket();
}

}  // namespace

size_t statusJson(char* out, size_t n, const NodeStatus& st) {
  const int len = snprintf(out, n,
      "{\"id\":\"%s\",\"name\":\"%s\",\"ip\":\"%s\",\"port\":5010,\"mac\":\"%s\",\"rssi\":%d,"
      "\"uptime_s\":%lu,\"mic\":\"%s\",\"state\":\"%s\",\"fw\":\"%s\",\"heap\":%lu,\"psram\":%lu,"
      "\"provisioned\":%d}",
      st.cfg->id, st.cfg->name, wifiIp().toString().c_str(), wifiMac().c_str(), wifiRssi(),
      static_cast<unsigned long>(millis() / 1000UL), st.micAlive ? "alive" : "flat",
      stateName(st.state), ACOUSTISCAN_FW_VERSION,
      static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getFreePsram()),
      st.cfg->provisioned ? 1 : 0);
  return len < 0 ? 0 : (static_cast<size_t>(len) >= n ? n - 1 : static_cast<size_t>(len));
}

void discoveryBegin() {
  g_begun = g_udp.begin(kPort) != 0;
}

void discoveryTick(const NodeStatus& st) {
  if (!g_begun || !wifiConnected()) return;
  const uint32_t now = millis();

  int packetLen;
  while ((packetLen = g_udp.parsePacket()) > 0) {
    char buf[64];
    const int n = g_udp.read(buf, sizeof(buf) - 1);
    buf[n > 0 ? n : 0] = '\0';
    g_udp.flush();
    if (strncmp(buf, kBeaconMagic, strlen(kBeaconMagic)) == 0) {
      g_server = g_udp.remoteIP();
      g_haveServer = true;
      g_lastBeacon = now;
      sendHeartbeat(st, g_server);
      g_lastSent = now;
    }
  }

  if (now - g_lastSent >= kHeartbeatMs) {
    g_lastSent = now;
    if (g_haveServer && now - g_lastBeacon < kBeaconStaleMs) {
      sendHeartbeat(st, g_server);
    } else {
      sendHeartbeat(st, IPAddress(255, 255, 255, 255));
    }
  }
}
