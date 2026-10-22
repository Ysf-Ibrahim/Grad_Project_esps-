#pragma once
// Wi-Fi station link with automatic reconnect (logic carried over from src/main.cpp).
#include <Arduino.h>
#include <IPAddress.h>

#include "config.h"

void      wifiBegin(const NodeConfig& cfg);   // hostname, STA mode, first attempt
void      wifiTick();                         // call often: reconnect + state logging
bool      wifiConnected();
int       wifiRssi();
IPAddress wifiIp();
String    wifiMac();
void      wifiScanPrint();                    // blocking scan, prints the visible networks
