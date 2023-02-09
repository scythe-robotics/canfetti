#pragma once

#include <functional>
#include <string>
#include <type_traits>
#include <variant>
#include "Types.h"
#include "canfetti/System.h"

namespace canfetti {

struct OdBuffer {
  uint8_t *buf;
  size_t len;
  OdBuffer() = default;
  OdBuffer(void *buf, size_t len) : buf(static_cast<uint8_t *>(buf)), len(len) {}
  OdBuffer(const OdBuffer &o)
  {
    buf = o.buf;
    len = o.len;
  }
};

using OdDynamicVarCopyFrom    = std::function<Error(const uint16_t idx, const uint8_t subIdx, size_t off, uint8_t *buf, size_t s)>;
using OdDynamicVarCopyInto    = std::function<Error(const uint16_t idx, const uint8_t subIdx, size_t off, uint8_t *buf, size_t s)>;
using OdDynamicVarGetSize     = std::function<size_t(const uint16_t idx, const uint8_t subIdx)>;
using OdDynamicVarResize      = std::function<bool(const uint16_t idx, const uint8_t subIdx, size_t newSize)>;
using OdDynamicVarBeginAccess = std::function<void(const uint16_t idx, const uint8_t subIdx)>;
using OdDynamicVarEndAccess   = std::function<void(const uint16_t idx, const uint8_t subIdx)>;

struct OdDynamicVar {
  OdDynamicVarCopyFrom copyFrom       = nullptr;
  OdDynamicVarCopyInto copyInto       = nullptr;
  OdDynamicVarGetSize size            = nullptr;
  OdDynamicVarResize resize           = nullptr;
  OdDynamicVarBeginAccess beginAccess = nullptr;
  OdDynamicVarEndAccess endAccess     = nullptr;
};

using OdVariant = std::variant<int8_t, uint8_t,
                               uint16_t, int16_t,
                               uint32_t, int32_t,
                               uint64_t, int64_t,
                               float, std::vector<uint8_t>, std::string, OdBuffer, OdDynamicVar>;

enum class Access {
  RO,
  WO,
  RW
};

using ChangedCallback = std::function<void(uint16_t idx, uint8_t subIdx)>;

struct OdEntry {
  OdEntry(uint16_t i, uint8_t s, Access a, OdVariant d, ChangedCallback cb = nullptr);
  OdEntry(OdEntry &&o)      = delete;
  OdEntry(const OdEntry &o) = delete;
  Access access;
  OdVariant data;

  bool lock();
  void unlock();
  bool isLocked();
  void fireCallbacks();
  void addCallback(ChangedCallback cb);
  inline unsigned generation() { return generation_; }
  inline void bumpGeneration() { generation_ = newGeneration(); }

 private:
  const uint16_t idx;
  const uint8_t subIdx;
  bool locked          = false;
  unsigned generation_ = newGeneration();
  std::vector<ChangedCallback> callbacks;
};

class OdProxy {
 public:
  OdProxy();
  OdProxy(uint16_t, uint8_t, const OdVariant &);
  OdProxy(uint16_t, uint8_t, OdVariant &);
  OdProxy(uint16_t, uint8_t, OdEntry &);
  OdProxy(OdProxy &&);
  OdProxy(const OdProxy &)            = delete;
  OdProxy &operator=(OdProxy &&)      = delete;
  OdProxy &operator=(const OdProxy &) = delete;
  ~OdProxy();

  bool resize(size_t newSize);
  Error copyInto(uint8_t *b, size_t s);  // Copy from variant
  Error copyFrom(uint8_t *b, size_t s);  // Write to variant
  Error copyFrom(const OdProxy &other);
  Error reset();
  size_t remaining();
  void suppressCallbacks();

  const uint16_t idx;
  const uint8_t subIdx;

 private:
  bool readOnly            = false;
  bool changed             = false;
  bool callbacksSuppressed = false;
  OdVariant *v             = nullptr;
  const OdVariant *roV     = nullptr;
  OdEntry *e               = nullptr;
  uint8_t *ptr             = nullptr;
  size_t off               = 0;
  size_t len               = 0;
  OdDynamicVar *dVar       = nullptr;
};

template <typename E>
static inline constexpr auto _e(E x)
{
  return static_cast<std::underlying_type_t<E>>(x);
}

template <typename T>
static inline constexpr uint8_t _s8(T x)
{
  return static_cast<int8_t>(x);
}

template <typename T>
static inline constexpr uint16_t _s16(T x)
{
  return static_cast<int16_t>(x);
}

template <typename T>
static inline constexpr uint32_t _s32(T x)
{
  return static_cast<int32_t>(x);
}

template <typename T>
static inline constexpr uint64_t _s64(T x)
{
  return static_cast<int64_t>(x);
}

template <typename T>
static inline constexpr uint8_t _u8(T x)
{
  return static_cast<uint8_t>(x);
}

template <typename T>
static inline constexpr uint16_t _u16(T x)
{
  return static_cast<uint16_t>(x);
}

template <typename T>
static inline constexpr uint32_t _u32(T x)
{
  return static_cast<uint32_t>(x);
}

template <typename T>
static inline constexpr uint64_t _u64(T x)
{
  return static_cast<uint64_t>(x);
}

template <typename T>
static inline constexpr uint32_t _f32(T x)
{
  return static_cast<float>(x);
}

inline OdBuffer _p(std::string &val)
{
  return OdBuffer{(uint8_t *)val.c_str(), val.size()};
}

template <typename T>
static inline OdBuffer _p(T &val)
{
  return OdBuffer{(uint8_t *)&val, sizeof(val)};
}

size_t size(const OdVariant &v);

}  // namespace canfetti
