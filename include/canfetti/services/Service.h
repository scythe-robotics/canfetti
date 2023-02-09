#pragma once
#include "canfetti/Node.h"

namespace canfetti {
class Service {
 public:
  Service(Node &co) : co(co) {}
  virtual Error init() { return Error::Success; }
  virtual canfetti::Error processMsg(const canfetti::Msg &msg) = 0;

 protected:
  Node &co;
};
}  // namespace canfetti
