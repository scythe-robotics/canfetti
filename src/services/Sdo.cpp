#include "canfetti/services/Sdo.h"

using namespace canfetti;
using namespace Sdo;

SdoService::SdoService(Node &co) : Service(co)
{
}

Error SdoService::init()
{
  if (Error e = Service::init(); e != Error::Success) {
    return e;
  }

  serverSegmentTimeoutMs = DefaultSegmentXferTimeoutMs;

  // Mandatory default SDO server
  return addSDOServer(0x600 + co.nodeId, 0x580 + co.nodeId, 0);
}

Error SdoService::syncServices()
{
  uint8_t node;
  servers.clear();

  for (uint16_t clientIdx = 0x1200; co.od.get(clientIdx, 3, node) == Error::Success; clientIdx++) {
    uint16_t clientToServer, serverToClient;
    if (co.od.get(clientIdx, 1, clientToServer) != Error::Success || co.od.get(clientIdx, 2, serverToClient) != Error::Success) {
      LogInfo("Malformed OD: %x", clientIdx);
      break;
    }

    servers.emplace(clientToServer, std::make_tuple(serverToClient, node));
  }

  return Error::Success;
}

Error SdoService::addSdoEntry(uint16_t paramIdx, uint16_t clientToServer, uint16_t serverToClient, uint8_t node)
{
  while (co.od.entryExists(paramIdx, 0)) {
    paramIdx++;
  }

  LogInfo("Adding SDO %x, [c: %x, s: %x, n: %x]", paramIdx, clientToServer, serverToClient, node);

  // Create Params entry
  co.od.insert(paramIdx, 0, Access::RO, _u8(3));
  co.od.insert(paramIdx, 1, Access::RO, clientToServer);
  co.od.insert(paramIdx, 2, Access::RO, serverToClient);
  co.od.insert(paramIdx, 3, Access::RO, node);

  return Error::Success;
}

Error SdoService::addSDOServer(uint16_t rxCobid, uint16_t txCobid, uint8_t clientId)
{
  Error err = addSdoEntry(0x1200, rxCobid, txCobid, clientId);

  if (err == Error::Success) {
    err = syncServices();
  }

  return err;
}

Error SdoService::addSDOClient(uint32_t txCobid, uint16_t rxCobid, uint8_t serverId)
{
  Error err = addSdoEntry(0x1280, txCobid, rxCobid, serverId);

  if (err == Error::Success) {
    err = syncServices();
  }

  return err;
}

Error SdoService::clientTransaction(bool read, uint8_t remoteNode, uint16_t idx, uint8_t subIdx,
                                    OdVariant &data, uint32_t segmentTimeout, FinishCallback cb)
{
  uint8_t node;

  for (uint16_t clientIdx = 0x1280; co.od.get(clientIdx, 3, node) == Error::Success; clientIdx++) {
    if (node != remoteNode) {
      continue;
    }

    uint16_t clientToServer, serverToClient;
    if (co.od.get(clientIdx, 1, clientToServer) != Error::Success || co.od.get(clientIdx, 2, serverToClient) != Error::Success) {
      LogInfo("Malformed OD: %x", clientIdx);
      break;
    }

    if (auto i = activeTransactions.find(serverToClient); i != activeTransactions.end()) {
      LogInfo("Transaction already in progress for node: %d", node);
      return Error::Error;
    }

    auto [err, client] = read ? Client::initiateRead(idx, subIdx, data, clientToServer, co) : Client::initiateWrite(idx, subIdx, data, clientToServer, co);

    if (client) {
      unsigned gen           = newGeneration();
      TransactionState state = {
          .protocol   = client,
          .timer      = co.sys.scheduleDelayed(segmentTimeout, std::bind(&SdoService::transactionTimeout, this, gen, serverToClient)),
          .generation = gen,
          .cb         = cb,
      };
      auto [i, success] = activeTransactions.emplace(serverToClient, state);
      (void)i;  // Silence unused variable warning
      if (!success) err = Error::Error;
    }

    return err;
  }

  LogInfo("No SDO client found for node: %d", remoteNode);
  return Error::Error;
}

void SdoService::transactionTimeout(unsigned generation, uint16_t key)
{
  if (auto i = activeTransactions.find(key); i != activeTransactions.end()) {
    auto &state = i->second;
    // Was the timer invalidated before the callback fired?
    if (state.generation != generation) return;

    state.protocol->finish(Error::Timeout, true);
    removeTransaction(key);
  }
}

void SdoService::removeTransaction(uint16_t key)
{
  if (auto i = activeTransactions.find(key); i != activeTransactions.end()) {
    auto &state          = i->second;
    auto cb              = state.cb;
    auto [finished, err] = state.protocol->isFinished();

    if (!finished) {
      LogInfo("*** Removing a transaction that wasn't finished?? ***");
    }

    co.sys.deleteTimer(state.timer);
    activeTransactions.erase(i);

    if (cb) cb(finished ? err : Error::InternalError);
  }
}

Error SdoService::processMsg(const Msg &msg)
{
  if (msg.len != 8) {
    LogInfo("SDO not 8 bytes");
    return Error::ParamLength;
  }

  if (auto &&c = activeTransactions.find(msg.id); c != activeTransactions.end()) {
    if (c->second.protocol->processMsg(msg)) {
      removeTransaction(msg.id);
    }
    else {
      c->second.generation = newGeneration();
      co.sys.deleteTimer(c->second.timer);
      c->second.timer = co.sys.scheduleDelayed(serverSegmentTimeoutMs, std::bind(&SdoService::transactionTimeout, this, c->second.generation, msg.id));
    }
  }
  else if (auto &&s = servers.find(msg.id); s != servers.end()) {
    auto [tx, node] = s->second;
    (void)node;  // Silence unused variable warning
    auto server = Server::processInitiate(msg, tx, co);

    if (server) {
      unsigned gen           = newGeneration();
      TransactionState state = {
          .protocol   = server,
          .timer      = co.sys.scheduleDelayed(serverSegmentTimeoutMs, std::bind(&SdoService::transactionTimeout, this, gen, msg.id)),
          .generation = gen,
          .cb         = nullptr,
      };
      auto [i, success] = activeTransactions.emplace(msg.id, state);
      (void)i;  // Silence unused variable warning
      if (!success) {
        return Error::Error;
      }
    }
  }

  return Error::Success;
}

size_t SdoService::getActiveTransactionCount()
{
  return activeTransactions.size();
}
