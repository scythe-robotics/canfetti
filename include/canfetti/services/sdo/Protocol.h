#pragma once

#include "canfetti/CanDevice.h"
#include "canfetti/Node.h"
#include "canfetti/ObjDict.h"

namespace canfetti::Sdo {

class Protocol {
 public:
  static const size_t BlockModeThreshold = 100;

  Protocol(uint16_t txCobid, canfetti::OdProxy proxy, Node &co);
  virtual ~Protocol();

  virtual bool processMsg(const canfetti::Msg &msg) = 0;
  virtual void finish(canfetti::Error status, bool sendAbort = true);
  std::tuple<bool, canfetti::Error> isFinished();

 protected:
  static inline bool isAbortMsg(const Msg &m) { return (m.data[0] & (0b111 << 5)) == (4 << 5); }
  static uint32_t getInitiateDataLen(const canfetti::Msg &m);
  static void abort(canfetti::Error status, uint16_t txCobid, uint16_t idx, uint8_t subIdx, CanDevice &bus);

  bool abortCheck(const Msg &msg);

  bool finished                  = false;
  canfetti::Error finishedStatus = canfetti::Error::Success;
  uint16_t txCobid;
  bool toggle = false;
  canfetti::OdProxy proxy;
  Node &co;
};

}  // namespace canfetti::Sdo
