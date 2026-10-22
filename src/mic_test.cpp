/*
 * ESP32-S3-WROOM-1 N16R8  ->  SPH0641LU4H-1 PDM microphone smoke test
 *
 * Scope: prove that ONE PDM mic is alive. Capture ~10 s of 16 kHz / 16-bit PCM
 * through the I2S0 PDM RX peripheral, print validation statistics, and ship a
 * WAV file to the laptop over the "UART"/COM connector (Serial0) as base64.
 * No Wi-Fi, no streaming, no sync, no raw PDM here.
 *
 * Wiring (fixed - matches the hardware on the bench):
 *   mic 3.3  -> 3V3        mic GND -> GND        mic SEL -> GND
 *   mic CLK  -> GPIO4      (I2S0 PDM clock; in the legacy driver this is ws_io_num)
 *   mic DATA -> GPIO5      (I2S0 data in)
 *
 * Why driver/i2s.h and not ESP_I2S.h: this project builds on Arduino-ESP32
 * 2.0.17 (ESP-IDF 4.4.7). ESP_I2S.h only exists in Arduino core 3.x.
 *
 * Why a STEREO capture for a single mic: in IDF 4.4.7 the mono formats
 * (ONLY_LEFT / ONLY_RIGHT) are mapped to PDM slot 0 / 1 differently by
 * i2s_driver_install() and by the remap inside i2s_set_clk(), so the slot you
 * get depends on call order. Capturing both slots makes the result observable.
 * ESP-IDF's PDM slot definition: slot 1 ("LEFT") = mic whose SELECT pin is
 * pulled LOW, slot 0 ("RIGHT") = SELECT pulled HIGH. SEL is on GND -> slot 1.
 * Both slots usually look alike (the unused slot sees a one-clock-delayed copy
 * of the same data line); stats for both are printed.
 *
 * PDM clock: i2s_set_pdm_rx_down_sample(DSR_16S) -> 16 kHz * 128 = 2.048 MHz,
 * inside the SPH0641 standard-performance band (1.0 - 3.25 MHz).
 *
 * Serial protocol (human lines go to BOTH connectors, the WAV payload goes to
 * Serial0 = COM port only):
 *   PDM_INIT PASS | PDM_INIT FAIL <step> <esp_err>
 *   READY                          (once per second while idle; send 'r' to record)
 *   STAT <key>=<value>             (statistics)
 *   MIC_ALIVE 0|1
 *   WAV_BEGIN <bytes> <crc32hex>   then base64 lines (76 chars), then
 *   WAV_END
 */

#include <Arduino.h>
#include <driver/gpio.h>
#include <soc/gpio_periph.h>
#include <soc/io_mux_reg.h>
#include <driver/i2s.h>
#include <esp_err.h>
#include <esp_rom_crc.h>
#include <mbedtls/base64.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstring>

