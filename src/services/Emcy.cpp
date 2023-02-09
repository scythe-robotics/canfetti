#include "canfetti/services/Emcy.h"
#include <cstring>

using namespace canfetti;

EmcyService::EmcyService(Node &co) : Service(co)
{
}

uint8_t EmcyService::setErrorReg(ErrorType type)
{
  uint8_t errorReg = 0;

  co.od.get(0x1001, 0, errorReg);
  errorReg |= type;
  co.od.set(0x1001, 0, errorReg);
  return errorReg;
}

uint8_t EmcyService::clearErrorReg(ErrorType type)
{
  uint8_t errorReg = 0;

  co.od.get(0x1001, 0, errorReg);
  errorReg &= ~type;
  co.od.set(0x1001, 0, errorReg);
  return errorReg;
}

canfetti::Error EmcyService::sendEmcy(uint16_t error, uint32_t specific, ErrorType type)
{
  std::array<uint8_t, 5> arr;
  memcpy(arr.begin(), &specific, 4);
  arr[4] = 0;

  return sendEmcy(error, arr, type);
}

canfetti::Error EmcyService::sendEmcy(uint16_t error, std::array<uint8_t, 5> &specific, ErrorType type)
{
  uint8_t errorReg   = setErrorReg(type);
  uint8_t payload[8] = {
      static_cast<uint8_t>(error & 0xff),
      static_cast<uint8_t>((error >> 8) & 0xff),
      errorReg,
  };
  memcpy(&payload[3], specific.begin(), 5);

  errorHistory[error]++;
  return co.bus.write(0x080 | co.nodeId, payload, true);
}

canfetti::Error EmcyService::clearEmcy(uint16_t error, ErrorType type)
{
  if (auto i = errorHistory.find(error); i != errorHistory.end()) {
    auto &[err, cnt] = *i;

    if (--cnt == 0) {
      errorHistory.erase(i);
      uint16_t errorReg = clearErrorReg(type);

      if (errorReg == 0) {
        uint8_t payload[8] = {0};
        return co.bus.write(0x080 | co.nodeId, payload, true);
      }
    }
  }

  return canfetti::Error::Success;
}

canfetti::Error EmcyService::registerCallback(EmcyCallback cb)
{
  if (this->cb) return Error::Error;  // Only allow 1 for now
  this->cb = cb;
  return Error::Success;
}

canfetti::Error EmcyService::processMsg(const canfetti::Msg &msg)
{
  if (msg.len != 8 || msg.rtr) {
    LogInfo("Invalid Emcy received (len: %d, rtr: %d)", msg.len, msg.rtr);
    return canfetti::Error::Error;
  }

  uint16_t errCode = (msg.data[1] << 8) | msg.data[0];
  //uint8_t errRegister = msg.data[2];

  std::array<uint8_t, 5> specific;
  memcpy(specific.begin(), &msg.data[3], 5);

  if (cb) cb(msg.getNode(), errCode, specific);

  return canfetti::Error::Success;
}
