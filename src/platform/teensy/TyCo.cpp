
#include "canfetti/TyCo.h"
#include "version.h"

using namespace canfetti;

unsigned canfetti::newGeneration()
{
  static unsigned g = 0;
  return g++;
}

Logger Logger::logger;

//******************************************************************************
// System Impl
//******************************************************************************

void System::service()
{
  uint32_t now = millis();

  for (auto td = timers.begin(); td != timers.end(); ++td) {
    if (td->enable && (now - td->lastFireTime) >= td->delay) {
      if (td->period) {
        td->lastFireTime = now;
        td->delay        = td->period;
      }
      else {
        td->enable = false;
      }
      if (td->cb) td->cb();
    }
  }
}

void System::deleteTimer(System::TimerHdl &hdl)
{
  if (hdl != System::InvalidTimer) {
    assert(hdl < timers.size());
    timers[hdl].enable    = false;
    timers[hdl].available = true;
    hdl                   = System::InvalidTimer;
  }
}

System::TimerHdl System::scheduleDelayed(uint32_t delayMs, std::function<void()> cb)
{
  for (size_t i = 0; i < timers.size(); i++) {
    TimerData &td = timers[i];
    if (td.available) {
      td.lastFireTime = millis();
      td.delay        = delayMs;
      td.enable       = true;
      td.available    = false;
      td.period       = 0;
      td.cb           = cb;
      return i;
    }
  }

  timers.push_back({
      .lastFireTime = millis(),
      .delay        = delayMs,
      .period       = 0,
      .cb           = cb,
      .enable       = true,
      .available    = false,
  });

  return timers.size() - 1;
}

System::TimerHdl System::schedulePeriodic(uint32_t periodMs, std::function<void()> cb, bool staggeredStart)
{
  uint32_t staggeredDelay = staggeredStart ? random(periodMs << 1) : 0;

  for (size_t i = 0; i < timers.size(); i++) {
    TimerData &td = timers[i];
    if (td.available) {
      td.lastFireTime = millis();
      td.delay        = periodMs + staggeredDelay;
      td.enable       = true;
      td.available    = false;
      td.period       = periodMs;
      td.cb           = cb;
      return i;
    }
  }

  timers.push_back({
      .lastFireTime = millis(),
      .delay        = periodMs + staggeredDelay,
      .period       = periodMs,
      .cb           = cb,
      .enable       = true,
      .available    = false,
  });

  return timers.size() - 1;
}

//******************************************************************************
// TyCoDev
//******************************************************************************

canfetti::Error TyCoDev::init(uint32_t baudrate, FlexCAN_T4 *busInstance)
{
  assert(busInstance);
  this->bus = busInstance;

  bus->begin();
  bus->setBaudRate(baudrate);

#if defined(__IMXRT1062__)
  bus->setPartition({RX, STD, 4, std::array<uint8_t, 2>{0x081, 0x0FF}});
  bus->setPartition({RX, STD, 50, ACCEPT_ALL});
  bus->setPartition({TX_PRIO, STD, 2});
  bus->setPartition({TX, STD, 8});
#elif defined(__MK20DX256__) || defined(__MK64FX512__) || defined(__MK66FX1M0__)
  bus->setPartition({RX, STD, 2, std::array<uint8_t, 2>{0x081, 0x0FF}});
  bus->setPartition({RX, STD, 9, ACCEPT_ALL});
  bus->setPartition({TX_PRIO, STD, 1});
  bus->setPartition({TX, STD, 4});
#endif

  return canfetti::Error::Success;
}

canfetti::Error TyCoDev::write(const Msg &msg, bool /* async */)
{
  CAN_message_t m;
  assert(msg.len <= sizeof(m.buf));

  memcpy(m.buf, msg.data, msg.len);
  m.id             = msg.id & ((1 << 29) - 1);
  m.len            = msg.len;
  m.flags.remote   = msg.rtr;
  m.flags.extended = false;

  if (int e = bus->write(m); e != 1) {
#if 0
      static unsigned next      = 0;
      static size_t lastDropped = 0;
      unsigned now              = millis();
      if (next < now) {
        LogInfo("CAN write failed: %d (delta: %d)", e, stats.droppedTx - lastDropped);
        next        = now + 5000;
        lastDropped = stats.droppedTx;
      }
#endif
    stats.droppedTx++;
    return canfetti::Error::HwError;
  }

  return canfetti::Error::Success;
}

canfetti::Error TyCoDev::writePriority(const Msg &msg)
{
  CAN_message_t m;
  assert(msg.len <= sizeof(m.buf));

  memcpy(m.buf, msg.data, msg.len);
  m.id             = msg.id & ((1 << 29) - 1);
  m.len            = msg.len;
  m.flags.remote   = msg.rtr;
  m.flags.extended = false;

  if (int e = bus->writePriority(m); e != 1) {
#if 0
      static unsigned next      = 0;
      static size_t lastDropped = 0;
      unsigned now              = millis();
      if (next < now) {
        LogInfo("CAN write failed: %d (delta: %d)", e, stats.droppedPrioTx - lastDropped);
        next        = now + 5000;
        lastDropped = stats.droppedTx;
      }
#endif
    stats.droppedPrioTx++;
    return canfetti::Error::Error;
  }

  return canfetti::Error::Success;
}

//******************************************************************************
// CanOpen Impl
//******************************************************************************
TyCo::TyCo(TyCoDev &d, uint8_t nodeId, const char *deviceName, uint32_t deviceType)
    : LocalNode(d, sys, nodeId, deviceName, deviceType), dev(d)
{
}

void TyCo::service()
{
  CAN_message_t m;

  if (dev.read(m)) {
    Msg msg = {
        .id   = m.id,
        .rtr  = !!m.flags.remote,
        .len  = m.len,
        .data = m.buf,
    };

    if (m.flags.overrun) {
      dev.stats.overruns++;
    }

    dev.stats.droppedRx += dev.newDroppedRx();

    processFrame(msg);
  }

  sys.service();
}
