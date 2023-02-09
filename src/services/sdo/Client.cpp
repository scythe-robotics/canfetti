#include "canfetti/services/sdo/Client.h"
#include <cstring>

using namespace canfetti::Sdo;

static inline bool isExpedited(const canfetti::Msg &m) { return (m.data[0] >> 1) & 0b1; }
static inline bool isUploadResponse(const canfetti::Msg &m) { return (m.data[0] >> 5) == 2; }
static inline bool isUploadSegResponse(const canfetti::Msg &m) { return (m.data[0] >> 5) == 0; }
static inline bool isDownloadResponse(const canfetti::Msg &m) { return (m.data[0] >> 5) == 3; }
static inline bool isDownloadSegResponse(const canfetti::Msg &m) { return (m.data[0] >> 5) == 1; }
static inline bool isDownloadBlockResponse(const canfetti::Msg &m) { return (m.data[0] >> 5) == 5; }

std::tuple<canfetti::Error, std::shared_ptr<Client>> Client::initiateRead(uint16_t idx, uint8_t subIdx,
                                                                          OdVariant &data, uint16_t txCobid, Node &co)
{
  LogDebug("Initiating read to cobid %x: %x[%d]", txCobid, idx, subIdx);

  OdProxy proxy(idx, subIdx, data);

  uint8_t payload[8] = {
      static_cast<uint8_t>(2 << 5),
      static_cast<uint8_t>(proxy.idx & 0xFF),
      static_cast<uint8_t>(proxy.idx >> 8),
      proxy.subIdx,
      0, 0, 0, 0};

  auto err = co.bus.write(txCobid, payload);
  auto ptr = err == Error::Success ? std::make_shared<Client>(txCobid, std::move(proxy), co) : nullptr;
  return std::make_tuple(err, ptr);
}

std::tuple<canfetti::Error, std::shared_ptr<Client>> Client::initiateWrite(uint16_t idx, uint8_t subIdx,
                                                                           OdVariant &data, uint16_t txCobid, Node &co)
{
  OdProxy proxy(idx, subIdx, data);

  if (proxy.remaining() < BlockModeThreshold) {
    LogDebug("Initiating write to cobid %x: %x[%d]", txCobid, proxy.idx, proxy.subIdx);

    uint8_t payload[8] = {
        static_cast<uint8_t>(1 << 5),
        static_cast<uint8_t>(proxy.idx & 0xFF),
        static_cast<uint8_t>(proxy.idx >> 8),
        proxy.subIdx,
        0, 0, 0, 0};

    if (auto len = proxy.remaining(); len > 0 && len <= 4) {  // Expedited transfer
      payload[0] |= 0b11;                                     // expedited, size indicated
      payload[0] |= (4 - len) << 2;                           // size = 4-n

      if (canfetti::Error e = proxy.copyInto(&payload[4], proxy.remaining()); e != canfetti::Error::Success) {
        return std::make_tuple(e, nullptr);
      }
    }
    else {                 // Segmented transfer
      payload[0] |= 0b01;  // !expedited, size indicated
      uint32_t l = proxy.remaining();
      memcpy(&payload[4], &l, sizeof(l));
    }

    auto err = co.bus.write(txCobid, payload);
    auto ptr = err == Error::Success ? std::make_shared<Client>(txCobid, std::move(proxy), co) : nullptr;
    return std::make_tuple(err, ptr);
  }
  else {
    LogDebug("Initiating block write to cobid %x: %x[%d]", txCobid, proxy.idx, proxy.subIdx);

    uint8_t payload[8] = {
        6 << 5 | 1 << 1,  // Size indicated, no crc, initiate download request
        static_cast<uint8_t>(proxy.idx & 0xFF),
        static_cast<uint8_t>(proxy.idx >> 8),
        proxy.subIdx};

    uint32_t l = proxy.remaining();
    memcpy(&payload[4], &l, sizeof(l));

    auto err = co.bus.write(txCobid, payload);
    auto ptr = err == Error::Success ? std::make_shared<Client>(txCobid, std::move(proxy), co) : nullptr;
    return std::make_tuple(err, ptr);
  }
}

void Client::blockSegmentWrite(uint8_t seqno)
{
  uint8_t payload[8] = {0};
  uint32_t len       = proxy.remaining();
  bool complete      = len <= 7;
  lastBlockBytes     = complete ? len : 7;

  payload[0] = (complete << 7) | seqno;

  if (canfetti::Error e = proxy.copyInto(&payload[1], lastBlockBytes); e != canfetti::Error::Success) {
    LogInfo("Error writing data: %x", (unsigned)e);
    finish(e);
  }

  co.bus.write(txCobid, payload);
}

