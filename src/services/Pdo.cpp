#include "canfetti/services/Pdo.h"
#include <optional>

using namespace canfetti;

static constexpr size_t MAX_MAPPINGS = 8;  // 1 per payload byte in classic CAN

//******************************************************************************
// Private Helpers
//******************************************************************************

static inline uint32_t canIdMask(uint32_t cobid)
{
  return cobid & ((1 << 29) - 1);
}

static inline bool isDisabled(uint32_t cobid)
{
  return !!(cobid & (1 << 31));
}

static inline bool isRtrAllowed(uint32_t cobid)
{
  return !(cobid & (1 << 30));
}

static bool isEventDriven(ObjDict &od, uint16_t paramIdx)
{
  uint8_t transmissionType = 0;

  if (od.get(paramIdx, 2, transmissionType) != canfetti::Error::Success) {
    assert(0 && "OD not configured properly");
  }

  // Event-driven  means  that  the  PDO  may  be  received  at  any  time.
  // The  CANopen  device will actualize the data immediately.

  // 0xFE - event-driven (manufacturer-specific)
  // 0xFF - event-driven (device-profile and application profile specific)
  return transmissionType == 0xFE || transmissionType == 0xFF;
}

//******************************************************************************
// Public API
//******************************************************************************

PdoService::PdoService(Node &co) : Service(co)
{
}

Error PdoService::addPdoEntry(uint16_t paramIdx, uint32_t cobid, uint16_t eventTime,
                              const std::tuple<uint16_t, uint8_t> *mapping, size_t numMapping, bool enabled, bool rtrAllowed, canfetti::ChangedCallback changedCb)
{
  if (co.od.entryExists(paramIdx, 0) || numMapping > 0xFFu) {
    return Error::Error;
  }

  LogDebug("Adding PDO %x (Cobid: %x, Timer: %d, En: %d, RTR: %d)", paramIdx, cobid, eventTime, enabled, rtrAllowed);

  cobid |= ((!enabled) << 31) | ((!rtrAllowed) << 30);

  uint16_t mappingIdx = paramIdx + 0x200;

  // Create Params entry
  co.od.insert(paramIdx, 0, canfetti::Access::RO, _u8(5));
  co.od.insert(paramIdx, 1, canfetti::Access::RO, cobid, changedCb);
  co.od.insert(paramIdx, 2, canfetti::Access::RO, _u8(0xFE));             // Transmission type
  co.od.insert(paramIdx, 3, canfetti::Access::RO, _u16(0));               // Inhibit time
  co.od.insert(paramIdx, 4, canfetti::Access::RO, _u8(0));                // unused
  co.od.insert(paramIdx, 5, canfetti::Access::RW, eventTime, changedCb);  // Event timer

  // Create Mapping entry
  size_t subIdx = 0;
  co.od.insert(mappingIdx, subIdx++, canfetti::Access::RO, _u8(numMapping));

  for (size_t i = 0; i < numMapping; ++i) {
    uint16_t refIdx   = std::get<0>(mapping[i]);
    uint8_t refSubIdx = std::get<1>(mapping[i]);

    assert(co.od.entryExists(refIdx, refSubIdx) && "Variable for PDO doesnt exist");

    uint32_t val = (refIdx << 16) | (refSubIdx << 8) | ((co.od.entrySize(refIdx, refSubIdx) * 8) & 0xff);
    co.od.insert(mappingIdx, subIdx++, canfetti::Access::RO, val);
  }

  return Error::Success;
}

void PdoService::enableTpdoEvent(uint16_t tpdoIdx)
{
  if (!pdoEnabled) return;

  if (uint32_t cobid; co.od.get(tpdoIdx, 1, cobid) == canfetti::Error::Success) {
    if (uint16_t period; co.od.get(tpdoIdx, 5, period) == canfetti::Error::Success) {
      // sync PDOs not currently supported
      if (isEventDriven(co.od, tpdoIdx) && !isDisabled(cobid) && period) {
        uint16_t busCobid = canIdMask(cobid);
        // Async because nothing can observe the return value
        auto hdl          = co.sys.schedulePeriodic(period, std::bind(&PdoService::sendTxPdo, this, tpdoIdx, /* async */ true, /* rtr */ false));
        auto t            = tpdoTimers.find(busCobid);

        // Update existing timer
        if (t != tpdoTimers.end()) {
          co.sys.deleteTimer(t->second);
          t->second = std::move(hdl);
          LogInfo("Updated event-driven TPDO %x @ %d ms", tpdoIdx, period);
        }
        // Create new timer
        else {
          tpdoTimers.emplace(std::make_pair(busCobid, std::move(hdl)));
          LogDebug("Created event-driven TPDO %x @ %d ms", tpdoIdx, period);
        }
      }
    }
  }
}

