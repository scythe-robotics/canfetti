#include "test.h"

using namespace canfetti;
using namespace std;

static Error odWrite(const uint16_t idx, const uint8_t subIdx, size_t off, uint8_t* buf, size_t s)
{
  return Error::Success;
}

static Error odRead(const uint16_t idx, const uint8_t subIdx, size_t off, uint8_t* buf, size_t s)
{
  uint32_t v = 2345;
  if (s == 4) {
    memcpy(buf, &v, 4);
    return Error::Success;
  }
  return Error::ParamIncompatibility;
}

static size_t odSize(const uint16_t idx, const uint8_t subIdx)
{
  return 4;
}

TEST(ObjDict, InsertGetSet)
{
  ObjDict od;
  uint16_t idx = 0x2000;

  {
    uint8_t v = 0;
    EXPECT_EQ(od.insert(idx, 0, canfetti::Access::RO, _u8(123)), Error::Success);
    EXPECT_EQ(od.get(idx, 0, v), Error::Success);
    EXPECT_EQ(v, 123);

    EXPECT_EQ(od.set(idx, 0, _u8(44)), Error::Success);
    EXPECT_EQ(od.get(idx, 0, v), Error::Success);
    EXPECT_EQ(v, 44);
  }

  {
    idx++;
    uint32_t v = 0;
    EXPECT_EQ(od.insert(idx, 0, canfetti::Access::RO, _u32(555)), Error::Success);
    EXPECT_EQ(od.get(idx, 0, v), Error::Success);
    EXPECT_EQ(v, 555);

    EXPECT_EQ(od.set(idx, 0, _u32(222)), Error::Success);
    EXPECT_EQ(od.get(idx, 0, v), Error::Success);
    EXPECT_EQ(v, 222);
  }

  {
    idx++;
    float v = 0;
    EXPECT_EQ(od.insert(idx, 0, canfetti::Access::RO, 12.2f), Error::Success);
    EXPECT_EQ(od.get(idx, 0, v), Error::Success);
    EXPECT_EQ(v, 12.2f);

    EXPECT_EQ(od.set(idx, 0, 555.f), Error::Success);
    EXPECT_EQ(od.get(idx, 0, v), Error::Success);
    EXPECT_EQ(v, 555.f);
  }

  {
    idx++;
    string v;
    EXPECT_EQ(od.insert(idx, 0, canfetti::Access::RO, "a string"), Error::Success);
    EXPECT_EQ(od.get(idx, 0, v), Error::Success);
    EXPECT_EQ(v, string("a string"));
  }

  {
    idx++;
    uint32_t somevalue = 4532;
    uint32_t v         = 0;
    EXPECT_EQ(od.insert(idx, 0, canfetti::Access::RO, _p(somevalue)), Error::Success);
    auto [e, p] = od.makeProxy(idx, 0);
    ASSERT_EQ(e, Error::Success);
    p.copyInto((uint8_t*)&v, 4);
    EXPECT_EQ(v, 4532);
  }

  {
    idx++;
    uint32_t v = 0;
    auto dvar  = canfetti::OdDynamicVar{.copyFrom = odWrite, .copyInto = odRead, .size = odSize};
    EXPECT_EQ(od.insert(idx, 0, canfetti::Access::RO, dvar), Error::Success);
    auto [e, p] = od.makeProxy(idx, 0);
    ASSERT_EQ(e, Error::Success);
    p.copyInto((uint8_t*)&v, 4);
    EXPECT_EQ(v, 2345);
  }
}

TEST(ObjDict, Size)
{
  ObjDict od;
  EXPECT_EQ(od.insert(0x2000, 0, canfetti::Access::RO, _u8(123)), Error::Success);

  EXPECT_EQ(od.entrySize(0x2000, 0), 1);
  EXPECT_EQ(od.entrySize(0x2000, 1), 0);
}

TEST(ObjDict, Exists)
{
  ObjDict od;
  EXPECT_EQ(od.insert(0x2000, 0, canfetti::Access::RO, _u8(123)), Error::Success);

  EXPECT_FALSE(od.entryExists(0x2000, 1));
  EXPECT_TRUE(od.entryExists(0x2000, 0));
}

TEST(ObjDict, GetInvalid)
{
  ObjDict od;
  float v = 0;
  EXPECT_EQ(od.insert(0x2000, 0, canfetti::Access::RO, _u32(123)), Error::Success);
  EXPECT_EQ(od.get(0x2000, 0, v), Error::ParamIncompatibility);
}

TEST(ObjDict, InsertDuplicate)
{
  ObjDict od;
  EXPECT_EQ(od.insert(0x2000, 1, canfetti::Access::RO, _u8(1)), Error::Success);
  EXPECT_EQ(od.insert(0x2000, 1, canfetti::Access::RO, _u8(1)), Error::Error);
  EXPECT_EQ(od.insert(0x2000, 1, canfetti::Access::RO, _u32(1)), Error::Error);
}

