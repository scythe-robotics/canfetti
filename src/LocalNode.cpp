#include "canfetti/LocalNode.h"

using namespace canfetti;

LocalNode::LocalNode(CanDevice &d, System &sys, uint8_t nodeId, const char *deviceName, uint32_t deviceType)
    : Node(d, sys, nodeId), nmt(*this), pdo(*this), sdo(*this), emcy(*this), deviceName(deviceName), deviceType(deviceType)
{
}

Error LocalNode::init()
{
  using namespace canfetti::StaticDataTypeIndex;

  if (Error e = nmt.init(); e != Error::Success) {
    return e;
  }
  if (Error e = pdo.init(); e != Error::Success) {
    return e;
  }
  if (Error e = sdo.init(); e != Error::Success) {
    return e;
  }
  if (Error e = emcy.init(); e != Error::Success) {
    return e;
  }

  //
  // Create default entries
  //

  // Supported Static Data Types
  if (Error e = od.insert(std::get<0>(Integer8), std::get<1>(Integer8), canfetti::Access::RW, _s8(0)); e != Error::Success) {
    return e;
  }

  if (Error e = od.insert(std::get<0>(Integer16), std::get<1>(Integer16), canfetti::Access::RW, _s16(0)); e != Error::Success) {
    return e;
  }

  if (Error e = od.insert(std::get<0>(Integer32), std::get<1>(Integer32), canfetti::Access::RW, _s32(0)); e != Error::Success) {
    return e;
  }

  if (Error e = od.insert(std::get<0>(Unsigned8), std::get<1>(Unsigned8), canfetti::Access::RW, _u8(0)); e != Error::Success) {
    return e;
  }

  if (Error e = od.insert(std::get<0>(Unsigned16), std::get<1>(Unsigned16), canfetti::Access::RW, _u16(0)); e != Error::Success) {
    return e;
  }

  if (Error e = od.insert(std::get<0>(Unsigned32), std::get<1>(Unsigned32), canfetti::Access::RW, _u32(0)); e != Error::Success) {
    return e;
  }

  if (Error e = od.insert(std::get<0>(Real32), std::get<1>(Real32), canfetti::Access::RW, _f32(0)); e != Error::Success) {
    return e;
  }

  // Device type
  if (Error e = od.insert(0x1000, 0, canfetti::Access::RO, deviceType); e != Error::Success) {
    return e;
  }

  // canfetti::Error register
  if (Error e = od.insert(0x1001, 0, canfetti::Access::RW, _u32(0)); e != Error::Success) {
    return e;
  }

  // Device name
  if (Error e = od.insert(0x1008, 0, canfetti::Access::RO, deviceName); e != Error::Success) {
    return e;
  }

  return Error::Success;
}

void LocalNode::processFrame(const Msg &msg)
{
  switch (msg.getFunction()) {
    case 0x000:
      nmt.processMsg(msg);
      break;

    case 0x080:
      if (msg.id == 0x080) {
        // Sync isn't used.
      }
      else {
        emcy.processMsg(msg);
      }
      break;

    case 0x100:
      // Timestamp
      break;

    case 0x180:
    case 0x200:
    case 0x280:
    case 0x300:
    case 0x380:
    case 0x400:
    case 0x480:
    case 0x500:
      pdo.processMsg(msg);
      break;

    case 0x580:
    case 0x600:
      sdo.processMsg(msg);
      break;

    case 0x700:
      nmt.processHeartbeat(msg);
      break;

    case 0x7E4:
    case 0x7E5:
      // LSS
      break;

    default:
      LogInfo("Unknown cobid received: %x", msg.id);
      break;
  }
}

Error LocalNode::setState(State s)
{
  if (s == state) return Error::Success;

  switch (s) {
    case State::Operational:
      LogInfo("State: Operational");
      pdo.enablePdoEvents();
      break;

    case State::Bootup:
      LogInfo("State: Bootup");
      pdo.disablePdoEvents();
      break;

    case State::Stopped:
      LogInfo("State: Stopped");
      pdo.disablePdoEvents();
      break;

    case State::PreOperational:
      LogInfo("State: PreOp");
      pdo.disablePdoEvents();
      break;

    default:
      break;
  }

  state = s;
  return Error::Success;
}