void PdoService::enableRpdoEvent(uint16_t rpdoIdx)
{
  if (!pdoEnabled) return;

  if (uint32_t cobid; co.od.get(rpdoIdx, 1, cobid) == canfetti::Error::Success) {
    if (uint16_t period; co.od.get(rpdoIdx, 5, period) == canfetti::Error::Success) {
      // sync PDOs not currently supported
      if (isEventDriven(co.od, rpdoIdx) && !isDisabled(cobid) && period) {
        uint16_t busCobid = canIdMask(cobid);
        auto t            = rpdoTimers.find(busCobid);

        if (t != rpdoTimers.end()) {
          auto &[timer, gen, storedPeriod, cb] = t->second;
          gen                                  = newGeneration();
          storedPeriod                         = period;

          if (timer != System::InvalidTimer) {
            co.sys.deleteTimer(timer);
            timer = System::InvalidTimer;
          }

          if (cb) {
            timer = co.sys.scheduleDelayed(period, std::bind(&PdoService::rpdoTimeout, this, gen, busCobid));
          }

          LogInfo("Updated event-driven RPDO %x @ %d ms", rpdoIdx, period);
        }
      }
    }
  }
}

void PdoService::rpdoTimeout(unsigned generation, uint16_t idx)
{
  if (auto r = rpdoTimers.find(idx); r != rpdoTimers.end()) {
    auto &[timer, gen, periodMs, cb] = r->second;
    (void)timer;  // Silence unused variable warning
    (void)periodMs;

    // Was the timer invalidated before the callback fired?
    if (generation != gen) return;

    if (cb) {
      cb(idx);
    }
  }
}

Error PdoService::addRPDO(uint16_t cobid, const std::tuple<uint16_t, uint8_t> *mapping, size_t numMapping, uint16_t timeoutMs, PdoService::TimeoutCb cb)
{
  const uint16_t MaxPdoEntries     = 512;
  const uint16_t StartRpdoParamIdx = 0x1400;

  uint16_t i = 0;
  while (co.od.entryExists(StartRpdoParamIdx + i, 0) && i < MaxPdoEntries) {
    i++;
  }

  auto err = addPdoEntry(StartRpdoParamIdx + i, cobid, timeoutMs, mapping, numMapping, true, false, std::bind(&PdoService::enableRpdoEvent, this, std::placeholders::_1));

  if (err == Error::Success && cb) {
    assert(rpdoTimers.find(cobid) == rpdoTimers.end() && "Multiple RPDO timers arent supported");
    rpdoTimers.emplace(std::make_pair(cobid, std::make_tuple(System::InvalidTimer, newGeneration(), timeoutMs, cb)));
    enableRpdoEvent(StartRpdoParamIdx + i);
  }

  return err;
}

Error PdoService::addTPDO(uint16_t pdoNum, uint16_t cobid, const std::tuple<uint16_t, uint8_t> *mapping, size_t numMapping, uint16_t periodMs, bool enabled)
{
  uint16_t paramIdx = 0x1800 + pdoNum;

  auto err = addPdoEntry(paramIdx, cobid, periodMs, mapping, numMapping, enabled, true, std::bind(&PdoService::enableTpdoEvent, this, std::placeholders::_1));
  if (err == Error::Success) {
    configuredTPDONums.push_back(pdoNum);
    enableTpdoEvent(paramIdx);
  }

  return err;
}

Error PdoService::updateTpdoEventTime(uint16_t paramIdx, uint16_t periodMs)
{
  return co.od.set(paramIdx, 5, periodMs);
}

Error PdoService::disablePdo(uint16_t paramIdx)
{
  uint32_t cobid;

  if (co.od.get(paramIdx, 1, cobid) != canfetti::Error::Success) {
    LogDebug("Invalid PDO idx %x", paramIdx);
    return canfetti::Error::IndexNotFound;
  }

  LogInfo("Disabling TPDO %x, cobid: %x", paramIdx, cobid);
  cobid |= 1 << 31;

  return co.od.set(paramIdx, 1, cobid);
}

Error PdoService::enablePdo(uint16_t paramIdx)
{
  uint32_t cobid;

  if (co.od.get(paramIdx, 1, cobid) != canfetti::Error::Success) {
    LogDebug("Invalid PDO idx %x", paramIdx);
    return canfetti::Error::IndexNotFound;
  }

  LogInfo("Enabling TPDO %x, cobid: %x", paramIdx, cobid);
  cobid &= ~(1 << 31);

  return co.od.set(paramIdx, 1, cobid);
}

