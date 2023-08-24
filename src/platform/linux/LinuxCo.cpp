#include "canfetti/LinuxCo.h"
#include <stdarg.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string>
#include <thread>
#include "linux/can/raw.h"
#include "net/if.h"

using namespace canfetti;

unsigned canfetti::newGeneration()
{
  static std::atomic<unsigned> g{0};
  return g.fetch_add(1, std::memory_order_relaxed);
}

Logger Logger::logger;

//******************************************************************************
// Log Impl
//******************************************************************************

void Logger::setLogCallback(void (*cb)(const char *))
{
  logCallback.store(cb, std::memory_order_relaxed);
}

//******************************************************************************
// System Impl
//******************************************************************************

const System::TimerHdl System::InvalidTimer{nullptr};

System::TimerHdl System::resetTimer(System::TimerHdl &hdl)
{
  if (!hdl) return InvalidTimer;
  hdl->enable   = true;
  hdl->deadline = std::chrono::steady_clock::now() + hdl->interval;
  generation = newGeneration();
  return hdl;
}

void System::deleteTimer(System::TimerHdl &hdl)
{
  if (!hdl) return;
  hdl->available = true;
  hdl->enable = false;
  hdl = InvalidTimer;
}

void System::disableTimer(System::TimerHdl &hdl)
{
  if (!hdl) return;
  hdl->enable = false;
}

System::TimerHdl System::scheduleDelayed(uint32_t delayMs, std::function<void()> cb)
{
  auto hdl      = getAvailableTimer();
  hdl->available = false;
  hdl->enable   = true;
  hdl->repeat   = false;
  hdl->interval = std::chrono::milliseconds(delayMs);
  hdl->deadline = std::chrono::steady_clock::now() + hdl->interval;
  hdl->callback = cb;
  generation = newGeneration();
  return hdl;
}

System::TimerHdl System::schedulePeriodic(uint32_t periodMs, std::function<void()> cb, bool staggeredStart)
{
  auto hdl            = getAvailableTimer();
  auto staggeredDelay = std::chrono::milliseconds(staggeredStart ? prng() % (periodMs << 1) : 0);
  hdl->available      = false;
  hdl->enable         = true;
  hdl->repeat         = true;
  hdl->interval       = std::chrono::milliseconds(periodMs);
  hdl->deadline       = std::chrono::steady_clock::now() + hdl->interval + staggeredDelay;
  hdl->callback       = cb;
  generation = newGeneration();
  return hdl;
}

void System::serviceTimers()
{
  auto now = std::chrono::steady_clock::now();
  // May mutate in callback, but never decreases in size
  for (size_t i = 0; i < timers.size(); ++i) {
    auto &t = timers[i];
    if (t->enable && t->deadline <= now) {
      if (t->repeat) {
        t->deadline = now + t->interval;
      }
      else {
        t->enable = false;
      }
      t->callback();
    }
  }
}

size_t System::getTimerCount()
{
  size_t n = 0;
  for (size_t i = 0; i < timers.size(); ++i) {
    if (!timers[i]->available) ++n;
  }
  return n;
}

std::chrono::steady_clock::time_point System::nextTimerDeadline()
{
  auto deadline = std::chrono::steady_clock::now() + std::chrono::hours(1);
  for (const auto &t : timers) {
    if (t->enable && deadline > t->deadline) {
      deadline = t->deadline;
    }
  }
  return deadline;
}

System::TimerHdl System::getAvailableTimer()
{
  for (const auto &t : timers) {
    if (t->available) return t;
  }
  auto t = std::make_shared<Timer>();
  timers.push_back(t);
  return t;
}

//******************************************************************************
// Device
//******************************************************************************
LinuxCoDev::LinuxCoDev(uint32_t baudrate)
{
}

canfetti::Error LinuxCoDev::open(const char *device)
{
  struct sockaddr_can addr = {0};
  struct ifreq ifr         = {0};

  s = socket(PF_CAN, SOCK_RAW, CAN_RAW);

  if (s == -1) {
    perror("socket failed");
    return Error::HwError;
  }

  strcpy(ifr.ifr_name, device);
  if (ioctl(s, SIOCGIFINDEX, &ifr) == -1) {
    perror("ioctl(SIOCGIFINDEX) failed");
    return Error::HwError;
  }

  addr.can_family  = AF_CAN;
  addr.can_ifindex = ifr.ifr_ifindex;

  if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
    perror("bind failed");
    return Error::HwError;
  }

  size_t ms = 500;
  struct timeval tv;
  tv.tv_sec = 0;

  while (ms >= 1000) {
    tv.tv_sec++;
    ms -= 1000;
  }

  tv.tv_usec = ms * 1000;
  if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv) == -1) {
    perror("setsockopt failed");
    return Error::HwError;
  }

  return Error::Success;
}

