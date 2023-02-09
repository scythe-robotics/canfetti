#include <future>
#include "test.h"

using namespace canfetti;
using namespace std;

using ::testing::_;
using ::testing::ElementsAre;
using ::testing::Invoke;
using ::testing::MockFunction;
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
    MOCK_METHOD(Error, write, (const Msg &msg), (override));
  };

  class MockLocalNode : public LocalNode {
  public:
    MockLocalNode() : LocalNode(dev, sys, 1, "Test Device", 0) {}
    Error sendResponse(const Msg &m)
    {
      processFrame(m);
      return Error::Success;
    }
    MockSystem sys;
    MockCanDevice dev;
  };
}

TEST(LinuxCoTest, emcyCallback)
{
  MockLocalNode co;
  co.init();

  ::testing::MockFunction<EmcyService::EmcyCallback> mcb;
  EXPECT_CALL(mcb, Call(co.nodeId, 0x1234, ElementsAre(0, 0, 0, 0, 0)));

  co.registerEmcyCallback(mcb.AsStdFunction());

  EXPECT_CALL(co.dev, write(FieldsAre(0x081, false, 8, _))).WillOnce(Invoke(&co, &MockLocalNode::sendResponse));

  co.sendEmcy(0x1234);
}
