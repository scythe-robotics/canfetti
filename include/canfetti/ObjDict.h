#pragma once

#include <array>
#include <map>
#include <tuple>
#include <type_traits>
#include <utility>
#include "canfetti/OdData.h"
#include "canfetti/System.h"

namespace canfetti {

class ObjDict {
 public:
  using OdTable = std::map<uint16_t, std::map<uint8_t, OdEntry>>;

  void dumpTable();
  Error registerCallback(uint16_t idx, uint8_t subIdx, ChangedCallback cb);
  Error fireCallbacks(uint16_t idx, uint8_t subIdx);
  Error generation(uint16_t idx, uint8_t subIdx, uint32_t &generationOut);
  std::tuple<Error, OdProxy> makeProxy(uint16_t idx, uint8_t subIdx);

  template <typename T>
  Error get(uint16_t idx, uint8_t subIdx, T &v)
  {
    if (OdEntry *entry = lookup(idx, subIdx)) {
      if constexpr (std::is_same_v<T, OdBuffer>) {
        if (entry->lock()) {
          OdProxy src(idx, subIdx, entry->data);
          OdProxy dst(idx, subIdx, v);
          dst.copyFrom(src);
          entry->unlock();
          return Error::Success;
        }
        return Error::Timeout;
      }
      else {
        if (T *p = std::get_if<T>(&entry->data)) {
          if (entry->lock()) {
            v = *p;
            entry->unlock();
            return Error::Success;
          }
          return Error::Timeout;
        }
        else {
          return Error::ParamIncompatibility;
        }
      }
    }
    return Error::IndexNotFound;
  }

  template <typename T>
  Error set(uint16_t idx, uint8_t subIdx, const T &v)
  {
    if (OdEntry *entry = lookup(idx, subIdx)) {
      if constexpr (std::is_same_v<T, OdBuffer>) {
        if (entry->lock()) {
          OdProxy dst(idx, subIdx, entry->data);
          OdProxy src(idx, subIdx, v);
          dst.copyFrom(src);

          entry->unlock();
          entry->fireCallbacks();
          return Error::Success;
        }
        return Error::Timeout;
      }
      else {
        if (auto p = std::get_if<T>(&entry->data)) {
          if (entry->lock()) {
            *p = v;
            entry->unlock();
            entry->bumpGeneration();
            entry->fireCallbacks();
            return Error::Success;
          }
          return Error::Timeout;
        }
        else {
          return Error::ParamIncompatibility;
        }
      }
    }
    return Error::IndexNotFound;
  }

  template <typename T>
  CANFETTI_NO_INLINE Error insert(uint16_t idx, uint8_t subIdx, canfetti::Access access, T &&v, ChangedCallback cb = nullptr, bool cbOnInsert = false)
  {
    if (lookup(idx, subIdx)) {
      LogInfo("Warning: index %x[%d] already exists in the OD", idx, subIdx);
      return Error::Error;
    }

    buildSubEntry(idx, subIdx, access, std::forward<T>(v), cb);

    if (cbOnInsert) cb(idx, subIdx);
    return Error::Success;
  }

  // When it doesn't matter where the variable is mapped (i.e. RPDO values)
  template <typename T>
  std::tuple<Error, std::tuple<uint16_t, uint8_t>> autoInsert(canfetti::Access access, T &&v, ChangedCallback cb = nullptr, uint16_t startIdx = 0x3500, uint16_t endIdx = 0x4000)
  {
    uint16_t subIdx = 0;

    for (; startIdx < endIdx; startIdx++) {
      if (lookup(startIdx, subIdx)) continue;
      buildSubEntry(startIdx, subIdx, access, std::forward<T>(v), cb);
      return std::make_tuple(Error::Success, std::make_tuple(startIdx, subIdx));
    }

    return std::make_tuple(Error::IndexNotFound, std::make_tuple(0, 0));
  }

  // Helper to insert an array reference
  template <typename T, size_t N>
  CANFETTI_NO_INLINE Error insert(uint16_t idx, canfetti::Access access, std::array<T, N> &v, ChangedCallback cb = nullptr)
  {
    if (lookup(idx, 0)) {
      LogInfo("Warning: index %x[%d] already exists in the OD", idx, 0);
      return Error::Error;
    }

    buildSubEntry(idx, 0, canfetti::Access::RO, _u8(N), cb);

    for (size_t i = 0; i < N; i++) {
      buildSubEntry(idx, i + 1, access, _p(v[i]), cb);
    }

    return Error::Success;
  }

  // Helper to insert an array reference
  template <typename T, size_t N>
  CANFETTI_NO_INLINE Error insert(uint16_t idx, canfetti::Access access, T (&v)[N], ChangedCallback cb = nullptr)
  {
    if (lookup(idx, 0)) {
      LogInfo("Warning: index %x[%d] already exists in the OD", idx, 0);
      return Error::Error;
    }

    buildSubEntry(idx, 0, canfetti::Access::RO, _u8(N), cb);

    for (size_t i = 0; i < N; i++) {
      buildSubEntry(idx, i + 1, access, _p(v[i]), cb);
    }

    return Error::Success;
  }

  inline bool entryExists(uint16_t idx, uint8_t subIdx)
  {
    return lookup(idx, subIdx) != nullptr;
  }

  inline size_t entrySize(uint16_t idx, uint8_t subIdx)
  {
    auto entry = lookup(idx, subIdx);
    return entry ? canfetti::size(entry->data) : 0;
  }

 protected:
  OdTable table;
  OdEntry *lookup(uint16_t idx, uint8_t subIdx);

  template <typename... Args>
  constexpr void buildSubEntry(uint16_t idx, uint8_t subIdx, Args &&...args)
  {
    table[idx].emplace(std::piecewise_construct, std::forward_as_tuple(subIdx), std::forward_as_tuple(idx, subIdx, std::forward<Args>(args)...));
  }
};

}  // namespace canfetti
