
#include "canfetti/ODriveCo.h"
#include <string.h>
#include <unistd.h>
#include <atomic>
#include "odrive_main.h"

using namespace canfetti;

unsigned canfetti::newGeneration()
{
  static std::atomic<unsigned> g{0};
  return g.fetch_add(1, std::memory_order_relaxed);
}

Logger canfetti::logger;

//******************************************************************************
// System Impl
//******************************************************************************

bool System::init(fibre::Callback<std::optional<uint32_t>, float, fibre::Callback<void>> timer, fibre::Callback<bool, std::optional<uint32_t> &> timerCancel)
{
  this->timer       = timer;
  this->timerCancel = timerCancel;
  return true;
}

void System::timerHelper(TimerData *td)
{
  td->handle.reset();  // The underlying timer was already freed

  if (td->periodMs) {
    td->handle = timer.invoke((float)td->periodMs / 1000.0f, MEMBER_CB(td, trigger));
    if (!td->handle) {
      LogInfo("Timer recreation failed");
    }
  }

  td->cb();
}

void System::deleteTimer(System::TimerHdl &hdl)
{
  if (hdl) {
    timerCancel.invoke(hdl->handle);
    hdl->available = true;
  }
}

System::TimerHdl System::scheduleDelayed(uint32_t delayMs, std::function<void()> cb)
{
  for (size_t i = 0; i < timers.size(); i++) {
    TimerData &td = timers[i];
    if (td.available) {
      auto hdl = timer.invoke((float)delayMs / 1000.0f, MEMBER_CB(&td, trigger));
      if (hdl) {
        td.available = false;
        td.cb        = cb;
        td.periodMs  = 0;
        td.handle    = hdl;
        td.parent    = this;

        return &td;
      }
    }
  }

  LogInfo("Ran out of timers. Fixme");
  return System::InvalidTimer;
}

System::TimerHdl System::schedulePeriodic(uint32_t periodMs, std::function<void()> cb, bool staggeredStart)
{
  for (size_t i = 0; i < timers.size(); i++) {
    TimerData &td = timers[i];
    if (td.available) {
      auto hdl = timer.invoke((float)periodMs / 1000.0f, MEMBER_CB(&td, trigger));
      if (hdl) {
        td.available = false;
        td.cb        = cb;
        td.periodMs  = periodMs;
        td.handle    = hdl;
        td.parent    = this;

        return &td;
      }
    }
  }

  LogInfo("Ran out of timers. Fixme");
  return System::InvalidTimer;
}

//******************************************************************************
// CanOpen Impl
//******************************************************************************
Logger Logger::logger;

ODriveCo::ODriveCo(CanBusBase *canbus)
    : LocalNode(*this, sys, config_.id, "odrive", 0), canbus(canbus)
{
}

void ODriveCo::init(fibre::Callback<std::optional<uint32_t>, float, fibre::Callback<void>> timer, fibre::Callback<bool, std::optional<uint32_t> &> timerCancel, uint32_t numPrioTxSlots)
{
  this->numTxPrioSlots = numPrioTxSlots;
  this->txPrioSlot     = 0;

  LocalNode::init();
  sys.init(timer, timerCancel);
  initObjDict();

  // Using range filters instead of bit mask
  MsgIdFilterSpecs filter1 = {
      .id   = static_cast<uint16_t>(0x000),  // Standard ID
      .mask = 0x199,                         // Accept all IDs from 0-199 (high prio msgs)
  };

  canbus->subscribe(0,
                    filter1,
                    MEMBER_CB(this, handle_can_message),
                    &canbusSubscription[0]);

  MsgIdFilterSpecs filter2 = {
      .id   = static_cast<uint16_t>(0x19E),
      .mask = 0x7FF,  // Accept 19E - 7FF
  };

  canbus->subscribe(1,
                    filter2,
                    MEMBER_CB(this, handle_can_message),
                    &canbusSubscription[1]);
}

bool ODriveCo::apply_config()
{
  nodeId = config_.id;
  return true;
}

Error ODriveCo::write(const Msg &msg)
{
  can_Message_t txmsg = {0};

  if (msg.len <= sizeof(txmsg.buf)) {
    txmsg.id  = msg.id;
    txmsg.len = msg.len;
    memcpy(txmsg.buf, msg.data, msg.len);

    bool e = canbus->send_message(~0, txmsg, [](bool b) {});
    return e ? Error::Success : Error::Error;
  }

  return Error::Error;
}

Error ODriveCo::writePriority(const Msg &msg)
{
  can_Message_t txmsg = {0};

  if (msg.len <= sizeof(txmsg.buf)) {
    txmsg.id  = msg.id;
    txmsg.len = msg.len;
    memcpy(txmsg.buf, msg.data, msg.len);

    uint32_t slot = txPrioSlot++;
    if (txPrioSlot == numTxPrioSlots) {
      txPrioSlot = 0;
    }

    bool e = canbus->send_message(slot, txmsg, [](bool b) {});
    return e ? Error::Success : Error::Error;
  }

  return Error::Error;
}

void ODriveCo::handle_can_message(const can_Message_t &m)
{
  Msg msg = {
      .id   = m.id,
      .rtr  = m.rtr,
      .len  = m.len,
      .data = const_cast<uint8_t *>(m.buf),
  };

  processFrame(msg);
}
