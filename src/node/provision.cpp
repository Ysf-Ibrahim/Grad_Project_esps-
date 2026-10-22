#include "provision.h"

#include <cstdlib>
#include <cstring>

#include "discovery.h"
#include "log.h"
#include "mic.h"
#include "version.h"
#include "wifi_link.h"

namespace {

ProvisionDeps g_deps;
char g_lineUsb[160];
char g_lineUart[160];
size_t g_lenUsb = 0;
size_t g_lenUart = 0;

bool busy() { return *g_deps.state != NodeState::Idle; }

void replyShow() {
  const NodeConfig& s = *g_deps.staged;
  logf("OK id=%s name=%s ssid=%s pass=**** provisioned=%d ip=%s fw=%s\r\n", s.id, s.name, s.ssid,
       g_deps.running->provisioned ? 1 : 0, wifiConnected() ? wifiIp().toString().c_str() : "-",
       ACOUSTISCAN_FW_VERSION);
}

void handleSet(char* kv) {
  char* eq = kv ? strchr(kv, '=') : nullptr;
  if (!eq) { logWrite("ERR usage: SET key=value\r\n"); return; }
  *eq = '\0';
  const char* key = kv;
  const char* val = eq + 1;
  NodeConfig& s = *g_deps.staged;
  if (strcasecmp(key, "id") == 0) {
    if (!configValidId(val)) { logWrite("ERR id must be 1-15 chars of A-Z a-z 0-9 - _\r\n"); return; }
    strlcpy(s.id, val, sizeof(s.id));
    logf("OK id=%s\r\n", s.id);
  } else if (strcasecmp(key, "name") == 0) {
    if (!configValidName(val)) { logWrite("ERR name must be up to 31 printable chars without \" or \\\r\n"); return; }
    strlcpy(s.name, val, sizeof(s.name));
    logf("OK name=%s\r\n", s.name);
  } else if (strcasecmp(key, "ssid") == 0) {
    if (strlen(val) < 1 || strlen(val) > 32) { logWrite("ERR ssid must be 1-32 chars\r\n"); return; }
    strlcpy(s.ssid, val, sizeof(s.ssid));
    logf("OK ssid=%s\r\n", s.ssid);
  } else if (strcasecmp(key, "pass") == 0) {
    if (strlen(val) > 63) { logWrite("ERR pass must be up to 63 chars\r\n"); return; }
    strlcpy(s.pass, val, sizeof(s.pass));
    logWrite("OK pass=****\r\n");
  } else {
    logWrite("ERR unknown key (id, name, ssid, pass)\r\n");
  }
}

void handleLine(char* line) {
  // trim
  size_t n = strlen(line);
  while (n && (line[n - 1] == '\r' || line[n - 1] == '\n' || line[n - 1] == ' ')) line[--n] = '\0';
  while (*line == ' ') ++line;
  if (*line == '\0') return;

  char* arg = strchr(line, ' ');
  if (arg) { *arg = '\0'; ++arg; while (*arg == ' ') ++arg; }
  const char* cmd = line;

  if (strcasecmp(cmd, "HELP") == 0) {
    logWrite("OK commands: SET id=|name=|ssid=|pass=  SHOW  SAVE  REBOOT  FACTORY  STATUS  PROBE  SCAN  LED r g b\r\n");
  } else if (strcasecmp(cmd, "SHOW") == 0) {
    replyShow();
  } else if (strcasecmp(cmd, "STATUS") == 0) {
    char json[400];
    NodeStatus st = {g_deps.running, *g_deps.state, *g_deps.micAlive};
    statusJson(json, sizeof(json), st);
    logf("OK %s\r\n", json);
  } else if (busy()) {
    logWrite("ERR busy\r\n");
  } else if (strcasecmp(cmd, "SET") == 0) {
    handleSet(arg);
  } else if (strcasecmp(cmd, "SAVE") == 0) {
    logWrite(configSave(*g_deps.staged) ? "OK saved\r\n" : "ERR save failed\r\n");
  } else if (strcasecmp(cmd, "REBOOT") == 0) {
    logWrite("OK rebooting\r\n");
    delay(150);
    ESP.restart();
  } else if (strcasecmp(cmd, "FACTORY") == 0) {
    configFactory();
    logWrite("OK factory\r\n");
    delay(150);
    ESP.restart();
  } else if (strcasecmp(cmd, "PROBE") == 0) {
    const MicHealth h = micProbe(200000);
    logf("OK clk=%lu data=%lu data_high=%lu alive=%d\r\n", static_cast<unsigned long>(h.clkTransitions),
         static_cast<unsigned long>(h.dataTransitions), static_cast<unsigned long>(h.dataHighPct), h.alive ? 1 : 0);
  } else if (strcasecmp(cmd, "SCAN") == 0) {
    wifiScanPrint();
  } else if (strcasecmp(cmd, "LED") == 0) {
    int r = 0, g = 0, b = 0;
    if (!arg || sscanf(arg, "%d %d %d", &r, &g, &b) != 3) { logWrite("ERR usage: LED r g b\r\n"); return; }
    g_deps.setLedOverride(true);
    g_deps.led(static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b));
    logWrite("OK led\r\n");
  } else {
    logWrite("ERR unknown command (HELP)\r\n");
  }
}

void feed(Stream& port, char* buf, size_t& len) {
  while (port.available()) {
    const char c = static_cast<char>(port.read());
    if (c == '\n') {
      buf[len] = '\0';
      len = 0;
      handleLine(buf);
    } else if (len < 159) {
      buf[len++] = c;
    }
  }
}

}  // namespace

void provisionBegin(const ProvisionDeps& deps) {
  g_deps = deps;
  g_lenUsb = g_lenUart = 0;
}

void provisionTick() {
  feed(Serial, g_lineUsb, g_lenUsb);
  feed(Serial0, g_lineUart, g_lenUart);
}
