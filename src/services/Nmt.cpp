#include "canfetti/services/Nmt.h"

using namespace canfetti;

NmtService::NmtService(Node &co) : Service(co)
{
}

canfetti::Error NmtService::addRemoteStateCb(uint8_t node, RemoteStateCb cb)
{
  for (auto &&x : slaveStateCbs) {
    if (!x.cb) {
      x.cb   = cb;
      x.node = node;
      return Error::Success;
    }
  }

  return Error::OutOfMemory;
}

void NmtService::notifyRemoteStateCbs(uint8_t node, canfetti::State state)
{
  for (auto &&x : slaveStateCbs) {
    if (x.cb && (x.node == node || x.node == AllNodes)) {
      x.cb(node, state);
    }
  }
}

canfetti::Error NmtService::setHeartbeatPeriod(uint16_t periodMs)
{
  Error e = co.od.set(0x1017, 0, periodMs);

  if (e == canfetti::Error::IndexNotFound) {
    e = co.od.insert(
        0x1017, 0, Access::RW, periodMs, [&](uint16_t idx, uint8_t subIdx) {
          co.sys.deleteTimer(hbTimer);
          if (uint16_t newTime; co.od.get(idx, subIdx, newTime) == Error::Success) {
            LogInfo("Setting heartbeat period to %d ms", newTime);
            hbTimer = co.sys.schedulePeriodic(newTime, std::bind(&NmtService::sendHeartbeat, this));
          }
        },
        true);
  }

  return e;
}

canfetti::Error NmtService::setRemoteTimeout(uint8_t node, uint16_t timeoutMs)
{
  uint32_t value = node << 16 | timeoutMs;

  Error e = co.od.set(0x1016, 0, value);

  if (e == canfetti::Error::IndexNotFound) {
    e = co.od.insert(
        0x1016, 0, Access::RW, value, [&](uint16_t idx, uint8_t subIdx) {
          if (uint32_t v; co.od.get(idx, subIdx, v) == Error::Success) {
            uint8_t node  = v >> 16;
            uint32_t time = v & 0xffff;
            LogInfo("Setting heartbeat timeout for node %x to %d ms", node, time);

            unsigned gen = newGeneration();
            if (peerStates.find(node) == peerStates.end()) {
              peerStates[node].timer      = System::InvalidTimer;
              peerStates[node].state      = canfetti::State::Offline;
              peerStates[node].generation = gen;
              peerStates[node].timeoutMs  = time;
            }
            else {
              co.sys.deleteTimer(peerStates[node].timer);
              peerStates[node].generation = gen;
            }

            if (time != 0) {
              peerStates[node].timer = co.sys.scheduleDelayed(time, std::bind(&NmtService::peerHeartbeatExpired, this, gen, node));
            }
          }
        },
        true);
  }

  return e;
}

void NmtService::resetNode()
{
}

void NmtService::resetComms()
{
}

canfetti::Error NmtService::setRemoteState(uint8_t node, canfetti::SlaveState state)
{
  uint8_t d[] = {static_cast<uint8_t>(state), node};

  canfetti::Msg m = {
      .id   = 0x000u,
      .rtr  = false,
      .len  = 2,
      .data = d,
  };

  return co.bus.write(m);
}

std::tuple<canfetti::Error, canfetti::State> NmtService::getRemoteState(uint8_t node)
{
  if (auto i = peerStates.find(node); i != peerStates.end()) {
    return std::make_tuple(Error::Success, i->second.state);
  }

  return std::make_tuple(Error::IndexNotFound, canfetti::State::Offline);
}

canfetti::Error NmtService::processMsg(const canfetti::Msg &msg)
{
  if (msg.len != 2) {
    LogInfo("Invalid NMT received (len: %d)", msg.len);
    return canfetti::Error::Error;
  }

  uint8_t node = msg.data[1];
  uint8_t raw  = msg.data[0];

  if (node && node != co.nodeId) {
    return canfetti::Error::Success;
  }

  if (raw == canfetti::SlaveState::GoOperational) {
    co.setState(canfetti::State::Operational);
  }
  else if (raw == canfetti::SlaveState::GoPreOperational) {
    co.setState(canfetti::State::PreOperational);
  }
  else if (raw == canfetti::SlaveState::Stop) {
    co.setState(canfetti::State::Stopped);
  }
  else if (raw == canfetti::SlaveState::ResetComms) {
    // Todo
    co.setState(canfetti::State::PreOperational);
  }
  else if (raw == canfetti::SlaveState::ResetNode) {
    // Todo
    co.setState(canfetti::State::PreOperational);
  }
  else {
    LogInfo("Invalid nmt state received: %x", raw);
    return canfetti::Error::Error;
  }

  return canfetti::Error::Success;
}

void NmtService::peerHeartbeatExpired(unsigned generation, uint8_t node)
{
  if (auto i = peerStates.find(node); i != peerStates.end()) {
    // Was the timer invalidated before the callback fired?
    if (i->second.generation != generation) return;

    i->second.state = canfetti::State::Offline;
    co.sys.deleteTimer(i->second.timer);
    i->second.generation = newGeneration();
    notifyRemoteStateCbs(node, canfetti::State::Offline);
  }
}

canfetti::Error NmtService::sendHeartbeat()
{
  uint8_t s = co.getState();

  canfetti::Msg m = {
      .id   = 0x700u + co.nodeId,
      .rtr  = false,
      .len  = 1,
      .data = &s,
  };

  return co.bus.write(m);
}

canfetti::Error NmtService::processHeartbeat(const canfetti::Msg &msg)
{
  if (msg.len != 1) {
    LogInfo("Invalid heartbeat received (len: %d)", msg.len);
    return canfetti::Error::Error;
  }

  uint8_t raw = msg.data[0];
  canfetti::State s;

  if (raw == canfetti::State::Bootup) {
    s = canfetti::State::Bootup;
  }
  else if (raw == canfetti::State::Operational) {
    s = canfetti::State::Operational;
  }
  else if (raw == canfetti::State::PreOperational) {
    s = canfetti::State::PreOperational;
  }
  else if (raw == canfetti::State::Stopped) {
    s = canfetti::State::Stopped;
  }
  else {
    LogInfo("Invalid nmt state received: %x", raw);
    return canfetti::Error::Error;
  }

  uint8_t node = msg.getNode();

  if (auto i = peerStates.find(node); i != peerStates.end()) {
    i->second.generation = newGeneration();
    co.sys.deleteTimer(i->second.timer);
    if (i->second.timeoutMs != 0) {
      i->second.timer = co.sys.scheduleDelayed(i->second.timeoutMs, std::bind(&NmtService::peerHeartbeatExpired, this, i->second.generation, node));
    }

    if (i->second.state != s) {
      i->second.state = s;
      notifyRemoteStateCbs(node, s);
    }
  }
  else {
    // A new node on the network that we dont have a heartbeat timeout for.  Track it
    peerStates[node].state      = s;
    peerStates[node].timer      = System::InvalidTimer;
    peerStates[node].timeoutMs  = 0;
    peerStates[node].generation = newGeneration();
    notifyRemoteStateCbs(node, s);
  }

  return canfetti::Error::Success;
}
