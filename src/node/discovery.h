#pragma once
// UDP 5005: answer the laptop's beacons with a heartbeat JSON (see PROTOCOL.md).
#include <Arduino.h>

#include "config.h"
#include "state.h"

struct NodeStatus {
  const NodeConfig* cfg;
  NodeState state;
  bool micAlive;
};

void   discoveryBegin();
void   discoveryTick(const NodeStatus& st);
size_t statusJson(char* out, size_t n, const NodeStatus& st);