namespace {

constexpr uint32_t kSerialBaud     = 115200;  // USB CDC (baud ignored)
constexpr uint32_t kUartBaud       = 921600;  // Serial0 / COM port: fast enough for a 10 s WAV
constexpr int      kPinPdmClk      = 4;       // mic CLK
constexpr int      kPinPdmData     = 5;       // mic DATA
constexpr uint32_t kSampleRate     = 16000;   // PCM output rate (intentional for this test)
constexpr uint32_t kCaptureFrames  = 160000;  // ~10 s
constexpr uint32_t kDiscardMs      = 300;     // mic wake-up + PDM filter settling + stale DMA
constexpr int      kSlots          = 2;       // stereo capture (both PDM slots)
constexpr int      kMicSlot        = 1;       // SEL=GND -> slot 1 (see header comment)
constexpr size_t   kDmaBufCount    = 8;
constexpr size_t   kDmaBufFrames   = 512;     // frames per DMA buffer -> 2048 bytes
constexpr size_t   kChunkBytes     = kDmaBufFrames * kSlots * sizeof(int16_t);
constexpr size_t   kCaptureBytes   = kCaptureFrames * kSlots * sizeof(int16_t);  // 640000
constexpr size_t   kWavHeaderBytes = 44;
constexpr size_t   kWavDataBytes   = kCaptureFrames * sizeof(int16_t);           // 320000
constexpr size_t   kWavBytes       = kWavHeaderBytes + kWavDataBytes;            // 320044
constexpr uint32_t kReadyEveryMs   = 1000;
constexpr uint32_t kExpectedMs     = kCaptureFrames * 1000UL / kSampleRate;  // 10000

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

// -------------------------------------------------------------------- state --

int16_t* g_capture     = nullptr;   // kCaptureBytes, interleaved [slot0, slot1, ...]
uint8_t* g_wav         = nullptr;   // kWavBytes, header + mono PCM of kMicSlot
bool     g_pdmOk       = false;
char     g_pdmFail[96] = "not initialised";
uint32_t g_pdmClockHz  = 0;
const char* g_heapKind = "none";
bool     g_haveWav     = false;   // g_wav holds a complete capture (for resend)

// -------------------------------------------------------------------- stats --

struct Stats {
  uint32_t count     = 0;
  int32_t  min       = 0;
  int32_t  max       = 0;
  double   mean      = 0.0;
  double   rms       = 0.0;     // raw RMS (includes DC)
  double   rmsAc     = 0.0;     // RMS after removing the mean
  int32_t  peak      = 0;       // max |sample|
  int32_t  peakAc    = 0;       // max |sample - mean|
  uint32_t distinct  = 0;
  uint32_t clipCount = 0;       // |sample| >= 32700
  bool     allZero   = true;
  bool     constant  = true;
};

Stats computeStats(const int16_t* interleaved, uint32_t frames, int slot) {
  static uint8_t seen[65536 / 8];   // bitmap of 16-bit values (8 KB, kept off the stack)
  memset(seen, 0, sizeof(seen));

  Stats s;
  s.count = frames;
  int64_t sum   = 0;
  double  sumSq = 0.0;
  s.min = INT32_MAX;
  s.max = INT32_MIN;

  for (uint32_t i = 0; i < frames; ++i) {
    const int16_t v = interleaved[i * kSlots + slot];
    sum   += v;
    sumSq += static_cast<double>(v) * v;
    if (v < s.min) s.min = v;
    if (v > s.max) s.max = v;
    if (v != 0) s.allZero = false;
    if (v >= 32700 || v <= -32700) ++s.clipCount;
    const uint16_t u = static_cast<uint16_t>(v);
    const uint8_t bit = 1u << (u & 7u);
    if ((seen[u >> 3] & bit) == 0) {
      seen[u >> 3] |= bit;
      ++s.distinct;
    }
  }
  if (frames == 0) { s.min = s.max = 0; return s; }

  s.mean     = static_cast<double>(sum) / frames;
  s.rms      = sqrt(sumSq / frames);
  const double varAc = sumSq / frames - s.mean * s.mean;
  s.rmsAc    = varAc > 0.0 ? sqrt(varAc) : 0.0;
  s.peak     = std::max(std::abs(s.min), std::abs(s.max));
  s.peakAc   = static_cast<int32_t>(std::max(fabs(s.max - s.mean), fabs(s.min - s.mean)));
  s.constant = (s.min == s.max);
  return s;
}

void printStats(const char* name, const Stats& s) {
  logf("STAT %s_count=%lu\r\n",    name, static_cast<unsigned long>(s.count));
  logf("STAT %s_min=%ld\r\n",      name, static_cast<long>(s.min));
  logf("STAT %s_max=%ld\r\n",      name, static_cast<long>(s.max));
  logf("STAT %s_mean=%.1f\r\n",    name, s.mean);
  logf("STAT %s_peak=%ld\r\n",     name, static_cast<long>(s.peak));
  logf("STAT %s_peak_ac=%ld\r\n",  name, static_cast<long>(s.peakAc));
  logf("STAT %s_rms=%.1f\r\n",     name, s.rms);
  logf("STAT %s_rms_ac=%.1f\r\n",  name, s.rmsAc);
  logf("STAT %s_distinct=%lu\r\n", name, static_cast<unsigned long>(s.distinct));
  logf("STAT %s_clip=%lu\r\n",     name, static_cast<unsigned long>(s.clipCount));
  logf("STAT %s_all_zero=%d\r\n",  name, s.allZero ? 1 : 0);
  logf("STAT %s_constant=%d\r\n",  name, s.constant ? 1 : 0);
}

// --------------------------------------------------------------- line probe --
// Samples the two mic lines directly through the GPIO input buffers, independent
// of the PDM decoder, to tell "is the ESP32 toggling CLK?" apart from "is the
// mic driving DATA?". GPIO4 is an I2S output: only its input buffer is enabled
// (PIN_INPUT_ENABLE) so the I2S matrix routing is left untouched.

struct LineProbe {
  uint32_t samples     = 0;
  uint32_t highs       = 0;
  uint32_t transitions = 0;
};

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

void enableInputReadback(int gpio) {
  PIN_INPUT_ENABLE(GPIO_PIN_MUX_REG[gpio]);
}

void printLineProbes(const char* prefix) {
  const LineProbe clk  = probeLine(kPinPdmClk, 200000);
  const LineProbe data = probeLine(kPinPdmData, 200000);
  logf("%sclk_transitions=%lu\r\n",  prefix, static_cast<unsigned long>(clk.transitions));
  logf("%sclk_high_pct=%lu\r\n",     prefix, static_cast<unsigned long>(100UL * clk.highs / clk.samples));
  logf("%sdata_transitions=%lu\r\n", prefix, static_cast<unsigned long>(data.transitions));
  logf("%sdata_high_pct=%lu\r\n",    prefix, static_cast<unsigned long>(100UL * data.highs / data.samples));
  logf(" Line probe : CLK GPIO%d %s | DATA GPIO%d %s\r\n", kPinPdmClk,
       clk.transitions > 100 ? "toggling (ESP32 clock output OK)" : "NOT toggling (no PDM clock!)",
       kPinPdmData,
       data.transitions > 100 ? "toggling (mic is driving data)"
                              : "FLAT (mic not driving: check mic 3V3/GND, CLK to mic, DATA to GPIO5)");
}

// ------------------------------------------------------------------ PDM init --
// Which I2S signal carries the PDM clock on the ESP32-S3 with this legacy driver
// is DISCOVERED at boot instead of assumed: each candidate routing is installed,
// GPIO4 is probed for transitions, and the first routing that really toggles the
// pin is kept. (ESP-IDF 5 documents the PDM RX clock on the RX "WS" signal; the
// 4.4.7 legacy driver was observed to leave that signal flat in RX-only mode.)

struct ClockRouting {
  const char* name;
  bool withTx;      // also enable I2S_MODE_TX (driver then routes the TX WS/BCK signals to the pins)
  bool clockOnBck;  // route the clock through bck_io_num instead of ws_io_num
};

constexpr ClockRouting kRoutings[] = {
  {"RX-only, clock on ws_io_num",  false, false},
  {"RX-only, clock on bck_io_num", false, true },
  {"RX+TX,   clock on ws_io_num",  true,  false},
  {"RX+TX,   clock on bck_io_num", true,  true },
};

const ClockRouting* g_routing = nullptr;

bool installRouting(const ClockRouting& r) {
  i2s_config_t cfg = {};
  int mode = I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM;
  if (r.withTx) mode |= I2S_MODE_TX;
  cfg.mode                 = static_cast<i2s_mode_t>(mode);
  cfg.sample_rate          = kSampleRate;
  cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT;   // both PDM slots (see header)
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count        = kDmaBufCount;
  cfg.dma_buf_len          = kDmaBufFrames;
  cfg.use_apll             = false;                        // no APLL on the S3 anyway
  cfg.tx_desc_auto_clear   = true;
  cfg.fixed_mclk           = 0;

  esp_err_t err = i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr);
  if (err != ESP_OK) {
    snprintf(g_pdmFail, sizeof(g_pdmFail), "i2s_driver_install %s", esp_err_to_name(err));
    return false;
  }

