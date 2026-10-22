#include "mic.h"

#include <driver/gpio.h>
#include <driver/i2s.h>
#include <esp_err.h>
#include <esp_rom_crc.h>
#include <soc/gpio_periph.h>
#include <soc/io_mux_reg.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "log.h"

namespace {

constexpr int    kPinPdmClk   = 4;
constexpr int    kPinPdmData  = 5;
constexpr int    kSlots       = 2;     // stereo capture: both PDM slots (see mic_test.cpp header)
constexpr int    kMicSlot     = 1;     // SEL=GND -> slot 1
constexpr size_t kDmaBufCount = 8;
constexpr size_t kDmaBufFrames = 512;  // 2048 bytes per DMA buffer
constexpr size_t kChunkBytes  = kDmaBufFrames * kSlots * sizeof(int16_t);
constexpr uint32_t kDiscardMs = 300;
constexpr size_t kMonoBytesMax = kMicSampleRate * kMicMaxSeconds * sizeof(int16_t);   // 960000

int16_t* g_mono = nullptr;             // PSRAM, allocated once
uint8_t  g_scratch[kChunkBytes];       // internal RAM for DMA chunk reads
bool     g_ok = false;
char     g_fail[96] = "not initialised";
uint32_t g_pdmClockHz = 0;
const char* g_routingName = "none";

struct LineProbe { uint32_t samples = 0, highs = 0, transitions = 0; };

LineProbe probeLine(int gpio, uint32_t samples) {
  LineProbe p;
  p.samples = samples;
  const gpio_num_t g = static_cast<gpio_num_t>(gpio);
  int last = gpio_get_level(g);
  for (uint32_t i = 0; i < samples; ++i) {
    const int v = gpio_get_level(g);
    p.highs += v;
    if (v != last) { ++p.transitions; last = v; }
  }
  return p;
}

// GPIO4 is an I2S output; enabling only its input buffer keeps the matrix routing.
void enableInputReadback(int gpio) { PIN_INPUT_ENABLE(GPIO_PIN_MUX_REG[gpio]); }

struct ClockRouting { const char* name; bool withTx; bool clockOnBck; };
constexpr ClockRouting kRoutings[] = {
  {"RX-only, clock on ws_io_num",  false, false},
  {"RX-only, clock on bck_io_num", false, true },
  {"RX+TX,   clock on ws_io_num",  true,  false},
  {"RX+TX,   clock on bck_io_num", true,  true },
};

bool installRouting(const ClockRouting& r) {
  i2s_config_t cfg = {};
  int mode = I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM;
  if (r.withTx) mode |= I2S_MODE_TX;
  cfg.mode                 = static_cast<i2s_mode_t>(mode);
  cfg.sample_rate          = kMicSampleRate;
  cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count        = kDmaBufCount;
  cfg.dma_buf_len          = kDmaBufFrames;
  cfg.use_apll             = false;
  cfg.tx_desc_auto_clear   = true;
  cfg.fixed_mclk           = 0;

  esp_err_t err = i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr);
  if (err != ESP_OK) { snprintf(g_fail, sizeof(g_fail), "i2s_driver_install %s", esp_err_to_name(err)); return false; }

  i2s_pin_config_t pins = {};
  pins.mck_io_num   = I2S_PIN_NO_CHANGE;
  pins.bck_io_num   = r.clockOnBck ? kPinPdmClk : I2S_PIN_NO_CHANGE;
  pins.ws_io_num    = r.clockOnBck ? I2S_PIN_NO_CHANGE : kPinPdmClk;
  pins.data_out_num = I2S_PIN_NO_CHANGE;
  pins.data_in_num  = kPinPdmData;
  err = i2s_set_pin(I2S_NUM_0, &pins);
  if (err != ESP_OK) { snprintf(g_fail, sizeof(g_fail), "i2s_set_pin %s", esp_err_to_name(err)); i2s_driver_uninstall(I2S_NUM_0); return false; }

