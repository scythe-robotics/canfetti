#pragma once
#include <array>
#include <unordered_map>
#include "Service.h"

namespace canfetti {

class EmcyService : public canfetti::Service {
 public:
  enum ErrorType : uint8_t {
    Generic              = 1 << 0,
    Current              = 1 << 1,
    Voltage              = 1 << 2,
    Temperature          = 1 << 3,
    Communication        = 1 << 4,
    ProfileSpecific      = 1 << 5,
    Reserved             = 1 << 6,
    ManufacturerSpecific = 1 << 7,
  };

  using EmcyCallback = std::function<void(uint8_t node, uint16_t error, std::array<uint8_t, 5> &specific)>;
  EmcyService(Node &co);

  canfetti::Error processMsg(const canfetti::Msg &msg);
  canfetti::Error sendEmcy(uint16_t error, uint32_t specific, ErrorType type);
  canfetti::Error sendEmcy(uint16_t error, std::array<uint8_t, 5> &specific, ErrorType type);
  canfetti::Error clearEmcy(uint16_t error, ErrorType type);
  canfetti::Error registerCallback(EmcyCallback cb);

 private:
  EmcyCallback cb = nullptr;
  std::unordered_map<uint16_t, size_t> errorHistory;
  uint8_t setErrorReg(ErrorType type);
  uint8_t clearErrorReg(ErrorType type);
};

}  // namespace canfetti
