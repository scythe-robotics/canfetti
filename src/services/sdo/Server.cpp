#include "canfetti/services/sdo/Server.h"
#include <cstring>
#include "canfetti/services/sdo/ServerBlockMode.h"

using namespace canfetti;
using namespace canfetti::Sdo;

static inline bool isExpedited(const canfetti::Msg &m) { return (m.data[0] >> 1) & 0b1; }

static inline bool isUploadInitiate(const canfetti::Msg &m) { return (m.data[0] >> 5) == 2; }
static inline bool isUploadSegRequest(const canfetti::Msg &m) { return (m.data[0] >> 5) == 3; }

static inline bool isDownloadInitiate(const canfetti::Msg &m) { return (m.data[0] >> 5) == 1; }
static inline bool isDownloadSegRequest(const canfetti::Msg &m) { return (m.data[0] >> 5) == 0; }

static void sendDownloadInitRsp(uint16_t txCobid, uint16_t idx, uint8_t subIdx, OdProxy &proxy, CanDevice &bus)
{
  uint8_t payload[8] = {
      3 << 5,
      static_cast<uint8_t>(proxy.idx & 0xFF),
      static_cast<uint8_t>(proxy.idx >> 8),
      proxy.subIdx,
  };

  bus.write(txCobid, payload);
}

bool Server::sendUploadInitRsp(uint16_t txCobid, uint16_t idx, uint8_t subIdx, OdProxy &proxy, CanDevice &bus)
{
  uint8_t payload[8] = {
      2 << 5,
      static_cast<uint8_t>(proxy.idx & 0xFF),
      static_cast<uint8_t>(proxy.idx >> 8),
      proxy.subIdx,
  };

  size_t remaining = proxy.remaining();

  bool expedited = remaining > 0 && remaining <= 4;

  if (expedited) {                       // Expedited transfer
    payload[0] |= 0b11;                  // expedited, size indicated
    payload[0] |= (4 - remaining) << 2;  // size = 4-n
    if (Error err = proxy.copyInto(&payload[4], remaining); err != Error::Success) {
      abort(err, txCobid, idx, subIdx, bus);
      return true;
    }
  }
  else {                 // Segmented transfer
    payload[0] |= 0b01;  // !expedited, size indicated
    uint32_t l = remaining;
    memcpy(&payload[4], &l, 4);
  }

  bus.write(txCobid, payload);
  return expedited;
}

std::shared_ptr<Server> Server::processInitiate(const Msg &msg, uint16_t txCobid, Node &co)
{
  uint16_t idx   = (msg.data[2] << 8) | msg.data[1];
  uint8_t subIdx = msg.data[3];

  if (isUploadInitiate(msg)) {  // read from us
    auto [err, proxy] = co.od.makeProxy(idx, subIdx);

    if (err != Error::Success) {
      LogInfo("Bad SDO read for cobid %x: %x[%d], err %x", msg.id, idx, subIdx, (unsigned)err);
      abort(err, txCobid, idx, subIdx, co.bus);
    }
    else if (!sendUploadInitRsp(txCobid, idx, subIdx, proxy, co.bus)) {
      return std::make_shared<Server>(txCobid, std::move(proxy), co);
    }
  }
  else if (isDownloadInitiate(msg)) {  // write to us
    auto [err, proxy] = co.od.makeProxy(idx, subIdx);

    if (err != Error::Success) {
      LogInfo("Bad SDO write for cobid %x: %x[%d], err %x", msg.id, idx, subIdx, (unsigned)err);
      abort(err, txCobid, idx, subIdx, co.bus);
    }
    else {
      uint32_t len = getInitiateDataLen(msg);

      if (proxy.remaining() != len && !proxy.resize(len)) {
        LogDebug("Buf too small for write");
        abort(Error::ParamLength, txCobid, idx, subIdx, co.bus);
      }
      else if (isExpedited(msg)) {
        if (Error err = proxy.copyFrom(&msg.data[4], len); err != Error::Success) {
          abort(err, txCobid, idx, subIdx, co.bus);
        }
        else {
          sendDownloadInitRsp(txCobid, idx, subIdx, proxy, co.bus);
        }
      }
      else {  // Non expedited
        sendDownloadInitRsp(txCobid, idx, subIdx, proxy, co.bus);
        if (proxy.remaining()) {
          return std::make_shared<Server>(txCobid, std::move(proxy), co);
        }
      }
    }
  }
  else if (ServerBlockMode::isDownloadBlockMsg(msg)) {
    uint32_t size     = *(uint32_t *)&msg.data[4];
    auto [err, proxy] = co.od.makeProxy(idx, subIdx);
    if (err == canfetti::Error::Success) {
      auto server = std::make_shared<ServerBlockMode>(txCobid, std::move(proxy), co, size);
      server->sendInitiateResponse();
      return server;
    }
  }

  return nullptr;
}

