#pragma once
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <unistd.h>
#include <ostream>
#include <string>
#include "canfetti/LocalNode.h"
#include "canfetti/System.h"

namespace canfetti {

static constexpr auto toNumber(const Error& e) -> ::std::underlying_type<Error>::type
{
  return static_cast<::std::underlying_type<Error>::type>(e);
}

static ::std::ostream& operator<<(::std::ostream& out, const Error& e)
{
  return out << "0x" << std::hex << toNumber(e);
}

}  // namespace canfetti