  i2s_pin_config_t pins = {};
  pins.mck_io_num   = I2S_PIN_NO_CHANGE;
  pins.bck_io_num   = r.clockOnBck ? kPinPdmClk : I2S_PIN_NO_CHANGE;
  pins.ws_io_num    = r.clockOnBck ? I2S_PIN_NO_CHANGE : kPinPdmClk;
  pins.data_out_num = I2S_PIN_NO_CHANGE;
  pins.data_in_num  = kPinPdmData;
  err = i2s_set_pin(I2S_NUM_0, &pins);
  if (err != ESP_OK) {
    snprintf(g_pdmFail, sizeof(g_pdmFail), "i2s_set_pin %s", esp_err_to_name(err));
    i2s_driver_uninstall(I2S_NUM_0);
    return false;
  }

  // Weak pull-down on DATA: with no (or an unpowered) mic the line reads all
  // zeros, which decodes to a CONSTANT PCM value -> easy to detect in the stats.
  gpio_set_pull_mode(static_cast<gpio_num_t>(kPinPdmData), GPIO_PULLDOWN_ONLY);

  // PDM clock = fs * 128 with DSR_16S (2.048 MHz), fs * 64 with DSR_8S (1.024 MHz).
  err = i2s_set_pdm_rx_down_sample(I2S_NUM_0, I2S_PDM_DSR_16S);
  if (err == ESP_OK) {
    g_pdmClockHz = kSampleRate * 128;
  } else {
    err = i2s_set_pdm_rx_down_sample(I2S_NUM_0, I2S_PDM_DSR_8S);
    if (err != ESP_OK) {
      snprintf(g_pdmFail, sizeof(g_pdmFail), "i2s_set_pdm_rx_down_sample %s", esp_err_to_name(err));
      i2s_driver_uninstall(I2S_NUM_0);
      return false;
    }
    g_pdmClockHz = kSampleRate * 64;
  }
  enableInputReadback(kPinPdmClk);
  return true;
}

