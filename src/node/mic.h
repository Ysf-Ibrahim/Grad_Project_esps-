#pragma once
// SPH0641 PDM microphone on I2S0 (legacy driver): init, health probe, capture, stats.
// Lifted from src/mic_test.cpp. Wiring: CLK -> GPIO4, DATA -> GPIO5, SEL -> GND.
#include <Arduino.h>

constexpr uint32_t kMicSampleRate = 16000;
constexpr uint32_t kMicMaxSeconds = 30;

struct MicStats {
  uint32_t count     = 0;
  int32_t  min       = 0;
  int32_t  max       = 0;
  double   mean      = 0.0;
  double   rms       = 0.0;
  double   rmsAc     = 0.0;
  int32_t  peak      = 0;
  int32_t  peakAc    = 0;
  uint32_t distinct  = 0;
  uint32_t clipCount = 0;
  bool     allZero   = true;
  bool     constant  = true;
};

struct MicHealth {
  uint32_t clkTransitions  = 0;
  uint32_t dataTransitions = 0;
  uint32_t dataHighPct     = 0;
  bool     clockOk         = false;
  bool     alive           = false;
};

struct MicCapture {
  uint32_t frames        = 0;     // mono samples stored
  uint32_t captureMs     = 0;
  uint32_t expectedMs    = 0;
  MicStats stats;                 // of the mic slot
  int32_t  otherMin      = 0;     // the unused PDM slot, for the SLOT_UNEXPECTED check
  int32_t  otherMax      = 0;
  bool     slotUnexpected = false;
  uint32_t crc32         = 0;     // over the mono bytes
};

// Called from inside the capture loop every DMA chunk (~32 ms). Return false to abort.
typedef bool (*MicProgressFn)(uint32_t elapsedMs, uint32_t totalMs, void* ctx);

bool        micInit();                    // routing discovery + buffer allocation; call before Wi-Fi
bool        micOk();
const char* micFailReason();
const char* micRoutingName();
uint32_t    micPdmClockHz();
MicHealth   micProbe(uint32_t samplesPerLine);
bool        micCapture(uint32_t seconds, MicProgressFn progress, void* ctx, MicCapture& out);
const int16_t* micBuffer();               // mono samples of the last capture
