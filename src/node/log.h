#pragma once
// Human-readable log lines go to BOTH USB-C connectors of the DevKitC-1:
//   Serial  = native USB CDC (ARDUINO_USB_CDC_ON_BOOT=1)
//   Serial0 = UART0 through the CH343 bridge ("COM" connector)
#include <Arduino.h>
#include <cstdarg>
#include <cstdio>

inline void logWrite(const char* text) {
  Serial.print(text);
  Serial0.print(text);
}

inline void logf(const char* fmt, ...) {
  char buf[320];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  logWrite(buf);
}