canfetti::Error Server::initiateRead()
{
  LogDebug("Initiating read to cobid %x: %x[%d]", txCobid, proxy.idx, proxy.subIdx);

  uint8_t payload[8] = {
      2 << 5,
      static_cast<uint8_t>(proxy.idx & 0xFF),
      static_cast<uint8_t>(proxy.idx >> 8),
      proxy.subIdx,
      0,
      0,
      0,
      0,
  };

  return co.bus.write(txCobid, payload);
}

canfetti::Error Server::initiateWrite()
{
  LogDebug("Initiating write to cobid %x: %x[%d]", txCobid, proxy.idx, proxy.subIdx);

  uint8_t payload[8] = {
      1 << 5,
      static_cast<uint8_t>(proxy.idx & 0xFF),
      static_cast<uint8_t>(proxy.idx >> 8),
      proxy.subIdx,
  };

  if (proxy.remaining() <= 4) {                  // Expedited transfer
    payload[0] |= 0b11;                          // expedited, size indicated
    payload[0] |= (4 - proxy.remaining()) << 2;  // size = 4-n

    if (Error err = proxy.copyInto(&payload[4], proxy.remaining()); err != Error::Success) {
      abort(err, txCobid, proxy.idx, proxy.subIdx, co.bus);
      return err;
    }
  }
  else {                 // Segmented transfer
    payload[0] |= 0b01;  // !expedited, size indicated
    uint32_t l = proxy.remaining();
    memcpy(&payload[4], &l, 4);
  }

  return co.bus.write(txCobid, payload);
}

bool Server::segmentRead()
{
  uint8_t payload[8] = {0};
  bool complete      = proxy.remaining() <= 7;
  uint32_t toSend    = complete ? proxy.remaining() : 7;

  payload[0] = (0 << 5) | (toggle << 4) | ((7 - toSend) << 1) | complete;
  toggle     = !toggle;

  if (Error err = proxy.copyInto(&payload[1], toSend); err != Error::Success) {
    finish(err, true);
    return true;
  }

  // LogInfo("(%d) %d %d", complete, proxy.remaining(), toSend);

  co.bus.write(txCobid, payload);

  if (complete) finish(canfetti::Error::Success);
  return complete;
}

void Server::segmentWrite()
{
  uint8_t payload[8] = {0};
  payload[0]         = (1 << 5) | (toggle << 4);
  toggle             = !toggle;

  canfetti::Msg msg = {
      .id   = txCobid,
      .rtr  = false,
      .len  = 8,
      .data = payload,
  };

  co.bus.write(msg);
}

bool Server::processMsg(const canfetti::Msg &msg)
{
  if (Protocol::abortCheck(msg)) return true;

  if (isUploadSegRequest(msg)) {  // read us
    return segmentRead();
  }
  else if (isDownloadSegRequest(msg)) {  // write us
    uint32_t len  = 7 - ((msg.data[0] >> 1) & 0b111);
    bool complete = msg.data[0] & 0b1;

    if (len > proxy.remaining()) {
      LogInfo("Supplied buf too small");
      finish(canfetti::Error::ParamLength);
      return true;
    }

    if (Error err = proxy.copyFrom(&msg.data[1], len); err != Error::Success) {
      abort(err, txCobid, proxy.idx, proxy.subIdx, co.bus);
      return true;
    }

    // LogInfo("(%d) %d %d", complete, proxy.remaining(), len);

    segmentWrite();

    if (complete) {
      finish(canfetti::Error::Success);
    }

    return complete;
  }

  LogInfo("Unhandled SDO protocol: %x", msg.data[0]);
  finish(canfetti::Error::InvalidCmd);
  return true;
}
