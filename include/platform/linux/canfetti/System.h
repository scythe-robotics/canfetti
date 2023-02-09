#pragma once
#include <assert.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <random>
#include <thread>

// Always enable assertions.
// FIXME: remove once all error handling paths are fleshed out
#ifdef NDEBUG
  #undef NDEBUG
#endif

#define CANFETTI_NO_INLINE

namespace canfetti {

unsigned newGeneration();

class Logger {
 public:
  static constexpr bool needNewline = false;
  static Logger logger;

  using LogCallback = void (*)(const char*);
  void setLogCallback(LogCallback);

  template <typename... Args>
  void emitLogMessage(const char* file, const char* func, int line, const char* fmt, Args... args)
  {
    (void)file;
    (void)func;
    (void)line;
    if (LogCallback cb = logCallback.load(std::memory_order_relaxed)) {
      static thread_local char msgbuf[4096];
      snprintf(msgbuf, sizeof msgbuf, fmt, args...);
      msgbuf[sizeof msgbuf - 1] = 0;  // sure why not
      cb(msgbuf);
    }
  }

  std::atomic<LogCallback> logCallback;
  std::atomic_bool debug = false;
};

class System {
 private:
  struct Timer {
    bool available = true;
    bool enable    = false;
    bool repeat    = false;
    std::chrono::steady_clock::duration interval;
    std::chrono::steady_clock::time_point deadline;
    std::function<void()> callback;
  };

 public:
  using TimerHdl = std::shared_ptr<Timer>;
  static const TimerHdl InvalidTimer;

  TimerHdl resetTimer(TimerHdl& hdl);
  void deleteTimer(TimerHdl& hdl);
  void disableTimer(TimerHdl& hdl);
  TimerHdl scheduleDelayed(uint32_t delayMs, std::function<void()> cb);
  TimerHdl schedulePeriodic(uint32_t periodMs, std::function<void()> cb, bool staggeredStart = true);

  // Return a time point no later than the earliest timer deadline. If there
  // are no timers, return an arbitrarily distant future time point.
  std::chrono::steady_clock::time_point nextTimerDeadline();
  // Invoke callbacks of any timers that have hit their deadline
  void serviceTimers();

  size_t getTimerCount();
  // For LinuxCo::doWithLock() to detect that timers have changed and main
  // thread should be woken
  unsigned getTimerGeneration() { return generation; }

 private:
  // vector instead of [unordered_]set to allow mutation during iteration in
  // service(). Size only increases.
  std::vector<TimerHdl> timers;
  TimerHdl getAvailableTimer();
  std::mt19937 prng;  // use default seed
  unsigned generation = newGeneration();
};

}  // namespace canfetti
