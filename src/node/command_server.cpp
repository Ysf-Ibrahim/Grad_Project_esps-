#include "command_server.h"

#include <WiFi.h>

#include <cstdlib>
#include <cstring>

#include "log.h"
#include "mic.h"

namespace {

constexpr uint16_t kPort        = 5010;
constexpr size_t   kSendChunk   = 8192;
constexpr uint32_t kProgEveryMs = 500;

WiFiServer g_server(kPort);

struct ProgressCtx {
  WiFiClient* client;
  const CommandDeps* deps;
  uint32_t lastProg;
};

bool onProgress(uint32_t elapsedMs, uint32_t totalMs, void* ctxp) {
  ProgressCtx* ctx = static_cast<ProgressCtx*>(ctxp);
  ctx->deps->tick();
  if (!ctx->client->connected()) return false;
  if (elapsedMs - ctx->lastProg >= kProgEveryMs || elapsedMs >= totalMs) {
    ctx->lastProg = elapsedMs;
    ctx->client->printf("PROG %lu %lu\n", static_cast<unsigned long>(elapsedMs), static_cast<unsigned long>(totalMs));
  }
  return true;
}

void sendStats(WiFiClient& c, const MicCapture& cap, const MicHealth& h) {
  const MicStats& s = cap.stats;
  c.printf("STAT capture_ms=%lu\n",   static_cast<unsigned long>(cap.captureMs));
  c.printf("STAT expected_ms=%lu\n",  static_cast<unsigned long>(cap.expectedMs));
  c.printf("STAT samples=%lu\n",      static_cast<unsigned long>(s.count));
  c.printf("STAT sample_rate=%lu\n",  static_cast<unsigned long>(kMicSampleRate));
  c.printf("STAT pdm_clock_hz=%lu\n", static_cast<unsigned long>(micPdmClockHz()));
  c.printf("STAT min=%ld\n",          static_cast<long>(s.min));
  c.printf("STAT max=%ld\n",          static_cast<long>(s.max));
  c.printf("STAT mean=%.1f\n",        s.mean);
  c.printf("STAT peak=%ld\n",         static_cast<long>(s.peak));
  c.printf("STAT peak_ac=%ld\n",      static_cast<long>(s.peakAc));
  c.printf("STAT rms=%.1f\n",         s.rms);
  c.printf("STAT rms_ac=%.1f\n",      s.rmsAc);
  c.printf("STAT distinct=%lu\n",     static_cast<unsigned long>(s.distinct));
  c.printf("STAT clip=%lu\n",         static_cast<unsigned long>(s.clipCount));
  c.printf("STAT all_zero=%d\n",      s.allZero ? 1 : 0);
  c.printf("STAT constant=%d\n",      s.constant ? 1 : 0);
  c.printf("STAT slot_unexpected=%d\n", cap.slotUnexpected ? 1 : 0);
  c.printf("STAT other_min=%ld\n",    static_cast<long>(cap.otherMin));
  c.printf("STAT other_max=%ld\n",    static_cast<long>(cap.otherMax));
  c.printf("STAT clk_transitions=%lu\n",  static_cast<unsigned long>(h.clkTransitions));
  c.printf("STAT data_transitions=%lu\n", static_cast<unsigned long>(h.dataTransitions));
  c.printf("STAT data_high_pct=%lu\n",    static_cast<unsigned long>(h.dataHighPct));
  c.printf("STAT mic_alive=%d\n",     h.alive ? 1 : 0);
}

void handleCapture(WiFiClient& c, const CommandDeps& d, const char* arg) {
  const long secs = arg ? strtol(arg, nullptr, 10) : 0;
  if (secs < 1 || secs > static_cast<long>(kMicMaxSeconds)) { c.print("ERR bad duration (1..30)\n"); return; }
  if (!micOk()) { c.printf("ERR mic %s\n", micFailReason()); return; }

  const uint32_t frames = static_cast<uint32_t>(secs) * kMicSampleRate;
  c.printf("OK id=%s samples=%lu rate=%lu bytes=%lu\n", d.cfg->id, static_cast<unsigned long>(frames),
           static_cast<unsigned long>(kMicSampleRate), static_cast<unsigned long>(frames * 2));

  const MicHealth health = micProbe(20000);
  *d.state = NodeState::Recording;
  logf("CAPTURE start %ld s for %s\r\n", secs, c.remoteIP().toString().c_str());

  ProgressCtx ctx = {&c, &d, 0};
  MicCapture cap;
  const bool ok = micCapture(static_cast<uint32_t>(secs), onProgress, &ctx, cap);
  if (!ok) {
    *d.state = NodeState::Idle;
    logf("CAPTURE failed: %s\r\n", micFailReason());
    if (c.connected()) c.printf("ERR capture %s\n", micFailReason());
    return;
  }

  *d.state = NodeState::Sending;
  sendStats(c, cap, health);
  const size_t bytes = cap.frames * sizeof(int16_t);
  c.printf("DATA %u\n", static_cast<unsigned>(bytes));
  const uint8_t* p = reinterpret_cast<const uint8_t*>(micBuffer());
  const uint32_t t0 = millis();
  size_t off = 0;
  while (off < bytes) {
    const size_t n = std::min(kSendChunk, bytes - off);
    const size_t w = c.write(p + off, n);
    if (w != n) {
      *d.state = NodeState::Idle;
      logf("CAPTURE transfer aborted at %u/%u bytes\r\n", static_cast<unsigned>(off + w), static_cast<unsigned>(bytes));
      return;
    }
    off += n;
    d.tick();
  }
  c.printf("END crc32=%08x\n", static_cast<unsigned>(cap.crc32));
  c.flush();
  *d.state = NodeState::Idle;
  logf("CAPTURE done: %lu samples in %lu ms, sent %u bytes in %lu ms, peak=%ld rms=%.0f\r\n",
       static_cast<unsigned long>(cap.frames), static_cast<unsigned long>(cap.captureMs),
       static_cast<unsigned>(bytes), static_cast<unsigned long>(millis() - t0),
       static_cast<long>(cap.stats.peak), cap.stats.rms);
}

}  // namespace

