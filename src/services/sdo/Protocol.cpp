#include "canfetti/services/sdo/Protocol.h"
#include <cstring>
#include <tuple>

using namespace canfetti;
using namespace Sdo;

Protocol::Protocol(uint16_t txCobid, OdProxy proxy, Node &co)
    : txCobid(txCobid), proxy(std::move(proxy)), co(co)
{
}

Protocol::~Protocol()
{
}

void Protocol::abort(canfetti::Error status, uint16_t txCobid, uint16_t idx, uint8_t subIdx, CanDevice &bus)
{
  uint8_t payload[8] = {
      4 << 5,
      static_cast<uint8_t>(idx & 0xFF),
      static_cast<uint8_t>(idx >> 8),
      subIdx,
  };

  uint32_t err = static_cast<uint32_t>(status);
  memcpy(&payload[4], &err, 4);

  canfetti::Msg msg = {
      .id   = txCobid,
      .rtr  = false,
      .len  = 8,
      .data = payload,
  };

  bus.write(msg);
}

void Protocol::finish(Error status, bool sendAbort)
{
  if (sendAbort && status != Error::Success) {
    LogInfo("Sending abort (%x) at %x", (unsigned)status, txCobid);
    abort(status, txCobid, proxy.idx, proxy.subIdx, co.bus);
  }
  finished       = true;
  finishedStatus = status;
}

std::tuple<bool, canfetti::Error> Protocol::isFinished()
{
  return std::make_tuple(finished, finishedStatus);
}

bool Protocol::abortCheck(const Msg &msg)
{
  if (isAbortMsg(msg)) {
    uint32_t abortCode;
    memcpy(&abortCode, &msg.data[4], 4);

    LogInfo("SDO %x[%d] aborted: %x", proxy.idx, proxy.subIdx, abortCode);

    // Todo bounds check https://stackoverflow.com/questions/4165439/generic-way-to-cast-int-to-enum-in-c
    finish(static_cast<Error>(abortCode), false);
    return true;
  }

  return false;
}

uint32_t Protocol::getInitiateDataLen(const Msg &m)
{
  uint8_t es = m.data[0] & 0b11;
  switch (es) {
    case 0b01: {
      uint32_t d;
      memcpy(&d, &m.data[4], 4);
      return d;
    }

    case 0b11: {
      uint8_t n = (m.data[0] >> 2) & 0b11;
      return 4 - n;
    }

    case 0b10:
      return 0;  // Unspecified number of bytes

    default:
      LogInfo("Invalid SDO cmd: %x", m.data[0]);
      break;
  }

  return 0;
}
