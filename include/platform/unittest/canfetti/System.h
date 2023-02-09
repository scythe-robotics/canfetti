#pragma once
#include <assert.h>
#include <gmock/gmock.h>
#include <time.h>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <random>
#include <thread>

#define CANFETTI_NO_INLINE

namespace canfetti {

unsigned newGeneration();

class Logger {
 public:
  static constexpr bool needNewline = false;
  static Logger logger;
  bool debug = false;
  template <typename... Args>
  void emitLogMessage(const char* file, const char* func, int line, const char* fmt, Args... args)
  {
    if (debug) printf(fmt, args...);
  }
};

class System {
 public:
  using TimerHdl                         = int;
  static constexpr TimerHdl InvalidTimer = -1;

  virtual TimerHdl resetTimer(TimerHdl& hdl)                                                                 = 0;
  virtual void deleteTimer(TimerHdl& hdl)                                                                    = 0;
  virtual void disableTimer(TimerHdl& hdl)                                                                   = 0;
  virtual TimerHdl scheduleDelayed(uint32_t delayMs, std::function<void()> cb)                               = 0;
  virtual TimerHdl schedulePeriodic(uint32_t periodMs, std::function<void()> cb, bool staggeredStart = true) = 0;
};

}  // namespace canfetti
