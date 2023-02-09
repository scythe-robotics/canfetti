#include "canfetti/OdData.h"
#include <cstring>

using namespace canfetti;

//******************************************************************************
// Helpers
//******************************************************************************
size_t canfetti::size(const OdVariant &v)
{
  auto f = [=](auto &&arg) {
    using T = std::decay_t<decltype(arg)>;

    if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
      return arg.size();
    }
    else if constexpr (std::is_same_v<T, std::string>) {
      return arg.size();
    }
    else if constexpr (std::is_same_v<T, OdBuffer>) {
      return arg.len;
    }
    else {
      return sizeof(arg);
    }
  };

  return std::visit(f, v);
}

//******************************************************************************
// OdEntry
//******************************************************************************
OdEntry::OdEntry(uint16_t i, uint8_t s, Access a, OdVariant d, ChangedCallback cb) : access(a), data(d), idx(i), subIdx(s)
{
  addCallback(std::move(cb));
}

bool OdEntry::lock()
{
  if (locked) return false;
  locked = true;
  if (OdDynamicVar *dvar = std::get_if<OdDynamicVar>(&data); dvar && dvar->beginAccess) {
    dvar->beginAccess(idx, subIdx);
  }
  return true;
}

void OdEntry::unlock()
{
  if (OdDynamicVar *dvar = std::get_if<OdDynamicVar>(&data); locked && dvar && dvar->endAccess) {
    dvar->endAccess(idx, subIdx);
  }
  locked = false;
}

bool OdEntry::isLocked()
{
  return locked;
}

void OdEntry::fireCallbacks()
{
  // May mutate in callback but never reduces in size
  for (size_t i = 0; i < callbacks.size(); ++i) {
    callbacks[i](idx, subIdx);
  }
}

void OdEntry::addCallback(ChangedCallback cb)
{
  if (cb) {
    callbacks.emplace_back(std::move(cb));
  }
}

//******************************************************************************
// OdProxy
//******************************************************************************
OdProxy::OdProxy() : idx(0), subIdx(0)
{
}

OdProxy::OdProxy(uint16_t idx, uint8_t subIdx, const OdVariant &v) : idx(idx), subIdx(subIdx), roV(&v)
{
  readOnly = true;
  reset();
}

OdProxy::OdProxy(uint16_t idx, uint8_t subIdx, OdVariant &v) : idx(idx), subIdx(subIdx), v(&v)
{
  reset();
}

OdProxy::OdProxy(uint16_t idx, uint8_t subIdx, OdEntry &e) : idx(idx), subIdx(subIdx), v(&e.data), e(&e)
{
  assert(e.isLocked());
  reset();
}

OdProxy::OdProxy(OdProxy &&o)
    : idx(o.idx), subIdx(o.subIdx), changed(o.changed), v(o.v), e(o.e), ptr(o.ptr), off(o.off), len(o.len), dVar(o.dVar)
{
  o.e = nullptr;
}

OdProxy::~OdProxy()
{
  if (e) {
    e->unlock();
    if (changed) {
      e->bumpGeneration();
      if (!callbacksSuppressed) {
        e->fireCallbacks();
      }
    }
  }
}

Error OdProxy::reset()
{
  auto f = [&](auto &&arg) {
    using T = std::decay_t<decltype(arg)>;

    if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
      ptr  = (uint8_t *)arg.data();
      off  = 0;
      len  = arg.size();
      dVar = nullptr;
    }
    else if constexpr (std::is_same_v<T, std::string>) {
      ptr  = (uint8_t *)arg.c_str();
      off  = 0;
      len  = arg.size();
      dVar = nullptr;
    }
    else if constexpr (std::is_same_v<T, OdBuffer>) {
      ptr  = arg.buf;
      off  = 0;
      len  = arg.len;
      dVar = nullptr;
    }
    else if constexpr (std::is_same_v<T, OdDynamicVar>) {
      assert(arg.size);
      ptr  = nullptr;
      off  = 0;
      len  = arg.size(idx, subIdx);
      dVar = &arg;
    }
    else {
      ptr  = (uint8_t *)&arg;
      off  = 0;
      len  = sizeof(arg);
      dVar = nullptr;
    }
  };

  changed = false;
  std::visit(f, readOnly ? *const_cast<OdVariant *>(roV) : *v);

  return Error::Success;
}

bool OdProxy::resize(size_t newSize)
{
  if (readOnly) return false;

  auto f = [=](auto &&arg) {
    using T = std::decay_t<decltype(arg)>;

    if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
      arg.resize(newSize);
      this->reset();
      return true;
    }
    else if constexpr (std::is_same_v<T, std::string>) {
      arg.resize(newSize);
      this->reset();
      return true;
    }
    else if constexpr (std::is_same_v<T, OdDynamicVar>) {
      if (arg.resize && arg.resize(idx, subIdx, newSize)) {
        this->reset();
        return true;
      }
      else {
        return false;
      }
    }
    else {
      return false;
    }
  };

  return std::visit(f, *v);
}

size_t OdProxy::remaining()
{
  return len - off;
}

Error OdProxy::copyInto(uint8_t *b, size_t s)
{
  if (len - off < s) {
    LogInfo("Data length violation on %x[%d]", idx, subIdx);
    return Error::ParamLength;
  }

  if (e && e->access == Access::WO) {
    LogDebug("Read access violation on %x[%d]", idx, subIdx);
    return Error::ReadViolation;
  }

  if (dVar) {
    if (!dVar->copyInto) return Error::UnsupportedAccess;

    auto err = dVar->copyInto(idx, subIdx, off, b, s);
    if (err == Error::Success) {
      off += s;
    }
    return err;
  }

  memcpy(b, ptr + off, s);
  off += s;

  return Error::Success;
}

Error OdProxy::copyFrom(uint8_t *b, size_t s)
{
  if (readOnly) return Error::UnsupportedAccess;

  if (len - off < s) {
    LogInfo("Data length violation on %x[%d]", idx, subIdx);
    return Error::ParamLength;
  }

  if (e && e->access == Access::RO) {
    LogDebug("Write access violation on %x[%d]", idx, subIdx);
    return Error::WriteViolation;
  };

  changed = true;

  if (dVar) {
    if (!dVar->copyFrom) return Error::UnsupportedAccess;

    auto err = dVar->copyFrom(idx, subIdx, off, b, s);
    if (err == Error::Success) {
      off += s;
    }
    return err;
  }

  memcpy(ptr + off, b, s);
  off += s;
  return Error::Success;
}

Error OdProxy::copyFrom(const OdProxy &other)
{
  return copyFrom(other.ptr, other.len);
}

void OdProxy::suppressCallbacks()
{
  callbacksSuppressed = true;
}
