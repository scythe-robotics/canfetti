
#pragma once

#include "FlexCAN_T4/FlexCAN_T4.h"
#include "canfetti/LocalNode.h"
#include "canfetti/System.h"

namespace canfetti {

class TyCoDev : public canfetti::CanDevice {
 public:
  canfetti::Error init(uint32_t baudrate, FlexCAN_T4 *busInstance);
  canfetti::Error write(const Msg &msg, bool async = false) override;
  canfetti::Error writePriority(const Msg &msg) override;

  inline bool read(CAN_message_t &m) { return bus->readMB(m); }
  inline size_t newDroppedRx() { return bus->readAndClearDroppedRx(); }

 private:
  FlexCAN_T4 *bus;
};

class TyCo : public canfetti::LocalNode {
 public:
  TyCo(TyCoDev &d, uint8_t nodeId, const char *deviceName, uint32_t deviceType = 0);
  void service();

  template <typename T>
  Error blockingRead(uint8_t node, uint16_t idx, uint8_t subIdx, T &data, uint32_t segmentTimeout = SdoService::DefaultSegmentXferTimeoutMs)
  {
    return blockingTransaction(true, node, idx, subIdx, data, segmentTimeout);
  }

  template <typename T>
  Error blockingWrite(uint8_t node, uint16_t idx, uint8_t subIdx, T &&data, uint32_t segmentTimeout = SdoService::DefaultSegmentXferTimeoutMs)
  {
    return blockingTransaction(false, node, idx, subIdx, std::forward<T>(data), segmentTimeout);
  }

  template <typename T>
  Error blockingTransaction(bool read, uint8_t node, uint16_t idx, uint8_t subIdx, T &&data, uint32_t segmentTimeout)
  {
    bool done    = false;
    Error result = Error::Error;
    OdVariant val(data);

    Error initErr = sdo.clientTransaction(read, node, idx, subIdx, val, segmentTimeout, [&](Error e) {
      done   = true;
      result = e;
    });
    if (initErr != Error::Success) return initErr;

    while (!done) {
      service();
    }

    if constexpr (!std::is_same_v<std::decay_t<T>, OdBuffer>) {
      data = std::move(*std::get_if<std::decay_t<T>>(&val));
    }
    return result;
  }

 private:
  TyCoDev &dev;
  System sys;
};

}  // namespace canfetti