Error LinuxCoDev::read(struct can_frame &frame, bool nonblock)
{
  ssize_t r = ::recv(s, &frame, sizeof(frame), nonblock ? MSG_DONTWAIT : 0);

  if (r < 0) {
    if (errno != EAGAIN) {
      perror("can raw socket read");
      return Error::HwError;
    }
  }

  return r > 0 ? Error::Success : Error::Timeout;
}

Error LinuxCoDev::write(const Msg &msg, bool async)
{
  struct can_frame frame = {0};
  assert(msg.len <= sizeof(frame.data));

  frame.can_id = msg.id & ((1 << 29) - 1);
  if (frame.can_id >= 0x800)
    frame.can_id |= CAN_EFF_FLAG;
  frame.can_dlc = msg.len;
  if (msg.rtr)
    frame.can_id |= CAN_RTR_FLAG;
  else
    memcpy(frame.data, msg.data, msg.len);

  if (async) {
    asyncFrames.push_back(frame);
  }
  else if (ssize_t written = ::write(s, &frame, sizeof(frame)); written < 0) {
    LogDebug("can socket write: errno %d", errno);
    stats.droppedTx++;
    return Error::HwError;
  }

  return Error::Success;
}

void LinuxCoDev::flushAsyncFrames()
{
  for (const struct can_frame& frame : asyncFrames) {
    if (ssize_t written = ::write(s, &frame, sizeof(frame)); written < 0) {
      LogDebug("can socket write (async): errno %d", errno);
      stats.droppedTx++;
    }
  }
  asyncFrames.clear();
}

//******************************************************************************
// CanOpen Impl
//******************************************************************************
LinuxCo::LinuxCo(LinuxCoDev &d, uint8_t nodeId, const char *deviceName, uint32_t deviceType)
    : LocalNode(d, sys, nodeId, deviceName, deviceType)
{
}

LinuxCo::~LinuxCo()
{
  shutdown.store(true);
  if (mainThread) mainThread->join();
  if (recvThread) recvThread->join();
}

Error LinuxCo::start(const char *dev)
{
  if (Error e = LocalNode::init(); e != Error::Success) return e;
  if (Error e = static_cast<LinuxCoDev &>(bus).open(dev); e != Error::Success) return e;
  mainThread    = std::make_unique<std::thread>([=]() { this->runMainThread(); });
  recvThread    = std::make_unique<std::thread>([=]() { this->runRecvThread(); });
  // Just to aid debugging
  std::string name;
  if (od.get(0x1008, 0, name) == Error::Success) {
    std::string mainName = name + ".main";
    pthread_setname_np(mainThread->native_handle(), mainName.c_str());
    std::string recvName = name + ".recv";
    pthread_setname_np(recvThread->native_handle(), recvName.c_str());
  }
  return Error::Success;
}

void LinuxCo::runMainThread()
{
  while (!shutdown.load()) {
    std::unique_lock u(mtx);
    unsigned gen = sys.getTimerGeneration();
    auto deadline = std::min(sys.nextTimerDeadline(), std::chrono::steady_clock::now() + std::chrono::milliseconds(500));
    mainThreadWakeup.wait_until(u, deadline, [&]() {return !pendingFrames.empty() || gen != sys.getTimerGeneration();});
    sys.serviceTimers();
    for (auto& frame : pendingFrames) {
      Msg msg;
      msg.id   = frame.can_id & CAN_EFF_MASK;
      msg.len  = frame.can_dlc;
      msg.data = frame.data;
      msg.rtr  = !!(frame.can_id & CAN_RTR_FLAG);
      processFrame(msg);
    }
    if (!pendingFrames.empty()) {
      recvThreadWakeup.notify_one();
    }
    pendingFrames.clear();
    u.unlock();
    static_cast<LinuxCoDev &>(bus).flushAsyncFrames();
  }
}

void LinuxCo::runRecvThread()
{
  constexpr size_t MAX_FRAMES_PER_BATCH = 64;
  std::vector<can_frame> frames;
  while (!shutdown.load()) {
    {
      std::unique_lock u(mtx);
      // Wait until all frames have been processed before reading another batch
      if (!recvThreadWakeup.wait_for(u, std::chrono::milliseconds(500), [&]() {return pendingFrames.empty();})) continue;
    }
    struct can_frame frame = {0};
    bool nonblock = false;
    Error e;
    while ((e = static_cast<LinuxCoDev &>(bus).read(frame, nonblock)) == Error::Success) {
      // Allow the first read to block for SO_RCVTMEO so we don't spin, then grab anything else in the queue ASAP to minimize batch latency
      nonblock = true;
      frames.push_back(frame);
      // Don't starve the node or allocate unbounded messages under heavy traffic
      if (frames.size() >= MAX_FRAMES_PER_BATCH) break;
    }
    if (!frames.empty()) {
      std::lock_guard g(mtx);
      assert(pendingFrames.empty());
      pendingFrames.swap(frames);
      mainThreadWakeup.notify_one();
    }
    if (e != Error::Success && e != Error::Timeout) {
      // Don't spin on unexpected errors
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
  }
}