// Proves the GPIO4 read-back path works before trusting it as a clock probe:
// drive GPIO4 as a plain GPIO and read it back 1000 times.
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
  logf(" GPIO%d read-back self-test : %lu/1000 %s\r\n", kPinPdmClk, static_cast<unsigned long>(ok),
       ok >= 990 ? "OK" : "FAILED (line probe unreliable)");
  return ok >= 990;
}

bool pdmInit() {
  const bool probeOk = readbackSelfTest();
  for (const ClockRouting& r : kRoutings) {
    if (!installRouting(r)) {
      logf(" Routing '%s': install failed (%s)\r\n", r.name, g_pdmFail);
      continue;
    }
    delay(20);
    const LineProbe clk = probeLine(kPinPdmClk, 200000);
    const bool toggles = clk.transitions > 100;
    logf(" Routing '%s': GPIO%d transitions=%lu high=%lu%% -> %s\r\n", r.name, kPinPdmClk,
         static_cast<unsigned long>(clk.transitions),
         static_cast<unsigned long>(100UL * clk.highs / clk.samples),
         toggles ? "CLOCK PRESENT" : "no clock");
    if (toggles || !probeOk) {
      g_routing = &r;
      break;
    }
    i2s_driver_uninstall(I2S_NUM_0);
  }
  if (!g_routing) {
    snprintf(g_pdmFail, sizeof(g_pdmFail), "no PDM clock on GPIO%d with any I2S routing", kPinPdmClk);
    return false;
  }
  logf(" Using routing              : %s\r\n", g_routing->name);
  logf(" i2s_get_clk                : %.0f Hz (PCM rate reported by driver)\r\n", i2s_get_clk(I2S_NUM_0));
  logf(" PDM clock on GPIO%d        : %lu Hz\r\n", kPinPdmClk, static_cast<unsigned long>(g_pdmClockHz));
  printLineProbes("STAT ");
  return true;
}

// ------------------------------------------------------------------- capture --

