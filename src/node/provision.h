#pragma once
// Serial provisioning on both USB-C connectors (see PROTOCOL.md section 1).
#include <Arduino.h>

#include "config.h"
#include "state.h"

struct ProvisionDeps {
  NodeConfig* running;         // the config the node booted with
  NodeConfig* staged;          // edited by SET, written by SAVE
  const NodeState* state;      // busy check
  const bool* micAlive;
  void (*led)(uint8_t r, uint8_t g, uint8_t b);
  void (*setLedOverride)(bool on);
};

void provisionBegin(const ProvisionDeps& deps);
void provisionTick();          // non-blocking; reads both serial ports