  // Weak pull-down on DATA: an unpowered / disconnected mic decodes to a constant.
  gpio_set_pull_mode(static_cast<gpio_num_t>(kPinPdmData), GPIO_PULLDOWN_ONLY);

  err = i2s_set_pdm_rx_down_sample(I2S_NUM_0, I2S_PDM_DSR_16S);   // PDM clock = fs * 128
  if (err == ESP_OK) {
    g_pdmClockHz = kMicSampleRate * 128;
  } else {
    err = i2s_set_pdm_rx_down_sample(I2S_NUM_0, I2S_PDM_DSR_8S);
    if (err != ESP_OK) { snprintf(g_fail, sizeof(g_fail), "i2s_set_pdm_rx_down_sample %s", esp_err_to_name(err)); i2s_driver_uninstall(I2S_NUM_0); return false; }
    g_pdmClockHz = kMicSampleRate * 64;
  }
  enableInputReadback(kPinPdmClk);
  return true;
}

bool readbackSelfTest() {
  const gpio_num_t g = static_cast<gpio_num_t>(kPinPdmClk);
  gpio_reset_pin(g);
  gpio_set_direction(g, GPIO_MODE_INPUT_OUTPUT);
  uint32_t ok = 0;
  for (uint32_t i = 0; i < 1000; ++i) {
    gpio_set_level(g, i & 1u);
    if (gpio_get_level(g) == static_cast<int>(i & 1u)) ++ok;
  }
  gpio_set_level(g, 0);
  gpio_reset_pin(g);
  return ok >= 990;
}

MicStats computeStats(const int16_t* x, uint32_t n) {
  static uint8_t seen[65536 / 8];
  memset(seen, 0, sizeof(seen));
  MicStats s;
  s.count = n;
  if (n == 0) return s;
  int64_t sum = 0;
  double sumSq = 0.0;
  s.min = INT32_MAX; s.max = INT32_MIN;
  for (uint32_t i = 0; i < n; ++i) {
    const int16_t v = x[i];
    sum += v;
    sumSq += static_cast<double>(v) * v;
    if (v < s.min) s.min = v;
    if (v > s.max) s.max = v;
    if (v != 0) s.allZero = false;
    if (v >= 32700 || v <= -32700) ++s.clipCount;
    const uint16_t u = static_cast<uint16_t>(v);
    const uint8_t bit = 1u << (u & 7u);
    if ((seen[u >> 3] & bit) == 0) { seen[u >> 3] |= bit; ++s.distinct; }
  }
  s.mean = static_cast<double>(sum) / n;
  s.rms = sqrt(sumSq / n);
  const double varAc = sumSq / n - s.mean * s.mean;
  s.rmsAc = varAc > 0.0 ? sqrt(varAc) : 0.0;
  s.peak = std::max(std::abs(s.min), std::abs(s.max));
  s.peakAc = static_cast<int32_t>(std::max(fabs(s.max - s.mean), fabs(s.min - s.mean)));
  s.constant = (s.min == s.max);
  return s;
}

}  // namespace

bool micInit() {
  g_mono = static_cast<int16_t*>(ps_malloc(kMonoBytesMax));
  if (!g_mono) g_mono = static_cast<int16_t*>(malloc(kMonoBytesMax));
  if (!g_mono) { snprintf(g_fail, sizeof(g_fail), "buffer allocation failed (%u bytes)", static_cast<unsigned>(kMonoBytesMax)); return false; }

  const bool probeOk = readbackSelfTest();
  logf("MIC read-back self-test: %s\r\n", probeOk ? "OK" : "FAILED (line probe unreliable)");
  for (const ClockRouting& r : kRoutings) {
    if (!installRouting(r)) { logf("MIC routing '%s': %s\r\n", r.name, g_fail); continue; }
    delay(20);
    const LineProbe clk = probeLine(kPinPdmClk, 200000);
    const bool toggles = clk.transitions > 100;
    logf("MIC routing '%s': clk transitions=%lu -> %s\r\n", r.name,
         static_cast<unsigned long>(clk.transitions), toggles ? "clock present" : "no clock");
    if (toggles || !probeOk) { g_routingName = r.name; g_ok = true; break; }
    i2s_driver_uninstall(I2S_NUM_0);
  }
  if (!g_ok) { snprintf(g_fail, sizeof(g_fail), "no PDM clock on GPIO%d with any routing", kPinPdmClk); return false; }
  const MicHealth h = micProbe(200000);
  logf("MIC ready: pdm_clock=%lu Hz clk_transitions=%lu data_transitions=%lu data_high=%lu%% -> %s\r\n",
       static_cast<unsigned long>(g_pdmClockHz), static_cast<unsigned long>(h.clkTransitions),
       static_cast<unsigned long>(h.dataTransitions), static_cast<unsigned long>(h.dataHighPct),
       h.alive ? "mic alive" : "mic FLAT (not driving data)");
  return true;
}

