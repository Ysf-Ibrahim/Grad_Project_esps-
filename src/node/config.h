#pragma once
// Node identity and Wi-Fi credentials, stored in NVS (namespace "acoustiscan").
#include <cstddef>

struct NodeConfig {
  char id[16];      // "ESP-1" ... hostname + capture folder name
  char name[32];    // display name, e.g. "East wall"
  char ssid[33];
  char pass[64];
  bool provisioned;       // true when an id was found in NVS
  bool credsFromHeader;   // true when ssid/pass came from include/wifi_credentials.h
};

// Loads from NVS and applies fallbacks (MAC-derived id, compiled-in credentials).
// Returns true when the NVS namespace existed.
bool configLoad(NodeConfig& cfg);
// Writes id/name/ssid/pass to NVS. Returns true on success.
bool configSave(const NodeConfig& cfg);
// Erases the namespace.
void configFactory();

bool configValidId(const char* id);
bool configValidName(const char* name);
void configDefaultId(char* out, size_t n);
