#include "canfetti/ObjDict.h"

using namespace std;
using namespace canfetti;

//******************************************************************************
// ObjDict
//******************************************************************************
OdEntry *ObjDict::lookup(uint16_t idx, uint8_t subIdx)
{
  if (auto subIdxs = table.find(idx); subIdxs != table.end()) {
    if (auto entry = subIdxs->second.find(subIdx); entry != subIdxs->second.end()) {
      return &entry->second;
    }
  }

  return nullptr;
}

Error ObjDict::registerCallback(uint16_t idx, uint8_t subIdx, ChangedCallback cb)
{
  if (auto entry = lookup(idx, subIdx)) {
    entry->addCallback(std::move(cb));
    return Error::Success;
  }

  return Error::IndexNotFound;
}

Error ObjDict::fireCallbacks(uint16_t idx, uint8_t subIdx)
{
  if (auto entry = lookup(idx, subIdx)) {
    entry->fireCallbacks();
    return Error::Success;
  }

  return Error::IndexNotFound;
}

Error ObjDict::generation(uint16_t idx, uint8_t subIdx, uint32_t& generationOut)
{
  if (auto entry = lookup(idx, subIdx)) {
    generationOut = entry->generation();
    return Error::Success;
  }

  return Error::IndexNotFound;
}

std::tuple<Error, OdProxy> ObjDict::makeProxy(uint16_t idx, uint8_t subIdx)
{
  OdEntry *entry = lookup(idx, subIdx);

  if (entry != nullptr) {
    if (entry->lock()) {
      return make_tuple(Error::Success, OdProxy(idx, subIdx, *entry));
    }
    return make_tuple(Error::DataXferLocal, OdProxy());
  }

  return make_tuple(Error::IndexNotFound, OdProxy());
}

void ObjDict::dumpTable()
{
  for (auto &idx : table) {
    LogInfo("%x:", idx.first);

    for (auto &subIdx : idx.second) {
      auto f = [&](auto &&arg) {
        using T       = std::decay_t<decltype(arg)>;
        const char *a = subIdx.second.access == canfetti::Access::RO ? "RO" : subIdx.second.access == canfetti::Access::RW ? "RW" : "WO";

        if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
          LogInfo("    %d: (%s) u8[%zu]", subIdx.first, a, arg.size());
        }
        else if constexpr (std::is_same_v<T, std::string>) {
          LogInfo("    %d: (%s) \"%s\"", subIdx.first, a, arg.c_str());
        }
        else if constexpr (std::is_same_v<T, canfetti::OdBuffer>) {
          LogInfo("    %d: (%s) %s", subIdx.first, a, "Proxy");
        }
        else if constexpr (std::is_same_v<T, canfetti::OdDynamicVar>) {
          LogInfo("    %d: (%s) %s", subIdx.first, a, "Dynamic");
        }
        else if constexpr (std::is_same_v<T, float>) {
          LogInfo("    %d: (%s) %f", subIdx.first, a, (double)arg);
        }
        else {
          LogInfo("    %d: (%s) 0x%x", subIdx.first, a, (unsigned)arg);
        }
      };

      std::visit(f, subIdx.second.data);
    }
  }
}
