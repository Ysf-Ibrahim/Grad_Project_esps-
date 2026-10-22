#pragma once
// Node run state, shared by the heartbeat, the LED and the command server.
enum class NodeState { Idle, Recording, Sending };

inline const char* stateName(NodeState s) {
  switch (s) {
    case NodeState::Recording: return "recording";
    case NodeState::Sending:   return "sending";
    default:                   return "idle";
  }
}