bool captureFrames(int16_t* dst, uint32_t& captureMs) {
  uint8_t* bytes = reinterpret_cast<uint8_t*>(dst);

  // Discard: stale DMA buffers from idle time + mic wake-up + filter settling.
  uint32_t t0 = millis();
  while (millis() - t0 < kDiscardMs) {
    size_t n = 0;
    i2s_read(I2S_NUM_0, bytes, kChunkBytes, &n, pdMS_TO_TICKS(100));
  }

  // Timed capture of exactly kCaptureFrames frames.
  size_t got = 0;
  t0 = millis();
  while (got < kCaptureBytes) {
    size_t n = 0;
    const size_t want = std::min(kChunkBytes, kCaptureBytes - got);
    const esp_err_t err = i2s_read(I2S_NUM_0, bytes + got, want, &n, pdMS_TO_TICKS(1000));
    if (err != ESP_OK) {
      logf("CAPTURE FAIL i2s_read %s\r\n", esp_err_to_name(err));
      return false;
    }
    if (n == 0) {
      logWrite("CAPTURE FAIL i2s_read returned no data for 1 s (no DMA activity)\r\n");
      return false;
    }
    got += n;
  }
  captureMs = millis() - t0;
  return true;
}

// ----------------------------------------------------------------------- WAV --

void putLE16(uint8_t* p, uint16_t v) { p[0] = v & 0xFF; p[1] = v >> 8; }
void putLE32(uint8_t* p, uint32_t v) { p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; p[2] = (v >> 16) & 0xFF; p[3] = v >> 24; }

void buildWavHeader(uint8_t* h, uint32_t dataBytes, uint32_t rate, uint16_t channels, uint16_t bits) {
  const uint16_t blockAlign = channels * bits / 8;
  memcpy(h + 0, "RIFF", 4);
  putLE32(h + 4, 36 + dataBytes);
  memcpy(h + 8, "WAVE", 4);
  memcpy(h + 12, "fmt ", 4);
  putLE32(h + 16, 16);                    // PCM fmt chunk size
  putLE16(h + 20, 1);                     // PCM
  putLE16(h + 22, channels);
  putLE32(h + 24, rate);
  putLE32(h + 28, rate * blockAlign);     // byte rate
  putLE16(h + 32, blockAlign);
  putLE16(h + 34, bits);
  memcpy(h + 36, "data", 4);
  putLE32(h + 40, dataBytes);
}

// The UART link has no flow control (CTS is not wired through the CH343), so a
// 430 KB burst at 921600 baud occasionally loses a few bytes somewhere between
// the bridge chip and Python. The WAV is therefore sent in CRC-checked blocks,
// each acknowledged by the laptop ('a' = ok, 'n' = resend), which makes the
// transfer lossless regardless of where the bytes went missing.
constexpr size_t   kBlockLines   = 72;                  // 72 base64 lines of 57 bytes
constexpr size_t   kBlockBytes   = kBlockLines * 57;    // 4104 bytes per block
constexpr uint32_t kAckTimeoutMs = 3000;
constexpr int      kBlockRetries = 10;

int waitAck(uint32_t timeoutMs) {   // 1 = ack, 0 = nack, -1 = timeout
  const uint32_t t0 = millis();
  while (millis() - t0 < timeoutMs) {
    while (Serial0.available()) {
      const int c = Serial0.read();
      if (c == 'a') return 1;
      if (c == 'n') return 0;
    }
    delay(1);
  }
  return -1;
}

void sendBlockLines(const uint8_t* p, size_t n) {
  unsigned char out[80];
  for (size_t off = 0; off < n; off += 57) {            // 57 bytes -> 76 base64 chars
    const size_t m = std::min<size_t>(57, n - off);
    size_t olen = 0;
    mbedtls_base64_encode(out, sizeof(out), &olen, p + off, m);
    Serial0.write(out, olen);
    Serial0.print("\r\n");
  }
}

