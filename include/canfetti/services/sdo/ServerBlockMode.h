#pragma once
#include "Server.h"

namespace canfetti::Sdo {

class ServerBlockMode : public Server {
 public:
  static inline bool isDownloadBlockMsg(const canfetti::Msg &m) { return (m.data[0] >> 5) == 6; }

  ServerBlockMode(uint16_t txCobid,
                  canfetti::OdProxy proxy,
                  Node &co,
                  uint32_t totalsize);

  bool processMsg(const canfetti::Msg &msg);
  void sendInitiateResponse();

 private:
  const uint8_t NumSubBlockSegments = 127;

  enum State {
    Start,
    SubBlock,
    End,
  };

  uint32_t totalsize;
  State state = State::Start;
  uint8_t lastSegmentData[7];
  uint8_t expectedSeqNo;
};
}  // namespace canfetti::Sdo