void Client::segmentWrite()
{
  uint8_t payload[8] = {0};
  uint32_t len       = proxy.remaining();
  bool complete      = len <= 7;
  uint32_t toSend    = complete ? len : 7;

  payload[0] = (0 << 5) | (toggle << 4) | ((7 - toSend) << 1) | complete;
  toggle     = !toggle;

  if (canfetti::Error e = proxy.copyInto(&payload[1], toSend); e != canfetti::Error::Success) {
    LogInfo("Error writing data: %x", (unsigned)e);
    finish(e);
  }

  // LogInfo("(%d) %d %d", complete, proxy.len, toSend);

  co.bus.write(txCobid, payload);
}

void Client::segmentRead()
{
  uint8_t payload[8] = {0};
  payload[0]         = (3 << 5) | (toggle << 4);
  toggle             = !toggle;

  co.bus.write(txCobid, payload);
}

canfetti::Error Client::checkSize(uint32_t msgLen, bool tooBigCheck)
{
  if ((msgLen > proxy.remaining()) && !proxy.resize(msgLen)) {
    LogInfo("Supplied buf too small on %x[%d] [remote: %d > local: %ld]", proxy.idx, proxy.subIdx, msgLen, proxy.remaining());
    return canfetti::Error::ParamLengthLow;
  }
  else if (tooBigCheck && (msgLen < proxy.remaining()) && !proxy.resize(msgLen)) {
    LogInfo("Supplied buf too big on %x[%d] [remote: %d < local: %ld]", proxy.idx, proxy.subIdx, msgLen, proxy.remaining());
    return canfetti::Error::ParamLengthHigh;
  }

  return canfetti::Error::Success;
}

bool Client::processMsg(const canfetti::Msg &msg)
{
  if (Protocol::abortCheck(msg)) return true;

  if (isUploadResponse(msg)) {  // Read
    uint32_t msgLen = getInitiateDataLen(msg);

    if (isExpedited(msg)) {
      canfetti::Error e = checkSize(msgLen, true);
      if (e != canfetti::Error::Success) {
        finish(e, false);
      }
      else {
        e = proxy.copyFrom(&msg.data[4], msgLen);
        finish(e);
      }

      return true;
    }
    else {
      // Proactive resize
      canfetti::Error e = checkSize(msgLen, true);
      if (e != canfetti::Error::Success) {
        finish(e);
        return true;
      }

      segmentRead();
      return false;
    }
  }
  else if (isUploadSegResponse(msg)) {
    uint32_t len  = 7 - ((msg.data[0] >> 1) & 0b111);
    bool complete = msg.data[0] & 0b1;

    if (canfetti::Error e = checkSize(len, false); e != canfetti::Error::Success) {
      finish(e, !complete);
      return true;
    }
    else if (canfetti::Error e = proxy.copyFrom(&msg.data[1], len); e != canfetti::Error::Success) {
      LogInfo("Error reading data: %x", (unsigned)e);
      finish(e, !complete);
      return true;
    }
    else if (!complete) {
      segmentRead();
      return false;
    }

    finish(canfetti::Error::Success);
    return true;
  }
  else if (isDownloadResponse(msg) || isDownloadSegResponse(msg)) {  // Write
    if (!proxy.remaining()) {
      finish(canfetti::Error::Success);
      return true;
    }
    else {
      segmentWrite();
      return false;
    }
  }
  else if (isDownloadBlockResponse(msg)) {
    uint8_t ss = msg.data[0] & 3;
    if (!proxy.remaining()) {
      if (ss == 2) {
        uint8_t payload[8] = {
            static_cast<uint8_t>((6 << 5) | ((7 - lastBlockBytes) << 2)),
            0, 0, 0, 0, 0, 0, 0};
        co.bus.write(txCobid, payload);
        return false;
      }
      else if (ss == 1) {
        finish(canfetti::Error::Success, false);
        return true;
      }
    }
    else {
      for (uint32_t i = 1; i <= 127 && proxy.remaining(); ++i) {
        blockSegmentWrite(i);
      }
      return false;
    }
  }

  LogInfo("Unhandled SDO protocol: %x", msg.data[0]);
  finish(canfetti::Error::InvalidCmd);
  return true;
}
