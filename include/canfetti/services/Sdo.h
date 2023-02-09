#pragma once
#include <memory>
#include <unordered_map>
#include "Service.h"
#include "sdo/Client.h"
#include "sdo/Server.h"

namespace canfetti {

class SdoService : public Service {
 public:
  static constexpr uint32_t DefaultSegmentXferTimeoutMs = 50;
  using FinishCallback                                  = std::function<void(Error err)>;

  SdoService(Node &co);
  Error init();
  Error processMsg(const Msg &msg);
  Error clientTransaction(bool read, uint8_t node, uint16_t idx, uint8_t subIdx,
                          OdVariant &data, uint32_t segmentTimeout, FinishCallback cb);
  Error addSDOServer(uint16_t rxCobid, uint16_t txCobid, uint8_t clientId);
  Error addSDOClient(uint32_t txCobid, uint16_t rxCobid, uint8_t serverId);
  size_t getActiveTransactionCount();

 private:
  struct TransactionState {
    std::shared_ptr<Sdo::Protocol> protocol;
    System::TimerHdl timer;
    unsigned generation;
    FinishCallback cb;
  };

  void transactionTimeout(unsigned generation, uint16_t key);
  void removeTransaction(uint16_t key);
  Error addSdoEntry(uint16_t paramIdx, uint16_t clientToServer, uint16_t serverToClient, uint8_t node);
  Error syncServices();
  std::unordered_map<uint16_t, TransactionState> activeTransactions;
  std::unordered_map<uint16_t, std::tuple<uint16_t, uint8_t>> servers;
};

}  // namespace canfetti
