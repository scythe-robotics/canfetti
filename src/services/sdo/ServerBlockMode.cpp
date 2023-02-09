#include "canfetti/services/sdo/ServerBlockMode.h"
#include <cstring>

using namespace canfetti;
using namespace canfetti::Sdo;

//******************************************************************************
// Public API
//******************************************************************************

ServerBlockMode::ServerBlockMode(uint16_t txCobid,
                                 canfetti::OdProxy proxy,
                                 Node &co,
                                 uint32_t totalsize) : Server(txCobid, std::move(proxy), co)
{
}

void ServerBlockMode::sendInitiateResponse()
{
  uint8_t payload[8] = {
      static_cast<uint8_t>(5 << 5),
      static_cast<uint8_t>(proxy.idx & 0xFF),
      static_cast<uint8_t>(proxy.idx >> 8),
      proxy.subIdx,
      NumSubBlockSegments,
  };

  expectedSeqNo = 1;
  co.bus.write(txCobid, payload);
}

bool ServerBlockMode::processMsg(const canfetti::Msg &msg)
{
  switch (state) {
    case State::Start: {
      uint8_t seqNo = msg.data[0] & 0x7f;
      if (expectedSeqNo != seqNo) {
        LogInfo("Out of sequence frame (%x, %x)", expectedSeqNo, seqNo);
        finish(Error::DataXfer, true);
        state = State::End;
        return false;
      }

      memcpy(lastSegmentData, &msg.data[1], 7);
      state         = State::SubBlock;
      expectedSeqNo = 2;
      return false;
    }

    case State::SubBlock: {
      uint8_t c     = msg.data[0] >> 7;
      uint8_t seqNo = msg.data[0] & 0x7f;

      if (Error err = proxy.copyFrom(lastSegmentData, 7); err != Error::Success) {
        finish(err, true);
        state = State::End;
        return false;
      }

      if (expectedSeqNo != seqNo) {
        LogInfo("Out of sequence frame (%x, %x)", expectedSeqNo, seqNo);
        finish(Error::DataXfer, true);
        // TODO request a resend of the block by sending the last good seqno
        // uint8_t payload[8] = {
        //     static_cast<uint8_t>((5 << 5) | 2),
        //     expectedSeqNo - 1,
        //     NumSubBlockSegments,
        // };
        // co.bus.write(txCobid, payload);
        state = State::End;
        return false;
      }

      memcpy(lastSegmentData, &msg.data[1], 7);

      expectedSeqNo = seqNo + 1;

      if (c) {
        state = State::End;
      }

      if (seqNo == NumSubBlockSegments || c) {
        uint8_t payload[8] = {
            static_cast<uint8_t>((5 << 5) | 2),
            seqNo,
            NumSubBlockSegments,
        };

        expectedSeqNo = 1;
        co.bus.write(txCobid, payload);
      }

      return false;
    }

    case State::End: {
      if (!isDownloadBlockMsg(msg)) {
        // LogInfo("Junk %x", msg.data[0]);
        return false;
      }

      uint8_t n       = (msg.data[0] >> 2) & 0b111;
      uint8_t lastLen = 7 - n;

      if (!finished) {
        if (Error err = proxy.copyFrom(lastSegmentData, lastLen); err != Error::Success) {
          finish(err, true);
        }
      }

      {
        uint8_t payload[8] = {static_cast<uint8_t>((5 << 5) | 1)};
        co.bus.write(txCobid, payload);
        finish(Error::Success);
      }

      return true;
    }
  }

  return true;
}