void commandServerBegin() {
  g_server.begin();
  g_server.setNoDelay(true);
}

void commandServerRejectNew() {
  WiFiClient nc = g_server.available();
  if (nc) {
    nc.print("ERR busy\n");
    nc.stop();
  }
}

void commandServerTick(const CommandDeps& d) {
  WiFiClient c = g_server.available();
  if (!c) return;
  if (*d.state != NodeState::Idle) { c.print("ERR busy\n"); c.stop(); return; }

  c.setTimeout(3000);
  String line = c.readStringUntil('\n');
  line.trim();
  if (line.length() == 0) { c.print("ERR no command\n"); c.stop(); return; }

  char cmd[48];
  strlcpy(cmd, line.c_str(), sizeof(cmd));
  char* arg = strchr(cmd, ' ');
  if (arg) { *arg = '\0'; ++arg; while (*arg == ' ') ++arg; }

  if (strcasecmp(cmd, "STATUS") == 0) {
    char json[400];
    NodeStatus st = {d.cfg, *d.state, *d.micAlive};
    statusJson(json, sizeof(json), st);
    c.printf("OK %s\n", json);
  } else if (strcasecmp(cmd, "PROBE") == 0) {
    const MicHealth h = micProbe(200000);
    *d.micAlive = h.alive;
    c.printf("OK clk=%lu data=%lu data_high=%lu alive=%d\n", static_cast<unsigned long>(h.clkTransitions),
             static_cast<unsigned long>(h.dataTransitions), static_cast<unsigned long>(h.dataHighPct), h.alive ? 1 : 0);
  } else if (strcasecmp(cmd, "CAPTURE") == 0) {
    handleCapture(c, d, arg);
  } else {
    c.print("ERR unknown command\n");
  }
  c.flush();
  c.stop();
}