bool        micOk()          { return g_ok; }
const char* micFailReason()  { return g_fail; }
const char* micRoutingName() { return g_routingName; }
uint32_t    micPdmClockHz()  { return g_pdmClockHz; }
const int16_t* micBuffer()   { return g_mono; }

MicHealth micProbe(uint32_t samplesPerLine) {
  MicHealth h;
  if (!g_ok) return h;
  const LineProbe c = probeLine(kPinPdmClk, samplesPerLine);
  const LineProbe d = probeLine(kPinPdmData, samplesPerLine);
  h.clkTransitions  = c.transitions;
  h.dataTransitions = d.transitions;
  h.dataHighPct     = d.samples ? 100UL * d.highs / d.samples : 0;
  h.clockOk         = c.transitions > 10;
  h.alive           = h.clockOk && d.transitions > 10;
  return h;
}

bool micCapture(uint32_t seconds, MicProgressFn progress, void* ctx, MicCapture& out) {
  out = MicCapture();
  if (!g_ok) return false;
  if (seconds < 1) seconds = 1;
  if (seconds > kMicMaxSeconds) seconds = kMicMaxSeconds;
  const uint32_t frames = seconds * kMicSampleRate;
  out.expectedMs = seconds * 1000UL;

  // Drain stale DMA buffers + mic wake-up.
  uint32_t t0 = millis();
  while (millis() - t0 < kDiscardMs) {
    size_t n = 0;
    i2s_read(I2S_NUM_0, g_scratch, kChunkBytes, &n, pdMS_TO_TICKS(100));
  }

  int32_t oMin = INT32_MAX, oMax = INT32_MIN;
  uint32_t got = 0;
  t0 = millis();
  while (got < frames) {
    const size_t wantFrames = std::min<size_t>(kDmaBufFrames, frames - got);
    size_t n = 0;
    const esp_err_t err = i2s_read(I2S_NUM_0, g_scratch, wantFrames * kSlots * sizeof(int16_t), &n, pdMS_TO_TICKS(1000));
    if (err != ESP_OK || n == 0) { snprintf(g_fail, sizeof(g_fail), "i2s_read %s", err != ESP_OK ? esp_err_to_name(err) : "no data"); return false; }
    const int16_t* in = reinterpret_cast<const int16_t*>(g_scratch);
    const uint32_t f = n / (kSlots * sizeof(int16_t));
    for (uint32_t i = 0; i < f; ++i) {
      g_mono[got + i] = in[i * kSlots + kMicSlot];
      const int16_t o = in[i * kSlots + (1 - kMicSlot)];
      if (o < oMin) oMin = o;
      if (o > oMax) oMax = o;
    }
    got += f;
    if (progress && !progress(millis() - t0, out.expectedMs, ctx)) { snprintf(g_fail, sizeof(g_fail), "aborted"); return false; }
  }
  out.captureMs = millis() - t0;
  out.frames = got;
  out.stats = computeStats(g_mono, got);
  out.otherMin = oMin;
  out.otherMax = oMax;
  out.slotUnexpected = out.stats.constant && (oMin != oMax);
  out.crc32 = esp_rom_crc32_le(0, reinterpret_cast<const uint8_t*>(g_mono), got * sizeof(int16_t));
  return true;
}
