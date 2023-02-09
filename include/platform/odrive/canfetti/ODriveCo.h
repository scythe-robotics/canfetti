
#pragma once
#include <cmsis_os.h>
#include <thread>
#include "canfetti/LocalNode.h"
#include "canfetti/System.h"
#include "fibre/callback.hpp"
#include "interfaces/canbus.hpp"

namespace canfetti {

class ODriveCo : public canfetti::CanDevice, public canfetti::LocalNode {
 public:
  static constexpr const uint32_t DEFAULT_CAN_ID = 0x7f;

  struct Config_t {
    uint32_t id = DEFAULT_CAN_ID;
  };

  ODriveCo(CanBusBase* canbus);
  canfetti::Error write(const canfetti::Msg& msg);
  canfetti::Error writePriority(const canfetti::Msg& msg);
  void handle_can_message(const can_Message_t& msg);
  void init(fibre::Callback<std::optional<uint32_t>, float, fibre::Callback<void>> timer, fibre::Callback<bool, std::optional<uint32_t>&> timerCancel, uint32_t numPrioTxSlots);
  void initObjDict();
  bool apply_config();

  Config_t config_;

 private:
  System sys;
  CanBusBase::CanSubscription* canbusSubscription[2];
  CanBusBase* canbus;
  uint32_t txPrioSlot;
  uint32_t numTxPrioSlots;
};

}  // namespace canfetti
