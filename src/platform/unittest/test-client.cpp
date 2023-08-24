#include <stdarg.h>
#include <unistd.h>
#include <atomic>
#include <future>
#include <thread>
#include "test.h"

using namespace canfetti;
using namespace std;

using ::testing::_;
using ::testing::Invoke;
using ::testing::MockFunction;
using ::testing::Return;
using ::testing::SetArgReferee;
using ::testing::FieldsAre;

namespace {
  class MockSystem : public canfetti::System {
  public:
    MOCK_METHOD(System::TimerHdl, resetTimer, (System::TimerHdl & hdl), (override));
    MOCK_METHOD(void, deleteTimer, (System::TimerHdl & hdl), (override));
    MOCK_METHOD(void, disableTimer, (System::TimerHdl & hdl), (override));
    MOCK_METHOD(System::TimerHdl, scheduleDelayed, (uint32_t delayMs, std::function<void()> cb), (override));
    MOCK_METHOD(System::TimerHdl, schedulePeriodic, (uint32_t periodMs, std::function<void()> cb, bool staggeredStart), (override));
  };

  class MockCanDevice : public CanDevice {
  public:
    MOCK_METHOD(Error, write, (const Msg &msg, bool /* async */), (override));
  };

  class MockLocalNode : public LocalNode {
  public:
    MockLocalNode() : LocalNode(dev, sys, 1, "Test Device", 0) {}
    void sendResponse(Msg m)
    {
      processFrame(m);
    }
    MockSystem sys;
    MockCanDevice dev;
  };
}

TEST(LinuxCoTest, readAsyncExpedited)
{
  uint32_t readback              = 0x11223344;
  System::TimerHdl tmrHdl        = 4321;
  System::TimerHdl invalidTmrHdl = -1;

  constexpr uint16_t node     = 5;
  constexpr uint16_t idx      = 0x2003;
  constexpr uint8_t subIdx    = 3;
  constexpr uint8_t remaining = sizeof(readback);

  uint8_t payload[8] = {
      2 << 5 | (4 - remaining) << 2 | 0b11,
      static_cast<uint8_t>(idx & 0xFF),
      static_cast<uint8_t>(idx >> 8),
      subIdx,
      0x44, 0x33, 0x22, 0x11};

  Msg m = {
      .id   = 0x580 + node,
      .rtr  = false,
      .len  = sizeof(payload),
      .data = payload};
  MockLocalNode co;
  co.init();

  EXPECT_EQ(co.addSDOClient(node, node), Error::Success);

  bool sentInit = false;
  EXPECT_CALL(co.dev, write(FieldsAre(0x605, false, 8, _), _))
      .WillOnce(
          Invoke([&](const Msg& m, bool /* async */) {
            sentInit = true;
            return Error::Success;
          }));

  // Make sure it sets a transaction timeout
  EXPECT_CALL(co.sys, scheduleDelayed)
      .WillOnce(Return(tmrHdl));

  // And cleans up the transaction timeout
  EXPECT_CALL(co.sys, deleteTimer(tmrHdl))
      .WillOnce(SetArgReferee<0>(invalidTmrHdl));

  auto mockCb = MockFunction<void(Error e, uint32_t & v)>();

  EXPECT_CALL(mockCb, Call(Error::Success, readback));

  EXPECT_EQ(co.read<uint32_t>(node, idx, subIdx, mockCb.AsStdFunction()), Error::Success);
  if (sentInit) {
    co.sendResponse(m);
  }
}
