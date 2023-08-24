#pragma once
#include <assert.h>
#include <time.h>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <random>
#include <thread>
#include "canfetti/LocalNode.h"
#include "canfetti/System.h"
#include "linux/can.h"

namespace canfetti {

class LinuxCoDev : public canfetti::CanDevice {
 public:
  LinuxCoDev(uint32_t baudrate);
  canfetti::Error open(const char* device);
  canfetti::Error write(const canfetti::Msg& msg, bool async = false) override;
  canfetti::Error read(struct can_frame& frame, bool nonblock);
  void flushAsyncFrames();

 private:
  int s;
  // Accessed by write() and flushAsyncFrames()
  std::vector<struct can_frame> asyncFrames;
};

class LinuxCo : public canfetti::LocalNode {
 public:
  LinuxCo(LinuxCoDev& d, uint8_t nodeId, const char* deviceName, uint32_t deviceType = 0);
  ~LinuxCo();
  Error start(const char* dev);
  size_t getTimerCount() { return sys.getTimerCount(); }

  // All external callers must go through this to access node state, OD data, etc. f() must not block.
  template <typename F>
  void doWithLock(F f)
  {
    std::lock_guard g(mtx);
    unsigned gen = sys.getTimerGeneration();
    f();
    if (gen != sys.getTimerGeneration()) {
      mainThreadWakeup.notify_one();
    }
  }

  template <typename T>
  Error blockingRead(uint8_t node, uint16_t idx, uint8_t subIdx, T &data, uint32_t segmentTimeout = SdoService::DefaultSegmentXferTimeoutMs)
  {
    return blockingTransaction(true, node, idx, subIdx, data, segmentTimeout);
  }

  template <typename T>
  Error blockingWrite(uint8_t node, uint16_t idx, uint8_t subIdx, T &&data, uint32_t segmentTimeout = SdoService::DefaultSegmentXferTimeoutMs)
  {
    return blockingTransaction(false, node, idx, subIdx, std::forward<T>(data), segmentTimeout);
  }

  template <typename T>
  Error blockingTransaction(bool read, uint8_t node, uint16_t idx, uint8_t subIdx, T &&data, uint32_t segmentTimeout)
  {
    std::mutex mtx;
    std::condition_variable cv;
    bool done = false;
    Error result = Error::Error;
    OdVariant val(data);

    Error initErr;
    doWithLock([&]() {
      initErr = sdo.clientTransaction(read, node, idx, subIdx, val, segmentTimeout, [&](Error e) {
        std::lock_guard g(mtx);
        done = true;
        result = e;
        cv.notify_one();
      });
    });
    if (initErr != Error::Success) return initErr;

    {
      std::unique_lock u(mtx);
      cv.wait(u, [&]() { return done; });
    }
    if constexpr (!std::is_same_v<std::decay_t<T>, OdBuffer>) {
      data = std::move(*std::get_if<std::decay_t<T>>(&val));
    }
    return result;
  }

 private:
  // These are separate threads due to the difficulty of combining socket IO with timers
  void runMainThread();
  void runRecvThread();

  std::recursive_mutex mtx;
  std::condition_variable_any mainThreadWakeup; // when pendingFrames becomes non-empty or timers have changed
  std::condition_variable_any recvThreadWakeup; // when pendingFrames becomes empty
  System sys;
  std::vector<can_frame> pendingFrames;
  std::unique_ptr<std::thread> mainThread;
  std::unique_ptr<std::thread> recvThread;
  std::atomic<bool> shutdown{false};
};

}  // namespace canfetti
