#pragma once
// TCP 5010: STATUS / PROBE / CAPTURE <s> with the framed transfer (see PROTOCOL.md).
#include <Arduino.h>

#include "discovery.h"
#include "state.h"

struct CommandDeps {
  const NodeConfig* cfg;
  NodeState* state;            // set to Recording / Sending / Idle by the server
  bool* micAlive;              // latest health result (used in STATUS)
  void (*tick)();              // keeps serial, UDP, LED alive during long operations
};

void commandServerBegin();
void commandServerTick(const CommandDeps& d);   // accepts and serves one client (may block for a capture)
void commandServerRejectNew();                  // while busy: answer newcomers with ERR busy