TEST(ObjDict, SimpleLocking)
{
  ObjDict od;
  EXPECT_EQ(od.insert(0x2000, 0, canfetti::Access::RO, _u8(55)), Error::Success);

  {
    // p1 holds the lock
    auto [e1, p1] = od.makeProxy(0x2000, 0);
    ASSERT_EQ(e1, Error::Success);

    auto [e2, p2] = od.makeProxy(0x2000, 0);
    ASSERT_EQ(e2, Error::DataXferLocal);

    uint8_t v;
    EXPECT_EQ(od.get(0x2000, 0, v), Error::Timeout);
    EXPECT_EQ(od.set(0x2000, 0, v), Error::Timeout);
  }

  {
    auto [e, p] = od.makeProxy(0x2000, 0);
    ASSERT_EQ(e, Error::Success);
  }

  {
    uint8_t v;
    EXPECT_EQ(od.get(0x2000, 0, v), Error::Success);
    EXPECT_EQ(od.set(0x2000, 0, v), Error::Success);
  }
}

TEST(ObjDict, Access)
{
  ObjDict od;

  {
    uint8_t v = 0;
    EXPECT_EQ(od.insert(0x2000, 0, canfetti::Access::RO, _u8(55)), Error::Success);
    auto [e, p] = od.makeProxy(0x2000, 0);
    ASSERT_EQ(e, Error::Success);

    EXPECT_EQ(p.copyFrom(&v, 1), Error::WriteViolation);
    EXPECT_EQ(p.reset(), Error::Success);
    EXPECT_EQ(p.copyInto(&v, 1), Error::Success);
  }

  {
    uint8_t v = 0;
    EXPECT_EQ(od.insert(0x2000, 1, canfetti::Access::RW, _u8(1)), Error::Success);
    auto [e, p] = od.makeProxy(0x2000, 1);
    ASSERT_EQ(e, Error::Success);

    ASSERT_EQ(p.idx, 0x2000);
    ASSERT_EQ(p.subIdx, 1);

    EXPECT_EQ(p.copyFrom(&v, 1), Error::Success);
    EXPECT_EQ(p.reset(), Error::Success);
    EXPECT_EQ(p.copyInto(&v, 1), Error::Success);
  }

  {
    uint8_t v = 0;
    EXPECT_EQ(od.insert(0x2000, 2, canfetti::Access::WO, _u8(1)), Error::Success);
    auto [e, p] = od.makeProxy(0x2000, 2);
    ASSERT_EQ(e, Error::Success);

    EXPECT_EQ(p.copyFrom(&v, 1), Error::Success);
    EXPECT_EQ(p.reset(), Error::Success);
    EXPECT_EQ(p.copyInto(&v, 1), Error::ReadViolation);
  }
}

TEST(ObjDict, Callback)
{
  ObjDict od;
  uint8_t subIdx = 0;

  // No callback
  {
    ::testing::MockFunction<void(uint16_t idx, uint8_t subIdx)> mcb;
    EXPECT_CALL(mcb, Call(0x2000, subIdx)).Times(0);
    EXPECT_EQ(od.insert(0x2000, subIdx, canfetti::Access::RO, _u8(1), mcb.AsStdFunction()), Error::Success);
  }

  // Callback on insert
  {
    subIdx++;
    ::testing::MockFunction<void(uint16_t idx, uint8_t subIdx)> mcb;
    EXPECT_CALL(mcb, Call(0x2000, subIdx)).Times(1);
    EXPECT_EQ(od.insert(0x2000, subIdx, canfetti::Access::RO, _u8(1), mcb.AsStdFunction(), true), Error::Success);
  }

  // Callback on set
  {
    subIdx++;
    ::testing::MockFunction<void(uint16_t idx, uint8_t subIdx)> mcb;
    EXPECT_CALL(mcb, Call(0x2000, subIdx)).Times(1);
    EXPECT_EQ(od.insert(0x2000, subIdx, canfetti::Access::RO, _u8(1), mcb.AsStdFunction()), Error::Success);
    od.set(0x2000, subIdx, _u8(4));
  }

  // Callback on insert & set
  {
    subIdx++;
    ::testing::MockFunction<void(uint16_t idx, uint8_t subIdx)> mcb;
    EXPECT_CALL(mcb, Call(0x2000, subIdx)).Times(2);
    EXPECT_EQ(od.insert(0x2000, subIdx, canfetti::Access::RO, _u8(1), mcb.AsStdFunction(), true), Error::Success);
    od.set(0x2000, subIdx, _u8(4));
  }
}

TEST(ObjDict, Generation)
{
  ObjDict od;
  EXPECT_EQ(od.insert(0x2000, 0, canfetti::Access::RO, _u8(42)), Error::Success);
  unsigned gen;
  EXPECT_EQ(od.generation(0x2000, 0, gen), Error::Success);

  // Bump generation on set
  {
    unsigned g;
    EXPECT_EQ(od.set(0x2000, 0, _u8(43)), Error::Success);
    EXPECT_EQ(od.generation(0x2000, 0, g), Error::Success);
    EXPECT_NE(gen, g);
    gen = g;
  }

  // Bump generation on set even if value not changed
  {
    unsigned g;
    EXPECT_EQ(od.set(0x2000, 0, _u8(43)), Error::Success);
    EXPECT_EQ(od.generation(0x2000, 0, g), Error::Success);
    EXPECT_NE(gen, g);
    gen = g;
  }
}