bool sendWav(const uint8_t* wav, size_t len) {
  const uint32_t crc = esp_rom_crc32_le(0, wav, len);   // same as Python zlib.crc32
  const size_t blocks = (len + kBlockBytes - 1) / kBlockBytes;
  char line[96];
  snprintf(line, sizeof(line), "WAV_BEGIN %u %08x %u %u\r\n", static_cast<unsigned>(len),
           static_cast<unsigned>(crc), static_cast<unsigned>(blocks), static_cast<unsigned>(kBlockBytes));
  Serial0.print(line);

  uint32_t retries = 0;
  for (size_t b = 0; b < blocks; ++b) {
    const uint8_t* p = wav + b * kBlockBytes;
    const size_t n = std::min(kBlockBytes, len - b * kBlockBytes);
    const uint32_t bcrc = esp_rom_crc32_le(0, p, n);
    int tries = 0;
    for (;;) {
      sendBlockLines(p, n);
      snprintf(line, sizeof(line), "BLK %u %08x\r\n", static_cast<unsigned>(b), static_cast<unsigned>(bcrc));
      Serial0.print(line);
      if (waitAck(kAckTimeoutMs) == 1) break;
      ++retries;
      if (++tries >= kBlockRetries) {
        Serial0.print("WAV_ABORT block retries exhausted\r\n");
        return false;
      }
    }
  }
  snprintf(line, sizeof(line), "WAV_END retries=%lu\r\n", static_cast<unsigned long>(retries));
  Serial0.print(line);
  return true;
}

// ------------------------------------------------------------------ recording --

void runRecording() {
  if (!g_pdmOk) {
    logf("PDM_INIT FAIL %s - cannot record\r\n", g_pdmFail);
    return;
  }
  logf("RECORDING discard %lu ms then %lu frames @ %lu Hz ...\r\n",
       static_cast<unsigned long>(kDiscardMs), static_cast<unsigned long>(kCaptureFrames),
       static_cast<unsigned long>(kSampleRate));

  printLineProbes("STAT ");
  uint32_t captureMs = 0;
  if (!captureFrames(g_capture, captureMs)) return;

  const Stats s0 = computeStats(g_capture, kCaptureFrames, 0);
  const Stats s1 = computeStats(g_capture, kCaptureFrames, 1);
  const Stats& mic   = (kMicSlot == 1) ? s1 : s0;
  const Stats& other = (kMicSlot == 1) ? s0 : s1;

  logf("STAT slot=%d\r\n", kMicSlot);
  logf("STAT capture_ms=%lu\r\n", static_cast<unsigned long>(captureMs));
  logf("STAT pdm_clock_hz=%lu\r\n", static_cast<unsigned long>(g_pdmClockHz));
  logf("STAT sample_rate=%lu\r\n", static_cast<unsigned long>(kSampleRate));
  logf("STAT heap=%s\r\n", g_heapKind);
  printStats("ch0", s0);
  printStats("ch1", s1);

  const bool alive = !mic.constant && mic.distinct >= 64 && fabs(mic.mean) <= 8000.0 &&
                     captureMs >= kExpectedMs * 94 / 100 && captureMs <= kExpectedMs * 110 / 100;
  logf("MIC_ALIVE %d\r\n", alive ? 1 : 0);
  if (mic.constant && !other.constant) {
    logWrite("SLOT_UNEXPECTED mic slot is flat but the other slot has data - check SEL wiring\r\n");
  }
  if (mic.constant && other.constant) {
    logWrite("NO_SIGNAL both slots flat - mic not powered / DATA not connected / no clock\r\n");
  }

  // Mono WAV from the mic slot, sent over the COM port only.
  buildWavHeader(g_wav, kWavDataBytes, kSampleRate, 1, 16);
  int16_t* pcm = reinterpret_cast<int16_t*>(g_wav + kWavHeaderBytes);
  for (uint32_t i = 0; i < kCaptureFrames; ++i) {
    pcm[i] = g_capture[i * kSlots + kMicSlot];
  }
  g_haveWav = true;
  if (sendWav(g_wav, kWavBytes)) {
    logWrite("CAPTURE PASS\r\n");
  } else {
    logWrite("CAPTURE FAIL transfer aborted (send 's' to retry)\r\n");
  }
}

