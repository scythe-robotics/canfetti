#pragma once
#include <type_traits>
#include "CanDevice.h"
#include "Node.h"
#include "ObjDict.h"
#include "Types.h"
#include "canfetti/System.h"
#include "services/Emcy.h"
#include "services/Nmt.h"
#include "services/Pdo.h"
#include "services/Sdo.h"

namespace canfetti {

class LocalNode : public Node {
 public:
  using Node::od;

  Error init();
  inline void setSDOServerTimeout(uint32_t timeoutMs) { sdo.setServerSegmentTimeout(timeoutMs); }
  inline size_t getActiveTransactionCount() { return sdo.getActiveTransactionCount(); }
  inline Error addSDOServer(uint16_t rxCobid, uint16_t txCobid, uint8_t clientId) { return sdo.addSDOServer(rxCobid, txCobid, clientId); }
  inline Error addSDOClient(uint32_t txCobid, uint16_t rxCobid, uint8_t serverId) { return sdo.addSDOClient(txCobid, rxCobid, serverId); }
  inline Error addSDOServer(uint8_t sdoId, uint8_t remoteNode) { return sdo.addSDOServer(0x600 + sdoId, 0x580 + sdoId, remoteNode); }
  inline Error addSDOClient(uint8_t sdoId, uint8_t remoteNode) { return sdo.addSDOClient(0x600 + sdoId, 0x580 + sdoId, remoteNode); }
  inline Error triggerTPDO(uint16_t pdoNum, bool async = false) { return pdo.sendTxPdo(0x1800 + pdoNum, async); }
  inline Error triggerAllTPDOs() { return pdo.sendAllTpdos(); }
  inline Error updateTpdoEventTime(uint16_t pdoNum, uint16_t periodMs) { return pdo.updateTpdoEventTime(0x1800 + pdoNum, periodMs); }
  inline Error disableTPDO(uint16_t pdoNum) { return pdo.disablePdo(0x1800 + pdoNum); }
  inline Error enableTPDO(uint16_t pdoNum) { return pdo.enablePdo(0x1800 + pdoNum); }
  inline Error setHeartbeatPeriod(uint16_t periodMs) { return nmt.setHeartbeatPeriod(periodMs); }
  inline Error setRemoteTimeout(uint8_t node, uint16_t timeoutMs) { return nmt.setRemoteTimeout(node, timeoutMs); }
  inline Error addRPDO(uint16_t cobid, const std::tuple<uint16_t, uint8_t> *mapping, size_t numMapping, uint16_t timeoutMs = 0, PdoService::TimeoutCb cb = nullptr) { return pdo.addRPDO(cobid, mapping, numMapping, timeoutMs, cb); }
  inline Error addRPDO(uint16_t cobid, std::initializer_list<std::tuple<uint16_t, uint8_t>> mapping, uint16_t timeoutMs = 0, PdoService::TimeoutCb cb = nullptr) { return pdo.addRPDO(cobid, mapping.begin(), mapping.size(), timeoutMs, cb); }
  inline Error addTPDO(uint16_t pdoNum, uint16_t cobid, const std::tuple<uint16_t, uint8_t> *mapping, size_t numMapping, uint16_t periodMs = 0, bool enabled = true) { return pdo.addTPDO(pdoNum, cobid, mapping, numMapping, periodMs, enabled); }
  inline Error addTPDO(uint16_t pdoNum, uint16_t cobid, std::initializer_list<std::tuple<uint16_t, uint8_t>> mapping, uint16_t periodMs = 0, bool enabled = true) { return pdo.addTPDO(pdoNum, cobid, mapping.begin(), mapping.size(), periodMs, enabled); }
  inline Error sendEmcy(uint16_t error, uint32_t specific = 0, EmcyService::ErrorType type = EmcyService::ErrorType::Generic) { return emcy.sendEmcy(error, specific, type); }
  inline Error sendEmcy(uint16_t error, std::array<uint8_t, 5> &specific, EmcyService::ErrorType type = EmcyService::ErrorType::Generic) { return emcy.sendEmcy(error, specific, type); }
  inline Error clearEmcy(uint16_t error, EmcyService::ErrorType type = EmcyService::ErrorType::Generic) { return emcy.clearEmcy(error, type); }
  inline const char *getDeviceName() { return deviceName; }
  Error setState(State s);
  canfetti::Error registerEmcyCallback(EmcyService::EmcyCallback cb) { return emcy.registerCallback(cb); }

  template <typename... Args>
  canfetti::Error autoAddTPDO(uint16_t pdoNum, uint16_t cobid, uint16_t periodMs, Args &&...args)
  {
    std::vector<std::tuple<uint16_t, uint8_t>> map;
    _autoInsertAll(map, canfetti::Access::RO, std::forward<Args>(args)...);
    addTPDO(pdoNum, cobid, map.data(), map.size(), periodMs);
    return canfetti::Error::Success;
  }

