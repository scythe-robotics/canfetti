#pragma once
#include <memory>
#include "Protocol.h"

namespace canfetti::Sdo {

class Server : public Protocol {
 public:
  using Protocol::Protocol;

  static std::shared_ptr<Server> processInitiate(const Msg &msg, uint16_t txCobid, Node &co);
  virtual bool processMsg(const canfetti::Msg &msg);
  canfetti::Error initiateRead();
  canfetti::Error initiateWrite();

 private:
  bool blockMode              = false;
  uint8_t receivedFistSegment = false;
  uint8_t lastSegmentData[7];
  void segmentWrite();
  bool segmentRead();
  static bool sendUploadInitRsp(uint16_t txCobid, uint16_t idx, uint8_t subIdx, OdProxy &proxy, CanDevice &bus);
};
}  // namespace canfetti::Sdo