// ---------------------------------------------------------------- allocation --

bool allocateBuffers() {
  g_capture = static_cast<int16_t*>(ps_malloc(kCaptureBytes));
  g_wav     = static_cast<uint8_t*>(ps_malloc(kWavBytes));
  g_heapKind = "psram";
  if (!g_capture || !g_wav) {
    free(g_capture);
    free(g_wav);
    g_capture = static_cast<int16_t*>(malloc(kCaptureBytes));
    g_wav     = static_cast<uint8_t*>(malloc(kWavBytes));
    g_heapKind = "internal";
  }
  return g_capture != nullptr && g_wav != nullptr;
}

}  // namespace

// ----------------------------------------------------------------- Arduino --

void setup() {
  Serial.begin(kSerialBaud);
  Serial.setTxTimeoutMs(20);
  Serial0.begin(kUartBaud);
  delay(1500);

  logWrite("\r\n\r\n============================================\r\n");
  logWrite(" ESP32-S3  SPH0641 PDM microphone smoke test\r\n");
  logWrite("============================================\r\n");
  logf(" Chip      : %s rev %d @ %lu MHz\r\n", ESP.getChipModel(), ESP.getChipRevision(),
       static_cast<unsigned long>(ESP.getCpuFreqMHz()));
  logf(" PSRAM     : %s, %lu KB free\r\n", psramFound() ? "found" : "NOT FOUND",
       static_cast<unsigned long>(ESP.getFreePsram() / 1024UL));
  logf(" Mic pins  : CLK=GPIO%d  DATA=GPIO%d  SEL=GND (slot %d)\r\n", kPinPdmClk, kPinPdmData, kMicSlot);
  logf(" Format    : %lu Hz, 16-bit, %lu frames (%lu ms), stereo capture -> mono WAV\r\n",
       static_cast<unsigned long>(kSampleRate), static_cast<unsigned long>(kCaptureFrames),
       static_cast<unsigned long>(kExpectedMs));

  if (!allocateBuffers()) {
    snprintf(g_pdmFail, sizeof(g_pdmFail), "buffer allocation failed");
    logf("PDM_INIT FAIL %s\r\n", g_pdmFail);
    return;
  }
  logf(" Buffers   : %u + %u bytes in %s\r\n", static_cast<unsigned>(kCaptureBytes),
       static_cast<unsigned>(kWavBytes), g_heapKind);

  g_pdmOk = pdmInit();
  if (g_pdmOk) {
    logWrite("PDM_INIT PASS\r\n");
  } else {
    logf("PDM_INIT FAIL %s\r\n", g_pdmFail);
  }
}

void loop() {
  static uint32_t lastReady = 0;
  const uint32_t now = millis();
  if (now - lastReady >= kReadyEveryMs) {
    lastReady = now;
    if (g_pdmOk) {
      // Live line status so wiring can be fixed while watching the serial output.
      const LineProbe c = probeLine(kPinPdmClk, 20000);
      const LineProbe d = probeLine(kPinPdmData, 20000);
      logf("READY clk=%s data=%s (data high %lu%%)\r\n",
           c.transitions > 10 ? "OK" : "NONE", d.transitions > 10 ? "ALIVE" : "FLAT",
           static_cast<unsigned long>(100UL * d.highs / d.samples));
    } else {
      logf("PDM_INIT FAIL %s\r\n", g_pdmFail);
    }
  }

  int c = -1;
  if (Serial0.available()) {
    c = Serial0.read();
  } else if (Serial.available()) {
    c = Serial.read();           // trigger also accepted from the USB connector
  }
  if (c == 's' || c == 'S') {           // resend the last WAV without re-recording
    if (g_haveWav) {
      logWrite(sendWav(g_wav, kWavBytes) ? "RESEND DONE\r\n" : "RESEND FAILED\r\n");
    } else {
      logWrite("NO_WAV\r\n");
    }
    lastReady = millis();
  } else if (c == 'r' || c == 'R') {
    runRecording();
    lastReady = millis();
  }
  delay(5);
}
