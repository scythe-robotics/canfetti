#pragma once
#include <unordered_map>
#include <vector>
#include "Service.h"

namespace canfetti {

class PdoService : public Service {
 public:
  using TimeoutCb = std::function<void(uint16_t cobid)>;
  PdoService(Node &co);

  Error processMsg(const Msg &msg);
  Error enablePdoEvents();
  Error disablePdoEvents();
  Error sendTxPdo(uint16_t paramIdx, bool rtr = false);
  Error disablePdo(uint16_t paramIdx);
  Error enablePdo(uint16_t paramIdx);
  Error requestTxPdo(uint16_t cobid);
  Error sendAllTpdos();
  Error addRPDO(uint16_t cobid, const std::tuple<uint16_t, uint8_t> *mapping, size_t numMapping, uint16_t timeoutMs, PdoService::TimeoutCb cb);
  Error addTPDO(uint16_t pdoNum, uint16_t cobid, const std::tuple<uint16_t, uint8_t> *mapping, size_t numMapping, uint16_t periodMs, bool enabled);
  Error updateTpdoEventTime(uint16_t paramIdx, uint16_t periodMs);

 private:
  bool pdoEnabled = false;
  std::unordered_map<uint16_t, System::TimerHdl> tpdoTimers;
  std::unordered_map<uint16_t, std::tuple<System::TimerHdl, unsigned /* generation */, uint16_t /* periodMs */, TimeoutCb>> rpdoTimers;
  void enableTpdoEvent(uint16_t idx);
  void enableRpdoEvent(uint16_t idx);
  void rpdoTimeout(unsigned generation, uint16_t idx);
  Error addPdoEntry(uint16_t paramIdx, uint32_t cobid, uint16_t eventTime,
                    const std::tuple<uint16_t, uint8_t> *mapping, size_t numMapping, bool enabled, bool rtrAllowed, canfetti::ChangedCallback changedCb);
};

}  // namespace canfetti
