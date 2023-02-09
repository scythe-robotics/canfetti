#pragma once

#include <cmsis_os.h>
#include <stdarg.h>
#include <stdint.h>
#include <time.h>
#include <chrono>
#include <functional>
#include <vector>
#include "fibre/callback.hpp"

#ifndef assert
  #include <assert.h>
#endif

#define CANFETTI_NO_INLINE __attribute__((noinline))

namespace canfetti {

unsigned newGeneration();

class Logger {
 public:
  static constexpr bool needNewline = true;
  static Logger logger;
  bool debug = false;

  template <typename... Args>
  inline void emitLogMessage(const char* file, const char* func, int line, const char* fmt, Args... args)
  {
    printf(fmt, args...);
  }
};

class ODriveCo;

class System {
  struct TimerData {
    std::optional<uint32_t> handle;
    uint32_t periodMs;
    std::function<void()> cb;
    bool available = true;
    System* parent;
    void trigger()
    {
      parent->timerHelper(this);
    }
  };
  std::array<TimerData, 24> timers;
  void timerHelper(TimerData* td);

 public:
  using TimerHdl                         = TimerData*;
  static constexpr TimerHdl InvalidTimer = nullptr;

  bool init(fibre::Callback<std::optional<uint32_t>, float, fibre::Callback<void>> timer, fibre::Callback<bool, std::optional<uint32_t>&> timerCancel);

  void deleteTimer(TimerHdl& hdl);
  TimerHdl scheduleDelayed(uint32_t delayMs, std::function<void()> cb);
  TimerHdl schedulePeriodic(uint32_t periodMs, std::function<void()> cb, bool staggeredStart = true);

 private:
  fibre::Callback<std::optional<uint32_t>, float, fibre::Callback<void>> timer;
  fibre::Callback<bool, std::optional<uint32_t>&> timerCancel;
};

// Global logger
extern Logger logger;

}  // namespace canfetti
