#pragma once
#include <memory>
#include <tuple>
#include "Protocol.h"

namespace canfetti::Sdo {

class Client : public Protocol {
 public:
  using Protocol::Protocol;

  static std::tuple<canfetti::Error, std::shared_ptr<Client>> initiateRead(uint16_t idx, uint8_t subIdx,
                                                                           OdVariant &data, uint16_t txCobid, Node &co);
  static std::tuple<canfetti::Error, std::shared_ptr<Client>> initiateWrite(uint16_t idx, uint8_t subIdx,
                                                                            OdVariant &data, uint16_t txCobid, Node &co);

  bool processMsg(const canfetti::Msg &msg);

 private:
  uint8_t lastBlockBytes;
  canfetti::Error checkSize(uint32_t msgLen, bool tooBigCheck);
  void segmentWrite();
  void segmentRead();
  void blockSegmentWrite(uint8_t seqno);
};
}  // namespace canfetti::Sdo
