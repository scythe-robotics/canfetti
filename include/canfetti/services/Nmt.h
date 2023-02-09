#pragma once
#include <array>
#include <tuple>
#include <unordered_map>
#include "Service.h"

namespace canfetti {

class NmtService : public canfetti::Service {
 public:
  using RemoteStateCb               = std::function<void(uint8_t node, canfetti::State)>;
  static constexpr uint8_t AllNodes = 0xFF;

  NmtService(Node &co);

  canfetti::Error setHeartbeatPeriod(uint16_t periodMs);
  canfetti::Error addRemoteStateCb(uint8_t node, RemoteStateCb cb);
  canfetti::Error setRemoteTimeout(uint8_t node, uint16_t timeoutMs);
  canfetti::Error sendHeartbeat();
  canfetti::Error processMsg(const canfetti::Msg &msg);
  canfetti::Error processHeartbeat(const canfetti::Msg &msg);
  canfetti::Error setRemoteState(uint8_t node, canfetti::SlaveState state);
  std::tuple<canfetti::Error, canfetti::State> getRemoteState(uint8_t node);

 private:
  struct NodeState {
    canfetti::State state;
    System::TimerHdl timer;
    uint16_t timeoutMs;
    unsigned generation;
  };

  struct NodeStateCb {
    uint8_t node;
    RemoteStateCb cb;
  };

  System::TimerHdl hbTimer = System::InvalidTimer;
  std::array<NodeStateCb, 4> slaveStateCbs;
  std::unordered_map<uint8_t, NodeState> peerStates;

  void peerHeartbeatExpired(unsigned generation, uint8_t node);
  void resetNode();
  void resetComms();
  void notifyRemoteStateCbs(uint8_t node, canfetti::State state);
};

}  // namespace canfetti