  template <typename... Args>
  canfetti::Error autoAddRPDO(uint16_t cobid, Args &&...args)
  {
    std::vector<std::tuple<uint16_t, uint8_t>> map;
    _autoInsertAll(map, canfetti::Access::WO, std::forward<Args>(args)...);
    addRPDO(cobid, map.data(), map.size());
    return canfetti::Error::Success;
  }

  template <typename... Args>
  canfetti::Error autoAddTimeoutRPDO(uint16_t cobid, uint16_t timeoutMs, PdoService::TimeoutCb cb, Args &&...args)
  {
    std::vector<std::tuple<uint16_t, uint8_t>> map;
    _autoInsertAll(map, canfetti::Access::WO, std::forward<Args>(args)...);
    addRPDO(cobid, map.data(), map.size(), timeoutMs, cb);
    return canfetti::Error::Success;
  }

  // Todo refactor into a RemoteNode class
  inline std::tuple<canfetti::Error, canfetti::State> getRemoteState(uint8_t node) { return nmt.getRemoteState(node); }
  inline Error setRemoteState(uint8_t node, SlaveState state) { return nmt.setRemoteState(node, state); }
  Error registerRemoteStateCb(NmtService::RemoteStateCb cb) { return nmt.addRemoteStateCb(NmtService::AllNodes, cb); }
  Error registerRemoteStateCb(uint8_t node, NmtService::RemoteStateCb cb) { return nmt.addRemoteStateCb(node, cb); }

  template <typename T>
  inline Error read(uint8_t node, uint16_t idx, uint8_t subIdx, std::function<void(Error e, T &)> cb, uint32_t segmentTimeout = SdoService::DefaultSegmentXferTimeoutMs)
  {
    OdVariant *v = new OdVariant(T());

    if (!v) {
      return Error::OutOfMemory;
    }

    Error err = sdo.clientTransaction(true, node, idx, subIdx, *v, segmentTimeout, [=](Error e) {
      if (cb) cb(e, *std::get_if<T>(v));
      delete v;
    });

    if (err != Error::Success) {
      delete v;
    }
    return err;
  }

  template <typename T>
  inline Error readData(uint8_t node, uint16_t idx, uint8_t subIdx, T &&data, std::function<void(Error e)> cb, uint32_t segmentTimeout = SdoService::DefaultSegmentXferTimeoutMs)
  {
    OdVariant *v = new OdVariant(data);

    if (!v) {
      return Error::OutOfMemory;
    }

    Error err = sdo.clientTransaction(true, node, idx, subIdx, *v, segmentTimeout, [=](Error e) {
      if (cb) cb(e);
      delete v;
    });

    if (err != Error::Success) {
      delete v;
    }
    return err;
  }

  template <typename T>
  Error write(uint8_t node, uint16_t idx, uint8_t subIdx, T &&data, std::function<void(Error e)> cb, uint32_t segmentTimeout = SdoService::DefaultSegmentXferTimeoutMs)
  {
    OdVariant *v = new OdVariant(data);

    if (!v) {
      return Error::OutOfMemory;
    }

    Error err = sdo.clientTransaction(false, node, idx, subIdx, *v, segmentTimeout, [=](Error e) {
      if (cb) cb(e);
      delete v;
    });

    if (err != Error::Success) {
      delete v;
    }
    return err;
  }

 protected:
  LocalNode(CanDevice &d, System &sys, uint8_t nodeId, const char *deviceName, uint32_t deviceType);
  void processFrame(const Msg &m);

  template <typename Arg>
  canfetti::Error _autoInsertAll(std::vector<std::tuple<uint16_t, uint8_t>> &map, canfetti::Access access, Arg &&arg)
  {
    auto [err, idx] = od.autoInsert(access, std::forward<Arg>(arg));
    map.push_back(std::move(idx));
    return err;
  }

  template <typename Arg, typename... Args>
  canfetti::Error _autoInsertAll(std::vector<std::tuple<uint16_t, uint8_t>> &map, canfetti::Access access, Arg &&arg, Args &&...args)
  {
    auto [err, idx] = od.autoInsert(access, std::forward<Arg>(arg));
    map.push_back(std::move(idx));
    if (err == canfetti::Error::Success) return _autoInsertAll(map, access, std::forward<Args>(args)...);
    return err;
  }

  NmtService nmt;
  PdoService pdo;
  SdoService sdo;
  EmcyService emcy;
  const char *deviceName;
  uint32_t deviceType;
};

}  // namespace canfetti
