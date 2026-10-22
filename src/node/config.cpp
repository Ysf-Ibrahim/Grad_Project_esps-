#include "config.h"

#include <Arduino.h>
#include <Preferences.h>
#include <esp_system.h>

#include <cctype>
#include <cstdio>
#include <cstring>

#include "wifi_credentials.h"   // include/ is on the include path (compiled-in fallback)

namespace {
const char* kNamespace = "acoustiscan";
}

bool configValidId(const char* id) {
  const size_t n = strlen(id);
  if (n < 1 || n > 15) return false;
  for (size_t i = 0; i < n; ++i) {
    const unsigned char c = static_cast<unsigned char>(id[i]);
    if (!(isalnum(c) || c == '-' || c == '_')) return false;
  }
  return true;
}

bool configValidName(const char* name) {
  const size_t n = strlen(name);
  if (n > 31) return false;
  for (size_t i = 0; i < n; ++i) {
    const unsigned char c = static_cast<unsigned char>(name[i]);
    if (c < 32 || c > 126 || c == '"' || c == '\\') return false;
  }
  return true;
}

void configDefaultId(char* out, size_t n) {
  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  snprintf(out, n, "ESP-%02X%02X", mac[4], mac[5]);
}

bool configLoad(NodeConfig& cfg) {
  memset(&cfg, 0, sizeof(cfg));
  Preferences prefs;
  const bool opened = prefs.begin(kNamespace, true);   // read-only; false if never created
  if (opened) {
    prefs.getString("id",   cfg.id,   sizeof(cfg.id));
    prefs.getString("name", cfg.name, sizeof(cfg.name));
    prefs.getString("ssid", cfg.ssid, sizeof(cfg.ssid));
    prefs.getString("pass", cfg.pass, sizeof(cfg.pass));
    prefs.end();
  }
  cfg.provisioned = cfg.id[0] != '\0';
  if (!cfg.provisioned) configDefaultId(cfg.id, sizeof(cfg.id));
  if (cfg.ssid[0] == '\0') {
    strlcpy(cfg.ssid, WIFI_SSID, sizeof(cfg.ssid));
    strlcpy(cfg.pass, WIFI_PASSWORD, sizeof(cfg.pass));
    cfg.credsFromHeader = true;
  }
  return opened;
}

bool configSave(const NodeConfig& cfg) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) return false;
  bool ok = true;
  ok &= prefs.putString("id",   cfg.id)   > 0;
  ok &= prefs.putString("name", cfg.name) == strlen(cfg.name);
  ok &= prefs.putString("ssid", cfg.ssid) > 0;
  ok &= prefs.putString("pass", cfg.pass) == strlen(cfg.pass);
  prefs.end();
  return ok;
}

void configFactory() {
  Preferences prefs;
  if (prefs.begin(kNamespace, false)) {
    prefs.clear();
    prefs.end();
  }
}