canfetti::Error PdoService::sendTxPdo(uint16_t paramIdx, bool async, bool rtr)
{
  constexpr uint16_t mappingOffset = 0x200;
  uint16_t mappingIdx              = paramIdx + mappingOffset;
  uint8_t numMappings;
  uint32_t cobid;
  uint8_t d[8];

  if (co.od.get(paramIdx, 1, cobid) != canfetti::Error::Success) {
    return canfetti::Error::IndexNotFound;
  }

  if (isDisabled(cobid)) {
    return canfetti::Error::DataXfer;
  }

  canfetti::Msg msg = {.id = canIdMask(cobid), .rtr = rtr, .len = 0, .data = d};

  if (co.od.get(mappingIdx, 0, numMappings) != canfetti::Error::Success) {
    return canfetti::Error::IndexNotFound;
  }

  if (numMappings > MAX_MAPPINGS) {
    return canfetti::Error::InternalError;
  }

  {
    std::optional<OdProxy> proxies[MAX_MAPPINGS];

    // Create all proxies up front so we don't partially fill the payload if some entries are locked
    for (size_t i = 0; i < numMappings; ++i) {
      uint32_t map;
      if (Error e = co.od.get(mappingIdx, i + 1, map); e != canfetti::Error::Success) {
        LogInfo("Failed to look up TPDO mapping at %x[%zx] for TPDO 0x%03x: error 0x%08x", mappingIdx, i + 1, msg.id, e);
        return e;
      }
      uint16_t valIdx   = (map >> 16) & 0xffff;
      uint8_t valSubIdx = (map >> 8) & 0xff;
      if (auto [e, p] = co.od.makeProxy(valIdx, valSubIdx); e == Error::Success) {
        proxies[i].emplace(std::move(p));
      }
      else {
        LogInfo("Failed to make proxy at %x[%x] for TPDO 0x%03x: error 0x%08x", valIdx, valSubIdx, msg.id, e);
        return e;
      }
    }

    uint8_t *pdoData = msg.data;
    for (size_t i = 0; i < numMappings; ++i) {
      uint8_t maxPdoRemaining = 8 - (pdoData - msg.data);
      auto len                = proxies[i]->remaining();
      assert(len <= maxPdoRemaining);  // Catch programming errors.  PDOs can only have 8 bytes
      (void)maxPdoRemaining;
      if (Error e = proxies[i]->copyInto(pdoData, len); e != Error::Success) {
        LogInfo("Failed to read OD at %x[%x] for TPDO 0x%03x: error 0x%08x", proxies[i]->idx, proxies[i]->subIdx, msg.id, e);
        return e;
      }
      pdoData += len;
      msg.len += len;
    }
  }

  return co.bus.write(msg, async);
}

canfetti::Error PdoService::enablePdoEvents()
{
  uint32_t cfgCobid;
  disablePdoEvents();

  pdoEnabled = true;

  // TPDO timers
  for (auto pdoNum : configuredTPDONums) {
    uint16_t paramIdx = 0x1800 + pdoNum;
    enableTpdoEvent(paramIdx);
  }

  // RPDO timers
  for (uint16_t paramIdx = 0x1400; co.od.get(paramIdx, 1, cfgCobid) == canfetti::Error::Success; paramIdx++) {
    enableRpdoEvent(paramIdx);
  }

  return canfetti::Error::Success;
}

canfetti::Error PdoService::disablePdoEvents()
{
  pdoEnabled = false;

  LogInfo("Disabling all TPDOs");

  for (auto t : tpdoTimers) {
    co.sys.deleteTimer(t.second);
  }
  tpdoTimers.clear();

  for (auto &r : rpdoTimers) {
    auto &[timer, gen, periodMs, cb] = r.second;
    (void)periodMs;  // Silence unused variable warning
    (void)cb;
    gen = newGeneration();
    co.sys.deleteTimer(timer);
  }

  return canfetti::Error::Success;
}

Error PdoService::requestTxPdo(uint16_t cobid)
{
  canfetti::Msg msg = {.id = cobid, .rtr = true, .len = 0, .data = nullptr};
  return co.bus.write(msg);
}

Error PdoService::sendAllTpdos()
{
  uint32_t cfgCobid;
  for (auto pdoNum : configuredTPDONums) {
    uint16_t tpdoParamIdx = 0x1800 + pdoNum;
    if (co.od.get(tpdoParamIdx, 1, cfgCobid) != canfetti::Error::Success) continue;

    if (isDisabled(cfgCobid)) {
      continue;
    }

    if (Error e = sendTxPdo(tpdoParamIdx); e != Error::Success) {
      return e;
    }
  }

  return Error::Success;
}

Error PdoService::processMsg(const canfetti::Msg &msg)
{
  constexpr uint16_t mappingOffset = 0x200;
  uint32_t cfgCobid;

  if (co.getState() != canfetti::State::Operational) return canfetti::Error::Success;

  if (msg.rtr) {
    for (auto pdoNum : configuredTPDONums) {
      uint16_t tpdoParamIdx = 0x1800 + pdoNum;

      if (co.od.get(tpdoParamIdx, 1, cfgCobid) != canfetti::Error::Success) continue;
      if (canIdMask(cfgCobid) != msg.id) continue;

      if (isDisabled(cfgCobid)) {
        LogInfo("Ignoring RTR on TPDO %x because it is disabled", canIdMask(cfgCobid));
        break;
      }
      else if (!isRtrAllowed(cfgCobid)) {
        LogInfo("Ignoring RTR on TPDO %x because RTR is not allowed", canIdMask(cfgCobid));
        break;
      }

      return sendTxPdo(tpdoParamIdx, /* async */ false, /* rtr */ true);
    }
  }
  else {
    for (uint16_t rpdoParamIdx = 0x1400; co.od.get(rpdoParamIdx, 1, cfgCobid) == canfetti::Error::Success; rpdoParamIdx++) {
      uint16_t busCobid = canIdMask(cfgCobid);
      if (busCobid != msg.id) {
        continue;
      }

      if (isDisabled(cfgCobid)) {
        LogDebug("Ignoring RPDO because it is disabled");
        continue;
      }

      uint8_t numMappings     = 0;
      uint16_t rpdoMappingIdx = rpdoParamIdx + mappingOffset;

      if (co.od.get(rpdoMappingIdx, 0, numMappings) != canfetti::Error::Success) {
        assert(0 && "OD not configured properly");
        continue;
      }

      // LogInfo("RPDO %x", msg.id);

      if (!isEventDriven(co.od, rpdoParamIdx)) {
        LogInfo("Only event driven PDO are supported");
        continue;
      }

      if (numMappings > MAX_MAPPINGS) {
        LogInfo("Too many mappings");
        continue;
      }

      std::tuple<uint16_t, uint8_t> entries[MAX_MAPPINGS];

      {
        std::optional<OdProxy> proxies[MAX_MAPPINGS];

        // Create all proxies up front so we don't partially apply the payload if some entries are locked
        for (size_t i = 0; i < numMappings; ++i) {
          uint32_t map;
          if (Error e = co.od.get(rpdoMappingIdx, i + 1, map); e != Error::Success) {
            LogInfo("Failed to look up RPDO mapping at %x[%zx] for RPDO 0x%03x: error 0x%08x", rpdoMappingIdx, i + 1, busCobid, e);
            return e;
          }
          uint16_t valIdx   = (map >> 16) & 0xffff;
          uint8_t valSubIdx = (map >> 8) & 0xff;
          entries[i]        = {valIdx, valSubIdx};
          if (auto [e, p] = co.od.makeProxy(valIdx, valSubIdx); e == Error::Success) {
            proxies[i].emplace(std::move(p));
          }
          else {
            LogInfo("Failed to make proxy at %x[%x] for RPDO 0x%03x: error 0x%08x", valIdx, valSubIdx, busCobid, e);
            return e;
          }
        }

        uint8_t *pdoData = msg.data;
        for (size_t i = 0; i < numMappings; ++i) {
          assert(pdoData - msg.data < 8);   // Catch programming errors.  PDOs can only have 8 bytes
          proxies[i]->suppressCallbacks();  // Defer until mapped entries are unlocked; allow callbacks to access them
          auto len = proxies[i]->remaining();
          if (Error e = proxies[i]->copyFrom(pdoData, len); e != Error::Success) {
            LogInfo("Failed to write OD at %x[%x] for RPDO 0x%03x: error 0x%08x", proxies[i]->idx, proxies[i]->subIdx, busCobid, e);
            return e;
          }
          pdoData += len;
        }
      }

      // Reset timer if active
      if (auto r = rpdoTimers.find(busCobid); r != rpdoTimers.end()) {
        auto &[timer, gen, periodMs, cb] = r->second;
        (void)cb;  // Silence unused variable warning
        gen = newGeneration();
        if (timer != System::InvalidTimer) {
          co.sys.deleteTimer(timer);
          timer = co.sys.scheduleDelayed(periodMs, std::bind(&PdoService::rpdoTimeout, this, gen, busCobid));
        }
      }

      // Fire callbacks after timer reset in case they mess with it
      for (size_t i = 0; i < numMappings; ++i) {
        auto [idx, subIdx] = entries[i];
        if (Error e = co.od.fireCallbacks(idx, subIdx); e != Error::Success) {
          LogInfo("Failed to fire callbacks for %x[%x]: error 0x%08x", idx, subIdx, e);
          return e;
        }
      }
    }
  }

  return canfetti::Error::Success;
}
